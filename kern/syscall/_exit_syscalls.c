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
     * If parent is kproc, mark as DEAD
     * proc will be destroyed when it's safe to do so
     */
    spinlock_acquire(&pid_lock);

    if (curproc->p_parent == kproc) {
        curproc->p_state = PROC_DEAD;
    }

    /* 
     * Mark all zombie children as DEAD and reassign live ones to kernel process.
     */
    struct proc *p;
    int i;
    for (i = 0; i < PID_COUNT; i++) {
        p = pid_table[i].proc;
        if (p != NULL && p->p_parent == curproc) {
            spinlock_acquire(&p->p_lock);         
            if (p->p_state == PROC_ZOMBIE) {
                // If child process is already in zombie state, mark it as DEAD.
                p->p_state = PROC_DEAD;
            } else {
                // If child process is still running, reassign it to kernel process.
                p->p_parent = kproc;
            }
            spinlock_release(&p->p_lock);
        }
    }

    spinlock_release(&pid_lock);
    spinlock_release(&curproc->p_lock);
    
    thread_exit();
    
    /* Should never get here */
    panic("sys__exit: thread_exit returned\n");
}