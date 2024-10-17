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