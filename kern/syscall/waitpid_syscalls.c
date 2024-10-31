/*
 * Waitpid system call.
 */
int
sys_waitpid(pid_t pid, int* status, int options, int *retval)
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
    if (child->p_parent != curproc) {
        return ECHILD;  /* Not a child of calling process */
    }
    
    /* Wait for child to exit */
    P(child->p_sem);
    
    /* Get exit status while holding the spinlock */
    spinlock_acquire(&child->p_lock);

    exitcode = child->p_exitcode;

    /* 
     * Mark as dead - means this process has been waited for
     * and is ready for cleanup
     */
    KASSERT(child->p_state == PROC_ZOMBIE);
    child->p_state = PROC_DEAD;

    spinlock_release(&child->p_lock);
    
    /* Now safe to destroy the process */
    proc_destroy(child);

    /* Now copy out exit status if requested */
    if (status != NULL) {
        int result = copyout(&exitcode, (userptr_t)status, sizeof(int));
        if (result) {
            return EFAULT;
        }
    }
    
    *retval = pid;
    return 0;
}