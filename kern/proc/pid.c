#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <pid.h>

/* PID status codes */
#define PID_FREE    0
#define PID_USED    1

/* Number of PIDs available */
#define PID_COUNT   (PID_MAX - PID_MIN + 1)

/* PID table entry structure */
struct pid_entry {
    pid_t pid;               /* Process ID */
    int status;             /* PID_FREE or PID_USED */
    struct proc *proc;      /* Pointer to process structure */
};

/* Global PID management state */
static struct pid_entry *pid_table;     /* PID allocation table */
static struct spinlock pid_lock;        /* Lock for PID operations */
static unsigned int pid_count;          /* Number of PIDs in use */
static pid_t next_pid;                  /* Next PID to try for allocation */

/*
 * Initialize the PID management system
 * Called during system bootstrap
 */
void 
pid_bootstrap(void) 
{
    size_t table_size = PID_COUNT * sizeof(struct pid_entry);
    
    /* Allocate PID table */
    pid_table = kmalloc(table_size);
    if (pid_table == NULL) {
        panic("pid_bootstrap: Unable to allocate PID table\n");
    }

    /* Initialize PID table entries */
    for (int i = 0; i < PID_COUNT; i++) {
        pid_table[i].pid = PID_MIN + i;
        pid_table[i].status = PID_FREE;
        pid_table[i].proc = NULL;
    }

    /* Initialize lock and state */
    spinlock_init(&pid_lock);
    pid_count = 0;
    next_pid = PID_MIN;

    /* Reserve PID 1 for init process if needed */
    pid_table[0].status = PID_USED;
    pid_count++;
}

/*
 * Convert PID to table index
 */
static inline int 
pid_to_index(pid_t pid) 
{
    KASSERT(pid >= PID_MIN && pid <= PID_MAX);
    return pid - PID_MIN;
}

/*
 * Find next available PID starting from next_pid
 * Must be called with pid_lock held
 */
static pid_t 
find_free_pid(void) 
{
    unsigned int start = pid_to_index(next_pid);
    unsigned int i = start;

    /* Search for a free PID */
    do {
        if (pid_table[i].status == PID_FREE) {
            return PID_MIN + i;
        }
        i = (i + 1) % PID_COUNT;
    } while (i != start);

    return ENOPID;
}

/*
 * Allocate a new PID and associate it with the given process
 * Returns allocated PID on success, ENOPID on failure
 */
pid_t 
pid_allocate(struct proc *proc) 
{
    pid_t pid;
    int index;

    KASSERT(proc != NULL);

    spinlock_acquire(&pid_lock);

    /* Check if we've reached maximum PIDs */
    if (pid_count >= PID_COUNT) {
        spinlock_release(&pid_lock);
        return ENOPID;
    }

    /* Find next available PID */
    pid = find_free_pid();
    if (pid == ENOPID) {
        spinlock_release(&pid_lock);
        return ENOPID;
    }

    /* Mark PID as used and associate with process */
    index = pid_to_index(pid);
    pid_table[index].status = PID_USED;
    pid_table[index].proc = proc;
    pid_count++;

    /* Update next_pid to start search from next position */
    next_pid = (pid < PID_MAX) ? (pid + 1) : PID_MIN;

    spinlock_release(&pid_lock);
    return pid;
}

/*
 * Free a PID and clear its process association
 */
void 
pid_free(pid_t pid) 
{
    int index;

    /* Validate PID */
    if (pid < PID_MIN || pid > PID_MAX) {
        return;
    }

    spinlock_acquire(&pid_lock);
    
    index = pid_to_index(pid);
    
    /* Only free if PID is currently in use */
    if (pid_table[index].status == PID_USED) {
        pid_table[index].status = PID_FREE;
        pid_table[index].proc = NULL;
        pid_count--;
    }
    
    spinlock_release(&pid_lock);
}

/*
 * Get process structure associated with PID
 * Returns NULL if PID is invalid or not in use
 */
struct proc *
pid_get_proc(pid_t pid) 
{
    struct proc *p = NULL;
    
    /* Validate PID */
    if (pid < PID_MIN || pid > PID_MAX) {
        return NULL;
    }

    spinlock_acquire(&pid_lock);
    
    int index = pid_to_index(pid);
    if (pid_table[index].status == PID_USED) {
        p = pid_table[index].proc;
    }
    
    spinlock_release(&pid_lock);
    return p;
}

/*
 * Check if a PID exists (is currently allocated)
 * Returns 1 if PID exists, 0 otherwise
 */
int 
pid_exists(pid_t pid) 
{
    int exists = 0;
    
    /* Validate PID */
    if (pid < PID_MIN || pid > PID_MAX) {
        return 0;
    }

    spinlock_acquire(&pid_lock);
    exists = (pid_table[pid_to_index(pid)].status == PID_USED);
    spinlock_release(&pid_lock);
    
    return exists;
}

/*
 * Get the parent PID of a given PID
 * Returns the parent PID if found, ENOPID if not found
 */
pid_t
pid_get_ppid(pid_t pid)
{
    pid_t ppid = ENOPID;
    struct proc *p;

    /* Get the process structure */
    p = pid_get_proc(pid);
    if (p != NULL && p->p_parent != NULL) {
        ppid = p->p_parent->p_pid;
    }

    return ppid;
}