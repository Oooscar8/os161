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


int
sys_read(int fd, userptr_t *buf, size_t buflen, int *retval) {
    struct filehandle *file;
    struct iovec iov;
    struct uio u;
    int result;

    // Check if the file descriptor is valid
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }
    lock_acquire(curproc->p_filetable->ft_lock);
    file = curproc->p_filetable->ft_entries[fd];
    if (file == NULL) {
        lock_release(curproc->p_filetable->ft_lock);
        return EBADF;
    }

    // Check if the file is opened for reading
    lock_acquire(file->fh_lock);
    lock_release(curproc->p_filetable->ft_lock);
    file->fh_refcount++;

    if ((file->fh_flags & O_ACCMODE) == O_WRONLY) {
        file->fh_refcount--;
        lock_release(file->fh_lock);
        return EBADF;
    }

    // Validate user buffer using copyin
    if (buf == NULL) {
        file->fh_refcount--;
        lock_release(file->fh_lock);
        return EFAULT;
    }

    // Set up uio structure for reading
    uio_kinit(&iov, &u, buf, buflen, file->fh_offset, UIO_READ);

    // Perform the read operation
    result = VOP_READ(file->fh_vnode, &u);
    if (result) {
        file->fh_refcount--;
        lock_release(file->fh_lock);
        return result;
    }

    // Return the number of bytes read
    *retval = buflen - u.uio_resid;

    // Update the seek position
    file->fh_offset += (off_t) *retval;

    file->fh_refcount--;
    lock_release(file->fh_lock);
    return 0;
}
