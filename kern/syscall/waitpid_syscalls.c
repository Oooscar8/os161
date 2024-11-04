#include <types.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <spinlock.h>
#include <copyinout.h>
#include <pid.h>
#include <syscall.h>

/*
 * Waitpid system call.
 */
int
sys_waitpid(pid_t pid, userptr_t status, int options, int *retval)
{
    struct proc *child;
    int exitcode;
    
    /* Check for invalid options */
    if (options != 0) {
        return EINVAL;
    }
    
    /* Get the child process */
    child = pid_get_proc(pid);
    if (child == NULL) {
        return ESRCH;  /* No such process */
    }
    
    /* Verify this is our child */
    lock_acquire(child->p_mutex);
    if (child->p_parent != curproc) {
        lock_release(child->p_mutex);
        return ECHILD;  /* Not a child of calling process */
    }

    /* Wait for child to exit */
    while (child->p_state == PROC_RUNNING) {
        cv_wait(child->p_cv, child->p_mutex);
    }

    /* Get exit status while holding the lock */
    exitcode = child->p_exitcode;
    child->p_state = PROC_DEAD;

    lock_release(child->p_mutex);

    if (curproc == kproc) {
        /* 
         * If we're the kernel, we do not exit, so we need to
         * clean up the child process here.
         */
        proc_remove_pid(child);
        proc_destroy(child);
    }

    /* Now copy out exit status if requested */
    if (status != NULL) {
        int result = copyout(&exitcode, status, sizeof(int));
        if (result) {
            return EFAULT;
        }
    }
    
    *retval = pid;
    return 0;
}