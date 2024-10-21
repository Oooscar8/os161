#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <copyinout.h>
#include <filetable.h>
#include <syscall.h>

int
sys_read(int fd, userptr_t buf_ptr, size_t nbytes, int32_t *retval)
{
    struct filetable *ft;
    struct filehandle *fh;
    struct iovec iov;
    struct uio u;
    void *kbuf;
    int result;

    // Check if file descriptor is valid
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    // Get the file descriptor table of the current process
    ft = curproc->p_ft;
    KASSERT(ft != NULL);

    // Get the file handle from the file descriptor table
    fh = filetable_get(ft, fd);
    if (fh == NULL) {
        return EBADF;
    }

    // Check if the file is opened for reading
    if ((fh->flags & O_ACCMODE) == O_WRONLY) {
        return EBADF;
    }

    // Handle zero-length read
    if (nbytes == 0) {
        *retval = 0;
        return 0;
    }

    // Allocate kernel buffer
    kbuf = kmalloc(nbytes);
    KASSERT(kbuf != NULL);

    // Set up the uio structure
    uio_kinit(&iov, &u, kbuf, nbytes, fh->offset, UIO_READ);

    // Acquire the lock for the file handle
    lock_acquire(fh->fh_lock);

    // Perform the read operation
    result = VOP_READ(fh->vn, &u);

    if (result) {
        kfree(kbuf);
        lock_release(fh->fh_lock);
        return result;
    }

    // Update the file offset
    fh->offset = u.uio_offset;

    // Calculate the number of bytes actually read
    *retval = nbytes - u.uio_resid;

    // Release the lock for the file handle
    lock_release(fh->fh_lock);

    // Copy data from kernel space to user space
    result = copyout(kbuf, buf_ptr, *retval);
    
    // Free the kernel buffer
    kfree(kbuf);

    if (result) {
        return result;
    }

    return 0;
}