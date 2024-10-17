#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <uio.h>
#include <vfs.h>
#include <copyinout.h>
#include <current.h>
#include <syscall.h>
#include <proc.h>

int sys__getcwd(userptr_t *buf, size_t buflen, int *retval) {
    struct uio u;          
    struct iovec iov;       
    int result;

    if (buf == NULL) {
        return EFAULT;
    }

    uio_kinit(&iov, &u, buf, buflen, 0, UIO_READ);
    u.uio_space = curproc->p_addrspace;
    u.uio_segflg = UIO_USERSPACE;

    result = vfs_getcwd(&u);
    if (result) {
        return result;
    }

    *retval = buflen - u.uio_resid;

    return 0; 
}
