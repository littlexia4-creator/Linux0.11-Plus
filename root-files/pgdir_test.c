/* Exercise process and thread address-space lifetimes in the guest. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

static volatile int shared_value = 7;

static void check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		exit(1);
	}
}

static void *thread_write(void *unused)
{
	(void)unused;
	shared_value = 99;
	return NULL;
}

int main(void)
{
	pthread_t thread;
	int pipefd[2], pid, status, i;
	char byte;

	check(pthread_create(&thread, NULL, thread_write, NULL) == 0,
	      "pthread_create");
	check(pthread_join(thread, NULL) == 0, "pthread_join");
	check(shared_value == 99, "threads share memory");
	check(pipe(pipefd) == 0, "pipe");
	pid = fork();
	check(pid >= 0, "fork");
	if (!pid) {
		close(pipefd[0]);
		if (shared_value != 99)
			_exit(1);
		shared_value = 13;
		byte = 1;
		if (write(pipefd[1], &byte, 1) != 1)
			_exit(2);
		_exit(0);
	}
	close(pipefd[1]);
	check(read(pipefd[0], &byte, 1) == 1, "child write handshake");
	close(pipefd[0]);
	check(shared_value == 99, "fork preserves private memory");
	check(waitpid(pid, &status, 0) == pid && status == 0, "child exit");
	for (i=0; i<128; i++) {
		pid = fork();
		check(pid >= 0, "repeated fork");
		if (!pid)
			_exit(0);
		check(waitpid(pid, &status, 0) == pid && status == 0,
		      "repeated waitpid");
	}
	puts("PASS: process isolation, thread sharing and repeated fork");
	return 0;
}
