#ifndef LIB_SPINLOCK_H
#define LIB_SPINLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include <lib/atomic.h>

typedef struct {
	volatile uint32_t value;
} spinlock_t;

#define SPINLOCK_INIT \
	{ 0 }

static inline void spinlock_init(spinlock_t *lock)
{
	lock->value = 0;
}

static inline bool spinlock_try_lock(spinlock_t *lock)
{
	return atomic_exchange(&lock->value, 1, __ATOMIC_ACQUIRE) == 0;
}

static inline void spinlock_lock(spinlock_t *lock)
{
	while (!spinlock_try_lock(lock)) {
		while (atomic_load(&lock->value, __ATOMIC_RELAXED) != 0)
			atomic_cpu_relax();
	}
}

static inline void spinlock_unlock(spinlock_t *lock)
{
	atomic_store(&lock->value, 0, __ATOMIC_RELEASE);
}

#endif // LIB_SPINLOCK_H
