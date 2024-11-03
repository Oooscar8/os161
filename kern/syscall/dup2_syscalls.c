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
 * sys_dup2 - clone file handles
 *
 * Duplicates the file handle specified by oldfd onto the file handle
 * newfd. If newfd is already open, it is closed first.
 *
 * On success, the new file descriptor is returned in retval and 0 is
 * returned.
 *
 * Errors:
 *  - EBADF: oldfd is not a valid file descriptor, or newfd is a value
 *    that cannot be a valid file descriptor.
 *  - EIO: a hardware I/O error occurred.
 */
int sys_dup2(int oldfd, int newfd, int *retval) {
    struct filehandle *file;
    struct filehandle *newfile;
    *retval = -1;

    // Check if the file descriptor is valid
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
        return EBADF;
    }

    // If oldfd equals newfd, just return newfd
    if (oldfd == newfd) {
        lock_acquire(curproc->p_filetable->ft_lock);
        if (curproc->p_filetable->ft_entries[oldfd] == NULL) {
            lock_release(curproc->p_filetable->ft_lock);
            return EBADF;
        }
        lock_release(curproc->p_filetable->ft_lock);
        *retval = newfd;
        return 0;
    }

    lock_acquire(curproc->p_filetable->ft_lock);
    
    // Check if oldfd is valid
    file = curproc->p_filetable->ft_entries[oldfd];
    if (file == NULL) {
        lock_release(curproc->p_filetable->ft_lock);
        return EBADF;
    }

    lock_acquire(file->fh_lock);
    file->fh_refcount++;
    lock_release(file->fh_lock);

    // Check if newfd is already open
    newfile = curproc->p_filetable->ft_entries[newfd];
    if (newfile != NULL) {
        curproc->p_filetable->ft_entries[newfd] = NULL;
        lock_release(curproc->p_filetable->ft_lock);
        
        sys_close(newfd);
        
        lock_acquire(curproc->p_filetable->ft_lock);
    }

    curproc->p_filetable->ft_entries[newfd] = file;
    lock_release(curproc->p_filetable->ft_lock);

    *retval = newfd;
    return 0;
}
