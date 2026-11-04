/* Signal-set operations for the guest's 32-bit sigset_t. */
#include <signal.h>
#include <errno.h>

int sigemptyset(sigset_t *set)
{
	*set = 0;
	return 0;
}

int sigaddset(sigset_t *set, int signo)
{
	if (signo < 1 || signo >= _NSIG) {
		errno = EINVAL;
		return -1;
	}
	*set |= 1U << (signo - 1);
	return 0;
}

int sigismember(const sigset_t *set, int signo)
{
	if (signo < 1 || signo >= _NSIG) {
		errno = EINVAL;
		return -1;
	}
	return (*set & (1U << (signo - 1))) != 0;
}
