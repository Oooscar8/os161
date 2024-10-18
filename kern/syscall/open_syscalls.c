#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

int
sys_open(const_userptr_t filename, int flags, mode_t mode)
{
    char kfilename[PATH_MAX];
    size_t actual_len;
    int result;

    /* Copy the filename from user space to kernel space */
    result = copyinstr(filename, kfilename, PATH_MAX, &actual_len);
    if (result) {
        return result;
    }

    /* Open the file */
    struct vnode *vn;
    result = vfs_open(kfilename, flags, mode, &vn);
    if (result) {
        return result  // Indicate failure
    }

    /* Allocate a file descriptor */
    int fd;
    struct filehandle *fh;
    result = file_alloc(&fh);
    if (result) {
        vfs_close(vn);
        return result;      // Indicate failure
    }
    result = proc_add_file(curproc, fh, &fd);
    if (result) {
        file_destroy(fh);
        vfs_close(vn);
        return result;      // Indicate failure
    }

    /* Set up the file handle */
    fh->fh_vnode = vn;
    fh->fh_flags = flags & O_ACCMODE;
    fh->fh_offset = 0;
    if (flags & O_APPEND) {
        struct stat st;
        result = VOP_STAT(vn, &st);
        if (result) {
            proc_remove_file(curproc, fd);
            file_destroy(fh);
            vfs_close(vn);
            return result;      // Indicate failure
        }
        fh->fh_offset = st.st_size;
    }
    return fd;
}