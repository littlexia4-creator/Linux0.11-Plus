#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <signal.h>

/* A library handle, independent of the kernel TID cleared at thread exit. */
typedef unsigned long pthread_t;
typedef struct { int __detachstate; } pthread_attr_t;
typedef struct { volatile int __lock; } pthread_mutex_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_MUTEX_INITIALIZER { 0 }

/* Level3 subset: default stacks with joinable or detached creation.
 * NULL attributes select joinable threads. The library owns 64 stacks of
 * 16 KiB: joinable slots are retained until joining, while finished detached
 * slots are reclaimed during subsequent creation. Functions returning int
 * return error numbers directly, without using errno.
 * clone's per-task file tables/cwd remain; TLS, cancellation, cleanup handlers,
 * pthread_detach and general guest-libc thread safety are not provided. */
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
		   void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);

/* Normal process-private mutexes, initialized with PTHREAD_MUTEX_INITIALIZER.
 * The owner must unlock; recursive locking is not supported. */
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

#endif
