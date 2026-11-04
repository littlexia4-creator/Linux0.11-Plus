/* pthread errors are returned directly, without changing errno. */
#include <signal.h>
#include "bits/sigthread.h"
#include <errno.h>
#include <sys/syscall.h>

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset)
{
	int saved_errno = errno;
	int error;
	long result;
	sigset_t copied_set;

	/* Snapshot the input in user space before entering the kernel. */
	if (set) {
		copied_set = *set;
		set = &copied_set;
	}
	result = syscall(SYS_sigprocmask, how, set, oldset);
	error = result < 0 ? errno : 0;
	errno = saved_errno;
	return error;
}
