/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;

/* Data structures for rope mappings */
typedef struct {
	int hook_to_stake[NROPES];	 // mappings from hook to stake
	int stake_to_hook[NROPES];	 // mappings from stake to hook
	int hook_connected[NROPES];	 // 0 means hook disconnected from rope, 1 means otherwise
	int stake_connected[NROPES]; // 0 means stake disconnected from rope, 1 means otherwise
} RopeMapping

RopeMapping mapping;

void initialize_ropemapping(RopeMapping *mapping) {
    for (int i = 0; i < NROPES; i++) {
        mapping->hook_to_stake[i] = i;
        mapping->stake_to_hook[i] = i;
        mapping->hook_connected[i] = 1;
        mapping->stake_connected[i] = 1;
    }
}

/* Synchronization primitives */



/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

static void dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");

	/* Implement this function */

	/* Dandelion succeeded unhooking one rope */
	thread_yield();
}

static void marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");

	/* Implement this function */
}

static void flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Lord FlowerKiller thread starting\n");

	/* Implement this function */
}

static void balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	/* Implement this function */

	/* all ropes have been severed */
	kprintf("Balloon freed and Prince Dandelion escapes!\n");
	/* balloon thread done*/
	kprintf("Balloon thread done\n");
	/* then exit */
}

// Change this function as necessary
int airballoon(int nargs, char **args)
{

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

	initialize_ropemapping(mapping);

	err = thread_fork("Marigold Thread",
					  NULL, marigold, NULL, 0);
	if (err)
		goto panic;

	err = thread_fork("Dandelion Thread",
					  NULL, dandelion, NULL, 0);
	if (err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++)
	{
		err = thread_fork("Lord FlowerKiller Thread",
						  NULL, flowerkiller, NULL, 0);
		if (err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
					  NULL, balloon, NULL, 0);
	if (err)
		goto panic;

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
		  strerror(err));

done:
	return 0;
}
