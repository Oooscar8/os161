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
void sys__exit(int exitcode)
{
    KASSERT(curproc != NULL);
    KASSERT(curproc != kproc); /* Kernel process cannot exit */

    /* Get the lock */
    lock_acquire(curproc->p_mutex);

    /* Set exit code and change state to zombie */
    curproc->p_exitcode = _MKWAIT_EXIT(exitcode);
    curproc->p_state = PROC_ZOMBIE;

    /*
     * Handle children
     * - zombies and dead children get cleaned up immediately,
     * - running processes become orphans that clean themselves up
     */
    struct proc *child;
    while (array_num(curproc->p_children) > 0)
    {
        child = array_get(curproc->p_children, 0);

        if (child->p_state == PROC_ZOMBIE || child->p_state == PROC_DEAD)
        {
            KASSERT(array_num(child->p_children) == 0);

            child->p_state = PROC_DEAD;

            /* Remove from children array */
            array_remove(curproc->p_children, 0);

            /* Clean up the zombie/dead child */
            proc_remove_pid(child);
            proc_destroy(child);
        }
        else
        {
            /* Mark as orphaned - will self-cleanup when done */
            child->p_parent = NULL;

            /* Remove from children array */
            array_remove(curproc->p_children, 0);
        }
    }

    /* Signal parent if we have one, otherwise self-cleanup */
    if (curproc->p_parent != NULL)
    {
        cv_signal(curproc->p_cv, curproc->p_mutex); // Wake up waiting parent
    }
    else
    {
        /* Orphaned process cleans itself up */
        curproc->p_state = PROC_DEAD;
    }

    lock_release(curproc->p_mutex);

    thread_exit();

    /* Should never get here */
    panic("sys__exit: thread_exit returned\n");
}