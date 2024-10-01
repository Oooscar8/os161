/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 0
#define NROPES 16
static int ropes_left = NROPES;

/* Data structures for rope mappings */
typedef struct
{
	int hook;
	bool is_connected;
	struct lock *lock;
} stake;

typedef struct
{
	int stake;
	bool is_connected;
	struct lock *lock;
} hook;

static stake stakes[NROPES];
static hook hooks[NROPES];

/* Synchronization primitives */
static struct lock *ropes_left_lock; // protect global variable ropes_left

static struct cv *all_threads_done_cv;
static struct lock *threads_exit_lock;
static struct cv *balloon_thread_done_cv;
static struct lock *balloon_exit_lock;

static int threads_exited = 0;
static bool balloon_finished = false;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

/* Initialize rope mappings. */
static void initialize_mappings()
{
	for (int i = 0; i < NROPES; ++i)
	{
		stakes[i].hook = i;
		stakes[i].is_connected = true;
		stakes[i].lock = lock_create("stake lock");

		hooks[i].stake = i;
		hooks[i].is_connected = true;
		hooks[i].lock = lock_create("hook lock");
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

static void cleanup_synchronization() {
	lock_destroy(ropes_left_lock);
	cv_destroy(all_threads_done_cv);
	lock_destroy(threads_exit_lock);
	cv_destroy(balloon_thread_done_cv);
    lock_destroy(balloon_exit_lock);
}

static void notify_thread_exit() {
    lock_acquire(threads_exit_lock);
    threads_exited++;
    if (threads_exited == N_LORD_FLOWERKILLER + 2) {
        cv_signal(all_threads_done_cv, threads_exit_lock);
    }
    lock_release(threads_exit_lock);
}

static void wait_for_balloon() {
    lock_acquire(balloon_exit_lock);
    while (!balloon_finished) {
        cv_wait(balloon_thread_done_cv, balloon_exit_lock);
    }
    lock_release(balloon_exit_lock);
}

/*
 * Helper function:
 * Check if the rope is cut.
 * Should be protected by lock when being invoked.
 */
static bool is_rope_cut(int hook)
{
	return (!hooks[hook].is_connected || !stakes[hooks[hook].stake].is_connected);
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
		lock_acquire(hooks[hook].lock);
		/* If the rope is not severed, sever it from the hook. */
		if (!is_rope_cut(hook))
		{
			hooks[hook].is_connected = false;

			lock_acquire(ropes_left_lock);
			ropes_left--;
			kprintf("Dandelion severed rope %d\n", hook);
			lock_release(ropes_left_lock);

			lock_release(hooks[hook].lock);

			/* Dandelion succeeded unhooking one rope. */
			thread_yield();
			continue;
		}
		lock_release(hooks[hook].lock);
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

		/* Mapping from stake to hook, protected by stake_locks. */
		lock_acquire(stakes[stake].lock);
		int hook = stakes[stake].hook;
		lock_release(stakes[stake].lock);

		lock_acquire(hooks[hook].lock);
		if (!is_rope_cut(hook))
		{
			stakes[stake].is_connected = false;

			lock_acquire(ropes_left_lock);
			ropes_left--;
			kprintf("Marigold severed rope %d from stake %d\n", hook, stake);
			lock_release(ropes_left_lock);

			lock_release(hooks[hook].lock);

			/* Marigold succeeded unhooking one rope. */
			thread_yield();
			continue;
		}
		lock_release(hooks[hook].lock);
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
}

static void balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	/* Implement this function */

	lock_acquire(threads_exit_lock);
    while (threads_exited != N_LORD_FLOWERKILLER + 2) {
        cv_wait(all_threads_done_cv, threads_exit_lock);
    }
    lock_release(threads_exit_lock);

	/* all ropes have been severed */
	kprintf("Balloon freed and Prince Dandelion escapes!\n");
	/* balloon thread done*/
	kprintf("Balloon thread done\n");

	/* notify the main thread that balloon has exited. */
	lock_acquire(balloon_exit_lock);
    balloon_finished = true;
    cv_signal(balloon_thread_done_cv, balloon_exit_lock);
    lock_release(balloon_exit_lock);

	/* then exit */
	thread_exit();
}

// Change this function as necessary
int airballoon(int nargs, char **args)
{

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

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
    wait_for_balloon();
	kprintf("Main thread done\n");
	cleanup_synchronization();
	return 0;
}
