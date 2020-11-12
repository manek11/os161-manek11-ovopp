#ifndef _PID_H_
#define _PID_H_

#include <types.h>
#include <spinlock.h>
#include <kern/limits.h>



struct pid {
   struct spinlock pidlock; /* lock the pid table */
   bool flag;   /* true is free, false is not*/
};

void pid_bootstrap(void);

pid_t get_new_pid(void);

void pidtable_cleanup(void);

void close_pid(pid_t pid);

bool check_pidtable(int pid);

#endif
