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
            result = copyin((const_userptr_t)&args[nargs], &arg, sizeof(userptr_t));
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
        char *temp_buf = kmalloc(ARG_MAX);
        if (temp_buf == NULL)
        {
            result = ENOMEM;
            goto err2;
        }

        for (int i = 0; i < nargs; i++)
        {
            size_t len;
            result = copyinstr((const_userptr_t)kargs[i], temp_buf, ARG_MAX, &len);
            if (result)
            {
                kfree(temp_buf);
                goto err2;
            }
            total_bytes += ROUNDUP(len, 4); // Align to 4 bytes
        }
        kfree(temp_buf);

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

    /* Create new address space */
    new_as = as_create();
    if (new_as == NULL)
    {
        result = ENOMEM;
        vfs_close(v);
        goto err3;
    }

    /* Switch to new address space */
    old_as = proc_setas(new_as);
    as_activate();

    /* Load the new executable */
    result = load_elf(v, &entrypoint);
    vfs_close(v);
    if (result)
    {
        proc_setas(old_as);
        as_activate();
        as_destroy(new_as);
        goto err3;
    }

    /* Define the user stack */
    result = as_define_stack(new_as, &stackptr);
    if (result)
    {
        proc_setas(old_as);
        as_activate();
        as_destroy(new_as);
        goto err3;
    }

    /* Copy arguments to user stack */
    if (nargs > 0)
    {
        size_t ptrs_space = (nargs + 1) * sizeof(userptr_t); // +1 for NULL terminator

        /* Adjust stack pointer for strings */
        stackptr -= total_bytes;
        stackptr = ROUNDUP(stackptr - 7, 8);

        /* Remember where strings start */
        vaddr_t strings_start = stackptr;

        /* Copy strings and save their user-space addresses in kargs */
        for (int i = 0; i < nargs; i++)
        {
            size_t got;
            /* Copy string to user stack */
            result = copyoutstr((const char *)kargs[i], (userptr_t)stackptr, ARG_MAX, &got);
            if (result)
            {
                proc_setas(old_as);
                as_activate();
                as_destroy(new_as);
                goto err3;
            }
            /* Save user space address in kargs */
            kargs[i] = (char *)stackptr;
            stackptr += ROUNDUP(got, 4); // Move to next aligned position
        }

        /* Adjust stack pointer for argv array */
        stackptr = strings_start - ptrs_space;
        stackptr = ROUNDUP(stackptr - 7, 8);

        /* Copy out argv array */
        for (int i = 0; i < nargs; i++)
        {
            result = copyout((const_userptr_t)&kargs[i], (userptr_t)(stackptr + i * sizeof(userptr_t)),
                             sizeof(userptr_t));
            if (result)
            {
                proc_setas(old_as);
                as_activate();
                as_destroy(new_as);
                goto err3;
            }
        }

        /* Set the last pointer to NULL */
        void* null_val = NULL;
        result = copyout(&null_val, (userptr_t)(stackptr + nargs * sizeof(userptr_t)),
                         sizeof(userptr_t));
        if (result)
        {
            proc_setas(old_as);
            as_activate();
            as_destroy(new_as);
            goto err3;
        }
    }

    /* Clean up */
    as_destroy(old_as);
    if (kprogram)
        kfree(kprogram);
    if (kargs)
        kfree(kargs);
    if (kargbuf)
        kfree(kargbuf);

    /* Enter user mode */
    enter_new_process(nargs /*argc*/, (userptr_t)stackptr /*argv*/, NULL /*env*/, stackptr, entrypoint);

    /* Should not get here */
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