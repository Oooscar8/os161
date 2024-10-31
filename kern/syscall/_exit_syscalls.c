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
     * Reassign all children to init process (PID 1)
     * This is done before becoming a zombie so we maintain
     * a consistent process hierarchy
     */
    struct proc *p;
    spinlock_acquire(&pid_lock);
    for (int i = 0; i < MAX_PROCS; i++) {
        p = pid_table[i].proc;
        if (p != NULL && p->p_parent == curproc) {
            spinlock_acquire(&p->p_lock);         
            p->p_parent = pid_table[PID_INIT].proc;
            
            /* If child is already zombie, signal init to collect it */
            if (p->p_state == PROC_ZOMBIE) {
                V(p->p_sem);
            }
            spinlock_release(&p->p_lock);
        }
    }
    spinlock_release(&pid_lock);

    /* Signal any waiting parent */
    if (curproc->p_parent != NULL) {
        V(curproc->p_sem);  /* Wake up parent if it's waiting */
    } 
    
    spinlock_release(&curproc->p_lock);
    
    thread_exit();
    
    /* Should never get here */
    panic("sys__exit: thread_exit returned\n");
}