#ifndef _PID_H_
#define _PID_H_

#include <types.h>
#include <synch.h>
#include <limits.h>

/* Constants for PID management */
#define NO_PID      -1       /* Invalid PID value */

/* Process state flags */
#define PROC_RUNNING    0x1
#define PROC_ZOMBIE     0x2
#define PROC_EXITED     0x4

/* PID entry structure */
struct pid_entry {
    pid_t pid;              /* Process ID */
    volatile int state;     /* Process state flags */
    pid_t parent_pid;       /* Parent process ID */
    int exit_code;          /* Process exit code */
    struct cv *wait_cv;     /* Condition variable for wait/exit synchronization */
    struct proc *proc;      /* Pointer to the process structure */
};

/** 
 * Function: pid_bootstrap
 * 
 * Description: Initializes the PID management system. Must be called during system startup
 *             before any other PID-related functions.
 * 
 * @param None
 * 
 * @return 0 on success, error code on failure
 **/
int pid_bootstrap(void);

/**
 * Function: pid_alloc
 * 
 * Description: Allocates a new PID for a process. Thread-safe implementation that
 *             ensures unique PID assignment.
 * 
 * @param parent_pid - PID of the parent process
 * 
 * @return 
 *   - Allocated PID on success
 *   - NO_PID if no PIDs are available
 **/
pid_t pid_alloc(pid_t parent_pid, struct proc *p);

/**
 * Function: pid_free
 * 
 * Description: Marks a PID as free and available for reuse. Does not immediately
 *             release the PID if there are waiting parent processes.
 * 
 * @param
 *   pid - The PID to be freed
 *   exit_code - The exit code of the terminating process
 * 
 * @return 0 on success, error code on failure
 **/
int pid_free(pid_t pid, int exit_code);

/**
 * Function: pid_wait
 * 
 * Description: Waits for a specific child process to terminate. Implements the
 *             functionality needed for sys_waitpid.
 * 
 * @param
 *   pid - PID of the child process to wait for
 *   status - Pointer to store the exit status
 *   options - Wait options (reserved for future use)
 * 
 * @return 
 *   - PID of the terminated child on success
 *   - -1 on error with errno set appropriately
 **/
pid_t pid_wait(pid_t pid, int *status, int options);

/**
 * Function: pid_exists
 * 
 * Description: Checks if a given PID is currently active in the system.
 * 
 * @param
 *   pid - The PID to check
 * 
 * @return 
 *   - true if PID exists
 *   - false if PID does not exist
 **/
bool pid_exists(pid_t pid);

/**
 * Function: get_process_parent
 * 
 * Description: Returns the parent PID of a given process.
 * 
 * @param
 *   pid - The PID of the process
 * 
 * @return 
 *   - Parent PID on success
 *   - NO_PID if process doesn't exist
 **/
pid_t get_process_parent(pid_t pid);

/**
 * Function: pid_cleanup
 * 
 * Description: Cleans up any zombie processes and releases associated resources.
 *             Should be called periodically by the system.
 * 
 * @param None
 * 
 * @return Number of cleaned up processes
 **/
int pid_cleanup(void);

bool status_is_zombie(pid_t pid);

#endif /* _PID_H_ */