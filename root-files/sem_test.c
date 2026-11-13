/* Linux 0.11: bash build_sem.sh && ./sem_test
 * Modern Linux: gcc -O2 -pthread sem_test.c -o sem_test && ./sem_test
 * The ping-pong test requires both threads to make progress: each iteration
 * waits for a permit that only the peer can supply. */
#include <limits.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#define ROUNDS 1000
#define WORKERS 4

static sem_t turn[2], ready, start, resource;
static int payload, total;
static volatile sig_atomic_t alarm_seen;

static void check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		_exit(1);
	}
}

static void wait_sem(sem_t *sem)
{
	int result;

	do {
		result = sem_wait(sem);
	} while (result < 0 && errno == EINTR);
	check(result == 0, "sem_wait");
}

static void counting_test(void)
{
	sem_t sem;
	int value;

	check(sem_init(&sem, 0, 2) == 0, "initialize two permits");
	check(sem_getvalue(&sem, &value) == 0 && value == 2, "initial count");
	check(sem_trywait(&sem) == 0, "take first permit");
	check(sem_wait(&sem) == 0, "take second permit");
	errno = 0;
	check(sem_trywait(&sem) == -1 && errno == EAGAIN, "empty trywait");
	check(sem_post(&sem) == 0, "return a permit");
	check(sem_getvalue(&sem, &value) == 0 && value == 1, "posted count");
	check(sem_wait(&sem) == 0, "consume posted permit");
	check(sem_destroy(&sem) == 0, "destroy semaphore");

	errno = 0;
	check(sem_init(&sem, 0, (unsigned int)SEM_VALUE_MAX + 1U) == -1 &&
	      errno == EINVAL, "reject initial count overflow");
	check(sem_init(&sem, 0, SEM_VALUE_MAX) == 0, "initialize maximum count");
	errno = 0;
	check(sem_post(&sem) == -1 && errno == EOVERFLOW, "reject post overflow");
	check(sem_getvalue(&sem, &value) == 0 && value == SEM_VALUE_MAX,
	      "overflow leaves count unchanged");
	check(sem_destroy(&sem) == 0, "destroy maximum-count semaphore");
	puts("PASS: counts, trywait and overflow");
}

static void *pass_token(void *arg)
{
	int id = *(int *)arg;
	int i;

	for (i = 0; i < ROUNDS; i++) {
		wait_sem(&turn[id]);
		check(payload == 2 * i + id, "peer's writes visible after wait");
		payload++;
		check(sem_post(&turn[1 - id]) == 0, "wake peer");
	}
	return NULL;
}

static void handoff_test(void)
{
	pthread_t threads[2];
	int ids[2], i;

	for (i = 0; i < 2; i++) {
		ids[i] = i;
		check(sem_init(&turn[i], 0, i == 0) == 0, "initialize turn semaphore");
	}
	for (i = 0; i < 2; i++)
		check(pthread_create(&threads[i], NULL, pass_token, &ids[i]) == 0,
		      "create handoff worker");
	for (i = 0; i < 2; i++)
		check(pthread_join(threads[i], NULL) == 0, "join handoff worker");
	for (i = 0; i < 2; i++)
		check(sem_destroy(&turn[i]) == 0, "destroy turn semaphore");
	check(payload == 2 * ROUNDS, "all handoffs completed");
	puts("PASS: blocking handoff and memory visibility");
}

static void *share_resource(void *unused)
{
	int i;

	(void)unused;
	check(sem_post(&ready) == 0, "announce worker readiness");
	wait_sem(&start);
	for (i = 0; i < ROUNDS; i++) {
		wait_sem(&resource);
		total++;
		check(sem_post(&resource) == 0, "release shared resource");
	}
	return NULL;
}

static void contention_test(void)
{
	pthread_t threads[WORKERS];
	int i;

	check(sem_init(&ready, 0, 0) == 0, "initialize readiness");
	check(sem_init(&start, 0, 0) == 0, "initialize start gate");
	check(sem_init(&resource, 0, 1) == 0, "initialize shared resource");
	for (i = 0; i < WORKERS; i++)
		check(pthread_create(&threads[i], NULL, share_resource, NULL) == 0,
		      "create resource worker");
	for (i = 0; i < WORKERS; i++)
		wait_sem(&ready);
	for (i = 0; i < WORKERS; i++)
		check(sem_post(&start) == 0, "release one waiting worker");
	for (i = 0; i < WORKERS; i++)
		check(pthread_join(threads[i], NULL) == 0, "join resource worker");
	check(total == WORKERS * ROUNDS, "mutual exclusion with several waiters");
	check(sem_destroy(&ready) == 0, "destroy readiness");
	check(sem_destroy(&start) == 0, "destroy start gate");
	check(sem_destroy(&resource) == 0, "destroy shared resource");
	puts("PASS: multiple waiters and shared resource");
}

static void alarm_handler(int sig)
{
	(void)sig;
	alarm_seen = 1;
}

static void interruption_test(void)
{
	struct sigaction action, old_action;
	sem_t sem;
	int result, error;

	memset(&action, 0, sizeof(action));
	action.sa_handler = alarm_handler;
	check(sigemptyset(&action.sa_mask) == 0, "initialize handler mask");
	check(sigaction(SIGALRM, &action, &old_action) == 0, "install alarm handler");
	check(sem_init(&sem, 0, 0) == 0, "initialize interrupted semaphore");
	alarm(1);
	result = sem_wait(&sem);
	error = errno;
	alarm(0);
	check(result == -1 && error == EINTR && alarm_seen,
	      "signal interrupts an empty semaphore wait");
	check(sem_post(&sem) == 0 && sem_wait(&sem) == 0,
	      "semaphore remains usable after interruption");
	check(sem_destroy(&sem) == 0, "destroy interrupted semaphore");
	check(sigaction(SIGALRM, &old_action, NULL) == 0, "restore alarm handler");
	puts("PASS: interrupted wait");
}

int main(void)
{
	/* A lost wakeup must fail rather than hang indefinitely. The last test
	 * replaces this timeout with its own one-second alarm. */
	alarm(30);
	counting_test();
	handoff_test();
	contention_test();
	interruption_test();
	puts("All semaphore tests passed.");
	return 0;
}
