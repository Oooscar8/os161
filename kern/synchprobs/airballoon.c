/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;


/* Data structures for rope mappings */
int hook_to_stake[NROPES];	 // mappings from hook to stake (e.g. hook_to_stake[3] = 7 means hook 3 connects to stake 7)
int stake_to_hook[NROPES];	 // mappings from stake to hook (e.g. stake_to_hook[7] = 3 means stake 7 connects to hook 3)
int hook_connected[NROPES];	 // 0 means hook disconnected from rope, 1 means otherwise
int stake_connected[NROPES]; // 0 means stake disconnected from rope, 1 means otherwise


/* Synchronization primitives */
static struct lock *rope_locks[NROPES]; // protect each rope
static struct lock *ropes_left_lock;	// protect global variable ropes_left
static struct lock *rope_mapping_lock;	// protect the stake-to-hook mapping


/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */


/* Initialize rope mappings and locks. */
static void initialize()
{
	for (int i = 0; i < NROPES; i++)
	{
		hook_to_stake[i] = i;
		stake_to_hook[i] = i;
		hook_connected[i] = 1;
		stake_connected[i] = 1;
		rope_locks[i] = lock_create("rope lock");
	}
	ropes_left_lock = lock_create("ropes_left lock");
	rope_mapping_lock = lock_create("rope mapping lock");
}


/*
 * Helper function:
 * Check if the rope is cut.
 * Should be protected by lock when being invoked.
 */
static bool is_rope_cut(int hook)
{
	return (!hook_connected[hook] || !stake_connected[hook_to_stake[hook]]);
}


/* Dandelion severs ropes from hooks. */
static void dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");

	/* Implement this function */
	while (true)
	{
		/* Check if all the ropes are severed. */
		lock_acquire(ropes_left_lock);
		if (ropes_left == 0)
		{
			lock_release(ropes_left_lock);
			break;
		}
		lock_release(ropes_left_lock);

		/* Randomly select a hook. */
		int hook = random() % NROPES;

		/* Try to sever the rope. */
		lock_acquire(rope_locks[hook]);
		/* If the rope is not severed, sever it from the hook. */
		if (!is_rope_cut(hook))
		{
			hook_connected[hook] = 0;

			lock_acquire(ropes_left_lock);
			ropes_left--;
			kprintf("Dandelion severed rope %d\n", hook);
			lock_release(ropes_left_lock);

			lock_release(rope_locks[hook]);

			/* Dandelion succeeded unhooking one rope. */
			thread_yield();
			continue;
		}
		lock_release(rope_locks[hook]);
	}

	kprintf("Dandelion thread done\n");
}


/* Marigold severs ropes from stakes. */
static void marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");

	/* Implement this function */
	while (true)
	{
		/* Check if all the ropes are severed. */
		lock_acquire(ropes_left_lock);
		if (ropes_left == 0)
		{
			lock_release(ropes_left_lock);
			break;
		}
		lock_release(ropes_left_lock);

		/* Randomly select a stake. */
		int stake = random() % NROPES;

		/*
		 * Mapping from stake to hook.
		 * Acquire the corresponding hook from the selected stake,
		 * protected by rope_mapping_lock.
		 */
		lock_acquire(rope_mapping_lock);
		int hook = stake_to_hook[stake];
		lock_release(rope_mapping_lock);

		lock_acquire(rope_locks[hook]);
		if (is_rope_cut(hook))
		{
			stake_connected[stake] = 0;
			
			lock_acquire(ropes_left_lock);
			ropes_left--;
			kprintf("Marigold severed rope %d from stake %d\n", hook, stake);
			lock_release(ropes_left_lock);

			lock_release(rope_locks[hook]);

			/* Marigold succeeded unhooking one rope. */
			thread_yield();
			continue;
		}
		lock_release(rope_locks[hook]);
	}

	kprintf("Marigold thread done\n");
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
	// kprintf("Balloon freed and Prince Dandelion escapes!\n");
	/* balloon thread done*/
	// kprintf("Balloon thread done\n");
	/* then exit */
}

// Change this function as necessary
int airballoon(int nargs, char **args)
{

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

	initialize();

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
