#include <file_table_syscall.h>
#include <vfs.h>
#include <copyinout.h>
#include <limits.h>
#include <vnode.h>
#include <uio.h>

struct file_table file_table_arr[OPEN_MAX];

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
        if(file_table_arr[i] == NULL){
            file_table_arr[i] = kmalloc(sizeof(struct file_table));
            file_table_arr[i].flags = flags;
            file_table_arr[i].mode = mode;
            file_table_arr[i].offset = 0;
            res = vfs_open(tmp, flags, mode, &file_table_arr[i].vnode);
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
    if(file_table_arr[fd] != NULL && file_table_arr[fd].flag != O_RDONLY){
        void *kbuf = kmalloc(sizeof(void*));
        struct uio myuio;
        struct iovec myiovec;
        uio_kinit(&myiovec, &uio, kbuf, buflen, file_table_arr[fd].offset, UIO_READ);
        
        int residbefore = myuio.uio_resid;
        int ret = VOP_READ(file_table_arr[fd].vnode, &myuio);
        file_table_arr[fd].offset = myuio.uio_offset;
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
        if(file_table_arr[fd] != NULL && file_table_arr[fd].flag != O_WRONLY){
            void *kbuf = kmalloc(sizeof(void*));
            struct uio myuio;
            struct iovec myiovec;
            
            int ret = copyin((const_userptr_t) buffer, kbuf, nbytes);
            
            uio_kinit(&myiovec, &uio, kbuf, nbytes, file_table_arr[fd].offset, UIO_WRITE);
            
            int residbefore = myuio.uio_resid;
            
            int ret2 = VOP_WRITE(file_table_arr[fd].vnode, &myuio);
            if(ret2){
                return ret2;
            }
            file_table_arr[fd].offset = myuio.uio_offset;
            
            return residbefore - myuio.uio_resid;
        }
        else{
            return EBADF;
        }
};


off_t
sys_lseek(int fd, off_t pos, int whence){
    
};


int
sys_close(int fd){

};

int
sys_dup2(int oldfd, int newfd){
    
};


int
sys_chdir(const char *pathname){

};


int
sys__getcwd(char *buf, size_t buflen){

};
