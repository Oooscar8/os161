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

    /* Signal parent if waiting */
    V(curproc->p_sem);

    /* 
     * If parent is kproc, mark as DEAD
     */
    if (curproc->p_parent == kproc) {
        curproc->p_state = PROC_DEAD;
    }

    /*
     * Mark all zombie children as DEAD and reassign live ones to kernel process.
     */
    struct proc *p;
    for (unsigned i = 0; i < array_num(curproc->p_children); i++) {
        p = array_get(curproc->p_children, i);
        spinlock_acquire(&p->p_lock);
        
        if (p->p_state == PROC_ZOMBIE) {
            // If child process is already in zombie state, mark it as DEAD.
            p->p_state = PROC_DEAD;
        } else {
            // If child process is still running, reassign it to kernel process.
            p->p_parent = kproc;
            
            // Add to kproc's children list
            spinlock_acquire(&kproc->p_lock);
            KASSERT(array_add(kproc->p_children, p, NULL) == 0);
            spinlock_release(&kproc->p_lock);
        }

        spinlock_release(&p->p_lock);
    }

    spinlock_release(&curproc->p_lock);
    
    thread_exit();
    
    /* Should never get here */
    panic("sys__exit: thread_exit returned\n");
}