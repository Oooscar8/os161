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
 * sys_chdir - change current directory of current process
 *
 *
 * Errors:
 * - ENOENT: the named directory does not exist
 * - EIO: a hard I/O error occurred while trying to change the current
 *   directory
 * - EFAULT: pathname was an invalid pointer
 * - EINVAL: pathname is not a valid path
 * - ENAMETOOLONG: the length of the pathname exceeded PATH_MAX
 * - ENOTDIR: a component of pathname is not a directory, or pathname is
 *   not a directory
 * - EPERM: the process does not have the ability to change its current
 *   directory (this is only possible if the process is running with
 *   privileges, and should never happen)
 */
int sys_chdir(userptr_t *pathname, int *retval)
{
    char *kpathname;
    int result;
    *retval = 0;

    kpathname = (char *)kmalloc(PATH_MAX);
    if (kpathname == NULL)
    {   
        *retval = -1;
        return ENOMEM;
    }

    result = copyinstr((const_userptr_t)pathname, kpathname, PATH_MAX, NULL);
    if (result)
    {
        *retval = -1;
        kfree(kpathname);
        return result;
    }

    result = vfs_chdir(kpathname);
    if (result) {
        *retval = -1;
        return result;
    }

    kfree(kpathname);
    return 0;
}