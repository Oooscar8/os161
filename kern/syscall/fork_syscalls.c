#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <addrspace.h>
#include <mips/trapframe.h>
#include <proc.h>

int sys_fork(struct trapframe *tf, pid_t *retval)
{
    struct trapframe *child_tf;  // Child's trapframe
    struct addrspace *child_as;  // Child's address space
    struct filetable *child_ft;  // Child's file table
    struct proc *child_proc;     // Child process
    int result;

    // Create child trapframe copy
    child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL)
    {
        return ENOMEM;
    }
    *child_tf = *tf;

    // Copy address space
    result = as_copy(proc_getas(), &child_as);
    if (result)
    {
        kfree(child_tf);
        return result;
    }

    // Copy file table
    child_ft = filetable_copy(curproc->p_filetable);
    if (child_ft == NULL)
    {
        as_destroy(child_as);
        kfree(child_tf);
        return ENOMEM;
    }

    // Create new process structure
    child_proc = proc_create_fork("child");
    if (child_proc == NULL)
    {
        filetable_destroy(child_ft);
        as_destroy(child_as);
        kfree(child_tf);
        return ENOMEM;
    }

    // Set up child process
    child_proc->p_filetable = child_ft;
    child_proc->p_addrspace = child_as;

    // Create new thread for child process
    result = thread_fork(
        curthread->t_name,
        child_proc,
        (void (*)(void *, unsigned long))enter_forked_process,    // New entry point function
        child_tf,           // Pass child trapframe as data1
        0                    // Unused data2
    );

    if (result)
    {
        proc_destroy(child_proc);
        kfree(child_tf);
        return result;
    }

    // Parent returns child's PID
    *retval = child_proc->p_pid;

    return 0;
}