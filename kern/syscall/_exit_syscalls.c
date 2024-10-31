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

    /* Check children and cleanup any that are zombies */
    struct proc *p;
    spinlock_acquire(&pid_lock);
    for (int i = 0; i < PID_COUNT; i++) {
        p = pid_table[i].proc;
        if (p != NULL && p->p_parent == curproc) {
            spinlock_acquire(&p->p_lock);
            if (p->p_state == PROC_ZOMBIE) {
                /* Child is zombie, can clean it up now */
                proc_destroy(p);
            }
            spinlock_release(&p->p_lock);
        }
    }
    spinlock_release(&pid_lock);
    
    /* Check if parent is alive (not zombie or dead) */
    if (curproc->p_parent != NULL && 
        curproc->p_parent->p_state != PROC_ZOMBIE && 
        curproc->p_parent->p_state != PROC_DEAD) {
        V(curproc->p_sem);  // Wake up parent if it's waiting
    } else {
        /* Parent is gone, self-cleanup */
        proc_destroy(curproc);
    } 
    
    spinlock_release(&curproc->p_lock);
    
    thread_exit();
    
    /* Should never get here */
    panic("sys__exit: thread_exit returned\n");
}