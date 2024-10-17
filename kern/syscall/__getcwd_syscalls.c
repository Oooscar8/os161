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
    struct uio ku;          
    struct iovec iov;       
    int result;

    if (buf == NULL) {
        return EFAULT;
    }

    uio_kinit(&iov, &ku, buf, buflen, 0, UIO_READ);

    result = vfs_getcwd(&ku);
    if (result) {
        return result;
    }

    *retval = buflen - ku.uio_resid;

    return 0; 
}
