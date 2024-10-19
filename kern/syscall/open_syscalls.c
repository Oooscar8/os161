#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <vfs.h>
#include <vnode.h>
#include <include/syscall.h>
#include <copyinout.h>
#include <filetable.h>

/**
 * sys_open - Open a file.
 *
 * Given a filename, flags, and mode, open a file and return a file
 * descriptor. The file is opened according to the given flags and
 * mode.
 *
 * @param filename[in] - The path of the file to open.
 * @param flags[in]    - The flags to open the file with.
 * @param mode[in]     - The mode to open the file with.
 *
 * @return A file descriptor is returned on success, otherwise an
 * error code is returned to indicate the nature of the failure.
 */
int sys_open(const_userptr_t filename, int flags, mode_t mode)
{
    char kfilename[PATH_MAX];
    size_t actual_len;
    int result;

    /* Copy the filename from user space to kernel space */
    result = copyinstr(filename, kfilename, PATH_MAX, &actual_len);
    if (result)
    {
        return -result;      // Indicate failure
    }

    /* Open the file */
    struct vnode *vn;
    result = vfs_open(kfilename, flags, mode, &vn);
    if (result)
    {
        return -result;      // Indicate failure
    }

    /* Create the file handle */
    struct filehandle *fh;
    fh = filehandle_create(vn, flags);
    KASSERT(fh != NULL);

    /* Add the file handle to the file descriptor table */
    int fd = filetable_add(curproc->p_ft, fh);
    if (fd == -1)
    {
        filehandle_destroy(fh);
        vfs_close(vn);
        return -EMFILE;      // Indicate failure
    }

    return fd;
}