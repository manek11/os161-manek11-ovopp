#include <types.h>
#include <kern/types.h>
#include <vnode.h>
#include <lib.h>
#include <kern/file_table_syscall.h>
#include <vfs.h>
#include <copyinout.h>
#include <limits.h>
#include <vnode.h>
#include <uio.h>
#include <kern/iovec.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <limits.h>
#include <vm.h>
#include <kern/fcntl.h>
#include <kern/stat.h>


int
sys_open(const char *filename, int flags, mode_t mode){

    char *tmp = kmalloc(sizeof(char*)); // hold result for copyinstr
    size_t actual_size = 0; // hold result size
        
    int val = copyinstr((const_userptr_t)filename, tmp, NAME_MAX, &actual_size);
    if(val == EFAULT){
        return EFAULT;
    }
    if(val == ENAMETOOLONG){
        return ENAMETOOLONG;
    }
    // error checking here 
    int res = 0;
    for (int i = 3 ; i < OPEN_MAX ; i++){
        if(curproc->file_table_arr[i].ft_vnode == NULL){
            curproc->file_table_arr[i].flag = flags;
            curproc->file_table_arr[i].mode = mode;
            curproc->file_table_arr[i].offset = 0;
            res = vfs_open(tmp, flags, mode, &curproc->file_table_arr[i].ft_vnode);
            if(res == EINVAL){
                return EINVAL;
            }
            else{
                return i;
            }
        }
    }
    // error here, file TABLE is full
    return ENOSPC; 
};


ssize_t
sys_read(int fd, void *buf , size_t buflen){
    if(fd < 0 || fd > OPEN_MAX){
        return EBADF;
    }
    if(curproc->file_table_arr[fd].ft_vnode != NULL && curproc->file_table_arr[fd].flag != O_RDONLY){
        void *kbuf = kmalloc(sizeof(void*));
        struct uio myuio;
        struct iovec myiovec;
        uio_kinit(&myiovec, &myuio, kbuf, buflen, curproc->file_table_arr[fd].offset, UIO_READ);
        
        int residbefore = myuio.uio_resid;
        int ret = VOP_READ(curproc->file_table_arr[fd].ft_vnode, &myuio);
        if(ret)
            return ret;
        curproc->file_table_arr[fd].offset = myuio.uio_offset;
        int ret2 = copyout(kbuf,(userptr_t) buf, buflen); //at this point we read the file from FD to the buffer in userspace
        if(ret2){
            return EFAULT;
        }
        return residbefore - myuio.uio_resid; // return the number of bytes 
    }
    else{
        return EBADF;
    }
};


ssize_t
sys_write(int fd, const void *buffer, size_t nbytes){
    if(fd < 0 || fd > OPEN_MAX){
            return EBADF;
        }
        if(curproc->file_table_arr[fd].ft_vnode != NULL && curproc->file_table_arr[fd].flag != O_WRONLY){
            void *kbuf = kmalloc(sizeof(void*));
            struct uio myuio;
            struct iovec myiovec;
            
            int ret = copyin((const_userptr_t) buffer, kbuf, nbytes);
            if(ret)
                return ret;
            
            uio_kinit(&myiovec, &myuio, kbuf, nbytes, curproc->file_table_arr[fd].offset,  UIO_WRITE);
            
            int residbefore = myuio.uio_resid;
            
            int ret2 = VOP_WRITE(curproc->file_table_arr[fd].ft_vnode, &myuio);
            if(ret2){
                return ret2;
            }
            curproc->file_table_arr[fd].offset = myuio.uio_offset;
            return residbefore - myuio.uio_resid;
        }
        else{
            return EBADF;
        }
};


off_t
sys_lseek(int fd, off_t pos, int whence){
    if(fd < 0 || fd > OPEN_MAX){
        return EBADF;
    }
    int ret = 0;
    struct stat mystat;
    switch (whence)
    {
        case SEEK_SET:
            curproc->file_table_arr[fd].offset = pos;
            break;
        
        case SEEK_CUR:
            curproc->file_table_arr[fd].offset += pos;
            break;
        
        case SEEK_END:
            ret = VOP_STAT(curproc->file_table_arr[fd].ft_vnode,&mystat);
            if(ret){
                return ret;
            }
            curproc->file_table_arr[fd].offset = pos + mystat.st_size;
            break;
        default:
            return EINVAL;
    }
    return curproc->file_table_arr[fd].offset;
};


int
sys_close(int fd){
    if(fd < 0 || fd > OPEN_MAX){
        return EBADF;
    }
    if(curproc->file_table_arr[fd].ft_vnode != NULL){
        vfs_close(curproc->file_table_arr[fd].ft_vnode);
        if(curproc->file_table_arr[fd].ft_vnode->vn_refcount == 0){
           vnode_cleanup(curproc->file_table_arr[fd].ft_vnode);
           // kfree(curproc->file_table_arr[fd]);
           return 0;
        }
        else{
            // need to decrement
            return 0;
        }
    }
    else{
        return EBADF;
    }
};

int
sys_dup2(int oldfd, int newfd){
    if(oldfd < 0 || newfd < 0){
        return EBADF;
    }
    if(oldfd > OPEN_MAX || newfd > OPEN_MAX){
       return EMFILE; 
    }
    if(oldfd == newfd){
        return newfd;
    }
    
    if(curproc->file_table_arr[newfd].ft_vnode == NULL){
        if(curproc->file_table_arr[oldfd].ft_vnode == NULL){
            return newfd;
        }
        else{
        // copy over vnode pointer and all the flags. both FDs now point to same pointer
            curproc->file_table_arr[newfd].ft_vnode = curproc->file_table_arr[oldfd].ft_vnode;
            curproc->file_table_arr[newfd].offset = curproc->file_table_arr[oldfd].offset;
            curproc->file_table_arr[newfd].flag = curproc->file_table_arr[oldfd].flag;
            return newfd;
        }
    }
    else if(curproc->file_table_arr[newfd].ft_vnode != NULL){
        return sys_close(newfd);
    }
    
    return EBADF;
};


int
sys_chdir(const char *pathname){
    char *tmp = kmalloc(sizeof(char*)); // hold result for copyinstr
    size_t actual_size = 0; // hold result size
        
    int val = copyinstr((const_userptr_t)pathname, tmp, NAME_MAX, &actual_size);
    if(val == EFAULT){
        return EFAULT;
    }
    if(val == ENAMETOOLONG){
        return ENAMETOOLONG;
    }
    
    return vfs_chdir(tmp);

};


int
sys__getcwd(char *buf, size_t buflen){
    void *kbuf = kmalloc(sizeof(void*));
    struct uio myuio;
    struct iovec myiovec;
    size_t actual_size;
    
    uio_kinit(&myiovec, &myuio, kbuf, buflen, 0, UIO_READ);
    int ret1 = vfs_getcwd(&myuio);
    if(ret1 == ENOENT){
        return ENOENT;
    }
    int ret2 = copyoutstr(kbuf, (userptr_t) buf, buflen, &actual_size);
    if(ret2==EFAULT){
        return EFAULT;
    }
    return actual_size;
};
