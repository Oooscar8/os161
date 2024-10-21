#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <vfs.h>
#include <vnode.h>
#include <copyinout.h>
#include <syscall.h>

int
sys___getcwd(userptr_t buf, size_t buflen, int32_t *retval)
{
    struct iovec iov;
    struct uio u;
    void *kbuf;
    int result;

    // Handle zero-length buffer
    if (buflen == 0) {
        *retval = 0;
        return 0;
    }

    // Allocate kernel buffer
    kbuf = kmalloc(buflen);
    KASSERT(kbuf != NULL);

    // Set up the uio structure
    uio_kinit(&iov, &u, kbuf, buflen, 0, UIO_READ);

    // Perform the getcwd operation
    result = vfs_getcwd(&u);
    if (result) {
        kfree(kbuf);
        return result;
    }

    // Calculate the number of bytes actually read
    *retval = buflen - u.uio_resid;

    // Copy data from kernel space to user space
    result = copyout(kbuf, buf, *retval);

    // Free the kernel buffer
    kfree(kbuf);

    if (result) {
        return result;
    }

    return 0;
}