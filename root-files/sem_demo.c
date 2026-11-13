/*
 * One producer and two consumers share a single item slot.
 * sem_wait() takes a permit, blocking if none is available.
 * sem_post() adds a permit so the other thread can continue.
 * Consumers compete for five consumption permits, with no fixed quotas.
 * The remaining semaphore lets both finish without extra stop messages.
 *
 * Linux 0.11: sh bld_usrsem.sh && ./a.out
 * Modern Linux: gcc -pthread sem_demo.c -o sem_demo && ./sem_demo
 */
#include <semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define ITEMS 10
#define CONSUMERS 2

static sem_t empty, full, remaining;
static int item;

static void fail(const char *operation)
{
	perror(operation);
	exit(EXIT_FAILURE);
}

static void check_thread(int error, const char *operation)
{
	/* Pthread functions return the error number directly. */
	if (error) {
		fprintf(stderr, "%s: %s\n", operation, strerror(error));
		exit(EXIT_FAILURE);
	}
}

static void *produce(void *unused)
{
	int value;
	for (value = 1; value <= ITEMS; value++) {
		/* The single slot also serializes the workers' stdio calls. */
		if (sem_wait(&empty) < 0)
			fail("sem_wait empty");
		item = value;
		printf("producer: put %d\n", value);
		fflush(stdout);
		if (sem_post(&full) < 0)
			fail("sem_post full");
	}
	return NULL;
}

static void *consume(void *arg)
{
	const char *name = arg;

	for (;;) {
		/* Claim one of the serveral consumptions. No permit means all five
		 * are already claimed; don't block waiting for a sixth item. */
		if (sem_trywait(&remaining) < 0) {
			if (errno == EAGAIN)
				return NULL;
			fail("sem_trywait error");
		}
		/* Wait until the producer has filled the slot. */
		if (sem_wait(&full) < 0)
			fail("sem_wait full");
		printf("consumer %s: got %d\n", name, item);
		fflush(stdout);
		if (sem_post(&empty) < 0)
			fail("sem_post empty");
	}
}

int main(void)
{
	pthread_t producer, consumers[CONSUMERS];
	char *names[CONSUMERS] = { "A", "B" };
	int i;

	/* pshared = 0: share between threads in this process.
	 * Initially the slot is empty (one permit), with no item to consume. */
	if (sem_init(&empty, 0, 1) < 0 || sem_init(&full, 0, 0) < 0 ||
	    sem_init(&remaining, 0, ITEMS) < 0)
		fail("sem_init");
	for (i = 0; i < CONSUMERS; i++)
		check_thread(pthread_create(&consumers[i], NULL, consume, names[i]),
			     "pthread_create");
	check_thread(pthread_create(&producer, NULL, produce, NULL), "pthread_create");
	check_thread(pthread_join(producer, NULL), "pthread_join");
	for (i = 0; i < CONSUMERS; i++)
		check_thread(pthread_join(consumers[i], NULL), "pthread_join");
	/* Destroy only after all threads have finished using the semaphores. */
	if (sem_destroy(&empty) < 0 || sem_destroy(&full) < 0 ||
	    sem_destroy(&remaining) < 0)
		fail("sem_destroy");
	puts("Done: all items consumed.");
	return 0;
}
