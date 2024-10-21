#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <vfs.h>
#include <vnode.h>
#include <copyinout.h>

int
sys_chdir(const_userptr_t pathname, int32_t *retval)
{
    int result;
    char *kpath;
    struct vnode *new_cwd;
    mode_t file_type;

    // Allocate kernel buffer for the pathname
    kpath = kmalloc(PATH_MAX);
    KASSERT(kpath != NULL);

    // Copy the pathname from user space to kernel space
    result = copyinstr(pathname, kpath, PATH_MAX, NULL);
    if (result) {
        kfree(kpath);
        return result;
    }

    // Attempt to open the directory
    result = vfs_open(kpath, O_RDONLY, 0, &new_cwd);
    if (result) {
        kfree(kpath);
        return result;
    }

    // Check if it's actually a directory
    result = vfs_gettype(new_cwd, &file_type);
    if (result) {
        vfs_close(new_cwd);
        kfree(kpath);
        return result;
    }
    if (file_type != _S_IFDIR) {
        vfs_close(new_cwd);
        kfree(kpath);
        return ENOTDIR;
    }

    // Lock the current process
    spinlock_acquire(&curproc->p_lock);

    // Replace the current working directory
    if (curproc->p_cwd) {
        VOP_DECREF(curproc->p_cwd);
        vfs_close(curproc->p_cwd);
    }
    curproc->p_cwd = new_cwd;
    VOP_INCREF(curproc->p_cwd);

    // Unlock the current process
    spinlock_release(&curproc->p_lock);

    // Free the kernel buffer
    kfree(kpath);

    // Set the return value
    *retval = 0;

    return 0;
}