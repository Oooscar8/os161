/*
 * Waitpid system call.
 */
int
sys_waitpid(pid_t pid, userptr_t status, int options)
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
    KASSERT(child->p_state == PROC_ZOMBIE);
    exitcode = child->p_exitcode;
    spinlock_release(&child->p_lock);
    
    /* Copy out the exit status if requested */
    if (status != NULL) {
        int result = copyout(&exitcode, status, sizeof(int));
        if (result) {
            /* Still need to clean up the child */
            spinlock_acquire(&pid_lock);
            pid_table[pid_to_index(pid)].proc = NULL;
            pid_count--;
            spinlock_release(&pid_lock);
            
            proc_destroy(child);
            return EFAULT;
        }
    }
    
    /* Remove from PID table */
    spinlock_acquire(&pid_lock);
    pid_table[pid_to_index(pid)].proc = NULL;
    pid_count--;
    spinlock_release(&pid_lock);
    
    /* Now safe to destroy the process */
    proc_destroy(child);
    
    return pid;
}