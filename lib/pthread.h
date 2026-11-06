#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <signal.h>

/* A library handle, independent of the kernel TID cleared at thread exit. */
typedef unsigned long pthread_t;
typedef struct { int __reserved; } pthread_attr_t;

/* Level3 subset: default joinable threads only (attr must be NULL).
 * The library owns up to 64 stacks of 16 KiB, retained after exit.
 * Creation returns error numbers directly, without using errno.
 * clone's per-task file tables/cwd remain; TLS, cancellation, detached threads
 * and general guest-libc thread safety are not provided by this interface. */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
		   void *(*start_routine)(void *), void *arg);

#endif
