#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <signal.h>

/* A library handle, independent of the kernel TID cleared at thread exit. */
typedef unsigned long pthread_t;
typedef struct { unsigned long __reserved; } pthread_attr_t;

/* Level2.5 subset: default joinable threads only (attr must be NULL).
 * The library owns up to 64 stacks of 16 KiB, retained until joining.
 * Creation and joining return error numbers directly, without using errno.
 * clone's per-task file tables/cwd remain; TLS, cancellation, cleanup handlers,
 * detached threads and general guest-libc thread safety are not provided. */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
		   void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
/* Does not return. Retain retval for joining; other threads keep running. */
void pthread_exit(void *retval);

#endif
