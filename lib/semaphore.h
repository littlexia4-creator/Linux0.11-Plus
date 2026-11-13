#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include "pthread.h"

#ifndef SEM_VALUE_MAX
#define SEM_VALUE_MAX 2147483647
#endif

typedef struct {
	pthread_mutex_t __lock;
	volatile int __value;
	unsigned int __waiters;
} sem_t;

/* Unnamed, process-private semaphores: pshared must be zero.
 * Functions return 0 on success, or -1 and set errno on failure.
 * The internal mutex preserves i386/GCC 1.40 compatibility. Consequently,
 * sem_post() must not be called from a signal handler. Named semaphores,
 * timed waits, cancellation and process-shared semaphores are not provided.
 * Initialize before sharing, and destroy only after all users have finished.
 * As with the rest of the guest library, errno is still process-global. */
int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_destroy(sem_t *sem);
int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);
int sem_post(sem_t *sem);
int sem_getvalue(sem_t *sem, int *value);

#endif
