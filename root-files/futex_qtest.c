/* Exercise private futex queue ordering and hash collisions in Linux 0.11.

futex_qtest.c checks that the futex implementation puts threads to sleep and wakes the right ones.

It creates three waiting threads:
words[0]  → waiter 0 → waiter 1
words[8]  → waiter

  * Build in the guest with: sh build_fq.sh && ./a.out */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include "futex_wait.h"

/* The guest is single-CPU; volatile keeps these progress checks visible. */
static volatile int words[9];
static volatile int phase[3];
static volatile int batch_phase[2];
static volatile int requeue;
static volatile int worker_error;

static void fail(const char *message)
{
	fprintf(stderr, "FAIL: %s\n", message);
	exit(1);
}

/* settle() gives a worker thread time to enter the futex wait queue before the main thread tries to wake it */
static void settle(void)
{
	int i;

	for (i=0; i<100; i++)
		sched_yield();
}

static void wait_phase(volatile int *progress, int wanted)
{
	int i;

	for (i=0; i<10000 && *progress < wanted; i++)
		sched_yield();
	if (*progress < wanted || worker_error)
		fail("worker did not reach expected phase");
}

static void *waiter_fun(void *argument)
{
	int id = *(int *)argument;
	volatile int *word = id == 2 ? &words[8] : &words[0];

	phase[id] = 1;
	if (demo_futex_wait(word, 0) != 0) {
		worker_error = 1;
		return NULL;
	}
	phase[id] = 2;
	if (id == 0) {
		while (!requeue)
			sched_yield();
		phase[id] = 3;
		if (demo_futex_wait(word, 0) != 0)
			worker_error = 1;
		phase[id] = 4;
	}
	return NULL;
}

static void *batch_waiter(void *argument)
{
	int id = *(int *)argument;

	batch_phase[id] = 1;
	if (demo_futex_wait(&words[0], 0) != 0)
		worker_error = 1;
	batch_phase[id] = 2;
	return NULL;
}

int main(void)
{
	pthread_t threads[3];
	int ids[3] = { 0, 1, 2 };
	int i;

	errno = 0;
	if (demo_futex_wait(&words[0], 1) != -1 || errno != EAGAIN)
		fail("mismatched value must return EAGAIN");
	if (demo_futex_wake(&words[0], 0) != 0)
		fail("wake count zero");
	if (demo_futex_wake(&words[0], -1) != 0)
		fail("negative wake count with no waiters");
	for (i=0; i<3; i++) {
		if (pthread_create(&threads[i], NULL, waiter_fun, &ids[i]))
			fail("pthread_create");
		wait_phase(&phase[i], 1);
		settle();
	}
	/* words[0] and words[8] hash to the same bucket but are different keys. */
	if (demo_futex_wake(&words[8], 1) != 1)
		fail("wake colliding address");
	wait_phase(&phase[2], 2);
	if (phase[0] != 1 || phase[1] != 1)
		fail("wake must not affect another address");
	if (demo_futex_wake(&words[0], 0) != 1)
		fail("wake zero must wake one queued waiter");
	wait_phase(&phase[0], 2);
	requeue = 1;
	wait_phase(&phase[0], 3);
	settle();
	if (demo_futex_wake(&words[0], -1) != 1)
		fail("negative wake count must wake one queued waiter");
	wait_phase(&phase[1], 2);
	if (phase[0] != 3)
		fail("requeued waiter woke before older waiter");
	if (demo_futex_wake(&words[0], 1) != 1)
		fail("wake requeued waiter");
	wait_phase(&phase[0], 4);
	if (demo_futex_wake(&words[0], 1) != 0)
		fail("queue must be empty");
	for (i=0; i<3; i++)
		if (pthread_join(threads[i], NULL))
			fail("pthread_join");
	for (i=0; i<2; i++) {
		if (pthread_create(&threads[i], NULL, batch_waiter, &ids[i]))
			fail("batch pthread_create");
		wait_phase(&batch_phase[i], 1);
		settle();
	}
	if (demo_futex_wake(&words[0], 2) != 2)
		fail("wake multiple waiters");
	for (i=0; i<2; i++) {
		wait_phase(&batch_phase[i], 2);
		if (pthread_join(threads[i], NULL))
			fail("batch pthread_join");
	}
	if (demo_futex_wake(&words[0], 1) != 0)
		fail("batch queue must be empty");
	puts("PASS: futex queue ordering and key isolation");
	return 0;
}
