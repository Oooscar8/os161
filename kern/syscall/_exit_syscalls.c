/*
 * Exit system call.
 */
void
sys__exit(int exitcode)
{
    struct proc *cur = curproc;
    
    KASSERT(cur != NULL);
    KASSERT(cur != kproc);    /* Kernel process cannot exit */
    
    /* Get the lock */
    spinlock_acquire(&cur->p_lock);
    
    /* Set exit code and change state to zombie */
    cur->p_exitcode = _MKWAIT_EXIT(exitcode);
    cur->p_state = PROC_ZOMBIE;

    /* Signal any waiting parent */
    if (cur->p_parent != NULL) {
        V(cur->p_sem);  /* Wake up parent if it's waiting */
    } 
    
    spinlock_release(&cur->p_lock);
    
    thread_exit();
    
    /* Should never get here */
    panic("sys__exit: thread_exit returned\n");
}