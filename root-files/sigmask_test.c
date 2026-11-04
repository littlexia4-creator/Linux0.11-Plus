/* Signal-mask regression tests for the guest and modern Linux.
 * gcc -O -Iinclude sigmask_test.c -Llib -lcsyscalls -o sigmask_test
 * Modern Linux: gcc -D_GNU_SOURCE -O sigmask_test.c -pthread -o sigmask_test
 * ./sigmask_test
 */
/* Guest: /usr/include */
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* Guest: /usr/local/include/unistd.h cover the one in /usr/include/ */
#include <unistd.h>

/* Guest: /usr/local/include/signal.h */
#include <signal.h>

/* Both headers define _NSIG as the highest signal number plus one. */
#define LAST_SIGNAL (_NSIG - 1)

static void check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		_exit(1);
	}
}

/* Compare membership instead of assuming sigset_t is an integer or has no padding. */
static int same_set(const sigset_t *left, const sigset_t *right)
{
	int sig, left_member, right_member;

	for (sig = 1; sig <= LAST_SIGNAL; sig++) {
		left_member = sigismember(left, sig);
		right_member = sigismember(right, sig);
		check(left_member >= 0 && right_member >= 0, "compare signal sets");
		if (left_member != right_member)
			return 0;
	}
	return 1;
}

static sigset_t one_signal(int sig)
{
	sigset_t set;

	check(sigemptyset(&set) == 0 && sigaddset(&set, sig) == 0,
	      "construct signal set");
	return set;
}

static sigset_t current_mask(void)
{
	sigset_t set;

	/* how is ignored when set is NULL. */
	check(pthread_sigmask(-1, 0, &set) == 0, "query thread mask");
	return set;
}

static int mask_matches(const sigset_t *expected)
{
	sigset_t actual = current_mask();

	return same_set(&actual, expected);
}

static void signal_sets_test(void)
{
	sigset_t set, saved;
	const sigset_t *view = &set;

	errno = 123;
	check(sigemptyset(&set) == 0 && sigismember(view, SIGUSR1) == 0,
	      "empty set excludes SIGUSR1");
	check(sigaddset(&set, SIGUSR1) == 0 && sigaddset(&set, SIGUSR1) == 0 &&
	      sigismember(view, SIGUSR1) == 1, "adding a signal is idempotent");
	check(sigaddset(&set, LAST_SIGNAL) == 0 && sigismember(view, LAST_SIGNAL) == 1,
	      "highest supported signal is representable");
	check(errno == 123, "successful set operations preserve errno");
	saved = set;
	errno = 0;
	check(sigaddset(&set, 0) == -1 && errno == EINVAL && same_set(&set, &saved),
	      "invalid addition leaves the set unchanged");
	errno = 0;
	check(sigaddset(&set, LAST_SIGNAL + 1) == -1 && errno == EINVAL &&
	      same_set(&set, &saved), "signal beyond the supported range is rejected");
	errno = 0;
	check(sigismember(view, LAST_SIGNAL + 1) == -1 && errno == EINVAL,
	      "invalid membership reports EINVAL");
	errno = 0;
	check(sigaddset(&set, -1) == -1 && errno == EINVAL && same_set(&set, &saved),
	      "negative signal number is rejected");
	printf("PASS signal sets, boundaries and errno\n");
}

static void mask_operations_test(void)
{
	sigset_t original = current_mask();
	sigset_t usr1 = one_signal(SIGUSR1);
	sigset_t usr2 = one_signal(SIGUSR2);
	sigset_t both = usr1;
	sigset_t empty, old, alias;

	check(sigemptyset(&empty) == 0 && sigaddset(&both, SIGUSR2) == 0,
	      "construct expected masks");
	errno = 123;
	check(pthread_sigmask(SIG_SETMASK, &empty, 0) == 0 && errno == 123,
	      "replace mask preserves errno");
	check(pthread_sigmask(SIG_BLOCK, &usr1, &old) == 0 && same_set(&old, &empty),
	      "block returns the previous mask");
	check(pthread_sigmask(SIG_BLOCK, &usr2, &old) == 0 && same_set(&old, &usr1) &&
	      mask_matches(&both), "block adds to the existing mask");
	check(pthread_sigmask(SIG_UNBLOCK, &usr1, &old) == 0 &&
	      same_set(&old, &both) && mask_matches(&usr2),
	      "unblock removes only the requested signal");
	alias = usr1;
	check(pthread_sigmask(SIG_SETMASK, &alias, &alias) == 0 &&
	      same_set(&alias, &usr2) && mask_matches(&usr1),
	      "input and output may alias");
	alias = one_signal(LAST_SIGNAL);
	check(pthread_sigmask(SIG_SETMASK, &alias, 0) == 0 &&
	      pthread_sigmask(SIG_SETMASK, &empty, &old) == 0 && same_set(&old, &alias),
	      "highest signal mask survives a round trip");
	old = usr1;
	check(sigaddset(&old, SIGKILL) == 0 && sigaddset(&old, SIGSTOP) == 0,
	      "construct mask containing unblockable signals");
	check(pthread_sigmask(SIG_SETMASK, &old, 0) == 0 && mask_matches(&usr1),
	      "SIGKILL and SIGSTOP stay unblocked");
	old = usr2;
	errno = 123;
	check(pthread_sigmask(-1, &usr2, &old) == EINVAL &&
	      errno == 123 && same_set(&old, &usr2) && mask_matches(&usr1),
	      "pthread errors preserve errno, mask and output");
	/* All-bits-one is outside the user address range on both systems. */
	check(pthread_sigmask(SIG_SETMASK, &usr2, (sigset_t *)-1UL) == EFAULT &&
	      errno == 123 && mask_matches(&usr2), "output fault leaves the new mask applied");
	check(pthread_sigmask(-1, 0, 0) == 0 && errno == 123, "query with no output is valid");
	check(pthread_sigmask(SIG_SETMASK, &original, 0) == 0, "restore original mask");
	printf("PASS mask operations, aliasing, unblockable signals and errors\n");
}

static volatile sig_atomic_t delivered;

static void handler(int sig)
{
	delivered++;
}

static void pending_signal_test(void)
{
	struct sigaction action, previous;
	sigset_t original = current_mask();
	sigset_t blocked = one_signal(SIGUSR1);

	memset(&action, 0, sizeof(action));
	action.sa_handler = handler;
	sigemptyset(&action.sa_mask);
	check(sigaction(SIGUSR1, &action, &previous) == 0, "install signal handler");
	check(pthread_sigmask(SIG_SETMASK, &blocked, 0) == 0, "block pending signal");
	check(kill(getpid(), SIGUSR1) == 0 && delivered == 0, "signal stays pending while blocked");
	check(pthread_sigmask(SIG_UNBLOCK, &blocked, 0) == 0 && delivered == 1,
	      "pending signal is delivered when unblocked");
	check(sigaction(SIGUSR1, &previous, 0) == 0, "restore signal handler");
	check(pthread_sigmask(SIG_SETMASK, &original, 0) == 0, "restore signal mask");
	printf("PASS pending signal delivery on unblock\n");
}

int main(void)
{
	signal_sets_test();
	mask_operations_test();
	pending_signal_test();
	printf("ALL SIGNAL MASK TESTS PASSED\n");
	return 0;
}
