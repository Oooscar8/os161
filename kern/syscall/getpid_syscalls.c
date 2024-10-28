#include <types.h>
#include <kern/errno.h> 
#include <proc.h>     
#include <current.h>   

/*  
 * System call for getting process ID.
 * Just returns the current process's pid.
 */
int sys_getpid(pid_t *retval)
{
    KASSERT(curproc != NULL);  // Make sure we have a current process

    *retval = curproc->p_pid;  // Set return value to current process's PID
    return 0;                   // Return success
}