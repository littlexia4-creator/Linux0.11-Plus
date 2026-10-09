/*
 * thr.c -- user-thread test using clone() and gettid() from libsyscall.a.
 *
 * Build and run inside the guest:
 *   sh build_thr.sh
 *   ./a.out
 *   ./a.out 50000     (override iterations per worker)
 * run.sh builds and stages lib/libsyscall.a with MinGW32-i386-linux;
 * Headers are staged in include/; build_thr.sh uses them and links the
 * demo with that archive inside the guest.
 *
 * Two threads share this process's address space. Each worker prints its
 * PID and TID and watches its peer's progress inside the loop. After
 * waiting for both to exit, the parent reports whether interleaving was
 * observed. A worker seeing its peer partway through the loop proves the
 * loops did not run serially.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <futex.h>

/* The guest's installed headers do not declare these syscall extensions. */
extern int gettid(void);
extern volatile void exit_group(int status);

#define DEFAULT_ITERATIONS 40000

static int iterations = DEFAULT_ITERATIONS;

/* Each worker writes its own slot. On i386 these aligned int loads/stores
 * are atomic; volatile makes the peer's progress visible on every read. */
static volatile int progress[2];
static int observed_peer[2];
/* Publish each TID before its thread can run; clear and wake on exit. */
static volatile int thread_tid[2];

static char stack_a[4096];
static char stack_b[4096];

static int thr_fn(void *arg)
{
	const char *name = arg;
	int id = name[0] - 'A';
	int peer_id = 1 - id;
	int i, peer_progress;

	printf("<%s> pid=%d tid=%d\n", name, getpid(), gettid());
	for (i = 0; i < iterations; i++) {
		progress[id] = i + 1;
		peer_progress = progress[peer_id];
		if (peer_progress > 0 && peer_progress < iterations)
			observed_peer[id] = peer_progress;
	}
	return 0;		/* clone() exits this thread with the returned status */
}

static int parse_iterations(const char *arg)
{
	long count;
	char *end;

	errno = 0;
	count = strtol(arg, &end, 10);
	if (errno || end == arg || *end || count < 1 || count > INT_MAX) {
		fprintf(stderr, "iterations must be an integer from 1 to %d\n",
			INT_MAX);
		exit(1);
	}
	return (int) count;
}

static int wait_for_exit(volatile int *tid_addr)
{
	int expected;

	/* When the worker exits, the kernel:
		1. Writes 0 to its TID word.
		2. Wakes the parent waiting on that address */
	while ((expected = *tid_addr) != 0) {
		if (futex(tid_addr, FUTEX_WAIT, expected) < 0 &&
		    errno != EAGAIN && errno != EINTR)
			return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int a, b;
	int flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD |
		CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [iterations]\n", argv[0]);
		exit(1);
	}
	if (argc == 2)
		iterations = parse_iterations(argv[1]);
	printf("parent: pid=%d tid=%d iterations=%d\n",
		getpid(), gettid(), iterations);
	fflush(stdout);
	a = clone(thr_fn, stack_a + sizeof(stack_a), flags, "A",
		(int *)&thread_tid[0], (void *)0, (int *)&thread_tid[0]);
	b = clone(thr_fn, stack_b + sizeof(stack_b), flags, "B",
		(int *)&thread_tid[1], (void *)0, (int *)&thread_tid[1]);
	if (a < 0 || b < 0) {
		perror("clone");
		exit_group(1);
	}
	if (wait_for_exit(&thread_tid[0]) < 0 ||
	    wait_for_exit(&thread_tid[1]) < 0) {
		perror("futex wait");
		exit_group(1);
	}
	printf("interleaving observed: %s\n",
		observed_peer[0] || observed_peer[1] ?
		"YES" : "NO (try more iterations)");
	if (observed_peer[0])
		printf("  A ran while B was at %d/%d iterations\n",
			observed_peer[0], iterations);
	if (observed_peer[1])
		printf("  B ran while A was at %d/%d iterations\n",
			observed_peer[1], iterations);
	return 0;
}
