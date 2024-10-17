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

int sys_dup2(int oldfd, int newfd, int *retval) {
    struct filehandle *file;
    struct filehandle *newfile;
    *retval = -1;

    // Check if the file descriptor is valid
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
        return EBADF;
    }

    lock_acquire(curproc->p_filetable->ft_lock);
    file = curproc->p_filetable->ft_entries[oldfd];
    if (file == NULL) {
        lock_release(curproc->p_filetable->ft_lock);
        return EBADF;
    }

    // Check if the new file descriptor is already open
    newfile = curproc->p_filetable->ft_entries[newfd];
    if (newfile != NULL) {
        if (newfile == file) {
            lock_release(curproc->p_filetable->ft_lock);
            *retval = newfd;
            return 0;
        }
        
        lock_acquire(file->fh_lock);
        lock_acquire(newfile->fh_lock);
        
        // Close the new file descriptor
        sys_close(newfd);
        file->fh_refcount++;
        curproc->p_filetable->ft_entries[newfd] = file;
        
        lock_release(newfile->fh_lock);
        lock_release(file->fh_lock);
        lock_release(curproc->p_filetable->ft_lock);
        *retval = newfd;
        return 0;
    }

    // Copy the file handle to the new file descriptor
    lock_acquire(file->fh_lock);
    file->fh_refcount++;
    curproc->p_filetable->ft_entries[newfd] = file;
    lock_release(file->fh_lock);
    lock_release(curproc->p_filetable->ft_lock);
    
    *retval = newfd;
    return 0;
}