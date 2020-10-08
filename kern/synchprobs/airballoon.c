/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;

/* Data structures for rope mappings */

struct b_ropes{
    bool is_severed;
};

struct b_hook{  
    int rope_num;
    bool is_severed;  
    struct lock *lk_hook;   
};

struct b_stake{    
    int rope_num;
    bool is_severed;     
    struct lock *lk_stake;
};


struct b_hook hooks[NROPES]; 
struct b_stake stakes[NROPES];
struct b_ropes ropes[NROPES];


/* Synchronization primitives */

struct lock *count_lock;
struct semaphore *balloon_lock;


/* Flags and severed ropes tracker*/

volatile int num_severed_ropes;
int severed_statements;
bool dandelion_done;
bool marigold_done;
bool flowerkiller_done;
bool balloon_done;

/*
* Describe your design and any invariants or locking protocols
* that must be maintained. Explain the exit conditions. How
* do all threads know when they are done?

* My design has:
* 3 structs: b_ropes, b_hooks, b_stakes
* 3 locks: count_lock, lk_hook, lk_stake
* 1 semaphore: balloon_lock,
* 3 struct arrays: hooks[NROPES], stakes[NROPES], ropes[NROPES]
* 2 counters: num_severed_ropes, severed_statements
* 4 bools: dandelion_done, marigold_done, flowerkiller_done, and balloon_done
* At startup, the hooks and stakes get 1:1 correspndence and locks 
* are created.
* During the execution of dandelion, marigold, and flowerkiller
* the hooks and stakes are used to indirectly access the rope
* and severe it. At the same times the stakes can get swapped
* and thus, loose its 1:1 correspondence.
* During this time, the locks are ensuring mutual exclusion
* for each counter operation i.e. whenever num_severed_ropes
* is incremented, and also when it is used to compare for
* any break conditions in the while(1) loops.
* There are also locks ensuring that each hook and stake access is
* is exclusive.
* Lastly, there is a semaphore which waits for N_LORD_FLOWERKILLER + 3
* times (lord flowerkiller * 8, dandelion, marigold, balloon). After each
* thread finishes executing it signals to the main thread.
* There are also 4 boolean variables which are used as secondary signals
* to make sure that each thread has finished executing in the desired manner.  
*/

static
void
startup(){

    int i;    
    num_severed_ropes = 0;
    severed_statements = 0;
    count_lock = lock_create("count_lock");
    balloon_lock = sem_create("balloon_lock", 0);

        for (i = 0; i < NROPES; i++){
            hooks[i].rope_num=i;
            hooks[i].is_severed = false;
            hooks[i].lk_hook = lock_create("hook_lock");

            stakes[i].rope_num=i;
            stakes[i].is_severed = false;
            stakes[i].lk_stake = lock_create("stake_lock");

            ropes[i].is_severed = false; 
        } 

    dandelion_done = false;
    marigold_done = false;
    flowerkiller_done = false;
    balloon_done = false;
}

static
void
cleanup(){

    int i;       

    lock_destroy(count_lock); 
    sem_destroy(balloon_lock);

    for (i=0; i<NROPES; i++){
        lock_destroy(hooks[i].lk_hook);
        lock_destroy(stakes[i].lk_stake); 
    }
    
}


static
void
dandelion(void *p, unsigned long arg)
{
    (void)p;
    (void)arg;


    kprintf("Dandelion thread starting\n");


    while (true){ 

        if (lock_do_i_hold(count_lock) == false){    	
                lock_acquire(count_lock);	
        }

        if (num_severed_ropes == NROPES) {
            if (lock_do_i_hold(count_lock) == true){    	
                lock_release(count_lock);	
            }            
            break;
        }

        if (lock_do_i_hold(count_lock) == true){    	
                lock_release(count_lock);	
        }

        int random_hook_index = random() % NROPES;	

        if (ropes[hooks[random_hook_index].rope_num].is_severed == false){  
            if ((lock_do_i_hold(hooks[random_hook_index].lk_hook)==false)){
                    lock_acquire(hooks[random_hook_index].lk_hook);
            }
        }    

        if (ropes[hooks[random_hook_index].rope_num].is_severed == true){  
            if ((lock_do_i_hold(hooks[random_hook_index].lk_hook)==true)){
                    lock_acquire(hooks[random_hook_index].lk_hook);
            }
        } 	    

            
        if (hooks[random_hook_index].is_severed == false && ropes[hooks[random_hook_index].rope_num].is_severed == false){ 	    		    	    

            hooks[random_hook_index].is_severed= true;
            ropes[hooks[random_hook_index].rope_num].is_severed = true;

            if (lock_do_i_hold(count_lock) == false){    	
                    lock_acquire(count_lock);	
            }

            num_severed_ropes++;	   
             
            if (lock_do_i_hold(count_lock) == true){    	
                    lock_release(count_lock);	
            }

            if ((lock_do_i_hold(hooks[random_hook_index].lk_hook)==true)){
                lock_release(hooks[random_hook_index].lk_hook);
            }

            kprintf("Dandelion severed rope %d\n", hooks[random_hook_index].rope_num);
            severed_statements++;

            thread_yield(); 	        	               
            }	

            if ((lock_do_i_hold(hooks[random_hook_index].lk_hook)==true)){
                lock_release(hooks[random_hook_index].lk_hook);
            }
    }

    kprintf("Dandelion thread done\n");
    dandelion_done = true;
    V(balloon_lock);
}

static
void
marigold(void *p, unsigned long arg)
{
    (void)p;
    (void)arg;

    kprintf("Marigold thread starting\n");

    while (true){

        if (lock_do_i_hold(count_lock) == false){    	
            lock_acquire(count_lock);	
        }

        if (num_severed_ropes == NROPES) {
            if (lock_do_i_hold(count_lock) == true){    	
                lock_release(count_lock);	
            }
            break;
        }


        if (lock_do_i_hold(count_lock) == true){    	
            lock_release(count_lock);	
        }

        int  random_stake_index = random() % NROPES;              

        if (ropes[stakes[random_stake_index].rope_num].is_severed == false){
            if ((lock_do_i_hold(stakes[random_stake_index].lk_stake)==false)){
                    lock_acquire(stakes[random_stake_index].lk_stake);
            }
        }

        if (ropes[stakes[random_stake_index].rope_num].is_severed == true){
            if ((lock_do_i_hold(stakes[random_stake_index].lk_stake)==true)){
                    lock_release(stakes[random_stake_index].lk_stake);
            }
        }    
            
        if (stakes[random_stake_index].is_severed == false && ropes[stakes[random_stake_index].rope_num].is_severed == false){        

            stakes[random_stake_index].is_severed = true;
            ropes[stakes[random_stake_index].rope_num].is_severed = true;                            
		
            if (lock_do_i_hold(count_lock) == false){    	
                    lock_acquire(count_lock);	
            }
            
            num_severed_ropes++;	                

            if (lock_do_i_hold(count_lock) == true){    	
                    lock_release(count_lock);	
            }	      	  

            if ((lock_do_i_hold(stakes[random_stake_index].lk_stake)==true)){
                    lock_release(stakes[random_stake_index].lk_stake);
            }
                   
            kprintf("Marigold severed rope %d from stake %d\n", stakes[random_stake_index].rope_num, random_stake_index);
            severed_statements++;
            thread_yield();               
        }


        if ((lock_do_i_hold(stakes[random_stake_index].lk_stake)==true)){
                lock_release(stakes[random_stake_index].lk_stake);
        }
    }

    kprintf("Marigold thread done\n"); 
    marigold_done = true;
    V(balloon_lock);
}


static
void
flowerkiller(void *p, unsigned long arg)
{
(void)p;
(void)arg;

    kprintf("Lord FlowerKiller thread starting\n");

    int swap_rope = 0;

    while (true){		

        if (lock_do_i_hold(count_lock) == false){    	
            lock_acquire(count_lock);	
        }			    

        if (num_severed_ropes > NROPES-2) {
            if (lock_do_i_hold(count_lock) == true){    	
                lock_release(count_lock);	
            }
            break;
        }

        if (lock_do_i_hold(count_lock) == true){    	
            lock_release(count_lock);	
        }

        int  random_stake_index_1 = random() % NROPES;
        int  random_stake_index_2 = random() % NROPES;


        if (random_stake_index_1 == random_stake_index_2){
            continue;
        }

        if (stakes[random_stake_index_1].rope_num != random_stake_index_1 ||
            stakes[random_stake_index_2].rope_num != random_stake_index_2){
            continue;
        }

        if (ropes[stakes[random_stake_index_1].rope_num].is_severed == false){
            if ((lock_do_i_hold(stakes[random_stake_index_1].lk_stake)==false)){
                    lock_acquire(stakes[random_stake_index_1].lk_stake);
            }
        }

        if (ropes[stakes[random_stake_index_2].rope_num].is_severed == false){
            if ((lock_do_i_hold(stakes[random_stake_index_2].lk_stake)==false)){
                    lock_acquire(stakes[random_stake_index_2].lk_stake);
            }
        }

        if (ropes[stakes[random_stake_index_1].rope_num].is_severed == true
        || ropes[stakes[random_stake_index_2].rope_num].is_severed == true){

            if ((lock_do_i_hold(stakes[random_stake_index_1].lk_stake)==true)){
                    lock_release(stakes[random_stake_index_1].lk_stake);
            }

            if ((lock_do_i_hold(stakes[random_stake_index_2].lk_stake)==true)){
                    lock_release(stakes[random_stake_index_2].lk_stake);
            }   
        }

        if (stakes[random_stake_index_1].is_severed == false && stakes[random_stake_index_2].is_severed == false && ropes[stakes[random_stake_index_1].rope_num].is_severed == false
        && ropes[stakes[random_stake_index_2].rope_num].is_severed == false){ 

            swap_rope = stakes[random_stake_index_1].rope_num;
            stakes[random_stake_index_1].rope_num = stakes[random_stake_index_2].rope_num;
            stakes[random_stake_index_2].rope_num = swap_rope;



            if ((lock_do_i_hold(stakes[random_stake_index_1].lk_stake)==true)){
                lock_release(stakes[random_stake_index_1].lk_stake);
            }

            if ((lock_do_i_hold(stakes[random_stake_index_2].lk_stake)==true)){
                lock_release(stakes[random_stake_index_2].lk_stake);
            }   

            kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", stakes[random_stake_index_1].rope_num, random_stake_index_1, random_stake_index_2);   
            kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", stakes[random_stake_index_2].rope_num, random_stake_index_2, random_stake_index_1);	

            thread_yield();	        	   	                                    
        }


        if ((lock_do_i_hold(stakes[random_stake_index_1].lk_stake)==true)){
            lock_release(stakes[random_stake_index_1].lk_stake);
        }

        if ((lock_do_i_hold(stakes[random_stake_index_2].lk_stake)==true)){
            lock_release(stakes[random_stake_index_2].lk_stake);
        }
    }

    kprintf("Lord FlowerKiller thread done\n");
    flowerkiller_done = true;
    V(balloon_lock);		
}

static
void
balloon(void *p, unsigned long arg)
{
    (void)p;
    (void)arg;

    kprintf("Balloon thread starting\n");

        while (true){	   

            if (lock_do_i_hold(count_lock) == false){    	
                    lock_acquire(count_lock);	
            }

            if(num_severed_ropes == NROPES && severed_statements == NROPES){
                if (lock_do_i_hold(count_lock) == true){    	
                    lock_release(count_lock);	
            }
                                        
            break;

            }

            if (lock_do_i_hold(count_lock) == true){    	
                    lock_release(count_lock);	
            }        
        }

    kprintf("Balloon freed and Prince Dandelion escapes!\n");
    kprintf("Balloon thread done\n");
    balloon_done = true;
    V(balloon_lock);	
}


int
airballoon(int nargs, char **args)
{

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;
	
	startup();

	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;
  
  	
	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:

    for (i = 0; i< (3 + N_LORD_FLOWERKILLER); i++){
        P(balloon_lock);
    }

    KASSERT(dandelion_done == true);
    KASSERT(marigold_done == true);
    KASSERT(flowerkiller_done == true);
    KASSERT(balloon_done == true);
    
    kprintf("Main thread done\n");
    cleanup();
    return 0;
}
