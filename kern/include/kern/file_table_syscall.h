#ifndef _FILE_TABLE_SYSCALL_H_
#define _FILE_TABLE_SYSCALL_H_


#include <vnode.h>
#include <kern/seek.h>
#include <uio.h>
#include <kern/types.h>
#include <types.h>
#include <lib.h>


typedef struct file_table{
   struct  vnode *ft_vnode;
   int flag;
   mode_t mode;
   off_t offset;       
}file_table;


int
sys_open(const char *filename, int flags, mode_t mode, int32_t *retval);

ssize_t
sys_read(int fd, void *buf , size_t buflen, int32_t *retval);

ssize_t
sys_write(int fd, const void *buffer, size_t nbytes, int32_t * retval);

off_t
sys_lseek(int fd, off_t pos, int whence, int64_t * retval);

int
sys_close(int fd);

int
sys_dup2(int oldfd, int newfd, int32_t * retval);

int
sys_chdir(const char *pathname);

int
sys__getcwd(char *buf, size_t buflen, int32_t * retval);
/*
int
file_table_init(struct proc curproc);
*/

#endif /* _FILE_TABLE_SYSCALL_H_ */
