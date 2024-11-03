#include <types.h>
#include <kern/errno.h>
#include <pid.h>
#include <current.h>
#include <proc.h>
#include <mips/trapframe.h>
#include <syscall.h>
#include <thread.h>
#include <kern/wait.h>
#include <copyinout.h>


pid_t 
sys_waitpid(pid_t pid, int *status, int options, int *retval)
{
    if (options != 0) {  
        return EINVAL;
    }
    
    if (!pid_exists(pid)) {
        return ESRCH;
    }

    KASSERT(curproc != NULL);

    if (get_process_parent(pid) != curproc->p_pid) {
        return ECHILD;
    }


    int exit_status;
    int result = pid_wait(pid, &exit_status, options);
    
    if (result < 0) {
        return result;
    }

    if (status != NULL) {
        int err = copyout(&exit_status, (userptr_t) status, sizeof(int));
        if (err) {
            return EFAULT;
        }
    }

    pid_cleanup();
    
    *retval = pid;  
    return 0;
}