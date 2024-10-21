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
#include <filetable.h>
#include <syscall.h>
#include <copyinout.h>

int sys_write(int fd, const_userptr_t buf_ptr, size_t nbytes, int32_t *retval)
{
    struct filetable *ft;
    struct filehandle *fh;
    struct iovec iov;
    struct uio u;
    void *kbuf;
    int result;

    // Check if file descriptor is valid
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }

    // Get the file descriptor table of the current process
    ft = curproc->p_ft;
    KASSERT(ft != NULL);

    // Get the file handle from the file descriptor table
    fh = filetable_get(ft, fd);
    KASSERT(fh != NULL);

    // Check if the file is opened for writing
    if ((fh->flags & O_ACCMODE) == O_RDONLY)
    {
        return EBADF;
    }

    // Handle zero-length write
    if (nbytes == 0)
    {
        *retval = 0;
        return 0;
    }

    // Allocate kernel buffer
    kbuf = kmalloc(nbytes);
    KASSERT(kbuf != NULL);
    // Copy data from user space to kernel space
    result = copyin(buf_ptr, kbuf, nbytes);
    if (result)
    {
        kfree(kbuf);
        return result;
    }

    // Set up the uio structure
    uio_kinit(&iov, &u, kbuf, nbytes, fh->offset, UIO_WRITE);

    // Acquire the lock for the file handle
    lock_acquire(fh->fh_lock);

    // Perform the write operation
    result = VOP_WRITE(fh->vn, &u);

    if (result)
    {
        return result;
    }
    else
    {
        // Update the file offset
        fh->offset = u.uio_offset;
        *retval = nbytes - u.uio_resid;
    }

    // Release the lock for the file handle
    lock_release(fh->fh_lock);

    // Free the kernel buffer
    kfree(kbuf);

    return 0;
}