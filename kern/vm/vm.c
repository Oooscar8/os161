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
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <types.h>
#include <spinlock.h>
#include <pmm.h>
#include <pagetable.h>
#include <vaa.h>

bool vm_initialized = false;
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock alloc_lock = SPINLOCK_INITIALIZER;


void vm_bootstrap(void)
{
	pagetable_bootstrap();
	pagetable_init();
	pmm_init();
	vm_initialized = true;
}

static paddr_t
getppages(unsigned long npages)
{
	if (!vm_initialized)
	{
		paddr_t addr;
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
		return addr;
	}
	else
	{
		KASSERT(npages == 1);
		spinlock_acquire(&alloc_lock);
		paddr_t addr = pmm_alloc_page();
		spinlock_release(&alloc_lock);
		if (addr == 0) {
			panic("out of memory\n");
		}
		return addr;
	}
}

vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa == 0)
	{
		return 0;
	}
	if (!vm_initialized)
	{
		return PADDR_TO_KVADDR(pa);
	}
	else
	{
		KASSERT(npages == 1);
		spinlock_acquire(&alloc_lock);
		vaddr_t va = vaa_alloc_kpage();
		KASSERT(va %PAGE_SIZE == 0);
		KASSERT(pa %PAGE_SIZE == 0);
		pte_map(kernel_pt, va, pa, PTE_WRITE);
		
		//zero out the page
    	memset((void *)va, 0, PAGE_SIZE);
		spinlock_release(&alloc_lock);
		return va;
	}
}

void free_kpages(vaddr_t addr)
{	
	//not free pages allocated by ram_stealmem
	if (!vm_initialized || (addr >= MIPS_KSEG0 && addr < MIPS_KSEG1))
	{
		return;
	}
	else if (addr >= MIPS_KSEG2)
	{	
		spinlock_acquire(&alloc_lock);
		vaa_free_kpage(addr);
		tlb_invalidate(addr);
		paddr_t paddr = pagetable_translate(kernel_pt, addr, NULL);
		pte_unmap(kernel_pt, addr);
		pmm_free_page(paddr);
		spinlock_release(&alloc_lock);
	}
}

void vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int vm_fault(int faulttype, vaddr_t faultaddress)
{
	paddr_t paddr;
	if (faultaddress >= MIPS_KSEG0 && faultaddress < MIPS_KSEG1)
	{
		paddr = faultaddress - MIPS_KSEG0;
	}
	if (faultaddress >= MIPS_KSEG2 )
	{
		uint32_t flags;
		paddr = pagetable_translate(kernel_pt, faultaddress, &flags);
		if (paddr == 0)
		{
			return EFAULT;
		}

		if (faulttype == VM_FAULT_READONLY)
		{
			if (flags & PTE_WRITE)
			{
				return EFAULT;
			}
		}
	}

	uint32_t ehi, elo;
	int i, spl;

	spl = splhigh();

	for (i = 0; i < NUM_TLB; i++)
	{
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID)
		{
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


// /* 跟踪最老的TLB条目的索引 */
// static uint32_t tlb_next_victim = 0;

// /* 
//  * 从TLB中查找空闲条目
//  * 返回找到的索引,如果没有空闲条目则返回-1
//  */
// static int
// tlb_find_empty(void) 
// {
//     int i;
//     uint32_t entryhi, entrylo;

//     for (i = 0; i < NUM_TLB; i++) {
//         tlb_read(&entryhi, &entrylo, i);
//         if (entrylo & TLBLO_VALID) {
//             continue;
//         }
//         return i;
//     }
//     return -1;
// }

// /*
//  * 将虚拟地址和物理页帧映射写入TLB
//  * 如果TLB已满,执行FIFO替换
//  */
// int
// tlb_insert(vaddr_t vaddr, paddr_t paddr)
// {
//     uint32_t ehi, elo;
//     int i;

//     /* 制作TLB条目 */
//     ehi = vaddr & TLBHI_VPAGE;
//     elo = (paddr & TLBLO_PPAGE) | TLBLO_VALID;

//     /* 查找空闲TLB条目 */
//     i = tlb_find_empty();
    
//     if (i < 0) {
//         /* TLB已满 - 使用FIFO替换 */
//         i = tlb_next_victim;
//         tlb_next_victim = (tlb_next_victim + 1) % NUM_TLB;
//     }

//     /* 写入新的TLB条目 */
//     tlb_write(ehi, elo, i);
//     return 0;
// }
