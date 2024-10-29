#ifndef _PID_H_
#define _PID_H_

/*
 * Process ID management.
 */

/* Error value returned by pid_allocate */
#define ENOPID      (-1)    /* No PIDs available */

/* 
 * Initialize the PID management system.
 * Called during system bootstrap.
 */
void pid_bootstrap(void);

/* 
 * Allocate a new PID and associate it with a process.
 * Returns allocated PID on success, ENOPID on failure.
 */
pid_t pid_allocate(struct proc *proc);

/*
 * Get process structure associated with PID.
 * Returns NULL if PID is invalid or not in use.
 */
struct proc *pid_get_proc(pid_t pid);

#endif /* _PID_H_ */