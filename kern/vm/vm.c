#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <synch.h>
#include <vm.h>

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct coremap_entry *coremap;
static bool bootstrapped = false;
static int num_pages = 0;

/* 
* Initializes the coremap structure to hold and map the physical memory.
* We set the first couple pages to hold the coremap structure, then the rest
* as free pages, ready to be allocated.
*/
void
vm_bootstrap(void)
{
    coremap_lock = lock_create("coremap_lock");
    size_t coremap_size;
    paddr_t firstaddr, lastaddr;
    spinlock_acquire(&stealmem_lock);

    lastaddr = ram_getsize();
    firstaddr = ram_getfirstfree();
    num_pages = (lastaddr-firstaddr) / PAGE_SIZE;

    coremap_size = num_pages * sizeof(struct coremap_entry);
    if(firstaddr + coremap_size >= lastaddr){
        panic("Coremap takes up too much physical memory");    
    }

    coremap = (struct coremap_entry*) PADDR_TO_KVADDR(firstaddr);

    paddr_t remaining_addr = firstaddr + num_pages * sizeof(struct coremap_entry);

    int fixed_pages = (num_pages / PAGE_SIZE);

    if(remaining_addr  % PAGE_SIZE != 0){
        fixed_pages++;
    }

    /* We set the first couple pages of the coremap to essentially contain the coremap*/
    for(int i = 0; i < fixed_pages; i++){
        coremap[i].status = FIXED;
        coremap[i].pfn = PAGE_SIZE * i;
        coremap[i].vfn = PADDR_TO_KVADDR(coremap[i].pfn);
        coremap[i].as = 0x0;
    }

    /* set remaining pages to be free*/
    for(int i = fixed_pages ; i < num_pages; i++){
        coremap[i].status = FREE;
        coremap[i].pfn = PAGE_SIZE * i;
        coremap[i].vfn = 0x0;
        coremap[i].as = 0x0;
    }

    bootstrapped = true;
    spinlock_release(&stealmem_lock);
}

static
paddr_t
getppages(unsigned long npages)
{
    paddr_t addr;

    if(bootstrapped == false){
          spinlock_acquire(&stealmem_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);
    }
    else{
        addr = coremap_get_pages(npages);
    }
    return addr;
}

/* 
* Allocate/free some kernel-space virtual pages 
*/
vaddr_t
alloc_kpages(unsigned npages)
{
    paddr_t pa;
    if(bootstrapped == 0){
        pa = getppages(npages);
    }
    else{
        pa = coremap_get_pages(npages);
    }
    
   return PADDR_TO_KVADDR(pa);
}

/*
* Given a virtual address, looks inside coremap and finds the entry
* Sets the entry to be free
*/
void
free_kpages(vaddr_t addr)
{
    
    struct coremap_entry* entry = coremap_find_entry_by_vaddr(addr);

    if(entry == NULL){
        kprintf("virtual address did not map to any coremap entry");
    return;
    }
    else{
        lock_acquire(coremap_lock);
        entry->status = FREE;
        
        if (entry->as != NULL){
            entry->pfn = 0;
            entry->vfn = 0;
            entry->as = 0x0;
        }
        
        lock_release(coremap_lock);
    }
    
    (void) addr;
}

/*
* Invalidates the entire TLB table
*/
void
vm_tlbshootdown_all(void)
{

    lock_acquire(coremap_lock);

    for(int i=0; i < NUM_TLB; i++){
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(),i);
    }
    lock_release(coremap_lock);

}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
(void)ts;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    (void) faulttype;
    (void) faultaddress;
    return 0;
}

/* 
* Gets the coremap entry based on the virtual address
*/
struct
coremap_entry * coremap_find_entry_by_vaddr(vaddr_t addr){
    lock_acquire(coremap_lock);
    for (int i = 0; i < num_pages; i++){
        if(coremap[i].vfn == addr){
            lock_release(coremap_lock);
            return &coremap[i];
        }
    } 
    lock_release(coremap_lock);  
    return NULL;
}

/* 
* We look through the coremap and find a contiguous block based on npages
*/
paddr_t coremap_get_pages(int npages){
    if(npages == 0){
        return 0;
    }
    else if(npages == 1){
        lock_acquire(coremap_lock);
    for(int i = 0 ; i < num_pages ; i++){
        if(coremap[i].status == FREE){
            coremap[i].status = DIRTY;
            coremap[i].as = proc_getas();
            coremap[i].vfn = PADDR_TO_KVADDR(coremap[i].pfn);
            lock_release(coremap_lock);
            return coremap[i].pfn;
        }        
    }
    lock_release(coremap_lock);
    return 0;
    }
    else{
        kprintf("\nRequested more than one page!\n");
        panic("Go crazy");
    }      
}
