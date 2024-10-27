#include <types.h>
#include <current.h>
#include <proc.h>
#include <syscall.h>

pid_t 
sys_getpid(pid_t *retval)
{
    KASSERT(curproc != NULL);
    *retval = curproc->p_pid;
    return 0;
}