#include <types.h>
#include <kern/errno.h>
#include <pid.h>
#include <current.h>
#include <proc.h>
#include <mips/trapframe.h>
#include <syscall.h>
#include <thread.h>
#include <kern/wait.h>

void 
sys__exit(int exitcode)
{
    KASSERT(curproc != NULL);

    if (curproc == kproc) {
        panic("Trying to exit the kernel process!\n");
    }

    int exit_status;
    
    if (exitcode & 0x80) {  
        int sig = (exitcode >> 8) & 0x7f;
        exit_status = _MKWAIT_SIG(sig);
    } else {
        exit_status = _MKWAIT_EXIT(exitcode);
    }

    struct proc *p = curproc;
    pid_free(p->p_pid, exit_status);
    thread_exit();
    panic("sys__exit returned\n");
}