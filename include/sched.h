#ifndef _USER_SCHED_H
#define _USER_SCHED_H

#define CLONE_VM       0x00000100 /* share the address space */
#define CLONE_SIGHAND  0x00000800 /* share signal dispositions, not masks */
#define CLONE_THREAD   0x00010000 /* join the caller's thread group */

/* Only CLONE_VM | CLONE_SIGHAND | CLONE_THREAD is supported.
 * Other flags or missing flags fail with EINVAL. fn(arg)'s return value
 * is collected by thread_join(). Rebuild callers of the old three-argument API. */
int clone(int (*fn)(void *), void *stack_top, int flags, void *arg);

#endif
