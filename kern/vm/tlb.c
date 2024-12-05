/* 
 * TLB management functions for OS/161 MIPS
 * Includes synchronization for 4-CPU system
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <vm.h>
#include <thread.h>
#include <wchan.h>
#include <cpu.h>
#include <membar.h>
#include <thread.h>
#include <pagetable.h>

static struct tlbshootdown ts;

void
tlbshootdown_broadcast(vaddr_t vaddr, pid_t pid)
{
    ts.ts_source = curthread;
    ts.ts_vaddr = vaddr;
    ts.ts_pid = pid;

    flush_tlb(&ts);
}

void vm_tlbshootdown_all(void) {
    KASSERT(curthread->t_in_interrupt || curthread->t_iplhigh_count > 0);
    // Invalidate all TLB entries
    for (uint32_t i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
}


void vm_tlbshootdown(const struct tlbshootdown *ts) {
    // Validate input
    KASSERT(ts != NULL);
    
    // Only invalidate TLB entries for the specified process

    // Invalidate the specific TLB entry for the virtual address
    int index = tlb_probe(ts->ts_vaddr, 0);
    if (index >= 0) {
        tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
    }
}


// accessed bit = 0
void
tlb_invalidate_entry(vaddr_t vaddr)
{
    //KASSERT(curthread->t_in_interrupt || curthread->t_iplhigh_count > 0);
    int spl = splhigh();
    int index = tlb_probe(vaddr, 0);
    if (index >= 0) {
        tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
    }
    splx(spl);
    
}

int tlb_write_entry(uint32_t entryhi, uint32_t entrylo) {

    int i;
    int spl = splhigh();










    i = tlb_evict();
    tlb_write(entryhi, entrylo, i);

    splx(spl);
    return 0;
}

int
tlb_evict(void)
{
    uint32_t entryhi, entrylo;
    int i;
    
    KASSERT(curthread->t_in_interrupt || curthread->t_iplhigh_count > 0);

    /* First try to find an invalid entry */
    for (i = 0; i < NUM_TLB; i++) {
        tlb_read(&entryhi, &entrylo, i);
        if (!(entrylo & TLBLO_VALID)) {
            return i;
        }
    }
    
    /* Then look for an no global entry with */
    for (i = 0; i < NUM_TLB; i++) {
        tlb_read(&entryhi, &entrylo, i);
        if ((entrylo & TLBLO_GLOBAL) != 0) {
            return i;
        }
    }

    /* random */
    int index = random() % NUM_TLB;
    tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
    return index;

    /* Should never get here - all TLB entries should fall into one of above categories */
    panic("tlb_evict: Cannot find entry to evict\n");
}

void tlb_invalidate_all(void) {
    int spl = splhigh();
    uint32_t i, ehi, elo;
    for (i = 0; i < NUM_TLB; i++) {
        tlb_read(&ehi, &elo, i);
        if ((elo & TLBLO_VALID ) && (elo & TLBLO_GLOBAL) == 0) {
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
        }
    }
    splx(spl);
}
