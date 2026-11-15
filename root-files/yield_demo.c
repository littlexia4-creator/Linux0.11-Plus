/*
 * Two user-space threads voluntarily yield after each step.
 * Linux 0.11: sh build_yield.sh && ./a.out
 * Modern Linux: gcc -pthread yield_demo.c -o yield_demo && ./yield_demo
 *
 * sched_yield() leaves the caller runnable. It does not guarantee alternating
 * output or replace synchronization: either worker may run again immediately.
 */
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

static void *yield_worker(void *arg)
{
	const char *name = arg;
	char message[] = "?: step 1, calling sched_yield()\n";
	int i;

	message[0] = name[0];
	for (i = 0; i < 5; i++) {
		message[8] = '1' + i;
		/* Each worker has its own buffer; avoid shared stdio buffering. */
		if (write(STDOUT_FILENO, message, sizeof(message) - 1) !=
		    sizeof(message) - 1 || sched_yield() != 0)
			return arg;
	}
	return NULL;
}

int main(void)
{
	pthread_t threads[2];
	char *names[2] = { "A", "B" };
	void *result;
	int i, error;

	puts("Each worker yields 5 times; output order is scheduler-dependent.");
	fflush(stdout);
	for (i = 0; i < 2; i++) {
		error = pthread_create(&threads[i], NULL, yield_worker, names[i]);
		if (error) {
			fprintf(stderr, "pthread_create failed: %d\n", error);
			return 1;
		}
	}
	for (i = 0; i < 2; i++) {
		error = pthread_join(threads[i], &result);
		if (error || result) {
			fprintf(stderr, "worker %s failed (join error: %d)\n",
				names[i], error);
			return 1;
		}
	}
	puts("Both workers completed their sched_yield() calls.");
	return 0;
}
