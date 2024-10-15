#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <types.h>
#include <vnode.h>
#include <synch.h>
#include <limits.h>

/* This is the structure that represents an open file in the kernel. */
struct filehandle
{
    struct vnode *fh_vnode;
    off_t fh_offset;
    int fh_flags;         // (O_RDONLY, O_WRONLY, O_RDWR, etc.)
    int fh_refcount;      // Number of references to this open file
    struct lock *fh_lock; // Lock for this filehandle
};

/* Per-process file descriptor table */
struct filetable
{
    struct filehandle *ft_entries[OPEN_MAX]; // Array of filehandles
    struct lock *ft_lock;                    // Lock for the filetable
};


/**
 * Creates and initializes a new file table.
 *
 * This function allocates memory for a new file table structure for a process,
 * initializes all entries to NULL, and sets up any necessary
 * synchronization primitives.
 *
 * @return A pointer to the newly created struct filetable on success,
 *         or NULL if memory allocation fails.
 */
struct filetable *filetable_create(void);

/**
 * Destroys a file table and releases all associated resources.
 *
 * This function closes all open file handles in the table, releases
 * any resources associated with each file handle, frees the memory
 * allocated for the file table structure, and destroys any associated
 * synchronization primitives.
 *
 * @param ft Pointer to the file table to be destroyed.
 * 
 * @note The operations in this function are protected by the file table lock.
 *
 */
void filetable_destroy(struct filetable *ft);

/**
 * Adds a new file handle to the file table.
 *
 * This function finds the first available slot in the file table,
 * adds the file handle to this slot, increments the reference count
 * of the file handle, and returns the index of the slot as the file
 * descriptor.
 *
 * @param ft Pointer to the file table.
 * @param fh Pointer to the file handle to be added.
 *
 * @return The file descriptor (non-negative integer) assigned to the
 *         new file handle on success, or -1 if the table is full.
 * 
 * @note The opreations in this function are protected by the file table lock.
 */
int filetable_add(struct filetable *ft, struct filehandle *fh);


/**
 * Removes a file handle from the file table.
 *
 * This function checks if the given file descriptor is valid and refers
 * to an open file. It decrements the reference count of the file handle.
 * If the reference count reaches zero, it closes the file and frees the
 * file handle. Finally, it sets the table entry to NULL.
 *
 * @param ft Pointer to the file table.
 * @param fd The file descriptor of the file handle to be removed.
 *
 * @note This function does not return an error if the file descriptor
 *       is invalid or already closed.
 */
void filetable_remove(struct filetable *ft, int fd);

/**
 * Creates a copy of an existing file table.
 *
 * This function is typically used during process forking. It allocates
 * memory for a new file table structure, copies all entries from the old
 * file table to the new one, increments the reference count for each file
 * handle in the new table, and creates new synchronization primitives for
 * the new table.
 *
 * @param old_ft Pointer to the file table to be copied.
 *
 * @return A pointer to the newly created copy of the file table on success,
 *         or NULL if memory allocation fails.
 *
 * @note This function creates a shallow copy of the file handles, meaning
 *       the actual file state is shared between the original and the copy.
 */

struct filetable *filetable_copy(struct filetable *old_ft);

/**
 * Creates a new file handle.
 *
 * @param vn The vnode associated with the file.
 * @param flags Flags for the new file handle.
 * @return A pointer to the new file handle, or NULL if allocation fails.
 */
struct filehandle *file_handle_create(struct vnode *vn, int flags);

/**
 * Destroys a file handle and releases associated resources.
 *
 * @param fh The file handle to destroy.
 */
void file_handle_destroy(struct filehandle *fh);

/*
 * Initialize standard file descriptors for a process.
 *
 * This function initializes the standard input (fd 0), standard output (fd 1),
 * and standard error (fd 2) file descriptors for the given process file table.
 * It ensures that these file descriptors are always open, as assumed by most
 * user-level code.
 *
 * Specifically, the function opens the console device for input and output,
 * assigns the appropriate file handles to the file descriptors 0, 1, and 2 in
 * the given file table, and creates file handles for each of them. The standard
 * input is opened in read-only mode, while the standard output and standard
 * error are opened in write-only mode.
 *
 * Parameters:
 *   ft - Pointer to the file table of the process being initialized.
 *
 * Returns:
 *   0 on success.
 *   ENOMEM if memory allocation fails for any of the file handles.
 *   Other error codes from vfs_open() if the console device cannot be opened.
 *
 * Errors:
 *   This function may fail if it cannot allocate memory for file handles or
 *   if it cannot open the console device. If an error occurs, the process
 *   file table may be partially initialized.
 */
int filetable_init_standard(struct filetable *ft);

#endif /* _FILETABLE_H_ */
