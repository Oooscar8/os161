#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <filetable.h>

int
sys_close(int fd)
{   
    /* Check if the file descriptor is valid */
    if (fd < 0 || fd >= OPEN_MAX) {
        return -EBADF;       // Indicate failure
    }
    
    /* Get the current process's file descriptor table */
    struct filetable *ft = curproc->p_ft;
    KASSERT(ft != NULL);

    /* Get the file handle indexed by the file descriptor */
    struct filehandle *fh = filetable_get(ft, fd);
    if (fh == NULL) {
        return -EBADF;       // Indicate failure
    }

    /* Remove the file handle from the file descriptor table */
    int result = filetable_remove(ft, fd);
    if (result) {
        return -result;      // Indicate failure
    }
    
    return 0;
}