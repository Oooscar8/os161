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
 * sys_close - close a file descriptor
 * @fd: the file descriptor to be closed
 *
 * This system call closes a file descriptor and releases any system
 * resources associated with it. If the file descriptor is invalid,
 * this function returns EBADF. Otherwise, it returns 0.
 */
int sys_close(int fd)
{
    
    if (filetable_remove(curproc->p_filetable, fd) == -1)
    {
        return EBADF;
    }

    return 0;
}