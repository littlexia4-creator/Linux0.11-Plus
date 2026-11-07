/*
 * Two uses of pthread_exit(): stop a worker from a nested helper, and let
 * main finish while another thread continues creating and joining workers.
 *
 * Linux 0.11: sh build_pexit.sh && ./pthr_exit
 * Modern Linux: gcc -pthread pthr_exit.c -o pthr_exit && ./pthr_exit
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

/* Keep the result alive after the worker's stack is released by joining. */

static void safe_print(const char *message)
{
	/* Avoid sharing the guest libc's stdio buffers between threads. */
	write(STDOUT_FILENO, message, strlen(message));
}

static void thread_error(const char *operation, int error)
{
	fprintf(stderr, "%s failed: %d\n", operation, error);
	_exit(1);
}

static void *handle_request(void *arg)
{
	static char msg[] = "request rejected";

	safe_print("worker: validator calls pthread_exit(msg)\n");
	pthread_exit(msg);
	assert((0) && "unreachable: pthread_exit() does not return");
	return NULL;
}

static void *superviser(void *arg)
{
	pthread_t worker;
	void *result;
	int error;

	(void)arg;
	error = pthread_create(&worker, NULL, handle_request, NULL);
	if (error)
		thread_error("pthread_create worker", error);
	error = pthread_join(worker, &result);
	if (error)
		thread_error("pthread_join worker", error);
	safe_print("supervisor: joined worker; result = ");
	safe_print(result);
	safe_print("\nsupervisor: finished; returning normally\n");
	return NULL;
}

int main(void)
{
	pthread_t supervisor;
	int error;

	printf("main: starting a supervisor, then calling pthread_exit(NULL)\n");
	fflush(stdout); /* pthread_exit() does not flush stdio buffers. */
	error = pthread_create(&supervisor, NULL, superviser, NULL);
	if (error) {
		thread_error("pthread_create supervisor", error);
		return 1;
	}
	/* Returning from main would terminate the process and its workers.
	 * Only this thread ends here; the process lives until superviser returns */
	pthread_exit(NULL);
	assert((0) && "unreachable: pthread_exit() does not return");
	return 0;
}
