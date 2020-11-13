/*
 * Process-related system call implementations.
 */
#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <copyinout.h>
#include <vfs.h>
#include <vnode.h>
#include <openfile.h>
#include <filetable.h>
#include <kern/syscall.h>
#include <kern/wait.h>
#include <addrspace.h>
#include <spl.h>
#include <pid.h>
#include <vm.h>
#include <syscall.h>


/*
* Entrypoint function to be called by thread_fork().
*/
void
child_thread (void *data1, unsigned long data2){
   
    struct trapframe tf;
    struct trapframe *c_tf = (struct trapframe *)data1;
    struct addrspace *c_addrspace = (struct addrspace *)data2;

    /* Copy c_tf to tf and then free it */
    memcpy((void *)&tf, (const void *)c_tf, sizeof(struct trapframe));

    kfree(c_tf);
    c_tf = NULL;      
      
    curproc->p_addrspace = c_addrspace;
    
    as_activate();            
    mips_usermode(&tf);
}

/*
* fork() - fork/replicate a process.
*/
pid_t 
sys_fork(struct trapframe *parent_tf, int32_t *ret){ 

    struct proc *forked_proc;
    struct trapframe *forked_tf;
    int err;
    unsigned int index;    

    /* process replicate */
    err = proc_fork(&forked_proc);  
    if(err){
        return err;
    } 

    /* copying trapframe from kernel heap to stack to make an exact copy of parent tf with proc state */   
    forked_tf = kmalloc(sizeof(struct trapframe));

    if (forked_tf == NULL) {
        return ENOMEM;
    }	

    memcpy((void *)forked_tf, (const void *)parent_tf, sizeof(struct trapframe));

    forked_tf->tf_v0 = 0;
    forked_tf->tf_v1 = 0;
    forked_tf->tf_a3 = 0;
    forked_tf->tf_epc += 4;
       
    /*At this point, the forked process has the same as, different PID, same ft, same current working directory and same name
    return child pid in parent */
    *ret = forked_proc->pid;

    /* set the forked child process to have a parent */   
    forked_proc->parent_proc = curproc;

    /* add child to the parent child array list */
    array_add(curproc->childarray, (void *) forked_proc, &index);

    err = thread_fork("Child", forked_proc, child_thread, forked_tf, (unsigned long)forked_proc->p_addrspace); 

    if(err){
        proc_destroy(forked_proc);
        kfree(forked_tf);
        forked_tf = NULL;
        array_remove(curproc->childarray, index);
        return err;
    }

    return 0;
}

/*
*getpid() - Return pid of curproc.
*/
pid_t
sys_getpid(int32_t *retval){

    spinlock_acquire(&curthread->t_proc->p_lock);
    *retval = curthread->t_proc->pid;
    spinlock_release(&curthread->t_proc->p_lock);
    return 0;  
}


/*
* Calls and exits the current process.
* We increment our child semaphore and wait for our parent destroy us.
* we will simply remove ourselves from the current proc and current thread and call thread_exit().
*/
void
sys__exit(int code){

    struct proc* tmp_proc;
    lock_acquire(curproc->childarray_lock);

    if(code == 0 || code == 107){
        curproc->ret_val = _MKWAIT_EXIT(code);
    }
    else{
        curproc->ret_val = _MKWAIT_SIG(code);
    }

    while(curproc->childarray->num != 0){
        tmp_proc = (struct proc*) array_get(curproc->childarray, 0);
        
        if(tmp_proc != NULL){
            P(tmp_proc->sem_child);
        }
        
        array_remove(curproc->childarray, 0);
    }
    
    lock_release(curproc->childarray_lock);
    
    array_destroy(curproc->childarray);

    curproc->dead = true;


    /* Tell my parent to save my values and I will exit */ 
    V(curproc->sem_child);
    thread_exit();
    
    kprintf("Should not return here");
}

/*
* Parent calling waitpid will wait for a child process semaphore in sys_exit to signal they are ready to exit.
* Once obtained, parent will save child's return value and return it in status and the pid in retval.
* The parent will signal the child to continue the exit and in doing so, the process will be destroyed.
*/
int
sys_waitpid(pid_t pid, int32_t *status, int options, int32_t* retval){

    int ret;
    struct proc* childproc;

    if(((int) status) % 4 != 0){
        return EFAULT;
    }

    if(options != 0){
        return EINVAL;
    }       

    if(pid < 0 || pid > PID_MAX){
        return ESRCH;
    }

    if(check_pidtable((int) pid)){


        childproc = find_child_by_pid(curproc->childarray, pid);

        if(childproc != NULL){
            P(childproc->sem_child);

            ret = copyout(&childproc->ret_val, (userptr_t) status, sizeof(int32_t));
            
            if(ret){
                if((int* ) 0x0 == status){
                    *retval = (int32_t) pid;
                    proc_destroy(childproc);
                    return 0;
                }
                return ret;
            }

            *retval = (int32_t) pid;
            close_pid(childproc->pid);
            proc_destroy(childproc);

            return 0;
        }
        return ECHILD;
    }

    else{
        return ESRCH;
    }    
}

/*
*   execv() - replace the currently executing program with a newly loaded program image.
*/
int
sys_execv(const char * program, char **args){

    int args_size;
    int ret;
    struct addrspace *as, *old_as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;  

    /*  check if one of the arguments is invalid pointer  */ 
    if (program == NULL || args == NULL){
        return EFAULT;
    }

    if((int *)args == (int *)0x40000000 || (int *)args == (int *)0x80000000){
        return EFAULT;
    }

    /*
    *Step1: Copy arguments from the old address space.
    */

    /* copy program path name from user to kernel space */
    char *prog = kmalloc(sizeof(char)*(PATH_MAX)); 

    if (prog == NULL){
        return ENOMEM;
    }

    ret = copyin((const_userptr_t) program, prog, PATH_MAX);

    if(strlen(prog) == 0){
        kfree(prog);       
        prog = NULL;
        return EINVAL;
    }

    if (ret){
        kfree(prog);
        prog = NULL;
        return ret;
    }

    for (int i=0; i<ARG_MAX; i++){

        if((int *)args[i] == (int *)0x40000000 || (int *)args[i] == (int *)0x80000000){
            kfree(prog);
            prog = NULL; 
            return EFAULT;
        }        

        if(args[i]==NULL){
            break;
        }
        else{
            args_size++;
        }
    }

    if(args_size > ARG_MAX){
        kfree(prog);
        prog = NULL;     
        return E2BIG;
    }

    /* At this point we have the number of arguments in args */  
    char **kern_args =  kmalloc(sizeof(char *) * args_size); 
    int *len = kmalloc(sizeof(int)*args_size);

    for (int i=0; i<args_size; i++){
        char *string = kmalloc(sizeof(char)*(strlen(args[i]) + 1));
        size_t length = 0;
        int err = 0;

        err = copyinstr((const_userptr_t) args[i], string, (strlen(args[i]) + 1), &length);

        if(err){  
            kfree(string);
            string = NULL;
            kfree(len);
            len = NULL;
            kfree(kern_args);
            kern_args = NULL;
            kfree(prog);
            prog = NULL;
            return err;
        }

        len[i] = strlen(string);
        char ch = '\0';
        
        for(int j = len[i]; j < (len[i] + (4 - (len[i] % 4))); j++){
            string[j] = ch;
        }

        kern_args[i] = string;
    } 

    /*  length of each argument is in len[] */

    /*
    *Step2: Get a new address space.
    */    

    /*  dont destroy old_as yet */
    old_as = curproc->p_addrspace; 
    curproc->p_addrspace = NULL;    

    /* We should be a new process. */
    KASSERT(proc_getas() == NULL);

    as = as_create();
    
    if (as == NULL) {
        vfs_close(v);
        kfree(len);
        len = NULL;
        kfree(kern_args);
        kern_args = NULL;
        kfree(prog);
        prog = NULL;
        curproc->p_addrspace = old_as; 
        return ENOMEM;
    }

    /*
    *Step 3 Switch to it and activate it. 
    */
    proc_setas(as);
    as_activate();

    /* Open the file. */
    result = vfs_open(prog, O_RDONLY, 0, &v);
    if (result) {
        kfree(len);
        len = NULL;
        kfree(kern_args);
        kern_args = NULL;
        kfree(prog);
        prog = NULL;
        return result;
    }

    /*
    *Step 4 Load the executable.
    */
    result = load_elf(v, &entrypoint);
    if (result) {
        /* p_addrspace will go away when curproc is destroyed */
        vfs_close(v);
        kfree(len);
        len = NULL;
        kfree(kern_args);
        kern_args = NULL;
        vfs_close(v);
        kfree(prog);
        prog = NULL;        
        return result;
    }

    /* Done with the file now. */
    vfs_close(v);

    /*
    *Step 5 Define the user stack in the address space. 
    */
    result = as_define_stack(as, &stackptr);
    if (result) {
        /* p_addrspace will go away when curproc is destroyed */
        vfs_close(v);
        kfree(len);
        len = NULL;
        kfree(kern_args);
        kern_args = NULL;
        kfree(prog);
        prog = NULL;        
        return result;
    }

    /*
    *Step 6 Copy args to new as, properly arragning them.
    */

    userptr_t args_addr = (userptr_t)(stackptr - sizeof(userptr_t *)*args_size - sizeof(NULL));
    userptr_t *args_out = (userptr_t *)(stackptr - sizeof(char *)*args_size - sizeof(NULL));
    userptr_t kargs_out_addr;

    /*
    * We loop through our entire argument array and put the address into args_out.
    * kargs_out_addr will point to the address of args_out[0]. 
    */
    for(int i = 0; i < args_size; i++){
        size_t actual = 0;
        args_addr -= (len[i]+(4 - (len[i]%4)));
        *args_out = args_addr;
        KASSERT((int)args_addr %4 == 0);
        int error = copyoutstr((const char *)kern_args[i], (userptr_t)args_addr, (size_t) len[i] + (4 - (len[i]%4)), &actual);
        if(error){
            kfree(len);
            len = NULL;
            kfree(kern_args);
            kern_args = NULL;
            kfree(prog);
            prog = NULL; 
            return error;
        }
        args_out++;  
    }
    /*  set last index as NULL  */
    *args_out = NULL;
    kargs_out_addr = (userptr_t) (stackptr - args_size*(sizeof(int)) - sizeof(NULL));
    args_addr -= (int) args_addr % sizeof(void*);  
    stackptr = (vaddr_t)args_addr; 

    kfree(prog);
    prog = NULL;
    
    for (int i=0; i<args_size; i++){
        kfree(kern_args[i]);
        kern_args[i] = NULL;           
    }

    /*
    *Step 7 Clean up old address space.
    */
    as_destroy(old_as);

    /*Step 8 Warp to user mode. */
    enter_new_process(args_size, kargs_out_addr,
    NULL ,
    stackptr, entrypoint);

    /* enter_new_process does not return. */
    panic("enter_new_process returned\n");
    return EINVAL;

}

