#include <pid.h>
#include <limits.h>
#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>

/**
* table to hold pids
*/
struct pid *pidtable[PID_MAX+1];


/*
 * Bootstrap method for pid table. Will be initialized when kernel first opens.
 * Loads the pidtable with pid objects with true flags and a new lock
 */
void pid_bootstrap(void){
    for(int i = 0; i < PID_MAX+1; i++){
        struct pid* new_pid = (struct pid*) kmalloc(sizeof(struct pid));
        new_pid->flag = true;
        spinlock_init(&new_pid->pidlock);
        pidtable[i] = new_pid;
    }
};

/*
 * Recursively calls itself until a pid in the pid table is found.
 * Checks the table starting at index PID_MIN and to PID_MAX
 */
pid_t get_new_pid(void){
    for(int i = PID_MIN; i < PID_MAX+1; i++){
        if(pidtable[i]->flag){
            spinlock_acquire(&pidtable[i]->pidlock);
            if(pidtable[i]->flag){
                pidtable[i]->flag = false;
                spinlock_release(&pidtable[i]->pidlock);
                return (pid_t) i;
            }
            spinlock_release(&pidtable[i]->pidlock);
        }
    }
    /* The process will continue to attempt to get a new pid */
    return get_new_pid();
};

/*
 * Frees the input pid in the pid table for other processes to acquire them.
 * Method is called in proc_destroy
 */
void close_pid(pid_t pid){
    int index;
    index = (int) pid;
    spinlock_acquire(&pidtable[index]->pidlock);
    pidtable[index]->flag = true;
    spinlock_release(&pidtable[index]->pidlock);
}


/*
 * Destroys and cleans up the pid table.
 * Called at Kernel Shutdown() to free the pid table.
 */
void pidtable_cleanup(void){
    for(int i = 0; i < PID_MAX+1; i++){
        spinlock_cleanup(&pidtable[i]->pidlock);
        kfree(pidtable[i]);
        pidtable[i] = NULL;
    }
};

/*
 * Check if a pid is currently in use.
 */
bool check_pidtable(int pid){
    return !pidtable[pid]->flag;
}

