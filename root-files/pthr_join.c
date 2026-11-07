/*
 * Two workers exchange greetings before either can finish.
 * Each sends through its own pipe and waits for the other worker's byte.
 * Serial execution would block the first worker waiting for the second.
 *
 * Linux 0.11: sh build_pthr.sh && ./pthr_join
 * Modern Linux: gcc -pthread pthr_join.c -o pthr_join && ./pthr_join
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

struct worker {
	const char *greeting;
	int send_fd;
	int receive_fd;
};

static void handshake_error(void)
{
	const char message[] = "worker: pipe handshake failed\n";

	write(STDERR_FILENO, message, sizeof(message) - 1);
	_exit(1); /* End the process so the peer is not left waiting. */
}

static void *greet_peer(void *arg)
{
	struct worker *worker = arg;
	char ready = 1;
	int result;

	/* Send first: both workers must be able to reach their receive step. */
	do {
		result = write(worker->send_fd, &ready, 1);
	} while (result < 0 && errno == EINTR);
	if (result != 1)
		handshake_error();

	/* An empty pipe blocks here until the peer has run and sent its byte.
	 * Separate pipes prevent a worker from receiving its own greeting. */
	do {
		result = read(worker->receive_fd, &ready, 1);
	} while (result < 0 && errno == EINTR);
	if (result != 1)
		handshake_error();

	/* Print one complete line without sharing the guest's stdio buffers. */
	write(STDOUT_FILENO, worker->greeting, strlen(worker->greeting));
	return NULL;
}

int main(void)
{
	struct worker workers[2] = {
		{ "I am A\n", 0, 0 },
		{ "I am B\n", 0, 0 }
	};
	pthread_t threads[2];
	int a_to_b[2], b_to_a[2];
	int i, error;

	if (pipe(a_to_b) < 0 || pipe(b_to_a) < 0) {
		perror("pipe");
		return 1;
	}
	/* pipe[0] reads; pipe[1] writes. Each worker receives from its peer. */
	workers[0].send_fd = a_to_b[1];
	workers[0].receive_fd = b_to_a[0];
	workers[1].send_fd = b_to_a[1];
	workers[1].receive_fd = a_to_b[0];

	/* Create both workers before joining either one. */
	for (i = 0; i < 2; i++) {
		error = pthread_create(&threads[i], NULL, greet_peer, &workers[i]);
		if (error) {
			fprintf(stderr, "pthread_create failed: %d\n", error);
			return 1;
		}
	}
	for (i = 0; i < 2; i++) {
		error = pthread_join(threads[i], NULL);
		if (error) {
			fprintf(stderr, "pthread_join failed: %d\n", error);
			return 1;
		}
	}
	/* Keep descriptors open until both workers finish; no read waits for EOF. */
	close(a_to_b[0]); close(a_to_b[1]);
	close(b_to_a[0]); close(b_to_a[1]);
	printf("Both workers started before either finished.\n");
	return 0;
}
