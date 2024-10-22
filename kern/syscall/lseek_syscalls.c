#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <filetable.h>
#include <limits.h>
#include <syscall.h>

int
sys_lseek(int fd, off_t pos, int whence, off_t *retval)
{
    struct filetable *ft;
    struct filehandle *fh;
    struct stat st;
    off_t new_pos;
    int result;

    // Check if file descriptor is valid
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    // Get the file descriptor table of the current process
    ft = curproc->p_ft;
    KASSERT(ft != NULL);

    // Get the file handle from the file descriptor table
    lock_acquire(ft->ft_lock);
    fh = ft->file_handles[fd];
    if (fh == NULL) {
        lock_release(ft->ft_lock);
        return EBADF;
    }

    // Acquire the lock for the file handle
    lock_acquire(fh->fh_lock);
    lock_release(ft->ft_lock);

    // Check if the file supports seeking
    if (!VOP_ISSEEKABLE(fh->vn)) {
        lock_release(fh->fh_lock);
        return ESPIPE;
    }

    // Calculate the new position based on whence
    switch (whence) {
        case SEEK_SET:
            new_pos = pos;
            break;
        case SEEK_CUR:
            new_pos = fh->offset + pos;
            break;
        case SEEK_END:
            // Get the file size
            result = VOP_STAT(fh->vn, &st);
            if (result) {
                lock_release(fh->fh_lock);
                return result;
            }
            new_pos = st.st_size + pos;
            break;
        default:
            lock_release(fh->fh_lock);
            return EINVAL;
    }

    // Check if the new position is valid
    if (new_pos < 0) {
        lock_release(fh->fh_lock);
        return EINVAL;
    }

    // Update the file offset
    fh->offset = new_pos;

    // Release the lock for the file handle
    lock_release(fh->fh_lock);

    // Set the return value
    *retval = new_pos;

    return 0;
}