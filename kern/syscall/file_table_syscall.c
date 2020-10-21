#include <types.h>
#include <kern/types.h>
#include <vnode.h>
#include <lib.h>
#include <kern/file_table_syscall.h>
#include <vfs.h>
#include <copyinout.h>
#include <limits.h>
#include <uio.h>
#include <kern/iovec.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <limits.h>
#include <vm.h>
#include <kern/fcntl.h>
#include <kern/stat.h>

/**
* functions that are called by syscall.c 's transfer points.
* sys_* based on the operation by the user.
*/

/* 
* sys_open returns the first available FD of the file-table 
*/
int
sys_open(const char *filename, int flags, mode_t mode, int32_t * retval){
    // setup parameters for copyinstr
    char *tmp = kmalloc(sizeof(char)*NAME_MAX); // holds result for copyinstr
    size_t actual_size = 0; // holds result size
    
    if (filename == NULL){
        return EFAULT;
    }
        
    int val = copyinstr((const_userptr_t)filename, tmp, PATH_MAX, &actual_size);
    if(val == EFAULT){
        return EFAULT;
    }
    if(val == ENAMETOOLONG){
        return ENAMETOOLONG;
    }
    
    // finds the next available file-table with an empty entry and fill it up
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
                *retval = i;
                return 0;
            }
        }
    }
    // error here, file TABLE is full
    return ENOSPC; 
};

/* 
* sys_read returns 0 and updates retval upon a successful read with the number of bytes read
*/
ssize_t
sys_read(int fd, void *buf , size_t buflen, int32_t * retval){
     // error checking for invalid FDs   
     if(fd < 0 || fd >= OPEN_MAX){
        return EBADF;
     }
     
     if (curproc->file_table_arr[fd].flag & O_WRONLY){
            return EBADF;
        }
    // using UIO and IOVEC to read the vnode associated with the FD entry.
    if(curproc->file_table_arr[fd].ft_vnode != NULL && curproc->file_table_arr[fd].flag != O_WRONLY){
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
        *retval = residbefore - myuio.uio_resid;
        return 0; // return the number of bytes 
    }
    else{
        return EBADF;
    }
};


ssize_t
sys_write(int fd, const void *buffer, size_t nbytes, int32_t * retval){
    // error checking for invalid parameters  
    if(fd < 0 || fd >= OPEN_MAX){
            return EBADF;
        }
        
        if (buffer == NULL){
            return EFAULT;
        } 

        if (curproc->file_table_arr[fd].ft_vnode == NULL){
            return EBADF;
        }    

        if (nbytes <= 0){
            return EFAULT;
        }
        
        if (!(curproc->file_table_arr[fd].flag & O_ACCMODE)){
            return EBADF;
        }
        
        // Using UIO and IOVEC to facilitate writing to userspace
        if(curproc->file_table_arr[fd].ft_vnode != NULL && curproc->file_table_arr[fd].flag != O_RDONLY){
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
            *retval = residbefore - myuio.uio_resid;
            return 0;
        }
        else{
            return EBADF;
        }
};


off_t
sys_lseek(int fd, off_t pos, int whence, int64_t * retval){
    // error checking for invalid parameters  
    if(fd < 0 || fd >= OPEN_MAX){
        return EBADF;
    }
    
    if (curproc->file_table_arr[fd].ft_vnode == NULL){
        return EBADF;
    }

    int ret = 0;
    struct stat mystat;
    off_t seek = curproc->file_table_arr[fd].offset;
    // switch case to handle the appropriate whence cases
    switch (whence)
    {
        case SEEK_SET:
            seek = pos;
            break;
        
        case SEEK_CUR:
            seek += pos;
            break;
        
        case SEEK_END:
            ret = VOP_STAT(curproc->file_table_arr[fd].ft_vnode,&mystat);
            if(ret){
                return ret;
            }
            seek = pos + mystat.st_size;
            break;
        default:
            return EINVAL;
    }
    if(seek < 0){
        return EINVAL;
    }
    curproc->file_table_arr[fd].offset = seek;
    
        
    if(!VOP_ISSEEKABLE(curproc->file_table_arr[fd].ft_vnode)){
        return ESPIPE;
    }
    
    
    *retval = curproc->file_table_arr[fd].offset;
    return 0;
};


int
sys_close(int fd){
    // Checks for bad parameters
    if(fd < 0 || fd >= OPEN_MAX){
        return EBADF;
    }
    // Resets the entry in our filetable
    if(curproc->file_table_arr[fd].ft_vnode != NULL){
        curproc->file_table_arr[fd].ft_vnode = NULL;
        curproc->file_table_arr[fd].flag = -1;
        curproc->file_table_arr[fd].offset = 0;
        curproc->file_table_arr[fd].mode = 0;
        return 0;
        
    /*    vfs_close(curproc->file_table_arr[fd].ft_vnode);
        if(curproc->file_table_arr[fd].ft_vnode->vn_refcount == 0){
           vnode_cleanup(curproc->file_table_arr[fd].ft_vnode);
           kfree(curproc->file_table_arr[fd].ft_vnode);
           curproc->file_table_arr[fd].ft_vnode = NULL;
           return 0;
        }
        return 0;
    }
    */
    }
    else{
        return EBADF;
    }
};

int
sys_dup2(int oldfd, int newfd, int32_t * retval){
    // Error checks for bad parameters
    if(oldfd < 0 || oldfd >= OPEN_MAX){
        return EBADF;
    }
    if( newfd < 0 || newfd >= OPEN_MAX){
       return EBADF; 
    }
    
    if (curproc->file_table_arr[oldfd].ft_vnode == NULL){
        return EBADF;
    }
    
    if(oldfd == newfd){
       *retval = newfd;
        return 0;
    }
    
    if(curproc->file_table_arr[newfd].ft_vnode == NULL){
        // copy over vnode pointer and all the flags. both FDs now point to same pointer
            curproc->file_table_arr[newfd].ft_vnode = curproc->file_table_arr[oldfd].ft_vnode;
            curproc->file_table_arr[newfd].offset = curproc->file_table_arr[oldfd].offset;
            curproc->file_table_arr[newfd].flag = curproc->file_table_arr[oldfd].flag;
            *retval = newfd;
            return 0;
        }
    else if(curproc->file_table_arr[newfd].ft_vnode != NULL){
        int ret_v = sys_close(newfd);
        if (ret_v){
            return ret_v;
        }
    }
    
    return EBADF;
};


int
sys_chdir(const char *pathname){
    char *tmp = kmalloc(sizeof(char*)*NAME_MAX); // hold result for copyinstr
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
sys__getcwd(char *buf, size_t buflen, int32_t * retval){
    // The name of the current directory is computed and stored in buf, an area of size buflen
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
    *retval = actual_size;
    return 0;
};

// useful functions later when we want to implement assignment 5
/*
int
file_table_init(struct proc newproc){
    for(int i= 0 ; i < OPEN_MAX; i++){
        newproc->file_table_arr[i].ft_vnode = NULL;
        newproc->file_table_arr[i].offset = 0;
        newproc->file_table_arr[i].flag = 0;
        newproc->file_table_arr[i].mode = 0;
    }
    return 0;
};
*/

