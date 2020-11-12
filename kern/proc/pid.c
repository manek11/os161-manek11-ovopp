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



void pid_bootstrap(void){
    // load the pidtable with pid objects with true flags and a new lock
    for(int i = 0; i < PID_MAX+1; i++){
        struct pid* new_pid = (struct pid*) kmalloc(sizeof(struct pid));
        new_pid->flag = true;
        spinlock_init(&new_pid->pidlock);
        pidtable[i] = new_pid;
    }
};


pid_t get_new_pid(void){
    for(int i = PID_MIN; i < PID_MAX+1; i++){
        // we want to check the value of the pid, then acquire the lock on the pid if it's free (true).
        // we need to make sure of atomacy, so we check after acquiring lock, if it's now false we release and keep going
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
    // rerun the function (this may be...interesting) RECURSION VS WHILE LOOP
    return get_new_pid();
};

void close_pid(pid_t pid){
    // since the table will give out pids and no duplicates
    // however, we need to make sure that we lock before updating flag
    int index;
    index = (int) pid;
    spinlock_acquire(&pidtable[index]->pidlock);
    pidtable[index]->flag = true;
    spinlock_release(&pidtable[index]->pidlock);
}


void pidtable_cleanup(void){
    for(int i = 0; i < PID_MAX+1; i++){
        spinlock_cleanup(&pidtable[i]->pidlock);
        kfree(pidtable[i]);
        pidtable[i] = NULL;
    }
};

bool check_pidtable(int pid){
    return !pidtable[pid]->flag;

}

