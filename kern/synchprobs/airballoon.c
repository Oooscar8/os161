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

/* rope structure */
typedef struct
{
	bool is_cut;
	int rope_number;
	struct lock *lock;
} rope;

/* stake structure, containing the connected rope */
typedef struct
{
	rope *connected_rope;
	struct lock *lock;
} stake;

/* hook structure, containing the connected rope */
typedef struct
{
	rope *connected_rope;
} hook;

/* represent each rope, stake and hook */
static rope ropes[NROPES];
static stake stakes[NROPES];
static hook hooks[NROPES];

/* Synchronization primitives */
static struct lock *ropes_left_lock; // protect global variable ropes_left

/* CV for balloon thread waiting for all dandelion, marigold and flowerkiller threads done */
static struct cv *all_threads_done_cv;
static struct lock *threads_exit_lock;
static int threads_exited = 0;

/* CV for main thread waiting for balloon thread finished. */
static struct cv *balloon_thread_done_cv;
static struct lock *balloon_exit_lock;
static bool balloon_finished = false;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

/*
 * Using three structures: rope, stake and hook
 * Each rope and stake has its own lock.
 * We don't need a lock for hooks as they always maintain 1:1 correspondence to the ropes.
 *
 * Dandelion accesses each rope only from its hook, required to lock the rope before severing it.
 * Marigold should lock the stake first before accessing the rope from its stake,
 * and lock the rope before severing it.
 * Flowerkiller should lock both stakes first before accessing the ropes from them,
 * and lock the corresponding ropes(in particular order) before swaping them.
 *
 * Dandelion and marigold threads should exit when global variable `ropes_left = 0`.
 * Flowerkiller threads should exit when global variable `ropes_left < 2`.
 * Balloon thread should wait for all dandelion, marigold and flowerkillers threads to exit before exiting.
 * Airballoon(main) thread should wait for balloon thread to exit before exiting.
 */

////////////////////////////////////////////////////////////
//
// Helper functions

/* Initialize rope mappings. */
static void initialize_mappings()
{
	for (int i = 0; i < NROPES; ++i)
	{
		ropes[i].is_cut = false;
		ropes[i].rope_number = i;
		ropes[i].lock = lock_create("rope lock");

		stakes[i].connected_rope = &ropes[i];
		stakes[i].lock = lock_create("stake lock");

		hooks[i].connected_rope = &ropes[i];
	}
}

/* Initialize sychronization primitives */
static void initialize_synchronization()
{
	ropes_left_lock = lock_create("ropes_left lock");
	all_threads_done_cv = cv_create("all threads done cv");
	threads_exit_lock = lock_create("threads exit lock");
	balloon_thread_done_cv = cv_create("balloon thread done cv");
	balloon_exit_lock = lock_create("balloon exit lock");
}

/* Clean up synchronization primitives */
static void cleanup_synchronization()
{
	lock_destroy(ropes_left_lock);
	cv_destroy(all_threads_done_cv);
	lock_destroy(threads_exit_lock);
	cv_destroy(balloon_thread_done_cv);
	lock_destroy(balloon_exit_lock);
	for (int i = 0; i < NROPES; ++i)
	{
		lock_destroy(ropes[i].lock);
		lock_destroy(stakes[i].lock);
	}
}

/*
 * Dandelion, marigold and all flowerkiller threads should call this right before exiting,
 * notifying their exit and waking up balloon thread when all of them have exited.
 */
static void notify_thread_exit()
{
	lock_acquire(threads_exit_lock);
	threads_exited++;
	if (threads_exited == N_LORD_FLOWERKILLER + 2)
	{
		cv_signal(all_threads_done_cv, threads_exit_lock);
	}
	lock_release(threads_exit_lock);
}

/* Airballoon(main) thread should call this before exiting, waiting for balloon thread to exit. */
static void wait_for_balloon()
{
	lock_acquire(balloon_exit_lock);
	while (!balloon_finished)
	{
		cv_wait(balloon_thread_done_cv, balloon_exit_lock);
	}
	lock_release(balloon_exit_lock);
}

////////////////////////////////////////////////////////////
//
// Threads

/* Dandelion severs ropes from hooks. */
static void dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");

	/* Implement this function */

	/* Loop until all the ropes have been severed. */
	while (true)
	{
		/* Check if all the ropes are severed. If so, break and exit. */
		lock_acquire(ropes_left_lock);
		if (ropes_left == 0)
		{
			lock_release(ropes_left_lock);
			break;
		}
		lock_release(ropes_left_lock);

		/* Randomly select a hook and get its connected rope. */
		int hook = random() % NROPES;
		rope *current_rope = hooks[hook].connected_rope;

		/* If the rope has already been severed by himself, abort. */
		if (current_rope == NULL)
		{
			continue;
		}

		/* lock the rope before trying to sever it */
		lock_acquire(current_rope->lock);
		/* If the rope is not severed, sever it from the hook. */
		if (!current_rope->is_cut)
		{
			current_rope->is_cut = true;
			hooks[hook].connected_rope = NULL;

			lock_acquire(ropes_left_lock);
			ropes_left--;
			kprintf("Dandelion severed rope %d\n", hook);
			lock_release(ropes_left_lock);
		}
		lock_release(current_rope->lock);
		/* give up the cpu and switch to another thread. */
		thread_yield();
	}

	kprintf("Dandelion thread done\n");
	notify_thread_exit();
	thread_exit();
}

/* Marigold severs ropes from stakes. */
static void marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");

	/* Implement this function */

	/* Loop until all the ropes have been severed. */
	while (true)
	{
		/* Check if all the ropes are severed. If so, break and exit. */
		lock_acquire(ropes_left_lock);
		if (ropes_left == 0)
		{
			lock_release(ropes_left_lock);
			break;
		}
		lock_release(ropes_left_lock);

		/* Randomly select a stake and get its connected rope, protected by its lock. */
		int stake = random() % NROPES;
		lock_acquire(stakes[stake].lock);
		rope *current_rope = stakes[stake].connected_rope;

		/* If the rope has already been severed by herself, abort. */
		if (current_rope == NULL)
		{
			lock_release(stakes[stake].lock);
			continue;
		}

		/* lock the rope before trying to sever it */
		lock_acquire(current_rope->lock);
		/* If the rope is not severed, sever it from the stake. */
		if (!current_rope->is_cut)
		{
			current_rope->is_cut = true;
			stakes[stake].connected_rope = NULL;

			lock_acquire(ropes_left_lock);
			ropes_left--;
			kprintf("Marigold severed rope %d from stake %d\n", current_rope->rope_number, stake);
			lock_release(ropes_left_lock);
		}
		/* release the lock in reverse order, avoiding deadlock */
		lock_release(current_rope->lock);
		lock_release(stakes[stake].lock);
		/* give up the cpu and switch to another thread. */
		thread_yield();
	}

	kprintf("Marigold thread done\n");
	notify_thread_exit();
	thread_exit();
}

static void flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Lord FlowerKiller thread starting\n");

	/* Implement this function */

	/* Loop until all the ropes have been severed. */
	while (true)
	{
		/* Check if there are at least two ropes left. If not, break and exit. */
		lock_acquire(ropes_left_lock);
		if (ropes_left < 2)
		{
			lock_release(ropes_left_lock);
			break;
		}
		lock_release(ropes_left_lock);

		/* Randomly select two different stakes to swap. */
		int stake1, stake2;
		do
		{
			stake1 = random() % NROPES;
			stake2 = random() % NROPES;
		} while (stake1 == stake2);

		/* get the smaller stake and bigger stake by their number */
		int first_stake = (stake1 < stake2) ? stake1 : stake2;
		int second_stake = (stake1 < stake2) ? stake2 : stake1;

		/* lock the smaller stake first, then lock the bigger stake, avoiding deadlock, before getting the connected ropes */
		lock_acquire(stakes[first_stake].lock);
		lock_acquire(stakes[second_stake].lock);
		rope *rope1 = stakes[stake1].connected_rope;
		rope *rope2 = stakes[stake2].connected_rope;

		/* If either rope has already been severed from its stake, abort. */
		if (rope1 == NULL || rope2 == NULL)
		{
			lock_release(stakes[second_stake].lock);
			lock_release(stakes[first_stake].lock);
			continue;
		}
        
		/* get the smaller rope and bigger rope by their number */
		rope *first_rope = (rope1->rope_number < rope2->rope_number) ? rope1 : rope2;
		rope *second_rope = (rope1->rope_number < rope2->rope_number) ? rope2 : rope1;

        /* lock the smaller rope first, then lock the bigger rope, avoiding deadlock, before trying to swap them. */
		lock_acquire(first_rope->lock);
		lock_acquire(second_rope->lock);
		/* If neither rope is severed, swap them from their stakes */
		if (!rope1->is_cut && !rope2->is_cut)
		{
			stakes[stake1].connected_rope = rope2;
			stakes[stake2].connected_rope = rope1;

			kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope1->rope_number, stake1, stake2);
			kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope2->rope_number, stake2, stake1);
		}
		/* release the lock in reverse order, avoiding deadlock */
		lock_release(second_rope->lock);
		lock_release(first_rope->lock);
		lock_release(stakes[second_stake].lock);
		lock_release(stakes[first_stake].lock);
		/* give up the cpu and switch to another thread. */
		thread_yield();
	}

	kprintf("Lord FlowerKiller thread done\n");
	notify_thread_exit();
	thread_exit();
}

static void balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	/* Implement this function */

    /* wait until all dandelion, marigold and flowerkiller threads are done */
	lock_acquire(threads_exit_lock);
	while (threads_exited != N_LORD_FLOWERKILLER + 2)
	{
		cv_wait(all_threads_done_cv, threads_exit_lock);
	}
	lock_release(threads_exit_lock);

    /* print the messages */
	kprintf("Balloon freed and Prince Dandelion escapes!\n");
	kprintf("Balloon thread done\n");

	/* wake up the airballoon(main) thread */
	lock_acquire(balloon_exit_lock);
	balloon_finished = true;
	cv_signal(balloon_thread_done_cv, balloon_exit_lock);
	lock_release(balloon_exit_lock);

	/* then exit */
	thread_exit();
}

/* This function initializes the main thread global variables. */
static void main_thread_init() {
	ropes_left = NROPES;
	threads_exited = 0;
    balloon_finished = false;
}

// Change this function as necessary
int airballoon(int nargs, char **args)
{

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

    /* Initialize global variables. */
	main_thread_init();

    /* Initialize rope mappings and synchronization primitives. */
	initialize_mappings();
	initialize_synchronization();

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
    /* wait until balloon thread is done */
	wait_for_balloon();
	kprintf("Main thread done\n");
	cleanup_synchronization();
	
	return 0;
}
