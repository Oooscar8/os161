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

    if ((flags & O_ACCMODE) != O_RDONLY && (flags & O_ACCMODE) != O_WRONLY && (flags & O_ACCMODE) != O_RDWR)
    {
        kfree(kfilename);
        return EINVAL;
    }

    // atomic not sure
    if ((flags & O_CREAT) && (flags & O_EXCL))
    {
        result = vfs_open(kfilename, flags, mode, &vn);
        if (result == 0)
        {
            vfs_close(vn);
            kfree(kfilename);
            return EEXIST;
        }
    }
    else
    {
        result = vfs_open(kfilename, flags, mode, &vn);
    }

    kfree(kfilename);
    if (result)
    {
        return result;
    }

    // not sure
    if ((flags & O_TRUNC) && (flags & O_ACCMODE) != O_RDONLY)
    {
        result = VOP_TRUNCATE(vn, 0);
        if (result)
        {
            vfs_close(vn);
            return result;
        }
    }

    struct filehandle *fh = file_handle_create(vn, flags);
    if (fh == NULL)
    {
        vfs_close(vn);
        return ENOMEM;
    }

    if (flags & O_APPEND)
    {
        //lock_acquire(fh->fh_lock);

        struct stat *fh_vn_stat;  
        fh_vn_stat = kmalloc(sizeof(struct stat));
        if (fh_vn_stat == NULL)
        {
            file_handle_destroy(fh);
            vfs_close(vn);
            return ENOMEM;
        }

        VOP_STAT(vn, fh_vn_stat);
        fh->fh_offset = (off_t)fh_vn_stat->st_size;
        if (fh->fh_offset < 0)
        {
            file_handle_destroy(fh);
            return EIO;
        }

        kfree(fh_vn_stat);
        //lock_release(fh->fh_lock);
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
