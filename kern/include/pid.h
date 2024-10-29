#ifndef _PID_H_
#define _PID_H_

#include <types.h>
#include <spinlock.h>
#include <limits.h>

/* Error codes */
#define ENOPID      (-1)     /* No PIDs available */

/* Initialize the PID management system */
void pid_bootstrap(void);

/* Allocate a new PID and associate it with a process */
pid_t pid_allocate(struct proc *proc);

/* Free a PID */
void pid_free(pid_t pid);

/* Get process structure associated with PID */
struct proc *pid_get_proc(pid_t pid);

/* Check if PID is used and is valid */
int pid_used(pid_t pid);

#endif /* _PID_H_ */