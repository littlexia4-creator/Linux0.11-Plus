#ifndef _LIB_FUTEX_H
#define _LIB_FUTEX_H

/* Return raw syscall results or negative errors without changing errno.
 * Callers must recheck their condition after a wakeup or -EAGAIN. */
int futex_wait(volatile int *word, int expected);
int futex_wake(volatile int *word, int count);

#endif
