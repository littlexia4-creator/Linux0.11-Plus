/*
 * Create a detached worker directly through pthread attributes.
 * Its thread resources are reclaimed automatically after it finishes.
 *
 * Linux 0.11: gcc -O -Iinclude pthr_detach.c -Llib -lpthread -lcsyscalls && ./a.out
 * Modern Linux: gcc -pthread pthr_detach.c -o pthr_detach && ./pthr_detach
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static void check_thread_error(int error, const char *operation)
{
	/* Pthread functions return error numbers directly, not through errno. */
	if (error) {
		fprintf(stderr, "%s failed: %d\n", operation, error);
		exit(EXIT_FAILURE);
	}
}

static void *background_work(void *arg)
{
	printf("worker: finished running with PTHREAD_CREATE_DETACHED; no pthread_join() is needed\n");
	fflush(stdout);
	return NULL;
}

int main(void)
{
	pthread_t worker;
	pthread_attr_t attr;

	check_thread_error(pthread_attr_init(&attr), "pthread_attr_init");
	check_thread_error(pthread_attr_setdetachstate(&attr,
		PTHREAD_CREATE_DETACHED), "pthread_attr_setdetachstate");

	printf("main: creating a detached worker\n");
	fflush(stdout);
	check_thread_error(pthread_create(&worker, &attr, background_work, NULL),
		"pthread_create");
	/* Creation copies the attributes; destroying attr does not stop the worker. */
	check_thread_error(pthread_attr_destroy(&attr), "pthread_attr_destroy");

	/* Keep the process alive by ending only main's thread. Returning from
	 * main would terminate the whole process, including detached workers. */
	pthread_exit(NULL);
	return 0;
}
