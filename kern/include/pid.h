#ifndef _PID_H_
#define _PID_H_

/*
 * Process ID management.
 */

#define PID_KERNEL    1

/* Number of PIDs available */
#define PID_COUNT   (PID_MAX - PID_MIN + 1)

/* Error value returned by pid_allocate */
#define ENOPID      (-1)    /* No PIDs available */

/* PID table entry structure */
struct pid_entry {
    pid_t pid;               /* Process ID */
    struct proc *proc;      /* Pointer to process structure */
};

/* Global PID management state */
struct pid_entry *pid_table;     /* PID allocation table */
struct spinlock pid_lock;        /* Lock for PID operations */
unsigned int pid_count;          /* Number of PIDs in use */
pid_t next_pid;                  /* Next PID to try for allocation */

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
 * Convert PID to table index.
 */
int pid_to_index(pid_t pid);

/*
 * Get process structure associated with PID.
 * Returns NULL if PID is invalid or not in use.
 */
struct proc *pid_get_proc(pid_t pid);

#endif /* _PID_H_ */