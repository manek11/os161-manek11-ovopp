
#include <spinlock.h>
#include <kern/limits.h>
#include <thread.h> /* required for struct threadarray */



struct pid {
   struct spinlock *pidlock; /* lock the pid table */
   bool flag;   /* true is free, false is not*/
};

struct pidtable{
    struct pid *pidtable[PID_MAX+1];
};


