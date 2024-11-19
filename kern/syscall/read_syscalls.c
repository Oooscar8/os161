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

int sys_read(int fd, userptr_t buf_ptr, size_t nbytes, int32_t *retval)
{
    struct filetable *ft;
    struct filehandle *fh;
    struct iovec iov;
    struct uio u;
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
    lock_acquire(ft->ft_lock);
    fh = ft->file_handles[fd];
    if (fh == NULL)
    {
        lock_release(ft->ft_lock);
        return EBADF;
    }

    // Acquire the lock for the file handle
    lock_acquire(fh->fh_lock);
    lock_release(ft->ft_lock);

    // Check if the file is opened for reading
    if ((fh->flags & O_ACCMODE) == O_WRONLY)
    {
        lock_release(fh->fh_lock);
        return EBADF;
    }

    // Set up the uio structure
    uio_kinit(&iov, &u, (void *)buf_ptr, nbytes, fh->offset, UIO_READ);
    u.uio_segflg = UIO_USERSPACE;
    u.uio_space = curproc->p_addrspace;

    // Perform the read operation
    result = VOP_READ(fh->vn, &u);

    if (result)
    {
        lock_release(fh->fh_lock);
        return result;
    }

    // Update the file offset
    fh->offset = u.uio_offset;

    // Calculate the number of bytes actually read
    *retval = nbytes - u.uio_resid;

    // Release the lock for the file handle
    lock_release(fh->fh_lock);

    return 0;
}