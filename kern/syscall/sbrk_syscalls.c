#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <syscall.h>

#define ROUNDDOWN(addr, page_size) ((addr) & ~((page_size) - 1))

int
sys_sbrk(intptr_t amount, void **retval)
{
    struct addrspace *as;
    vaddr_t old_break;
    vaddr_t new_break;
    
    as = proc_getas();
    if (as == NULL) {
        *retval = (void *)-1;
        return EFAULT;
    }
    
    old_break = as->heap_end;
    
    /* just return current break */
    if (amount == 0) {
        *retval = (void *)old_break;
        return 0;
    }
    
    new_break = old_break + amount;
    
    /* Page align */
    if (amount > 0) {
        new_break = ROUNDUP(new_break, PAGE_SIZE);
    } else {
        new_break = ROUNDDOWN(new_break, PAGE_SIZE);
    }
    
    if (new_break < as->heap_start) {
        *retval = (void *)-1;
        return EINVAL;
    }
    
    /* Check for overflow in virtual address space */
    if (amount > 0 && new_break < old_break) {
        *retval = (void *)-1;
        return ENOMEM;
    }
    
    /* Check if new break would collide with stack */
    if (new_break >= USERSTACK - PAGE_SIZE) {
        *retval = (void *)-1;
        return ENOMEM;
    }
    
    as->heap_end = new_break;
    
    *retval = (void *)old_break;
    return 0;
}