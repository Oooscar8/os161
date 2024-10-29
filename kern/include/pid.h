#include <types.h>
#include <spinlock.h>

/* Global variables for PID management */
extern pid_t next_pid;        /* Next PID to assign */
extern struct spinlock pid_lock;         /* Lock for pid allocation */

/* Call once during system startup to initialize the PID system. */
void pid_bootstrap(void);

pid_t pid_allocate(void);