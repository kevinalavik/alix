#ifndef LIB_ATOMIC_H
#define LIB_ATOMIC_H

#include <stdbool.h>
#include <stdint.h>

#define atomic_load(ptr, order) __atomic_load_n((ptr), (order))
#define atomic_store(ptr, value, order) __atomic_store_n((ptr), (value), (order))
#define atomic_exchange(ptr, value, order) \
	__atomic_exchange_n((ptr), (value), (order))
#define atomic_fetch_add(ptr, value, order) \
	__atomic_fetch_add((ptr), (value), (order))
#define atomic_fetch_sub(ptr, value, order) \
	__atomic_fetch_sub((ptr), (value), (order))
#define atomic_compare_exchange(ptr, expected, desired, success, failure) \
	__atomic_compare_exchange_n((ptr), (expected), (desired), false, (success), \
								(failure))

static inline void atomic_cpu_relax(void)
{
	__asm__ volatile("pause");
}

#endif // LIB_ATOMIC_H
