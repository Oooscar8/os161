[toc]



# Assignment2

>Use [this document](https://www.gnu.org/prep/standards/html_node/Writing-C.html) as a guideline for writing high-quality code.

>In this assignment you finally get to write your own code in OS161! By the end of this assignment you will:
>
>- Have a good understanding of the implementation of **spinlocks** and **semaphores** in OS161
>- Implement locks
>- Implement condition variables.



## Step 1. Prepare

Make sure you don't have any uncommitted updates in your repo. Now, tag your repository as shown here:

```
git tag asst2-start
```

Now push that new tag:

```
git push --tags
```

Make a directory submit/asst2 in your os161 tree. You will put your file with the answers to code reading questions in that directory. 

* **Physical memory and CPUs**

Change `sys161.conf` file in `~/os161/root`:

```
31 mainboard ramsize=2097152 cpus=4
```

* **Built-in thread tests**

The thread test code uses the **semaphore synchronization primitive**

You should trace the execution of one of these thread tests in GDB to see **how the scheduler acts**, **how threads are created**, and what exactly happens in a **context switch**. You should be able to step through a call to `thread_switch()` and see exactly **where** the current thread changes.

* **Debugging concurrent programs**

`thread_yield()` is automatically called for you at intervals that vary randomly.

Change `sys161.conf` file in `~/os161/root`:

```
28	random seed=1
```

Once you are done with initial debugging/testing, remember to set the random device back to `autoseed`.

This should allow you to **test your solutions under varying conditions** and may expose scenarios that you had not anticipated, which is central to effective testing.



## Step 2. Code reading exercises

> 1. What happens to a thread when it exits (i.e., calls thread_exit())? What about when it sleeps?

`thread_exit()` detaches the thread from the process, checks the stack guard band, turns interrupts off on this processor and performs a thread switch.

`thread.c` 777:

```c
/*
 * Cause the current thread to exit.
 *
 * The parts of the thread structure we don't actually need to run
 * should be cleaned up right away. The rest has to wait until
 * thread_destroy is called from exorcise().
 *
 * Does not return.
 */
void
    thread_exit(void)
{
    struct thread *cur;

    cur = curthread;

    /*
	 * Detach from our process. You might need to move this action
	 * around, depending on how your wait/exit works.
	 */
    proc_remthread(cur);

    /* Make sure we *are* detached (move this only if you're sure!) */
    KASSERT(cur->t_proc == NULL);

    /* Check the stack guard band. */
    thread_checkstack(cur);

    /* Interrupts off on this processor */
    splhigh();
    thread_switch(S_ZOMBIE, NULL, NULL);
    panic("braaaaaaaiiiiiiiiiiinssssss\n");
}
```

First, `wchan_sleep` makes sure that we are not in an interrupt handler, hold the specific  spinlock associated with the wait channel WC and no others. Then, it performs a thread switch, and puts the thread go to sleep on the specified wait channel WC. Lastly, when being woken up, it locks the spinlock again before returning.

`thread.c` 1026:

```c
/*
 * Yield the cpu to another process, and go to sleep, on the specified
 * wait channel WC, whose associated spinlock is LK. Calling wakeup on
 * the channel will make the thread runnable again. The spinlock must
 * be locked. The call to thread_switch unlocks it; we relock it
 * before returning.
 */
void
    wchan_sleep(struct wchan *wc, struct spinlock *lk)
{
    /* may not sleep in an interrupt handler */
    KASSERT(!curthread->t_in_interrupt);

    /* must hold the spinlock */
    KASSERT(spinlock_do_i_hold(lk));

    /* must not hold other spinlocks */
    KASSERT(curcpu->c_spinlocks == 1);

    thread_switch(S_SLEEP, wc, lk);
    spinlock_acquire(lk);
}
```

> 2. What function(s) handle(s) a context switch?

`thread.c` 560: 

`thread_switch()`

> 3. What does it mean for a thread to be in each of the possible thread states?

During context switching, we need to put the thread in the right place according to its `newstate`.

* **S_RUN**:
  A thread should never be put into the `S_RUN` state within `thread_switch`. But if it does, call panic.

* **S_READY**:
  A thread in the `S_READY` state is ready to run but is not currently running. `thread_make_runnable` is called to add it to the run queue so that the scheduler can pick it up for execution later.

* **S_SLEEP**:
  A thread in the `S_SLEEP` state is blocked, waiting to be woken up. The thread is added to the wait channel `wc`, and it will remain in this state until a wakeup call (like `wchan_wake`) signals it. After putting the thread on the list, we release the spinlock associated with the wait channel `wc`.

* **S_ZOMBIE**:
  A thread in the `S_ZOMBIE` state has finished its execution but has not yet been fully cleaned up. The thread is added to the CPU's zombie list `c_zombies`, where it waits to be freed.

`thread.c`: `thread_switch()`: 595:

```c
/* Put the thread in the right place. */
switch (newstate) {
    case S_RUN:
        panic("Illegal S_RUN in thread_switch\n");
    case S_READY:
        thread_make_runnable(cur, true /*have lock*/);
        break;
    case S_SLEEP:
        cur->t_wchan_name = wc->wc_name;
        /*
		 * Add the thread to the list in the wait channel, and
		 * unlock same. To avoid a race with someone else
		 * calling wchan_wake*, we must keep the wchan's
		 * associated spinlock locked from the point the
		 * caller of wchan_sleep locked it until the thread is
		 * on the list.
		 */
        threadlist_addtail(&wc->wc_threads, cur);
        spinlock_release(lk);
        break;
    case S_ZOMBIE:
        cur->t_wchan_name = "ZOMBIE";
        threadlist_addtail(&curcpu->c_zombies, cur);
        break;
}
cur->t_state = newstate;
```

> 4. What does it mean to turn interrupts off? How is this accomplished? Why is it important to turn off interrupts in the thread subsystem code?

Turning interrupts off means that the current processor is temporarily prevented from responding to hardware interrupts, meaning that the current CPU cannot perform a context switch.

In os161, turning interrupts off is accomplished by changing the processor's interrupt priority level(IPL). The function `splhigh()` is used to set the IPL to its highest level to disable all interrupts.

```c
/* Explicitly disable interrupts on this processor */
spl = splhigh();
```

It is important to turn off interrupts in the thread subsystem code. When we perform operations on threads such as context switching, blocking threads, exit threads, we don't want to be interrupted. If we are interrupted during those operations, we may lose important information of the thread. For example, if we are interrupted during context switching when we are saving the thread's state, we may lose the state during interrupts. Turning off interrupts also helps avoid deadlock and improve performance.

> 5. What happens when a thread wakes up another thread? How does a sleeping thread get to run again?

When a thread wants to wake up another thread, it calls the function `wchan_wakeone`, the function grabs a thread from the wait channel and then calls `thread_make_runnable` to make the target thread runnable.

In `thread_make_runnable`, the target thread should lock the run queue of the CPU, ensuring only one thread in the run queue can modify the CPU. Then it puts the target thread on the run queue of the CPU.

> 6. What function(s) choose(s) the next thread to run?

Function `schedule` (in `thread.c`:820) and `thread_switch` both choose the next thread to run.

> 7. How does it (do they) pick the next thread?

`schedule` and `thread_switch` both use `threadlist_remhead` function, which removes and returns the **first thread** from the run queue of the current CPU, which is the next thread to run. 

> 8. What role does the hardware timer play in scheduling? What hardware independent function is called on a timer interrupt?

The hardware timer generates periodic interrupts to allow the operating system to regain control of the CPU at regular intervals, ensuring that it can check the status of threads and make scheduling decisions.

`hardclock` is the hardware independent function called on a timer interrupt.

> 9. Describe how `wchan_sleep()` and `wchan_wakeone()` are used to implement semaphores.

For function `V`, `wchan_wakeone()` is called after it increments `sem_count` and wants to wake up another thread.

For function `P`, `wchan_sleep` is called when `sem_count = 0` and wants to put the thread to sleep waiting to be awakened. It should be called after we lock the wait channel(using semaphore spinlock) so that we can put the thread to sleep while also release the wait channel lock afterwards. After being woken up, it locks the wait channel again before returning.

```c
struct semaphore {
    char *sem_name;
    struct wchan *sem_wchan;
    struct spinlock sem_lock;
    volatile unsigned sem_count;
}

void
    V(struct semaphore *sem)
{
    KASSERT(sem != NULL);

    spinlock_acquire(&sem->sem_lock);

    sem->sem_count++;
    KASSERT(sem->sem_count > 0);
    wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

    spinlock_release(&sem->sem_lock);
}

void
    P(struct semaphore *sem)
{
    KASSERT(sem != NULL);
    KASSERT(curthread->t_in_interrupt == false);

    /* Use the semaphore spinlock to protect the wchan as well. */
    spinlock_acquire(&sem->sem_lock);
    while (sem->sem_count == 0) {
        wchan_sleep(sem->sem_wchan, &sem->sem_lock);
        // sem_lock is also the wait channel lock.
        // Before returning from sleep, we should relock.
    }
    KASSERT(sem->sem_count > 0);
    sem->sem_count--;
    spinlock_release(&sem->sem_lock);
}
```

> 10. How does the implementation of `wchan` ensure that a thread never misses a wakeup signal: that another thread cannot attempt to awaken the first thread just as it is preparing to sleep, but before it is actually placed into the sleep queue?

In `P`, `wchan_sleep` is called after we acquire the semaphore spinlock(`spinlock_acquire(&sem->sem_lock)`), which also serves as locking the wait channel. In this way, when we call `wchan_sleep` to put the current thread to sleep, no other threads can manipulate the wait channel(i.e. by calling `wchan_wakeone`). That is to say, no threads can sneak in and try to awaken the thread during its "sleep" process.

```c
/* Use the semaphore spinlock to protect the wchan as well. */
spinlock_acquire(&sem->sem_lock);
while (sem->sem_count == 0) {
    wchan_sleep(sem->sem_wchan, &sem->sem_lock);
    // sem_lock is also the wait channel lock.
    // Before returning from sleep, we should relock.
}
```

