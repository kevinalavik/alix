#include <lib/mutex.h>

#include <debug/panic.h>
#include <sys/sched.h>

static void mutex_release_guard(void *arg)
{
	spinlock_unlock((spinlock_t *)arg);
}

void mutex_init(mutex_t *mutex)
{
	if (!mutex)
		return;

	spinlock_init(&mutex->guard);
	mutex->owner = NULL;
	rq_init(&mutex->waiters);
}

bool mutex_try_lock(mutex_t *mutex)
{
	tcb_t *current;
	bool acquired = false;

	if (!mutex)
		return false;

	current = sched_current();
	if (!current)
		kpanic(NULL, "mutex_try_lock: no current thread");

	spinlock_lock(&mutex->guard);
	if (mutex->owner == NULL) {
		mutex->owner = current;
		acquired = true;
	}
	spinlock_unlock(&mutex->guard);

	return acquired;
}

void mutex_lock(mutex_t *mutex)
{
	tcb_t *current;

	if (!mutex)
		return;

	current = sched_current();
	if (!current)
		kpanic(NULL, "mutex_lock: no current thread");

	for (;;) {
		spinlock_lock(&mutex->guard);
		if (mutex->owner == NULL) {
			mutex->owner = current;
			spinlock_unlock(&mutex->guard);
			return;
		}

		if (mutex->owner == current) {
			spinlock_unlock(&mutex->guard);
			kpanic(NULL, "mutex_lock: recursive locking is not allowed");
		}

		if (!rq_contains(&mutex->waiters, current))
			rq_push_back(&mutex->waiters, &current->wait_node, current);

		thread_block_current(mutex_release_guard, &mutex->guard);

		if (mutex->owner == current)
			return;
	}
}

void mutex_unlock(mutex_t *mutex)
{
	tcb_t *current;
	tcb_t *next;

	if (!mutex)
		return;

	current = sched_current();
	if (!current)
		kpanic(NULL, "mutex_unlock: no current thread");

	spinlock_lock(&mutex->guard);
	if (mutex->owner != current) {
		spinlock_unlock(&mutex->guard);
		kpanic(NULL, "mutex_unlock: current thread does not own mutex");
	}

	next = rq_pop_front(&mutex->waiters);
	if (next)
		mutex->owner = next;
	else
		mutex->owner = NULL;

	spinlock_unlock(&mutex->guard);

	if (next)
		thread_wake(next);
}

bool mutex_is_locked(mutex_t *mutex)
{
	bool locked;

	if (!mutex)
		return false;

	spinlock_lock(&mutex->guard);
	locked = mutex->owner != NULL;
	spinlock_unlock(&mutex->guard);

	return locked;
}
