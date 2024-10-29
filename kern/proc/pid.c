#include <types.h>
#include <lib.h>
#include <synch.h>
#include <proc.h>
#include <pid.h>
#include <limits.h>
#include <kern/errno.h>
#include <current.h>

static struct {
    struct pid_entry *pid_table;     
    struct lock *pid_lock;          
    pid_t next_pid;           
    unsigned int pid_count;          
} pid_manager;


static bool 
is_pid_valid(pid_t pid) 
{
    return (pid >= PID_MIN && pid <= PID_MAX);
}

static bool 
pid_exists_nlock(pid_t pid) 
{
    if (!is_pid_valid(pid)) {
        return false;
    }

    bool exists = (pid_manager.pid_table[pid].pid != NO_PID);

    return exists;
}

static void 
pid_entry_init(struct pid_entry *entry, pid_t pid, pid_t parent_pid, struct proc *p) 
{
    entry->pid = pid;
    entry->state = PROC_RUNNING;
    entry->parent_pid = parent_pid;
    entry->exit_code = 0;
    entry->wait_cv = cv_create("pid_wait_cv");
    entry->proc = p;
    KASSERT(entry->wait_cv != NULL);
}


int 
pid_bootstrap(void) 
{
    pid_manager.pid_table = kmalloc(sizeof(struct pid_entry) * (PID_MAX + 1));
    if (pid_manager.pid_table == NULL) {
        return ENOMEM;
    }

    pid_manager.pid_lock = lock_create("pid_lock");
    if (pid_manager.pid_lock == NULL) {
        kfree(pid_manager.pid_table);
        return ENOMEM;
    }

    pid_manager.next_pid = PID_MIN;
    pid_manager.pid_count = 0;

    // initialize all pid entries
    for (int i = 0; i <= PID_MAX; i++) {
        pid_manager.pid_table[i].pid = NO_PID;
        pid_manager.pid_table[i].state = 0;
        pid_manager.pid_table[i].wait_cv = NULL;
    }

    return 0;
}

pid_t 
pid_alloc(pid_t parent_pid, struct proc *p) 
{
    pid_t new_pid = NO_PID;
    
    lock_acquire(pid_manager.pid_lock);

    if (pid_manager.pid_count >= (PID_MAX - PID_MIN + 1)) {
        lock_release(pid_manager.pid_lock);
        return NO_PID;
    }

    pid_t start_pid = pid_manager.next_pid;
    do {
        if (pid_manager.pid_table[pid_manager.next_pid].pid == NO_PID) {
            new_pid = pid_manager.next_pid;
            pid_entry_init(&pid_manager.pid_table[new_pid], new_pid, parent_pid, p);
            pid_manager.pid_count++;
            break;
        }
        pid_manager.next_pid++;
        if (pid_manager.next_pid > PID_MAX) {
            pid_manager.next_pid = PID_MIN;
        }
    } while (pid_manager.next_pid != start_pid);

    pid_manager.next_pid++;
    if (pid_manager.next_pid > PID_MAX) {
        pid_manager.next_pid = PID_MIN;
    }

    lock_release(pid_manager.pid_lock);
    return new_pid;
}

int 
pid_free(pid_t pid, int exit_code) 
{
    if (!is_pid_valid(pid)) {
        return EINVAL;
    }

    lock_acquire(pid_manager.pid_lock);

    struct pid_entry *entry = &pid_manager.pid_table[pid];
    if (entry->pid == NO_PID) {
        lock_release(pid_manager.pid_lock);
        return EINVAL;
    }

    // update the process state
    entry->state = PROC_EXITED;
    entry->exit_code = exit_code;

    // if the parent process has exited, free the PID directly
    if (!pid_exists_nlock(entry->parent_pid) || 
        (pid_manager.pid_table[entry->parent_pid].state & PROC_EXITED)) {
        cv_destroy(entry->wait_cv);
        entry->wait_cv = NULL;
        entry->pid = NO_PID;
        pid_manager.pid_count--;
    } else {
        // mark the process as a zombie
        entry->state |= PROC_ZOMBIE;
        // notify the parent process
        cv_broadcast(entry->wait_cv, pid_manager.pid_lock);
    }

    lock_release(pid_manager.pid_lock);
    return 0;
}

pid_t 
pid_wait(pid_t pid, int *status, int options) 
{
    (void)options;

    if (!is_pid_valid(pid)) {
        return -1;
    }

    lock_acquire(pid_manager.pid_lock);

    struct pid_entry *entry = &pid_manager.pid_table[pid];
    if (entry->pid == NO_PID) {
        lock_release(pid_manager.pid_lock);
        return -1;
    }

    // wait until the child process exits
    while (!(entry->state & (PROC_EXITED | PROC_ZOMBIE))) {
        cv_wait(entry->wait_cv, pid_manager.pid_lock);
    }

    //get the exit code
    if (status != NULL) {
        *status = entry->exit_code;
    }

    // free the PID if the child process is a zombie
    if (entry->state & PROC_ZOMBIE) {
        struct proc *p = entry->proc;
        KASSERT(p != NULL);
        // KASSERT(threadarray_num(&p->p_threads) == 0);
        // proc_destroy(p);
        cv_destroy(entry->wait_cv);
        entry->wait_cv = NULL;
        entry->pid = NO_PID;
        pid_manager.pid_count--;
    }

    lock_release(pid_manager.pid_lock);
    return pid;
}

bool 
pid_exists(pid_t pid) 
{
    if (!is_pid_valid(pid)) {
        return false;
    }

    lock_acquire(pid_manager.pid_lock);
    bool exists = (pid_manager.pid_table[pid].pid != NO_PID);
    lock_release(pid_manager.pid_lock);

    return exists;
}

pid_t 
get_process_parent(pid_t pid) 
{
    if (!is_pid_valid(pid)) {
        return NO_PID;
    }

    lock_acquire(pid_manager.pid_lock);
    
    struct pid_entry *entry = &pid_manager.pid_table[pid];
    pid_t parent_pid = (entry->pid != NO_PID) ? entry->parent_pid : NO_PID;
    
    lock_release(pid_manager.pid_lock);
    return parent_pid;
}

int 
pid_cleanup(void) 
{
    int cleaned = 0;
    
    lock_acquire(pid_manager.pid_lock);

    for (pid_t pid = PID_MIN; pid <= PID_MAX; pid++) {
        struct pid_entry *entry = &pid_manager.pid_table[pid];
        if (entry->pid != NO_PID && 
            (entry->state & PROC_ZOMBIE) && 
            (!pid_exists_nlock(entry->parent_pid) || 
             (pid_manager.pid_table[entry->parent_pid].state & PROC_EXITED))) {
            
             struct proc *p = entry->proc;
            if (p != NULL) {
                proc_destroy(p);  
                entry->proc = NULL;
            }

            cv_destroy(entry->wait_cv);
            entry->wait_cv = NULL;
            entry->pid = NO_PID;
            pid_manager.pid_count--;
            cleaned++;
        }
    }

    lock_release(pid_manager.pid_lock);
    return cleaned;
}

bool status_is_zombie(pid_t pid) 
{
    if (!is_pid_valid(pid)) {
        return false;
    }

    lock_acquire(pid_manager.pid_lock);
    bool is_zombie = (pid_manager.pid_table[pid].state & (PROC_ZOMBIE | PROC_EXITED));
    lock_release(pid_manager.pid_lock);

    return is_zombie;
}