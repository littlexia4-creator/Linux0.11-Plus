#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#ifndef SEM_VALUE_MAX
#define SEM_VALUE_MAX 2147483647
#endif

typedef struct {
	/* Tokens occupy bits 1..31; bit 0 announces possible futex waiters. */
	volatile unsigned int __value;
	volatile unsigned int __waiters;
} sem_t;

/* Unnamed, process-private semaphores: pshared must be zero.
 * Functions return 0 on success, or -1 and set errno on failure.
 * Uses 32-bit atomics (i486+ CMPXCHG, including the Bochs guest) and futexes;
 * sem_post() takes no internal lock and may be called from a signal handler.
 * Rebuild applications when this sem_t layout changes. Named semaphores,
 * timed waits, cancellation and process-shared semaphores are not provided.
 * Initialize before sharing, and destroy only after all users have finished.
 * sem_destroy() does not mark storage invalid; reuse needs sem_init().
 * As with the rest of the guest library, errno is still process-global;
 * signal handlers should save and restore errno around semaphore calls. */
int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_destroy(sem_t *sem);
int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);
int sem_post(sem_t *sem);
int sem_getvalue(sem_t *sem, int *value);

#endif
