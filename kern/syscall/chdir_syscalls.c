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

int sys_chdir(userptr_t *pathname)
{
    char *kpathname;
    int result;

    kpathname = (char *)kmalloc(PATH_MAX);
    if (kpathname == NULL)
    {
        return ENOMEM;
    }

    result = copyinstr((const_userptr_t)pathname, kpathname, PATH_MAX, NULL);
    if (result)
    {
        kfree(kpathname);
        return result;
    }

    result = vfs_chdir(kpathname);
    if (result) {
        return result;
    }

    kfree(kpathname);
    return 0;
}