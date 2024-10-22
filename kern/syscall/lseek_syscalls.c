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
 * sys_lseek - change current position in file
 *
 * Changes the current seek position of the file handle specified by
 * fd to a new position based on pos and whence.
 *
 * If whence is
 * - SEEK_SET, the new position is pos.
 * - SEEK_CUR, the new position is the current position plus pos.
 * - SEEK_END, the new position is the position of end-of-file
 *   plus pos.
 *
 * If the file descriptor is invalid, EBADF is returned.
 * If the file is not seekable, ESPIPE is returned.
 * If the whence parameter is invalid, EINVAL is returned.
 * If the new position is negative, EINVAL is returned.
 *
 * On success, the new position is returned in retval.
 */
off_t sys_lseek(int fd, off_t pos, int whence, off_t *retval)
{
    *retval = -1;
    struct filehandle *file;
    off_t newpos;

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

    // Check if the file is seekable
    if (!VOP_ISSEEKABLE(file->fh_vnode)) {
        file->fh_refcount--;
        lock_release(file->fh_lock);
        return ESPIPE;  
    }

    // Validate the whence parameter
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
    {
        file->fh_refcount--;
        lock_release(file->fh_lock);
        return EINVAL;
    }

    // Calculate the new position
    struct stat *fh_vn_stat;  

    switch (whence)
    {
    case SEEK_SET:
        newpos = pos;
        break;
    case SEEK_CUR:
        newpos = file->fh_offset + pos;
        break;
    case SEEK_END:
        // Get the file size
        fh_vn_stat = kmalloc(sizeof(struct stat));
        if (fh_vn_stat == NULL)
        {
            file->fh_refcount--;
            lock_release(file->fh_lock);
            return ENOMEM;
        }
        VOP_STAT(file->fh_vnode, fh_vn_stat);
        off_t size = (off_t)fh_vn_stat->st_size;
        newpos = size + pos;
        kfree(fh_vn_stat);
        break;
    }

    // Check if the new position is valid
    if (newpos < 0)
    {
        file->fh_refcount--;
        lock_release(file->fh_lock);
        return EINVAL;
    }

    // Update the file offset
    file->fh_offset = newpos;
    *retval = newpos;
    
    file->fh_refcount--;
    lock_release(file->fh_lock);
    return 0;
}