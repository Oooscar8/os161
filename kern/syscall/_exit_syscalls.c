#include <types.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <spinlock.h>
#include <pid.h>
#include <syscall.h>

/*
 * Exit system call.
 */
void
sys__exit(int exitcode)
{  
    KASSERT(curproc != NULL);
    KASSERT(curproc != kproc);    /* Kernel process cannot exit */
    
    /* Get the lock */
    spinlock_acquire(&curproc->p_lock);
    
    /* Set exit code and change state to zombie */
    curproc->p_exitcode = _MKWAIT_EXIT(exitcode);
    curproc->p_state = PROC_ZOMBIE;

    /* 
     * Handle children
     * - zombies and dead children get cleaned up immediately,
     * - running processes become orphans that clean themselves up
     */
    struct proc *child;
    for (unsigned i = 0; i < array_num(curproc->p_children); i++) {
        child = array_get(curproc->p_children, i);
        spinlock_acquire(&child->p_lock);
        
        /* Clean up zombie or dead child immediately */
        if (child->p_state == PROC_ZOMBIE || child->p_state == PROC_DEAD) {
            child->p_state = PROC_DEAD;
            spinlock_release(&child->p_lock);

            /* Remove from children array */
            array_remove(curproc->p_children, i);
            i--;

            spinlock_release(&curproc->p_lock);
            /* Clean up the zombie or dead child */
            proc_remove_pid(child);
            proc_destroy(child);
            spinlock_acquire(&curproc->p_lock);
        } else {
            /* Mark as orphaned - will self-cleanup when done */
            child->p_parent = NULL;
            spinlock_release(&child->p_lock);
            
            /* Remove from our children array */
            array_remove(curproc->p_children, i);
            i--;
        }
    }

    /* Signal parent if we have one, otherwise self-cleanup */
    if (curproc->p_parent != NULL) {
        spinlock_release(&curproc->p_lock);
        V(curproc->p_sem);  // Wake up waiting parent
    } else {
        /* Orphaned process cleans itself up */
        curproc->p_state = PROC_DEAD;
        spinlock_release(&curproc->p_lock);
    }
    
    thread_exit();
    
    /* Should never get here */
    panic("sys__exit: thread_exit returned\n");
}