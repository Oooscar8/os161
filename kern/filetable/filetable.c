#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <filetable.h>

//////////////////////////////////////////////////
//
// File descriptor table functions

/**
 * @brief Create a new file descriptor table.
 *
 * This function allocates memory for a new file descriptor table and initializes it by creating a lock
 * for synchronization. It also initializes the file handles array with standard I/O.
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

    // Initialize standard I/O
    struct filehandle *stdin_handle = create_stdio_handle("con:", O_RDONLY);
    struct filehandle *stdout_handle = create_stdio_handle("con:", O_WRONLY);
    struct filehandle *stderr_handle = create_stdio_handle("con:", O_WRONLY);

    for (int i = 0; i < OPEN_MAX; i++)
    {
        ft->file_handles[i] = NULL;
    }
    
    filetable_add(ft, stdin_handle);
    filetable_add(ft, stdout_handle);
    filetable_add(ft, stderr_handle);

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
    KASSERT(ft != NULL);

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
 * @brief Add an entry to the process's file descriptor table that 
 * maps a new file descriptor to this file handle.
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
 * @return 0 on success, or EBADF if the file descriptor is not valid.
 */
int filetable_remove(struct filetable *ft, int fd)
{
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }

    lock_acquire(ft->ft_lock);
    if (ft->file_handles[fd] != NULL)
    {
        filehandle_decref(ft->file_handles[fd]);
        ft->file_handles[fd] = NULL;
    }
    else {
        lock_release(ft->ft_lock);
        return EBADF;
    }
    lock_release(ft->ft_lock);
    
    return 0;
}


//////////////////////////////////////////////////
//
// File handle functions

/**
 * @brief Create a file handle for standard I/O.
 * 
 * @param device The device name (e.g., "con:")
 * @param flags The flags for opening the device
 * @return The newly-create file handle
 */
struct filehandle *
create_stdio_handle(const char *device, int flags)
{
    struct vnode *vn;
    int result = vfs_open(kstrdup(device), flags, 0, &vn);
    KASSERT(result == 0);
    
    struct filehandle *fh = kmalloc(sizeof(struct filehandle));
    KASSERT(fh != NULL);
    
    fh->vn = vn;
    fh->offset = 0;
    fh->refcount = 0;
    fh->flags = flags;
    fh->fh_lock = lock_create("file_handle_lock");
    KASSERT(fh->fh_lock != NULL);
    
    return fh;
}

/**
 * @brief Create a new file handle.
 *
 * This function allocates memory for a new file handle and initializes it with the provided vnode
 * and flags. It also initializes the file handle's offset, reference count, and file status flags. 
 * Additionally, it creates a lock for the file handle for synchronization purposes.
 * 
 * @param vn The vnode associated with the file handle.
 * @param flags The file status flags for the file handle.
 * @return A pointer to the newly created file handle.
 */
struct filehandle *
filehandle_create(struct vnode *vn, int flags)
{
    struct filehandle *fh = kmalloc(sizeof(struct filehandle));
    KASSERT(fh != NULL);

    fh->vn = vn;
    fh->offset = 0;
    fh->refcount = 0;
    fh->flags = flags;
    fh->fh_lock = lock_create("file_handle_lock");
    KASSERT(fh->fh_lock != NULL);

    return fh;
}

/**
 * @brief Destroy a file handle.
 *
 * This function closes the vnode associated with the file handle, destroys the lock associated with the file handle, and
 * frees the memory allocated for the file handle. It is called when the reference count of a file handle reaches 0.
 *
 * @param fh The file handle to be destroyed.
 */
void filehandle_destroy(struct filehandle *fh)
{
    KASSERT(fh != NULL);

    vfs_close(fh->vn);
    lock_destroy(fh->fh_lock);
    kfree(fh);
}

/**
 * @brief Increment the reference count of a file handle.
 *
 * This function increments the reference count of a file handle, which is used to determine when a file handle can be
 * safely destroyed. The reference count is incremented atomically to ensure the integrity of the file handle.
 *
 * @param fh The file handle whose reference count to increment.
 */
void filehandle_incref(struct filehandle *fh)
{
    KASSERT(fh != NULL);

    lock_acquire(fh->fh_lock);
    fh->refcount++;
    lock_release(fh->fh_lock);
}

/**
 * @brief Decrement the reference count of a file handle.
 *
 * This function decreases the reference count of the provided file handle.
 * If the reference count reaches zero, the file handle is destroyed, 
 * releasing any resources associated with it. The function is protected
 * by a lock to ensure thread safety during the operation.
 * 
 * @param fh The file handle whose reference count is to be decremented.
 */
void filehandle_decref(struct filehandle *fh)
{
    KASSERT(fh != NULL);

    lock_acquire(fh->fh_lock);
    fh->refcount--;
    int count = fh->refcount;
    lock_release(fh->fh_lock);

    if (count == 0)
    {
        filehandle_destroy(fh);
    }
}