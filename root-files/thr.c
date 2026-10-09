/*
 * thr.c -- user-thread test using clone() and gettid() from libsyscall.a.
 *
 * Build and run inside the guest:
 *   sh build_thr.sh
 *   ./a.out
 * run.sh builds and stages lib/libsyscall.a with MinGW32-i386-linux;
 * build_thr.sh links the demo with that archive inside the guest.
 *
 * Two threads share this process's address space; both increment the
 * SAME counter.  If the kernel's clone() really shares memory, the
 * final count is 10; with plain fork() each task would count to 5 in
 * its own private copy. All threads print the same PID, with a
 * distinct TID for each. thread_join() collects each thread's exit status.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "sched.h"

/* The guest's installed headers do not declare these syscall extensions. */
extern int gettid(void);
extern int thread_join(int tid, int *status);

static volatile int counter = 0;

static char stack_a[4096];
static char stack_b[4096];

static int thr_fn(void *arg)
{
	int i;

	printf("<%c> pid=%d tid=%d\n", (int) (long) arg,
		getpid(), gettid());
	for (i = 0; i < 5; i++) {
		counter++;
		printf("<%c> counter=%d\n", (int) (long) arg, counter);
	}
	return 0;		/* clone() exits this thread with the returned status */
}

int main(void)
{
	int a, b, st;
	int flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD;

	printf("parent: pid=%d tid=%d\n", getpid(), gettid());
	fflush(stdout);
	a = clone(thr_fn, stack_a + sizeof(stack_a), flags, (void *) (long) 'A');
	b = clone(thr_fn, stack_b + sizeof(stack_b), flags, (void *) (long) 'B');
	printf("parent: A'tid=%d B'tid=%d\n", a, b);
	if (a < 0 || b < 0) {
		perror("clone");
		return 1;
	}
	if (thread_join(a, &st) < 0 || thread_join(b, &st) < 0) {
		perror("thread_join");
		return 1;
	}
	printf("parent: counter=%d (10 means truly shared)\n", counter);
	return 0;
}
