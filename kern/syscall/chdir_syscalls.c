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
#include <syscall.h>

int
sys_chdir(const_userptr_t pathname)
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

    // Do the actual directory change
    result = vfs_chdir(kpath);

    // Free the kernel buffer
    kfree(kpath);

    if (result) {
        return result;
    }

    return 0;
}