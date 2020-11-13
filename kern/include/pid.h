#ifndef _PID_H_
#define _PID_H_

/*
 * PID object and PID-array management management
 */
 
#include <types.h>
#include <spinlock.h> /* for the pid spinlock */
#include <kern/limits.h> /* for PID_MAX */


struct pid {
   struct spinlock pidlock; /* lock the pid entry in the table */
   bool flag;   /* true is free, false is not*/
};

/*
 * Pid-table ops:
 *
 * bootstrap -  Constructs a pid table in the kernel.
 * cleanup - cleans up the pid table.
 * get_new_pid -    Allocates a free pid to the proc.
 * close_pid -    Relinquishes the current proc's pid for other proc's to use. Called in proc_destroy
 * check_pidtable - Checks to see if the pid is occupied in the pidtable
 */
 
void pid_bootstrap(void);

pid_t get_new_pid(void);

void pidtable_cleanup(void);

void close_pid(pid_t pid);

bool check_pidtable(int pid);

#endif
