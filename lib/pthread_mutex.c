#include "pthread.h"
#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include "atomic.h"
#include "sysdep.h"

/* 0: unlocked; 1: locked; 2: locked with possible futex waiters.
 * State 2 is a wakeup obligation, not a count of sleeping threads. */
int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	int result;

	if (!mutex)
		return EINVAL;
	if (!atomic_exchange_acquire(&mutex->__lock, 1))
		return 0;
	/* Contenders acquire in state 2 so unlocking passes the wakeup on.
	 * FUTEX_WAIT compares against 2 before sleeping, avoiding a lost wake
	 * if the owner unlocks between this exchange and the syscall. */
	while (atomic_exchange_acquire(&mutex->__lock, 2)) {
		result = __syscall3(SYS_futex, (long)&mutex->__lock, FUTEX_WAIT, 2);
		if (result < 0 && result != -EAGAIN && result != -EINTR)
			return -result;
	}
	return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	int previous, result;

	if (!mutex)
		return EINVAL;
	previous = atomic_exchange_release(&mutex->__lock, 0);
	/* An uncontended lock/unlock pair needs no kernel call. */
	if (previous != 2)
		return 0;
	result = __syscall3(SYS_futex, (long)&mutex->__lock, FUTEX_WAKE, 1);
	return result < 0 ? -result : 0;
}
