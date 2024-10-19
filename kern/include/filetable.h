#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <types.h>
#include <synch.h>
#include <vnode.h>
#include <limits.h>

/* File handle structure */
struct filehandle {
    struct vnode *vn;       // Pointer to the v-node
    off_t offset;           // Current file offset
    unsigned int refcount;  // Reference count
    int flags;              // file status flags
    struct lock *fh_lock;   // Lock for the file handle
};

/* Per-process file descriptor table */
struct filetable {
    struct filehandle *file_handles[OPEN_MAX];
    struct lock *ft_lock;  // Lock for the file descriptor table
};

/* File descriptor table functions */
struct filetable *filetable_create(void);
void filetable_destroy(struct filetable *ft);
int filetable_add(struct filetable *ft, struct filehandle *fh);
struct filehandle *filetable_get(struct filetable *ft, int fd);
int filetable_remove(struct filetable *ft, int fd);

/* File handle functions */
struct filehandle *filehandle_create(struct vnode *vn, int flags);
void filehandle_destroy(struct filehandle *fh);
void filehandle_incref(struct filehandle *fh);
void filehandle_decref(struct filehandle *fh);

#endif /* _FILETABLE_H_ */