/*
 * Create joinable workers, then detach them with pthread_detach().
 * Like thr.c, each worker publishes its progress during a long ++i loop
 * and checks whether its peer is partway through its loop. An observation
 * shows that the loops interleaved. No observation is inconclusive: try
 * more iterations. Detaching changes cleanup, not how threads are scheduled.
 * A mutex protects progress; release it after each update and peer read,
 * so the two loops can interleave instead of locking an entire loop.
 *
 * Modern Linux: gcc -pthread detach_demo.c -o detach_demo && ./detach_demo 10000000
 * Linux 0.11: bash build_det.sh
 * ./detach_demo [iterations]    (default: 1000000 per worker)
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#define DEFAULT_ITERATIONS 1000000

/* Shared state and string-literal arguments outlive main's pthread_exit(). */
static int iterations = DEFAULT_ITERATIONS;
static int progress[2];
static pthread_mutex_t progress_mutex = PTHREAD_MUTEX_INITIALIZER;

static void safe_print(int fd, const char *message)
{
	int length = strlen(message);
	int result;

	/* One line per write, without sharing the guest's stdio buffers. */
	do {
		result = write(fd, message, length);
	} while (result < 0 && errno == EINTR);
	if (result != length)
		_exit(1);
}

static void check_thread_error(int error, const char *operation)
{
	/* Pthread functions return an error number directly. */
	if (error) {
		fprintf(stderr, "%s failed: %d\n", operation, error);
		exit(EXIT_FAILURE);
	}
}

static int update_progress(int id, int value)
{
	int peer_progress;

	check_thread_error(pthread_mutex_lock(&progress_mutex), "pthread_mutex_lock");
	progress[id] = value;
	peer_progress = progress[1 - id];
	check_thread_error(pthread_mutex_unlock(&progress_mutex), "pthread_mutex_unlock");
	return peer_progress;
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
		exit(EXIT_FAILURE);
	}
	return (int)count;
}

static void *count_iterations(void *arg)
{
	const char *name = arg;
	int id = name[0] - 'A';
	int i, peer_progress, observed_peer = 0;
	char message[160];

	for (i = 0; i < iterations; ++i) {
		/* Each worker increments its own i and publishes its completed work. */
		peer_progress = update_progress(id, i + 1);
		if (peer_progress > 0 && peer_progress < iterations)
			observed_peer = peer_progress;
	}
	if (observed_peer)
		sprintf(message, "%s: observed peer at %d/%d iterations; loops interleaved\n",
			name, observed_peer, iterations);
	else
		sprintf(message, "%s: no partial peer progress observed; try more iterations\n",
			name);
	safe_print(STDOUT_FILENO, message);
	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t thread;
	int i;
	char message[128];

	if (argc > 2) {
		fprintf(stderr, "usage: %s [iterations]\n", argv[0]);
		return 1;
	}
	if (argc == 2)
		iterations = parse_iterations(argv[1]);
	sprintf(message, "main: %d iterations per worker; mutex + pthread_detach()\n",
		iterations);
	safe_print(STDOUT_FILENO, message);
	for (i = 0; i < 2; i++) {
		check_thread_error(pthread_create(&thread, NULL, count_iterations, i ? "B" : "A"),
			"pthread_create");
		check_thread_error(pthread_detach(thread), "pthread_detach");
	}
	/* No joining: end only main's thread so the detached workers can finish.
	 * Returning from main would terminate the whole process. */
	pthread_exit(NULL);
	return 0;
}
