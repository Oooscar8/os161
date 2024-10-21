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

int sys_open(userptr_t *filename, int flags, mode_t mode, int *retval)
{
    struct vnode *vn;
    char *kfilename;
    int result;

    kfilename = (char *)kmalloc(PATH_MAX);
    if (kfilename == NULL)
    {
        return ENOMEM;
    }

    result = copyinstr((const_userptr_t) filename, kfilename, PATH_MAX, NULL);
    if (result)
    {
        kfree(kfilename);
        return result;
    }

    /* Open the file */
    result = vfs_open(kfilename, flags, mode, &vn);
    if (result)
    {
        return result;      // Indicate failure
    }

    struct filehandle *fh = file_handle_create(vn, flags);
    if (fh == NULL)
    {
        vfs_close(vn);
        return ENOMEM;
    }

    *retval = filetable_add(curproc->p_filetable, fh);

    //filetable is full
    if (*retval == -1)
    {   
        file_handle_destroy(fh);
        return EMFILE;
    }

    return 0;
}
