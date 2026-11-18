/* One producer and two competing consumers use kernel-owned semaphores.
 * The pipe carries item bytes: fork() does not share ordinary variables.
 * Linux 0.11: sh bld_syssem.sh; ./a.out
 * Modern Linux: gcc sysv_sem.c -o sysv_sem; ./sysv_sem */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/sem.h>
#include <sys/wait.h>

#define ITEMS 10
#define CONSUMERS 2

enum { EMPTY, FULL, REMAINING };

union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
};

static int semid = -1;

static void destroy_semaphores(void)
{
	if (semid < 0)
		return;
	/* Removal also releases a peer blocked in semop(), with EIDRM. */
	if (semctl(semid, 0, IPC_RMID) < 0) {
		perror("IPC_RMID");
		exit(1);
	}
	semid = -1;
}

static void fail(const char *message)
{
	perror(message);
	destroy_semaphores();
	exit(1);
}

static int operate(unsigned short number, short change, short flags)
{
	struct sembuf op;
	int result;

	op.sem_num = number;
	op.sem_op = change;
	op.sem_flg = flags;
	do {
		result = semop(semid, &op, 1);
	} while (result < 0 && errno == EINTR);
	return result;
}

static void P(unsigned short number)
{
	if (operate(number, -1, 0) < 0)
		fail("P");
}

static void V(unsigned short number)
{
	if (operate(number, 1, 0) < 0)
		fail("V");
}

static int claim_item(void)
{
	/* Consumers compete for claims rather than receiving fixed quotas.
	 * After all claims are taken, exit instead of waiting for another item. */
	if (operate(REMAINING, -1, IPC_NOWAIT) == 0)
		return 1;
	if (errno != EAGAIN)
		fail("claim item");
	return 0;
}

static void write_item(int fd, int value)
{
	char item = value;
	int written;

	do {
		written = write(fd, &item, 1);
	} while (written < 0 && errno == EINTR);
	if (written != 1) {
		if (written == 0)
			errno = EIO;
		fail("write item");
	}
}

static int read_item(int fd)
{
	char item;
	int received;

	do {
		received = read(fd, &item, 1);
	} while (received < 0 && errno == EINTR);
	if (received != 1) {
		if (received == 0)
			errno = EIO;
		fail("read item");
	}
	return (unsigned char)item;
}

static void produce(int fd)
{
	int value;

	for (value=1; value<=ITEMS; value++) {
		P(EMPTY); /* Wait for the single logical slot to become empty. */
		write_item(fd, value);
		printf("producer: put %d\n", value);
		fflush(stdout);
		V(FULL);  /* Publish one item to either waiting consumer. */
	}
}

static void consume(const char *name, int fd)
{
	int item;

	while (claim_item()) {
		P(FULL);
		item = read_item(fd);
		printf("consumer %s: got %d\n", name, item);
		fflush(stdout);
		V(EMPTY); /* Let the producer reuse the slot after reading/printing. */
	}
}

static void create_semaphores(void)
{
	/* One empty slot, no ready items, and ITEMS total consumption claims. */
	unsigned short values[3] = {1, 0, ITEMS};
	union semun arg;

	semid = semget(IPC_PRIVATE, 3, 0600);
	if (semid < 0)
		fail("semget");
	arg.array = values;
	if (semctl(semid, 0, SETALL, arg) < 0)
		fail("SETALL");
}

static void wait_for_consumer(int child)
{
	int status, waited;

	do {
		waited = waitpid(child, &status, 0);
	} while (waited < 0 && errno == EINTR);
	if (waited < 0)
		fail("waitpid");
	if (status) {
		errno = EIO;
		fail("consumer failed");
	}
}

int main(void)
{
	const char *names[CONSUMERS] = {"A", "B"};
	int data_pipe[2], children[CONSUMERS], i;

	create_semaphores();
	if (pipe(data_pipe) < 0)
		fail("pipe");
	for (i=0; i<CONSUMERS; i++) {
		children[i] = fork();
		if (children[i] < 0)
			fail("fork");
		if (!children[i]) {
			close(data_pipe[1]); /* Consumers only read. */
			consume(names[i], data_pipe[0]);
			close(data_pipe[0]);
			return 0;
		}
	}
	close(data_pipe[0]); /* The parent is the only producer. */
	produce(data_pipe[1]);
	close(data_pipe[1]);
	for (i=0; i<CONSUMERS; i++)
		wait_for_consumer(children[i]);
	destroy_semaphores();
	printf("Done: %d items produced and consumed; semaphore set removed.\n", ITEMS);
	return 0;
}
