#include "semaphore.h"
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include "atomic.h"
#include "futex.h"

/* Follow glibc's 32-bit semaphore protocol: a token count plus a conservative
 * waiter bit in one word, and a separate registered-waiter count. */
#define SEM_WAITERS 1U
#define SEM_TOKEN 2U

static unsigned int decode_token_count(unsigned int value)
{
	return value >> 1;
}

static unsigned int encode_token_count(unsigned int value)
{
	return value << 1;
}

/* These helpers operate on a local snapshot, not the shared semaphore. */
static unsigned int with_waiter_bit(unsigned int value)
{
	return value | SEM_WAITERS;
}

static int has_waiter_bit(unsigned int value)
{
	return (value & SEM_WAITERS) != 0;
}

/* Atomically modify the shared word and return its previous value. */
static unsigned int clear_waiter_bit(sem_t *sem)
{
	return atomic_fetch_and_acquire(&sem->__value, ~SEM_WAITERS);
}

static unsigned int set_waiter_bit(sem_t *sem)
{
	return atomic_fetch_or_release(&sem->__value, SEM_WAITERS);
}

static int sem_error(int error)
{
	errno = error;
	return -1;
}

/* Caller has observed at least one token. Return 1 on acquisition;
 * otherwise update *value with the word observed by the failed CAS. */
static int acquire_token(sem_t *sem, unsigned int *value)
{
	return atomic_compare_exchange_acquire(&sem->__value, value,
					      *value - SEM_TOKEN);
}

/* Publish one token; failure updates *value with the observed word.
 * The caller must check for token-count overflow before each attempt. */
static int release_token(sem_t *sem, unsigned int *value)
{
	return atomic_compare_exchange_release(&sem->__value, value,
					      *value + SEM_TOKEN);
}

/* Return 0 after publishing, or -1 on overflow. Preserve the old word in
 * *value so the caller can decide whether to wake without another load. */
static int release_token_retry(sem_t *sem, unsigned int *value)
{
	do {
		if (decode_token_count(*value) == SEM_VALUE_MAX)
			return sem_error(EOVERFLOW);
	} while (!release_token(sem, value));
	return 0;
}

/* Try one acquire CAS; sem_wait() can fall back to its waiter path. */
static int take_token_fast(sem_t *sem)
{
	unsigned int value = atomic_load_relaxed(&sem->__value);

	if (decode_token_count(value) == 0)
		return 0;
	return acquire_token(sem, &value);
}

/* Retry CAS conflicts until we acquire a token or observe an empty count.
 * sem_trywait() must not report EAGAIN just because a CAS lost a race. */
static int take_token_retry(sem_t *sem)
{
	unsigned int value = atomic_load_relaxed(&sem->__value);

	while (decode_token_count(value)) {
		if (acquire_token(sem, &value))
			return 1;
	}
	return 0;
}

static void register_waiter(sem_t *sem)
{
	/* Acquire pairs with the departing waiter's release decrement. */
	atomic_fetch_add_acquire(&sem->__waiters, 1U);
}

static void clear_bit_and_unregister_waiter(sem_t *sem)
{
	unsigned int observed_waiters = atomic_load_relaxed(&sem->__waiters);
	unsigned int competitive_waiters;
	assert(observed_waiters >= 1);

	if (observed_waiters == 1)
		/* 
		The opposite order is wrong, it allows:
		A decrements waiter count: 1 → 0; old count was 1.
		B registers: count becomes 1, and B sets the waiter bit.
		A clears the bit afterward.
		A thinks no repair is needed because its decrement observed 1.
		*/
	    /* when observed_waiters>1 we believe another waiter exists, so we leave the waiter bit set */
		clear_waiter_bit(sem);

	competitive_waiters = atomic_fetch_add_release(&sem->__waiters, -1U);

	/* another thread can register between the initial load and the atomic decrement */
	if (observed_waiters == 1 &&competitive_waiters != 1) {
		unsigned int available_tokens;

		assert(competitive_waiters > 1);
		/* revert the previous clear operation */
		/* wake up any waiting threads */
		available_tokens = decode_token_count(set_waiter_bit(sem));
		if (available_tokens)
			futex_wake((volatile int *)&sem->__value, available_tokens);
	}
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
	if (!sem || value > SEM_VALUE_MAX)
		return sem_error(EINVAL);
	if (pshared)
		return sem_error(ENOSYS);
	/* Initialize before publishing the semaphore to other threads. */
	sem->__value = encode_token_count(value);
	sem->__waiters = 0;
	return 0;
}

int sem_destroy(sem_t *sem)
{
	if (!sem)
		return sem_error(EINVAL);
	if (atomic_load_relaxed(&sem->__waiters))
		return sem_error(EBUSY);
	/* No storage or kernel object to release. All users must have finished. */
	return 0;
}

int sem_trywait(sem_t *sem)
{
	if (!sem)
		return sem_error(EINVAL);
	return take_token_retry(sem) ? 0 : sem_error(EAGAIN);
}

int sem_wait(sem_t *sem)
{
	if (!sem)
		return sem_error(EINVAL);
	if (take_token_fast(sem))
		return 0;
	register_waiter(sem);
	for (;;) {
		unsigned int value = atomic_load_relaxed(&sem->__value);
		if (!has_waiter_bit(value))
			value = with_waiter_bit(set_waiter_bit(sem));
		if (decode_token_count(value) == 0) {
			int result;

			assert(value == SEM_WAITERS);
			/* Sleep only while the count is zero and the waiter bit is set.
			 * Posting first changes this word, making WAIT return EAGAIN. */
			result = futex_wait((volatile int *)&sem->__value, SEM_WAITERS);
			if (result < 0 && result != -EAGAIN) {
				clear_bit_and_unregister_waiter(sem);
				return sem_error(-result);
			}
			continue;
		}
		if (acquire_token(sem, &value)) {
			clear_bit_and_unregister_waiter(sem);
			return 0;
		}
	}
}

int sem_post(sem_t *sem)
{
	unsigned int value;

	if (!sem)
		return sem_error(EINVAL);
	value = atomic_load_relaxed(&sem->__value);
	if (release_token_retry(sem, &value) < 0)
		return -1;
	/* The condition is an optimization;
	   Calling futex_wake() unconditionally would also be correct, 
	   but would make every post enter the kernel */
	if (has_waiter_bit(value))
		futex_wake((volatile int *)&sem->__value, 1);
	return 0;
}

int sem_getvalue(sem_t *sem, int *value)
{
	if (!sem || !value)
		return sem_error(EINVAL);
	*value = decode_token_count(atomic_load_relaxed(&sem->__value));
	return 0;
}
