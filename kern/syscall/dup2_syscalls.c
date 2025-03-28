#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <syscall.h>
#include <filetable.h>

int sys_dup2(int oldfd, int newfd, int32_t *retval)
{
    struct filetable *ft;
    struct filehandle *old_fh, *new_fh;

    // Check validity of file descriptors
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX)
    {
        return EBADF;
    }

    // Get the current process's file descriptor table
    ft = curproc->p_ft;
    KASSERT(ft != NULL);

    // If oldfd and newfd are the same, return success immediately
    if (oldfd == newfd)
    {
        *retval = newfd;
        return 0;
    }

    // Acquire the lock for the file descriptor table
    lock_acquire(ft->ft_lock);

    // Get the file handle for oldfd
    old_fh = ft->file_handles[oldfd];
    if (old_fh == NULL)
    {
        lock_release(ft->ft_lock);
        return EBADF;
    }

    // Get the file handle for newfd
    new_fh = ft->file_handles[newfd];

    // Check if newfd is already open, if so, close it
    if (new_fh != NULL)
    {
        filehandle_decref(new_fh);
        ft->file_handles[newfd] = NULL;
    }

    // Add old_fh to newfd
    ft->file_handles[newfd] = old_fh;
    filehandle_incref(old_fh);

    // Release the lock for the file decriptor table
    lock_release(ft->ft_lock);

    // Set the return value
    *retval = newfd;

    return 0;
}