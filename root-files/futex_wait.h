#ifndef DEMO_FUTEX_WAIT_H
#define DEMO_FUTEX_WAIT_H

#include <errno.h>
#include <stddef.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>

static int __futex_wait(volatile int *tid_addr, int expected)
{
	return syscall(SYS_futex, tid_addr, FUTEX_WAIT, expected,
		       NULL, NULL, 0);
}

static int wait_for_tid_exit(volatile int *tid_addr)
{
	int expected;

	/* CLONE_CHILD_CLEARTID clears this word and wakes waiters on exit. */
	while ((expected = *tid_addr) != 0) {
		if (__futex_wait(tid_addr, expected) < 0 &&
		    errno != EAGAIN && errno != EINTR)
			return -1;
	}
	return 0;
}

#endif
