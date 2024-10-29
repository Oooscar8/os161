#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <current.h>
#include <proc.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <copyinout.h>
#include <syscall.h>
#include <filetable.h>
#include <pid.h>

pid_t sys_fork(struct trapframe *tf, pid_t *retval) {
    KASSERT(curproc != NULL);
    
    // Create a new process structure for the child process.
    struct proc *child_proc = proc_create_fork("child_process");
    if (child_proc == NULL) {
        return ENOMEM; 
    }

    // Create and copy the address space.
    struct addrspace *child_as;
    int result = as_copy(curproc->p_addrspace, &child_as);
    if (result) {
        proc_destroy(child_proc);
        return result; 
    }

    child_proc->p_addrspace = child_as;
    child_proc->p_pid = pid_alloc(curproc->p_pid, child_proc);
    if (child_proc->p_pid == NO_PID) {
        as_destroy(child_as);
        proc_destroy(child_proc);
        return ENPROC;
    }

    // Copy the trapframe to the kernel heap.
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL) {
        as_destroy(child_as);
        proc_destroy(child_proc);
        return ENOMEM;
    }
    *child_tf = *tf;

    // Create a new thread for the child process.
    result = thread_fork("child_thread", child_proc,
                         (void (*)(void *, unsigned long))enter_forked_process,
                         (void *)child_tf, (unsigned long)0);
    if (result) {
        kfree(child_tf);
        as_destroy(child_as);
        proc_destroy(child_proc);
        return result; 
    }

    *retval = child_proc->p_pid;
    return 0;
}
