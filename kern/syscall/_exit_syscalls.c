#include <types.h>
#include <kern/errno.h>
#include <pid.h>
#include <current.h>
#include <proc.h>
#include <mips/trapframe.h>
#include <syscall.h>
#include <thread.h>

void 
sys__exit(int exitcode)
{
    KASSERT(curproc != NULL);

    if (curproc == kproc) {
        panic("Trying to exit the kernel process!\n");
    }

    int exit_code = exitcode & 0x7f;  

    DEBUG(DB_SYSCALL, "Process %d (_exit: %d)\n", 
          curproc->p_pid, exit_code);

    struct proc *p = curproc;
    //proc_remthread(curthread);
    pid_free(p->p_pid, exit_code);
    //pid_cleanup();
    thread_exit();
    panic("sys__exit returned\n");
}