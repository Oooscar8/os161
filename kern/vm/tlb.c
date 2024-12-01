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


void
tlb_invalidate_entry(vaddr_t vaddr)
{
    KASSERT(curthread->t_in_interrupt || curthread->t_iplhigh_count > 0);
     
    int index = tlb_probe(vaddr, 0);
    if (index >= 0) {
        tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
    }
    
}

int tlb_write_entry(paddr_t pa, vaddr_t va) {

    uint32_t ehi, elo;
    int i;

    for (i = 0; i < NUM_TLB; i++) {
        tlb_read(&ehi, &elo, i);
        if ((elo & TLBLO_VALID) && (ehi == (va & TLBHI_VPAGE))) {
            elo = (pa & TLBLO_PPAGE) | TLBLO_VALID | TLBLO_DIRTY;
            tlb_write(ehi, elo, i);
            return 0;
        }
    }

    for (i = 0; i < NUM_TLB; i++) {
        tlb_read(&ehi, &elo, i);
        if (!(elo & TLBLO_VALID)) {
            ehi = va & TLBHI_VPAGE; 
            elo = (pa & TLBLO_PPAGE) | TLBLO_VALID | TLBLO_DIRTY;
            tlb_write(ehi, elo, i);
            return 0;
        }
    }

    panic("tlb_write: All TLB entries are valid\n");
    return EFAULT;  
}

// static void
// tlb_invalidate_pid(pid_t pid)
// {
//     KASSERT(curthread->t_in_interrupt || curthread->t_iplhigh_count > 0);
    
//     int spl = splhigh();
//     uint32_t entryhi, entrylo;
    
//     for (uint32_t i = 0; i < NUM_TLB; i++) {
//         tlb_read(&entryhi, &entrylo, i);
        
//         tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
//     }
    
//     splx(spl);
// }
