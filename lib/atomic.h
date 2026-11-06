#ifndef _LIB_ATOMIC_H
#define _LIB_ATOMIC_H

/* Keep the implementation out of line: GCC 1.40 has no memory clobber,
 * so an external call must order accesses to data protected by the lock. */
int __atomic_exchange_int(volatile int *word, int value);

/* Aligned 32-bit words; compare/exchange requires an i486 or newer CPU.
 * Failure updates *expected with the observed value. These external helpers
 * keep GCC 1.40 from moving protected accesses across the atomic operation. */
unsigned int __atomic_load_uint(volatile unsigned int *word);
int __atomic_compare_exchange_uint(volatile unsigned int *word,
				   unsigned int *expected, unsigned int desired);
unsigned int __atomic_fetch_add_uint(volatile unsigned int *word, unsigned int value);
unsigned int __atomic_fetch_and_uint(volatile unsigned int *word, unsigned int value);
unsigned int __atomic_fetch_or_uint(volatile unsigned int *word, unsigned int value);

/* i386 memory xchg supplies full ordering for both operations. */
#define atomic_exchange_acquire(word, value) __atomic_exchange_int(word, value)
#define atomic_exchange_release(word, value) __atomic_exchange_int(word, value)

/* Aligned x86 loads need no extra instruction for relaxed ordering.
 * Locked CMPXCHG supplies full ordering, stronger than acquire/release. */
#define atomic_load_relaxed(word) __atomic_load_uint(word)
#define atomic_compare_exchange_acquire(word, expected, desired) \
	__atomic_compare_exchange_uint(word, expected, desired)
#define atomic_compare_exchange_release(word, expected, desired) \
	__atomic_compare_exchange_uint(word, expected, desired)
#define atomic_fetch_add_acquire(word, value) __atomic_fetch_add_uint(word, value)
#define atomic_fetch_add_release(word, value) __atomic_fetch_add_uint(word, value)
#define atomic_fetch_and_acquire(word, value) __atomic_fetch_and_uint(word, value)
#define atomic_fetch_or_release(word, value) __atomic_fetch_or_uint(word, value)

#endif
