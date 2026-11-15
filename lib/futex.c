#include "futex.h"
#include <linux/futex.h>
#include <sys/syscall.h>
#include "sysdep.h"

int futex_wait(volatile int *word, int expected)
{
	return __syscall3(SYS_futex, (long)word, FUTEX_WAIT, expected);
}

int futex_wake(volatile int *word, int count)
{
	return __syscall3(SYS_futex, (long)word, FUTEX_WAKE, count);
}
