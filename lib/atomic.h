#ifndef _LIB_ATOMIC_H
#define _LIB_ATOMIC_H

/* Keep the implementation out of line: GCC 1.40 has no memory clobber,
 * so an external call must order accesses to data protected by the lock. */
int __atomic_exchange_int(volatile int *word, int value);

/* i386 memory xchg supplies full ordering for both operations. */
#define atomic_exchange_acquire(word, value) __atomic_exchange_int(word, value)
#define atomic_exchange_release(word, value) __atomic_exchange_int(word, value)

#endif
