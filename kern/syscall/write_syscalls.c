#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <vfs.h>
#include <current.h>
#include <proc.h>
#include <uio.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/seek.h>
#include <filetable.h>
#include <limits.h>
#include <vnode.h>
#include <kern/stat.h>

/**
 * sys_write - write data to a file
 *
 * Writes up to nbytes bytes to the file specified by fd, at the
 * current offset specified by the file, taking the data from the
 * space pointed to by buf. The file must be open for writing.
 *
 * On success, the number of bytes written is returned in retval.
 * If an error occurs, an appropriate error code is returned.
 *
 * Errors:
 *  - EBADF: fd is not a valid file descriptor, or is not open for
 *    writing.
 *  - EFAULT: buf is an invalid address.
 *  - EIO: a hardware I/O error occurred writing the data.
 */
int sys_write(int fd, userptr_t *buf, size_t nbytes, int *retval)
{
    struct filehandle *file;
    struct iovec iov;
    struct uio u;
    int result;

    // Check if the file descriptor is valid
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }
    lock_acquire(curproc->p_filetable->ft_lock);
    file = curproc->p_filetable->ft_entries[fd];
    if (file == NULL)
    {
        lock_release(curproc->p_filetable->ft_lock);
        return EBADF;
    }

    // Check if the file is opened for writing
    lock_acquire(file->fh_lock);
    lock_release(curproc->p_filetable->ft_lock);
    file->fh_refcount++;

    if ((file->fh_flags & O_ACCMODE) == O_RDONLY)
    {
        file->fh_refcount--;
        lock_release(file->fh_lock);
        return EBADF;
    }

    // Validate user buffer
     if (buf == NULL)
    {
        file->fh_refcount--;
        lock_release(file->fh_lock);
        return EFAULT;
    }

    // Set up uio structure for writing
    uio_kinit(&iov, &u, buf, nbytes, file->fh_offset, UIO_WRITE);
    u.uio_segflg = UIO_USERSPACE;
    u.uio_space = curproc->p_addrspace;

    // Perform the write operation
    result = VOP_WRITE(file->fh_vnode, &u);
    if (result)
    {
        file->fh_refcount--;
        lock_release(file->fh_lock);
        return result;
    }

    // Update the file offset
    *retval = nbytes - u.uio_resid;
    file->fh_offset += (off_t) *retval;

    file->fh_refcount--;
    lock_release(file->fh_lock);

    return 0;
}