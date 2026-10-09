#ifndef _USER_SCHED_H
#define _USER_SCHED_H

#define CLONE_VM       0x00000100 /* share the address space */
#define CLONE_SIGHAND  0x00000800 /* share signal dispositions, not masks */
#define CLONE_THREAD   0x00010000 /* join the caller's thread group */

/* Only CLONE_VM | CLONE_SIGHAND | CLONE_THREAD is supported.
 * Other flags or missing flags fail with EINVAL. The wrapper passes fn(arg)'s
 * return value to _exit(); result retention and joining belong in user space. */
int clone(int (*fn)(void *), void *stack_top, int flags, void *arg);

#endif
