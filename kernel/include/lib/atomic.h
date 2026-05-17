#ifndef LIB_ATOMIC_H
#define LIB_ATOMIC_H

#include <stdbool.h>
#include <stdint.h>

typedef int atomic_int;

#define memory_order_relaxed __ATOMIC_RELAXED
#define memory_order_consume __ATOMIC_CONSUME
#define memory_order_acquire __ATOMIC_ACQUIRE
#define memory_order_release __ATOMIC_RELEASE
#define memory_order_acq_rel __ATOMIC_ACQ_REL
#define memory_order_seq_cst __ATOMIC_SEQ_CST

#define atomic_load(ptr, order) __atomic_load_n((ptr), (order))
#define atomic_store(ptr, value, order) \
	__atomic_store_n((ptr), (value), (order))
#define atomic_exchange(ptr, value, order) \
	__atomic_exchange_n((ptr), (value), (order))
#define atomic_fetch_add(ptr, value, order) \
	__atomic_fetch_add((ptr), (value), (order))
#define atomic_fetch_sub(ptr, value, order) \
	__atomic_fetch_sub((ptr), (value), (order))
#define atomic_fetch_or(ptr, value, order) \
	__atomic_fetch_or((ptr), (value), (order))
#define atomic_fetch_and(ptr, value, order) \
	__atomic_fetch_and((ptr), (value), (order))
#define atomic_fetch_xor(ptr, value, order) \
	__atomic_fetch_xor((ptr), (value), (order))
#define atomic_compare_exchange(ptr, expected, desired, success, failure) \
	__atomic_compare_exchange_n((ptr), (expected), (desired), false,      \
								(success), (failure))

#define atomic_init(ptr, value) \
	atomic_store((ptr), (value), memory_order_relaxed)
#define atomic_load_explicit(ptr, order) atomic_load((ptr), (order))
#define atomic_store_explicit(ptr, value, order) \
	atomic_store((ptr), (value), (order))
#define atomic_fetch_add_explicit(ptr, value, order) \
	atomic_fetch_add((ptr), (value), (order))
#define atomic_fetch_sub_explicit(ptr, value, order) \
	atomic_fetch_sub((ptr), (value), (order))
#define atomic_fetch_or_explicit(ptr, value, order) \
	atomic_fetch_or((ptr), (value), (order))
#define atomic_fetch_and_explicit(ptr, value, order) \
	atomic_fetch_and((ptr), (value), (order))
#define atomic_fetch_xor_explicit(ptr, value, order) \
	atomic_fetch_xor((ptr), (value), (order))
#define atomic_compare_exchange_explicit(ptr, expected, desired, success, failure) \
	atomic_compare_exchange((ptr), (expected), (desired), (success), (failure))

static inline void atomic_cpu_relax(void)
{
	__asm__ volatile("pause");
}

#endif // LIB_ATOMIC_H
