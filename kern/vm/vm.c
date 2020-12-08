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

#define DUMBVM_STACKPAGES    18

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct coremap_entry *coremap;
static bool bootstrapped = false;
static int num_pages = 0;

void
vm_bootstrap(void)
{
    coremap_lock = lock_create("coremap_lock");
    size_t coremap_size;
    paddr_t firstaddr, lastaddr;
    spinlock_acquire(&stealmem_lock);
    // [coremap array] - firstaddr ----------- lastaddr
    lastaddr = ram_getsize();
    // kprintf("%d last addr", lastaddr);
    firstaddr = ram_getfirstfree();
    num_pages = (lastaddr-firstaddr) / PAGE_SIZE;
    // kprintf("%d first addr\n",firstaddr);
    
    coremap_size = num_pages * sizeof(struct coremap_entry);
    if(firstaddr + coremap_size >= lastaddr){
        panic("Coremap takes up too much physical memory");    
    }
    
    coremap = (struct coremap_entry*) PADDR_TO_KVADDR(firstaddr);
    
    // coremap live in kva, figure out which addr the kva is at, loop through and init each entry
    
    paddr_t remaining_addr = firstaddr + num_pages * sizeof(struct coremap_entry);
    
    // we need to make the first couple pages in coremap fixed
    // we need to make sure the size is aligned to page_size
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

	spinlock_acquire(&stealmem_lock);
    if(bootstrapped == false){
	addr = ram_stealmem(npages);
    }
    else{
        addr = coremap_get_pages(npages);
    }
	spinlock_release(&stealmem_lock);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;
	if(bootstrapped == 0){
	    pa = getppages(npages);
	}
	else{
	    for(int i = 0; i < num_pages; i++){
	        if(coremap[i].status == FREE){
	            lock_acquire(coremap_lock);
	            coremap[i].status = FIXED;
	            pa = coremap[i].pfn;
	            coremap[i].vfn = PADDR_TO_KVADDR(pa);
	            lock_release(coremap_lock);
	            break;
	        }
	    }
	}
	return PADDR_TO_KVADDR(pa);
}

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
}

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
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}

/* COREMAP FUNCTIONS */

/* gets the coremap entry based on the virtual address */
struct coremap_entry * coremap_find_entry_by_vaddr(vaddr_t addr){
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

/* We look through the coremap and find a contiguous block based on npages*/
paddr_t coremap_get_pages(int npages){
    // error cannot request 0 pages
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
