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
#include <kern/fcntl.h>
#include <vm.h>
#include <vfs.h>
#include <copyinout.h>
#include <kern/limits.h>

static int copyout_args(char **kargs, int nargs, vaddr_t *stackptr);
static int count_args(char **args, size_t *nargs_ret, size_t *total_size_ret);

/* Count the number of arguments and the total size of the arguments */
static int
count_args(char **args, size_t *nargs_ret, size_t *total_size_ret)
{
    size_t nargs = 0;
    size_t total_size = 0;
    int result;
    
    while (1) {
        char *arg_ptr;
        result = copyin((userptr_t)&args[nargs], &arg_ptr, sizeof(char *));
        if (result) {
            return result;
        }

        if (arg_ptr == NULL) {
            break;
        }

        if (nargs >= ARG_MAX / sizeof(char *)) {
            return E2BIG;
        }

        size_t len;
        len = strlen(arg_ptr) + 1;
        
        if (len > ARG_MAX - total_size) {
            return E2BIG;
        }
        total_size += len;
        nargs++;
    }


    if (nargs == 0) {
        return EINVAL;
    }

    *nargs_ret = nargs;
    *total_size_ret = total_size;
    return 0;
}

int sys_execv(const char *program, char **args) {
    if (program == NULL || args == NULL) {
        return EFAULT;
    }
    
    char *kprogram = kmalloc(PATH_MAX);
    int result = copyinstr((userptr_t)program, kprogram, PATH_MAX, NULL);
    if (result) {
        kfree(kprogram);
        return result;
    }
    
    char **kargs = NULL;
    size_t nargs;
    size_t total_size;
    result = count_args(args, &nargs, &total_size);
    if (result) {
        kfree(kprogram);
        return result;
    }

    kargs = kmalloc((nargs + 1) * sizeof(char *));
    char *argbuf = kmalloc(total_size);
    size_t offset = 0;
    
    //copy arguments into kernel space
    for (size_t i = 0; i < nargs; i++) {
        kargs[i] = &argbuf[offset];
        size_t len;
        result = copyinstr((userptr_t)args[i], kargs[i], ARG_MAX - offset, &len);
        if (result) {
            kfree(kprogram);
            kfree(kargs);
            kfree(argbuf);
            return result;
        }
        offset += len;
    }
    kargs[nargs] = NULL;
    
    struct vnode *v;
    result = vfs_open(kprogram, O_RDONLY, 0, &v);
    if (result) {
        kfree(kprogram);
        kfree(kargs);
        kfree(argbuf);
        return result;
    }
    
    struct addrspace *old_as = proc_getas();
    struct addrspace *new_as;
    
    new_as = as_create();
    if (new_as == NULL) {
        vfs_close(v);
        kfree(kprogram);
        kfree(kargs);
        kfree(argbuf);
        return result;
    }
    
    proc_setas(new_as);
    as_activate();
    vaddr_t entry_point; 
    result = load_elf(v, &entry_point);
    if (result) {
        proc_setas(old_as);
        as_activate();
        as_destroy(new_as);
        vfs_close(v);
        kfree(kprogram);
        kfree(kargs);
        kfree(argbuf);
        return result;
    }
    
    vfs_close(v);
    
    vaddr_t stackptr;
    result = as_define_stack(new_as, &stackptr);
    if (result) {
        proc_setas(old_as);
        as_activate();
        as_destroy(new_as);
        kfree(kprogram);
        kfree(kargs);
        kfree(argbuf);
        return result;
    }
    
    result = copyout_args(kargs, nargs, &stackptr);
    if (result) {
        proc_setas(old_as);
        as_activate();
        as_destroy(new_as);
        kfree(kprogram);
        kfree(kargs);
        kfree(argbuf);
        return result;
    }
    
    kfree(kprogram);
    kfree(kargs);
    kfree(argbuf);

    as_destroy(old_as);

    enter_new_process(nargs,        
                     (userptr_t)stackptr,      
                     NULL,          
                     stackptr,     
                     entry_point); 
    
    panic("enter_new_process returned\n");
    return EINVAL;
}


static int copyout_args(char **kargs, int nargs, vaddr_t *stackptr) {
    int result;
    vaddr_t stack = *stackptr;

    // Calculate total size of arguments
    size_t total_size = (nargs + 1) * sizeof(vaddr_t);
    
    for (int i = 0; i < nargs; i++) {
        if (kargs[i] == NULL) {
            return EFAULT;
        }
        size_t len = strlen(kargs[i]) + 1;
        total_size += ROUNDUP(len, 4);
    }

    // Allocate space for argument pointers
    vaddr_t *argv_ptrs = kmalloc((nargs + 1) * sizeof(vaddr_t));
    if (argv_ptrs == NULL) {
        return ENOMEM;
    }

    // align it
    stack = (stack - total_size) & ~0x3;
    vaddr_t cur_ptr = stack;
    
    cur_ptr += ROUNDUP((nargs + 1) * sizeof(vaddr_t), 4);

    size_t actual_len;
    // save addresses
    for (int i = 0; i < nargs; i++) {
        size_t len = strlen(kargs[i]) + 1;
        result = copyoutstr(kargs[i], (userptr_t)cur_ptr, len, &actual_len);
        if (result) {
            kfree(argv_ptrs);
            return result;
        }
        argv_ptrs[i] = cur_ptr;
        cur_ptr += ROUNDUP(len, 4);
    }
    argv_ptrs[nargs] = 0;

    // Copy the argv array
    result = copyout(argv_ptrs, (userptr_t)stack, (nargs + 1) * sizeof(vaddr_t));
    kfree(argv_ptrs);
    if (result) {
        return result;
    }

    *stackptr = stack;
    return 0;
}