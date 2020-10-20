#ifndef _FILE_TABLE_SYSCALL_H_
#define _FILE_TABLE_SYSCALL_H_

/*
#include <lib.h>
#include <vnode.h>
*/
/*
#include <kern/seek.h>
*/
struct file_table{
   struct  vnode *ft_vnode;
   mode_t flag;
   off_t offset;       
};

struct file_table file_table_arr[128];

/*
int
sys_open(const char *filename, int flags, mode_t mode);

ssize_t
sys_read(int fd, void *buf , size_t buflen);

ssize_t
sys_write(int fd, const void *buffer, size_t nbytes);

off_t
sys_lseek(int fd, off_t pos, int whence);

int
sys_close(int fd);

int
sys_dup2(int oldfd, int newfd);

int
sys_chdir(const char *pathname);

int
sys__getcwd(char *buf, size_t buflen);

*/

int
sys_open(char *, int , mode_t);

ssize_t
sys_read(int, void *, size_t);

ssize_t
sys_write(int , void *, size_t);

off_t
sys_lseek(int, off_t, int);

int
sys_close(int);

int
sys_dup2(int, int );

int
sys_chdir(char *);

int
sys__getcwd(char *, size_t );


#endif /* _FILE_TABLE_SYSCALL_H_ */
