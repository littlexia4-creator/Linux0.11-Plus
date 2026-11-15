#ifndef DEMO_FUTEX_WAIT_H
#define DEMO_FUTEX_WAIT_H

#include <errno.h>
#include <stddef.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>

static long demo_futex_op(volatile int *tid_addr, int op, int expected)
{
	return syscall(SYS_futex, tid_addr, op, expected,
		       NULL, NULL, 0);
}

static long demo_futex_wait(volatile int *tid_addr, int expected)
{
	return demo_futex_op(tid_addr, FUTEX_WAIT, expected);
}

static long demo_futex_wake(volatile int *tid_addr, int expected)
{
	return demo_futex_op(tid_addr, FUTEX_WAKE, expected);
}

static int wait_for_tid_exit(volatile int *tid_addr)
{
	int expected;

	/* CLONE_CHILD_CLEARTID clears this word and wakes waiters on exit. */
	while ((expected = *tid_addr) != 0) {
		if (demo_futex_wait(tid_addr, expected) < 0 &&
		    errno != EAGAIN && errno != EINTR)
			return -1;
	}
	return 0;
}

#endif
