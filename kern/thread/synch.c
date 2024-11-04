/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
        struct semaphore *sem;

        sem = kmalloc(sizeof(struct semaphore));
        if (sem == NULL) {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void
P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
	spinlock_release(&sem->sem_lock);
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

////////////////////////////////////////////////////////////
//
// Lock.

/* Create the lock. */
struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        lock = kmalloc(sizeof(struct lock));
        if (lock == NULL)
        {
                return NULL;
        }

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL)
        {
                kfree(lock);
                return NULL;
        }

        // add stuff here as needed

        spinlock_init(&lock->lk_spinlock);            /* initialize spinlock */
        lock->lk_wchan = wchan_create(lock->lk_name); /* create wait channel */
        /* if failing to create wait channel, free the lock */
        if (lock->lk_wchan == NULL)
        {
                kfree(lock->lk_name);
                kfree(lock);
                return NULL;
        }
        /* initially no thread is holding the lock */
        lock->lk_holder = NULL;
        lock->lk_hold = 0;

        return lock;
}

/* Clean up the lock. */
void lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);

        // add stuff here as needed

        /* acquire the spinlock making sure no other threads can modify the lock */
        spinlock_acquire(&lock->lk_spinlock);

        KASSERT(!lock->lk_hold);                                    /* make sure the lock is not used */
        KASSERT(lock->lk_holder == NULL);                           /* make sure no thread is holding the lock */
        KASSERT(wchan_isempty(lock->lk_wchan, &lock->lk_spinlock)); /* make sure wait channel is empty */

        spinlock_release(&lock->lk_spinlock);

        wchan_destroy(lock->lk_wchan);        /* destroy the wait channel */
        spinlock_cleanup(&lock->lk_spinlock); /* clean up the spinlock */

        /* clean up the lock structure */
        kfree(lock->lk_name);
        kfree(lock);
}

/*
 * Get the lock. Only one thread can hold the lock at the same time.
 * If the lock is held by another thread, put the current thread to sleep.
 */
void lock_acquire(struct lock *lock)
{
        // Write this

        KASSERT(lock != NULL);

        /* acquire the spinlock, protecting the lock and the wait channel */
        spinlock_acquire(&lock->lk_spinlock);

        /* if the lock is held by other threads, put the current thread to sleep */
        while (lock->lk_hold)
        {
                /*
                 * wchan_sleep would unlocks the spinlock before sleeping, and
                 * acquire the spinlock again before returning.
                 */
                wchan_sleep(lock->lk_wchan, &lock->lk_spinlock);
        }

        /* acquire the lock for the current thread */
        lock->lk_hold = 1;
        lock->lk_holder = curthread;

        spinlock_release(&lock->lk_spinlock);
}

/* Free the lock. Only the thread holding the lock can do this. */
void lock_release(struct lock *lock)
{
        // Write this

        KASSERT(lock != NULL);
        /* make sure the current thread is holding the lock */
        KASSERT(lock_do_i_hold(lock));

        /* acquire the spinlock, protecting the lock and the wait channel */
        spinlock_acquire(&lock->lk_spinlock);

        /* free the lock */
        lock->lk_hold = 0;
        lock->lk_holder = NULL;
        /* wake up a thread waiting on the wait channel */
        wchan_wakeone(lock->lk_wchan, &lock->lk_spinlock);

        spinlock_release(&lock->lk_spinlock);
}

/*
 * Returns true if the current thread is holding the lock.
 * Otherwise, return false.
 */
bool lock_do_i_hold(struct lock *lock)
{
        // Write this

        KASSERT(lock != NULL);

        /* if there is no current thread, nobody is holding the lock */
        if (curthread == NULL)
        {
                return false;
        }

        /* acquire the spinlock before checking whether the current threads holds the lock */
        spinlock_acquire(&lock->lk_spinlock);
        bool do_i_hold = (lock->lk_holder == curthread);
        spinlock_release(&lock->lk_spinlock);

        return do_i_hold;
}

////////////////////////////////////////////////////////////
//
// CV

/* Create the cv. */
struct cv *
cv_create(const char *name)
{
        struct cv *cv;

        cv = kmalloc(sizeof(struct cv));
        if (cv == NULL)
        {
                return NULL;
        }

        cv->cv_name = kstrdup(name);
        if (cv->cv_name == NULL)
        {
                kfree(cv);
                return NULL;
        }

        // add stuff here as needed

        cv->cv_wchan = wchan_create(cv->cv_name); /* create the wait channel */
        /* if failing to create wait channel, free the cv */
        if (cv->cv_wchan == NULL)
        {
                kfree(cv->cv_name);
                kfree(cv);
                return NULL;
        }
        spinlock_init(&cv->cv_lock); /* initialize the spinlock */

        return cv;
}

/* Clean up the cv. */
void cv_destroy(struct cv *cv)
{
        KASSERT(cv != NULL);

        // add stuff here as needed

        /*
         * acquire the spinlock making sure no other threads can modify the cv
         * when we check the wait channel is empty.
         */
        spinlock_acquire(&cv->cv_lock);
        KASSERT(wchan_isempty(cv->cv_wchan, &cv->cv_lock));
        spinlock_release(&cv->cv_lock);

        wchan_destroy(cv->cv_wchan);    /* destroy the wait channel */
        spinlock_cleanup(&cv->cv_lock); /* clean up the spinlock */

        kfree(cv->cv_name);
        kfree(cv);
}

/*
 * Release the supplied lock, go to sleep, and, after
 * waking up again, re-acquire the lock.
 */
void cv_wait(struct cv *cv, struct lock *lock)
{
        // Write this

        KASSERT(cv != NULL);
        KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));       /* check the current thread is holding the lock passed in */
        KASSERT(!curthread->t_in_interrupt); /* make sure we are not in an interrupt handler */

        /* acquire the spinlock, protecting the wait channel */
        spinlock_acquire(&cv->cv_lock);

        /* release the lock passed in */
        lock_release(lock);
        /*
         * Before wchan_sleep puts the current thread to sleep, it releases the spinlock.
         * Before returning from wchan_sleep, it relocks the spinlock.
         */
        wchan_sleep(cv->cv_wchan, &cv->cv_lock);

        spinlock_release(&cv->cv_lock);

        /* After waking up, re-acquire the lock passed in again before returning. */
        lock_acquire(lock);
}

/* Wake up one thread that's sleeping on the cv. */
void cv_signal(struct cv *cv, struct lock *lock)
{
        // Write this

        KASSERT(cv != NULL);
        KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));       /* check the current thread is holding the lock passed in */
        KASSERT(!curthread->t_in_interrupt); /* make sure we are not in an interrupt handler */

        /* acquire the spinlock before manipulating the wait channel */
        spinlock_acquire(&cv->cv_lock);

        /* wake up one thread waiting on the cv */
        wchan_wakeone(cv->cv_wchan, &cv->cv_lock);

        spinlock_release(&cv->cv_lock);
}

/* Wake up all threads sleeping on the cv. */
void cv_broadcast(struct cv *cv, struct lock *lock)
{
        // Write this

        KASSERT(cv != NULL);
        KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));       /* check the current thread is holding the lock passed in */
        KASSERT(!curthread->t_in_interrupt); /* make sure we are not in an interrupt handler */

        /* acquire the spinlock before manipulating the wait channel */
        spinlock_acquire(&cv->cv_lock);

        /* wake up all threads waiting on the cv */
        wchan_wakeall(cv->cv_wchan, &cv->cv_lock);

        spinlock_release(&cv->cv_lock);
}
