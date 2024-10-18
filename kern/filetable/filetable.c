#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <vnode.h>
#include <filetable.h>

//////////////////////////////////////////////////
//
// File descriptor table functions

/**
 * @brief Create a new file descriptor table.
 *
 * This function allocates memory for a new file descriptor table and initializes it by creating a lock
 * for synchronization. It also initializes the file handles array by setting all elements to NULL.
 *
 * @return A pointer to the newly created file descriptor table, 
 * or NULL if memory allocation fails or if the lock creation fails.
 */
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
        ft->file_handles[i] = NULL;
    }

    return ft;
}

/**
 * @brief Destroy the file descriptor table.
 * 
 * This function releases all file handles in the file descriptor table by decrementing their reference counts
 * and setting them to NULL. It also destroys the lock associated with the file descriptor table and frees the memory.
 * 
 * @param ft The file descriptor table to be destroyed.
 */
void filetable_destroy(struct filetable *ft)
{
    if (ft == NULL)
    {
        return;
    }

    lock_acquire(ft->ft_lock);
    for (int i = 0; i < OPEN_MAX; i++)
    {
        if (ft->file_handles[i] != NULL)
        {
            filehandle_decref(ft->file_handles[i]);
            ft->file_handles[i] = NULL;
        }
    }
    lock_release(ft->ft_lock);

    lock_destroy(ft->ft_lock);
    kfree(ft);
}

/**
 * @brief Add a file handle to the file descriptor table.
 *
 * @param ft The file descriptor table to add the file handle to.
 * @param fh The file handle to add.
 *
 * @return The assigned file descriptor for the file handle, 
 * or -1 if the file descriptor table is full.
 */
int filetable_add(struct filetable *ft, struct filehandle *fh)
{
    int fd = -1;
    lock_acquire(ft->ft_lock);

    // Find the first available file descriptor
    for (int i = 0; i < OPEN_MAX; i++)
    {
        if (ft->file_handles[i] == NULL)
        {
            ft->file_handles[i] = fh;
            filehandle_incref(fh);
            fd = i;
            break;
        }
    }

    lock_release(ft->ft_lock);
    return fd;
}

/**
 * @brief Get the file handle associated with the given file descriptor from the file descriptor table.
 *
 * @param ft The file descriptor table to retrieve the file handle from.
 * @param fd The file descriptor to get the file handle for.
 *
 * @return The file handle associated with the provided file descriptor, 
 * or NULL if the file descriptor is out of bounds 
 * or there is no file handle associated with the file descriptor.
 */
struct filehandle *
filetable_get(struct filetable *ft, int fd)
{
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return NULL;
    }

    struct filehandle *fh;
    lock_acquire(ft->ft_lock);
    fh = ft->file_handles[fd];
    lock_release(ft->ft_lock);

    return fh;
}

/**
 * @brief Remove a file handle from the file descriptor table.
 *
 * This function removes the file handle associated with the specified
 * file descriptor from the file descriptor table by decrementing its
 * reference count and setting the entry to NULL. The operation is
 * protected by a lock to ensure thread safety.
 *
 * @param ft The file descriptor table to remove the file handle from.
 * @param fd The file descriptor whose associated file handle is to be removed.
 */
void filetable_remove(struct filetable *ft, int fd)
{
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return;
    }

    lock_acquire(ft->ft_lock);
    if (ft->file_handles[fd] != NULL)
    {
        filehandle_decref(ft->file_handles[fd]);
        ft->file_handles[fd] = NULL;
    }
    lock_release(ft->ft_lock);
}


//////////////////////////////////////////////////
//
// File handle functions

struct filehandle *
filehandle_create(struct vnode *vn, int flags)
{
    struct filehandle *fh = kmalloc(sizeof(struct file_handle));
    if (fh == NULL)
    {
        return NULL;
    }

    fh->vn = vn;
    fh->offset = 0;
    fh->refcount = 1;
    fh->flags = flags;
    fh->fh_lock = lock_create("file_handle_lock");
    if (fh->fh_lock == NULL)
    {
        kfree(fh);
        return NULL;
    }

    return fh;
}

void filehandle_destroy(struct filehandle *fh)
{
    if (fh == NULL)
    {
        return;
    }

    vfs_close(fh->vn);
    lock_destroy(fh->fh_lock);
    kfree(fh);
}

int filehandle_incref(struct filehandle *fh)
{
    if (fh == NULL)
    {
        return EINVAL;
    }

    lock_acquire(fh->fh_lock);
    fh->refcount++;
    lock_release(fh->fh_lock);

    return 0;
}

int filehandle_decref(struct filehandle *fh)
{
    if (fh == NULL)
    {
        return EINVAL;
    }

    lock_acquire(fh->fh_lock);
    fh->refcount--;
    int count = fh->refcount;
    lock_release(fh->fh_lock);

    if (count == 0)
    {
        filehandle_destroy(fh);
    }

    return 0;
}