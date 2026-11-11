#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <signal.h>

/* A library handle, independent of the kernel TID cleared at thread exit. */
typedef unsigned long pthread_t;
typedef struct { int __reserved; } pthread_attr_t;
typedef struct { volatile int __lock; } pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }

/* Level3 subset: default joinable threads only (attr must be NULL).
 * The library owns up to 64 stacks of 16 KiB, retained until joining.
 * Creation and joining return error numbers directly, without using errno.
 * clone's per-task file tables/cwd remain; TLS, cancellation, detached threads
 * and general guest-libc thread safety are not provided by this interface. */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
		   void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);

/* Normal process-private mutexes, initialized with PTHREAD_MUTEX_INITIALIZER.
 * The owner must unlock; recursive locking is not supported. */
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

#endif
