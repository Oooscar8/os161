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

    /* Signal any waiting parent */
    if (curproc->p_parent != NULL) {
        V(curproc->p_sem);  /* Wake up parent if it's waiting */
    } 
    
    spinlock_release(&curproc->p_lock);
    
    thread_exit();
    
    /* Should never get here */
    panic("sys__exit: thread_exit returned\n");
}