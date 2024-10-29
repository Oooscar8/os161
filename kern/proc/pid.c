#include <types.h>
#include <spinlock.h>
#include <kern/errno.h>
#include <lib.h>
#include <limits.h>
#include <pid.h>

/* Global variables for PID management */
pid_t next_pid = PID_MIN;        /* Next PID to assign */
struct spinlock pid_lock;         /* Lock for pid allocation */

/* Initialize the PID system. Called from bootstrap. */
void
pid_bootstrap(void)
{
    spinlock_init(&pid_lock);
}

/*
 * Allocate a new PID.
 * Returns PID_MIN to PID_MAX on success, 
 * or -1 if no PIDs are available.
 */
pid_t
pid_allocate(void)
{
    pid_t pid;

    spinlock_acquire(&pid_lock);
	if (next_pid > PID_MAX) {
		kprintf("No more PIDs available\n");
		spinlock_release(&pid_lock);
		return -1;
	}

    /* Get next available PID */
    pid = next_pid;
    
    /* Increment PID for next allocation */
    next_pid++;
    
    spinlock_release(&pid_lock);
    return pid;
}