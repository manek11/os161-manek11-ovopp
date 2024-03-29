/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <proc.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

/*
 * manager for creating addresspace.
 * 
*/

struct addrspace *
as_create(void)
{
    struct addrspace *as;

    as = kmalloc(sizeof(struct addrspace));
    if (as == NULL) {
        return NULL;
    }

    /*
    * Initialize as needed.
    */
    as->as_vbase1 = 0;
    as->as_vbase2 = 0;
    as->as_npages1 = 0;
    as->as_npages2 = 0;
    as->as_heapbase = 0;
    as->as_stackpbase = 0;
    return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    struct addrspace *newas;

    newas = as_create();
    if (newas==NULL) {
        return ENOMEM;
    }

    newas->as_vbase1 = old->as_vbase1;
    newas->as_vbase2 = old->as_vbase2;
    newas->as_npages1 = old->as_npages1;
    newas->as_npages2 = old->as_npages2;
    newas->as_heapbase = newas->as_vbase2 + (newas->as_npages2*PAGE_SIZE);
    newas->as_stackpbase = newas->as_heapbase;
    
    *ret = newas;
    return 0;
}

void
as_destroy(struct addrspace *as)
{
    /*
    * Clean up as needed.
    */

    kfree(as);
}

void
as_activate(void)
{
    /* 
    * Should handle and swap pages during context switches 
    */
    
}

void
as_deactivate(void)
{
    /*
    * Write this. For many designs it won't need to actually do
    * anything. See proc.c for an explanation of why it (might)
    * be needed.
    */
}

/*
* Set up a segment at virtual address VADDR of size MEMSIZE. The
* segment in memory extends from VADDR up to (but not including)
* VADDR+MEMSIZE.
*
* The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
* write, or execute permission should be set on the segment. At the
* moment, these are ignored. When you write the VM system, you may
* want to implement them.
*/
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
int readable, int writeable, int executable)
{
    // segments, base vaddr, array of pages (each entry of coremap uses the same pages), defining overlaps
    // we don't want multiple / overlapping segments
    int npages;
    (void)readable;
    (void)writeable;
    (void)executable;
    sz += vaddr & ~(vaddr_t) PAGE_FRAME;
    vaddr &= PAGE_FRAME;
    sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;
    npages = sz / PAGE_SIZE;

    if (as->as_vbase1 == 0) {
        as->as_vbase1 = vaddr;
        as->as_npages1 = npages;
        as->as_heapbase = as->as_vbase2 + (as->as_npages2 * PAGE_SIZE);
        as->as_stackpbase = as->as_heapbase;
    return 0;
    }

    if (as->as_vbase2 == 0) {
        as->as_vbase2 = vaddr;
        as->as_npages2 = npages;
        as->as_heapbase = as->as_vbase2 + (as->as_npages2 * PAGE_SIZE);
        as->as_stackpbase = as->as_heapbase;
        return 0;
    }
    return ENOSYS;
}

int
as_prepare_load(struct addrspace *as)
{
    /*
    * Write this.
    */

    (void)as;
    return 0;
}

int
as_complete_load(struct addrspace *as)
{
    /*
    * Write this.
    */

    (void)as;
    return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    /*
    * Write this.
    */

    (void)as;

    /* Initial user-level stack pointer */
    *stackptr = USERSTACK;

    return 0;
}

