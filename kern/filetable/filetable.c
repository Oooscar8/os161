#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <filetable.h>
#include <kern/unistd.h>
#include <vfs.h>
#include <kern/fcntl.h>

struct filetable *
filetable_create(void)
{
    struct filetable *ft = kmalloc(sizeof(struct filetable));
    if (ft == NULL)
    {
        return NULL;
    }

    ft->ft_lock = lock_create("filetable_lock");
    if (ft->ft_lock == NULL)
    {
        kfree(ft);
        return NULL;
    }

    for (int i = 0; i < OPEN_MAX; i++)
    {
        ft->ft_entries[i] = NULL;
    }

    KASSERT(ft != NULL);

    return ft;
}

void filetable_destroy(struct filetable *ft)
{
    KASSERT(ft != NULL);

    lock_acquire(ft->ft_lock);
    for (int i = 0; i < OPEN_MAX; i++)
    {
        if (ft->ft_entries[i] != NULL)
        {
            filehandle_decref(ft->ft_entries[i]);
            ft->ft_entries[i] = NULL;
        }
        ft->ft_entries[i] = NULL;
    }
    lock_release(ft->ft_lock);

    lock_destroy(ft->ft_lock);
    kfree(ft);
    ft = NULL;
}

int filetable_add(struct filetable *ft, struct filehandle *fh)
{
    KASSERT(ft != NULL);
    KASSERT(fh != NULL);

    lock_acquire(ft->ft_lock);

    for (int i = 0; i < OPEN_MAX; i++)
    {
        if (ft->ft_entries[i] == NULL)
        {
            ft->ft_entries[i] = fh;

            // increment the reference count
            lock_acquire(fh->fh_lock);
            fh->fh_refcount++;
            lock_release(fh->fh_lock);

            lock_release(ft->ft_lock);
            return i; // return the file descriptor
        }
    }

    lock_release(ft->ft_lock);
    return -1; // full
}


int filetable_remove(struct filetable *ft, int fd)
{
    KASSERT(ft != NULL);

    if (fd < 0 || fd >= OPEN_MAX)
    {
        return -1;
    }

    lock_acquire(ft->ft_lock);
    if (ft->ft_entries[fd] != NULL)
    {
        filehandle_decref(ft->ft_entries[fd]);
        ft->ft_entries[fd] = NULL;
    }
    else {
        lock_release(ft->ft_lock);
        return -1;
    }
    lock_release(ft->ft_lock);
    return 0;
}

struct filetable *
filetable_copy(struct filetable *old_ft)
{
    KASSERT(old_ft != NULL);

    struct filetable *new_ft = filetable_create();
    if (new_ft == NULL) {
        return NULL;
    }

    lock_acquire(old_ft->ft_lock);
    lock_acquire(new_ft->ft_lock);

    for (int i = 0; i < OPEN_MAX; i++) {
        if (old_ft->ft_entries[i] != NULL) {
            new_ft->ft_entries[i] = old_ft->ft_entries[i];
            lock_acquire(new_ft->ft_entries[i]->fh_lock);
            new_ft->ft_entries[i]->fh_refcount++;
            lock_release(new_ft->ft_entries[i]->fh_lock);
        }
    }

    lock_release(new_ft->ft_lock);
    lock_release(old_ft->ft_lock);

    return new_ft;
}

struct filehandle *
file_handle_create(struct vnode *vn, int flags)
{
    struct filehandle *fh;

    fh = kmalloc(sizeof(struct filehandle));
    if (fh == NULL) {
        return NULL;
    }

    /* Initialize the file handle fields */
    fh->fh_flags = flags;
    fh->fh_offset = 0;  
    fh->fh_vnode = vn;
    fh->fh_refcount = 0; //added in the filetable_add function

    /* Create a lock for this file handle */
    fh->fh_lock = lock_create("file_handle_lock");
    if (fh->fh_lock == NULL) {
        kfree(fh);
        return NULL;
    }

    return fh;
}

void file_handle_destroy(struct filehandle *fh)
{
    KASSERT(fh != NULL);
    KASSERT(lock_do_i_hold(fh->fh_lock));

    vfs_close(fh->fh_vnode);
    lock_release(fh->fh_lock);
    lock_destroy(fh->fh_lock);
    kfree(fh);
}

int filetable_init_standard(struct filetable *ft) {
    struct vnode *vn;
    int result;
    const char *cons = "con:";

    // Open standard input (fd 0)
    char *cons1 = kstrdup(cons);
    result = vfs_open(cons1, O_RDONLY, 0, &vn);
    kfree(cons1);
    if (result) {
        return result;
    }

    struct filehandle *fh = file_handle_create(vn, O_RDONLY);
    if (fh == NULL) {
        vfs_close(vn);
        return ENOMEM;
    }

    int fd0 = filetable_add(ft, fh);
    KASSERT(fd0 == STDIN_FILENO);

    // Open standard output (fd 1)
    cons1 = kstrdup(cons);
    result = vfs_open(cons1, O_WRONLY, 0, &vn);
    kfree(cons1);
    if (result) {
        return result;
    }

    fh = file_handle_create(vn, O_WRONLY);
    if (fh == NULL) {
        vfs_close(vn);
        return ENOMEM;
    }

    int fd1 = filetable_add(ft, fh);
    KASSERT(fd1 == STDOUT_FILENO);

    // Open standard error (fd 2)
    char *cons2 = kstrdup(cons);
    result = vfs_open(cons2, O_WRONLY, 0, &vn);
    kfree(cons2);
    if (result) {
        return result;
    }

    fh = file_handle_create(vn, O_WRONLY);
    if (fh == NULL) {
        vfs_close(vn);
        return ENOMEM;
    }

    int fd2 = filetable_add(ft, fh);
    KASSERT(fd2 == STDERR_FILENO);

    return 0;
}

void filehandle_incref(struct filehandle *fh)
{
    KASSERT(fh != NULL);

    lock_acquire(fh->fh_lock);
    fh->fh_refcount++;
    lock_release(fh->fh_lock);
}

void filehandle_decref(struct filehandle *fh)
{
    KASSERT(fh != NULL);

    lock_acquire(fh->fh_lock);
    fh->fh_refcount--;
    if (fh->fh_refcount == 0)
    {
        file_handle_destroy(fh);
        return;
    }
    lock_release(fh->fh_lock);
}
