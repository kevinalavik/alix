#ifndef LIB_MUTEX_H
#define LIB_MUTEX_H

#include <stdbool.h>

#include <lib/rq.h>
#include <lib/spinlock.h>

typedef struct tcb tcb_t;

typedef struct mutex {
	spinlock_t guard;
	tcb_t *owner;
	rq_t waiters;
} mutex_t;

#define MUTEX_INIT \
	{ .guard = SPINLOCK_INIT, .owner = NULL, .waiters = RQ_INIT() }

void mutex_init(mutex_t *mutex);
bool mutex_try_lock(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
bool mutex_is_locked(mutex_t *mutex);

#endif // LIB_MUTEX_H
