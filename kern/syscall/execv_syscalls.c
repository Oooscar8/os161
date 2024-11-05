#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>

/*
 * System call that replaces the currently executing program with a newly loaded
 * program image.
 *
 * @param program - User pointer to the executable path
 * @param args - User pointer to array of argument pointers
 * @return - Does not return on success, returns error code on failure
 */
int sys_execv(const_userptr_t program, userptr_t *args)
{
    int result;
    size_t actual;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    char *kprogram = NULL;  // Kernel buffer for program path
    char **kargs = NULL;    // Kernel buffer for args array
    char *kargbuf = NULL;   // Kernel buffer for arg strings
    int nargs = 0;          // Number of arguments
    size_t total_bytes = 0; // Total size of arguments
    struct addrspace *old_as;
    struct addrspace *new_as;

    /* Copy the program path and verify it */
    kprogram = kmalloc(PATH_MAX);
    if (kprogram == NULL)
    {
        result = ENOMEM;
        goto err1;
    }

    result = copyinstr(program, kprogram, PATH_MAX, &actual);
    if (result)
    {
        goto err1;
    }

    /* Copy the arguments into kernel space */
    if (args != NULL)
    {
        /* First, count the number of arguments */
        while (true)
        {
            userptr_t arg;
            result = copyin((const_userptr_t)args[nargs], arg, sizeof(userptr_t));
            if (result)
            {
                goto err1;
            }
            if (arg == NULL)
                break;
            nargs++;
        }

        /* Allocate kernel args array */
        if (nargs > 0)
        {
            kargs = kmalloc(nargs * sizeof(char *));
            if (kargs == NULL)
            {
                result = ENOMEM;
                goto err1;
            }
        }

        /* Copy the array of argument pointers from user space */
        result = copyin((const_userptr_t)args, kargs, nargs * sizeof(char *));
        if (result)
        {
            goto err2;
        }

        /* Calculate total bytes needed for strings */
        total_bytes = 0;
        char *argstr;
        for (int i = 0; i < nargs; i++)
        {
            size_t len;
            result = copyinstr((const_userptr_t)kargs[i], argstr, ARG_MAX, &len);
            if (result)
            {
                goto err2;
            }
            total_bytes += ROUNDUP(len, 4); // Align to 4 bytes
        }

        /* Check if total size exceeds ARG_MAX */
        if (total_bytes > ARG_MAX)
        {
            result = E2BIG;
            goto err2;
        }

        /* Allocate buffer for argument strings */
        kargbuf = kmalloc(total_bytes);
        if (kargbuf == NULL)
        {
            result = ENOMEM;
            goto err2;
        }

        /* Copy strings to kernel buffer */
        char *bufptr = kargbuf;
        for (int i = 0; i < nargs; i++)
        {
            size_t len;
            /* Copy string from user space */
            result = copyinstr((const_userptr_t)kargs[i], bufptr, ARG_MAX, &len);
            if (result)
            {
                goto err3;
            }
            kargs[i] = bufptr; /* Store pointer to string in kernel array */
            bufptr += ROUNDUP(len, 4);
        }
    }

    /* Open the executable */
    result = vfs_open(kprogram, O_RDONLY, 0, &v);
    if (result)
    {
        goto err3;
    }

    /* Step 4: Create new address space */
    new_as = as_create();
    if (new_as == NULL)
    {
        result = ENOMEM;
        vfs_close(v);
        goto err3;
    }

    /* Step 5: Switch to new address space */
    old_as = proc_setas(new_as);
    as_activate();

    /* Step 6: Load the executable */
    result = load_elf(v, &entrypoint);
    if (result)
    {
        proc_setas(old_as);
        as_activate();
        as_destroy(new_as);
        vfs_close(v);
        goto err3;
    }

    vfs_close(v);

    /* Step 7: Define the user stack */
    result = as_define_stack(new_as, &stackptr);
    if (result)
    {
        proc_setas(old_as);
        as_activate();
        as_destroy(new_as);
        goto err3;
    }

    /* Step 8: Copy arguments to user stack */
    if (nargs > 0)
    {
        vaddr_t stack_ptr = stackptr;
        userptr_t *arg_ptrs;
        userptr_t argv_ptr;

        /* Allocate space for argument pointers */
        stack_ptr -= (nargs + 1) * sizeof(userptr_t);
        stack_ptr = ROUNDDOWN(stack_ptr, 8); // Ensure proper alignment
        arg_ptrs = (userptr_t *)kmalloc((nargs + 1) * sizeof(userptr_t));
        if (arg_ptrs == NULL)
        {
            result = ENOMEM;
            proc_setas(old_as);
            as_activate();
            as_destroy(new_as);
            goto err3;
        }
        argv_ptr = (userptr_t)stack_ptr;

        /* Copy strings to user stack and build pointer array */
        for (int i = nargs - 1; i >= 0; i--)
        {
            size_t len = strlen(kargs[i]) + 1;
            stack_ptr -= ROUNDUP(len, 4);
            result = copyout(kargs[i], (userptr_t)stack_ptr, len);
            if (result)
            {
                kfree(arg_ptrs);
                proc_setas(old_as);
                as_activate();
                as_destroy(new_as);
                goto err3;
            }
            arg_ptrs[i] = (userptr_t)stack_ptr;
        }
        arg_ptrs[nargs] = NULL;

        /* Copy argument pointer array */
        result = copyout(arg_ptrs, argv_ptr, (nargs + 1) * sizeof(userptr_t));
        kfree(arg_ptrs);
        if (result)
        {
            proc_setas(old_as);
            as_activate();
            as_destroy(new_as);
            goto err3;
        }

        /* Update stack pointer */
        stackptr = (vaddr_t)argv_ptr;
    }

    /* Step 9: Clean up old address space */
    as_destroy(old_as);
    kfree(kprogram);
    if (kargs)
        kfree(kargs);
    if (kargbuf)
        kfree(kargbuf);

    /* Step 10: Enter user mode */
    enter_new_process(nargs /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
                      stackptr, entrypoint);

    /* enter_new_process does not return */
    panic("enter_new_process returned\n");

err3:
    if (kargbuf)
        kfree(kargbuf);
err2:
    if (kargs)
        kfree(kargs);
err1:
    if (kprogram)
        kfree(kprogram);
    return result;
}