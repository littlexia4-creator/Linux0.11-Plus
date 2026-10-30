#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_WAIT_PRIVATE (FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_PRIVATE (FUTEX_WAKE | FUTEX_PRIVATE_FLAG)

/* Minimal subset: aligned 32-bit words within one address space, no timeout.
 * WAIT/WAKE and their PRIVATE forms all use the caller's thread group. */
int futex(volatile int *uaddr, int op, int value);

#endif
