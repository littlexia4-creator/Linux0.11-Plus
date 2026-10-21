/* Linux 0.11 guest regression test for the fixed clone thread-group policy.
 * gcc -O -Iinclude -o clone_test clone_test.c -Llib -lcsyscalls
 * Run from /usr/root: ./clone_test
 * Callback results stay in user space; exit waits use CLONE_CHILD_CLEARTID
 * and futex instead of a kernel thread-join syscall.
 */
/* Test operations inside assertions must never be compiled out. */
#ifdef NDEBUG
#error "clone_test requires assertions enabled"
#endif
/* Use the system header: GCC's private assert.h only prints on failure. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sched.h>
#include <linux/futex.h>
#include <sys/syscall.h>


/* Each record owns its stack and result until join observes tid == 0. */
struct thread {
	volatile int tid, result;
	int const_thread_id;
	int (*fn)(void *);
	void *arg;
	char stack[4096];
};
static volatile int ready, go;
static int process_id, parent_id;
static int wake_pipe[2], ready_pipe[2], leader_pipe[2];

/* Shared test helpers. */
static long ticks(void)
{
	struct tms usage;
	return times(&usage);
}

static void until(volatile int *value)
{
	long start = ticks();
	while (!*value && ticks() - start < 500)
		;
	if (!*value) {
		fprintf(stderr, "thread handshake timed out\n");
		_exit(1);
	}
}

static int thread_start(void *arg)
{
	struct thread *thread = arg;

	assert((thread->tid == gettid()) && "TID published before child starts");
	thread->result = thread->fn(thread->arg);
	return thread->result;
}

static struct thread *spawn(int (*fn)(void *), void *arg)
{
	struct thread *thread;
	int flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD |
		CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;

	thread = (struct thread *)malloc(sizeof(*thread));
	assert((thread != 0) && "allocate thread record and stack");
	thread->tid = 0;
	thread->fn = fn;
	thread->arg = arg;
	thread->result = -1;
	/* Keep the returned TID separate: tid may already be zero. */
	thread->const_thread_id = clone(thread_start, thread->stack + sizeof(thread->stack),
		flags, thread, (int *)&thread->tid, (void *)0,
		(int *)&thread->tid);
	if (thread->const_thread_id < 0) {
		int error = errno;
		free(thread);
		errno = error;
		assert((0) && "clone");
	}
	return thread;
}

static int futex_wait(volatile int *tid_addr, int expected)
{
	return syscall(SYS_futex, tid_addr, FUTEX_WAIT, expected,
		       NULL, NULL, 0);
}

static void join(struct thread *thread, int expected)
{
	int value;

	while ((value = thread->tid) != 0) {
		assert((futex_wait(&thread->tid, value) == 0 || errno == EAGAIN || errno == EINTR) &&
		      "thread exit futex wait");
	}
	assert((thread->result == expected) && "callback result after thread exit");
	/* The kernel no longer accesses this record or its user stack. */
	free(thread);
}

static void waited(int pid, int expected)
{
	int result, status = -1;
	do { result = waitpid(pid, &status, 0); } while (result < 0 && errno == EINTR);
	assert((result == pid && status == expected) && "process wait status");
}

static void wait_for_stop(int pid, int sig)
{
	int status;

	/* WUNTRACED (2) reports a stopped child without reaping it. */
	assert((waitpid(pid, &status, 2) == pid && status == ((sig << 8) | 0x7f)) &&
	      "process stop notification");
}

static void assert_child_alive(int pid)
{
	int status;
	/* WNOHANG (1) returns immediately: zero means the child is not reapable. */
	assert((waitpid(pid, &status, 1) == 0) && "child process must still be alive");
}

static char read_byte(int fd)
{
	int n;
	char byte = 0;
	do { n = read(fd, &byte, 1); } while (n < 0 && errno == EINTR);
	assert((n == 1) && "read one byte");
	return byte;
}

static void wait_for_pipe_eof(int fd[2])
{
	int n;
	char byte;
	/* Drop our write end so only the other holders can delay EOF. */
	close(fd[1]);
	do { n = read(fd[0], &byte, 1); } while (n < 0 && errno == EINTR);
	assert((n == 0) && "pipe EOF after all writers close");
	close(fd[0]);
}

static int value_worker(void *arg) { return (int)(long)arg; }

static volatile int child_signals;

static void child_handler(int sig) { child_signals++; }

static int identity_check(void *arg)
{
	assert((getpid() == process_id && getppid() == parent_id &&
	      gettid() != getpid()) && "thread identity and process parent");
	ready = 1;
	until(&go);
	assert((getppid() == parent_id) && "parent after thread creator exits");
	return (int)arg;
}

static int wait_for_go(void *arg)
{
	ready = 1;
	until(&go);
	return (int)arg;
}

static void identity_test(void)
{
	struct thread *thread;
	int status;

	signal(SIGCHLD, child_handler);

	ready = go = 0;
	parent_id = getppid();
	thread = spawn(identity_check, (void *)37);
	until(&ready);
	errno = 0;
	assert((waitpid(thread->const_thread_id, &status, 1) == -1 && errno == ECHILD) &&
	      "waitpid rejects a peer TID");
	go = 1;
	join(thread, 37);
	assert((child_signals == 0) && "child thread exit must not send SIGCHLD");
	signal(SIGCHLD, SIG_DFL);
	printf("PASS identity, no per-thread SIGCHLD, futex wait versus waitpid\n");
}

static int shared_fd;

static int private_files(void *arg)
{
	assert((close(shared_fd) == 0 && chdir("/") == 0) && "thread file/fs copies without CLONE_FILES and CLONE_FS");
	return 0;
}

static void private_files_test(void)
{
	struct thread *thread;
	struct stat before, after;

	assert((stat(".", &before) == 0) && "stat cwd");
	shared_fd = open("thread.tmp", O_CREAT|O_TRUNC|O_RDWR, 0600);
	assert((shared_fd >= 0 && write(shared_fd, "A", 1) == 1) && "open test file");
	lseek(shared_fd, 0, 0);
	thread = spawn(private_files, 0);
	join(thread, 0);
	assert((read_byte(shared_fd) == 'A') && "fd table remains private");
	close(shared_fd);
	assert((stat(".", &after) == 0 && before.st_ino == after.st_ino &&
	      before.st_dev == after.st_dev) && "cwd remains private");
	unlink("thread.tmp");
	printf("PASS no implicit CLONE_FILES or CLONE_FS\n");
}

/* Normal thread exits and parent relationships. */
static struct thread *volatile nested_thread;

static int nested_spawn(void *arg)
{
	nested_thread = spawn(wait_for_go, (void *)19);
	return 17;
}

static void creator_exit_test(void)
{
	ready = go = 0;
	join(spawn(nested_spawn, 0), 17);
	until(&ready);
	go = 1;
	join(nested_thread, 19);
	printf("PASS peer join after creator exit\n");
}

static volatile int forked_pid;

static int fork_worker(void *arg)
{
	int pid = fork();
	assert((pid >= 0) && "fork from worker");
	if (!pid) {
		/* Wait for the fater process's byte, sent after the creating worker
		 * exits, so this child process checks getppid() only afterward. */
		read_byte(wake_pipe[0]);
		assert((getppid() == process_id) && "child parent after creating thread exits");
	} else {
		forked_pid = pid;
	}
	return 0;
}

static void child_process_wait_test(void)
{
	assert((pipe(wake_pipe) == 0) && "creator exit pipe");
	join(spawn(fork_worker, 0), 0);

	assert((write(wake_pipe[1], "G", 1) == 1) && "wake child process after creator exit");
	waited(forked_pid, 0);
	close(wake_pipe[0]); close(wake_pipe[1]);
	printf("PASS child process waiting after creator exit\n");
}

static int surviving_worker(void *arg)
{
	close(leader_pipe[0]); close(leader_pipe[1]);
	read_byte(wake_pipe[0]);
	assert((getppid() == parent_id) && "parent survives leader exit");
	return 0;
}

static void leader_exits_before_worker_test(void)
{
	int pid;

	assert((pipe(leader_pipe) == 0 && pipe(wake_pipe) == 0) && "leader test pipes");
	pid = fork();
	assert((pid >= 0) && "fork child process's leader test");
	if (!pid) {
		parent_id = getppid();
		spawn(surviving_worker, 0);
		syscall(SYS_exit, 0);
	}

	/* Observe thread-leader exit while its worker still waits for our byte. */
	wait_for_pipe_eof(leader_pipe);
	assert_child_alive(pid);

	/* Let the final worker finish, then reap the child process. */
	assert((write(wake_pipe[1], "G", 1) == 1) && "wake survivor");
	waited(pid, 0);

	close(wake_pipe[0]); close(wake_pipe[1]);
	printf("PASS worker survives leader exit; parent reaps process after worker exits\n");
}

static void orphan_adoption_test(void)
{
	int child_pid, grandson_pid;

	assert((pipe(wake_pipe) == 0 && pipe(ready_pipe) == 0) && "orphan pipes");
	fflush(stdout);
	child_pid = fork();
	assert((child_pid >= 0) && "fork orphan parent");
	if (!child_pid) {
		grandson_pid = fork();
		assert((grandson_pid >= 0) && "fork orphan child");
		if (!grandson_pid) {
			/* Wait until our parent is reaped before checking adoption by init. */
			read_byte(wake_pipe[0]);
			assert((getppid() == 1) && "orphan adopted by the kernel init");
			assert((write(ready_pipe[1], "R", 1) == 1) && "orphan confirmation");
		}
		_exit(0);
	}
	waited(child_pid, 0);
	assert((write(wake_pipe[1], "G", 1) == 1) && "wake orphan");
	/* Wait for the grandson to finish checking that init is its parent. */
	read_byte(ready_pipe[0]);

	close(wake_pipe[0]); close(wake_pipe[1]);
	close(ready_pipe[0]); close(ready_pipe[1]);
	printf("PASS orphan adopted by init after parent reaping\n");
}


static volatile int hits, hit_tid;

/* Signal delivery, interrupted waits, and group-wide control. */
static void handler(int sig) { hit_tid = gettid(); hits++; }

static void install_handler(struct sigaction *previous)
{
	struct sigaction action;
	action.sa_handler = handler;
	assert((sigemptyset(&action.sa_mask) == 0) && "empty handler signal mask");
	action.sa_flags = 0;
	action.sa_restorer = 0;
	assert((sigaction(SIGUSR1, &action, previous) == 0) && "install shared handler");
}

static int signal_worker(void *arg)
{
	sigset_t mask;

	install_handler(0);
	assert((sigemptyset(&mask) == 0) && "empty worker signal mask");
	assert((pthread_sigmask(SIG_SETMASK, &mask, 0) == 0) && "unblock worker signals");
	ready = 1;
	until(&go);
	return 0;
}

static void shared_signal_handler_test(void)
{
	struct thread *thread;
	sigset_t oldmask, newmask, currentmask;
	struct sigaction action;

	ready = go = 0;
	assert((sigemptyset(&newmask) == 0 && sigaddset(&newmask, SIGUSR1) == 0) &&
	      "build SIGUSR1 mask");
	assert((pthread_sigmask(SIG_SETMASK, &newmask, &oldmask) == 0) && "block SIGUSR1");
	thread = spawn(signal_worker, 0);
	until(&ready);
	/* The guest libc writes through a NULL action; reinstall to retrieve the old one. */
	install_handler(&action);
	assert((action.sa_handler == handler) && "signal dispositions shared");
	assert((kill(getpid(), SIGUSR1) == 0) && "process signal");
	until(&hits);
	assert((hits == 1 && hit_tid == thread->const_thread_id) && "one eligible signal recipient");
	assert((pthread_sigmask(SIG_SETMASK, 0, &currentmask) == 0) && "query signal mask");
	assert((sigismember(&currentmask, SIGUSR1) == 1) && "masks remain per-thread");
	go = 1;
	join(thread, 0);
	assert((pthread_sigmask(SIG_SETMASK, &oldmask, 0) == 0) && "restore signal mask");
	assert((kill(getpid(), SIGUSR1) == 0 && hits == 2 && hit_tid == gettid()) &&
	      "handler survives installing thread exit");
	printf("PASS shared handlers, per-thread masks, process signal delivery\n");
}

static int interrupted_wait(void *arg)
{
	struct thread *exit_target = arg;
	sigset_t mask;

	assert((sigemptyset(&mask) == 0) && "empty waiter signal mask");
	assert((pthread_sigmask(SIG_SETMASK, &mask, 0) == 0) && "unblock waiter signals");
	ready = 1;
	assert((futex_wait(&exit_target->tid, exit_target->const_thread_id) == -1 && errno == EINTR) && "exit wait interrupted by a signal");
	return 0;
}

static void interrupted_waiter_test(void)
{
	struct thread *exit_target, *waiter;
	sigset_t oldmask, newmask;
	long start;

	go = ready = 0;
	assert((sigemptyset(&newmask) == 0 && sigaddset(&newmask, SIGUSR1) == 0) &&
	      "build SIGUSR1 mask");
	assert((pthread_sigmask(SIG_SETMASK, &newmask, &oldmask) == 0) && "block SIGUSR1");
	exit_target = spawn(wait_for_go, 0);
	until(&ready);
	ready = 0;
	waiter = spawn(interrupted_wait, exit_target);
	until(&ready);
	/* Allow the waiter to enter its interruptible kernel wait. */
	start = ticks();
	while (ticks() - start < 50) ;
	assert((kill(getpid(), SIGUSR1) == 0) && "interrupt exit waiter");
	join(waiter, 0);
	go = 1;
	join(exit_target, 0);
	assert((pthread_sigmask(SIG_SETMASK, &oldmask, 0) == 0) && "restore signal mask");
	printf("PASS signal interrupts futex wait\n");
}

static int busy_worker(void *arg) 
{ 
	for (;;) getpid(); 
	return 0; 
}

static void group_stop_test(void)
{
	int pid;

	assert((pipe(ready_pipe) == 0) && "stop handshake pipe");
	pid = fork();
	assert((pid >= 0) && "fork stop test");
	if (!pid) {
		spawn(busy_worker, 0);
		write(ready_pipe[1], "R", 1);
		busy_worker(NULL);
	}
	read_byte(ready_pipe[0]);
	assert((kill(pid, SIGSTOP) == 0) && "group stop");
	wait_for_stop(pid, SIGSTOP);
	assert((kill(pid, SIGCONT) == 0) && "group continue");
	assert((kill(pid, SIGKILL) == 0) && "group kill");
	waited(pid, SIGKILL);
	close(ready_pipe[0]); close(ready_pipe[1]);
	printf("PASS group stop, continue and kill\n");
}

static int group_exit_worker(void *arg) { _exit(*(int *)arg); return 0; }

static int group_fatal_worker(void *arg) { kill(getpid(), SIGTERM); return 0; }

static void group_exit_test(void)
{
	int exit_code = 42;
	int pid = fork();
	assert((pid >= 0) && "fork error");
	if (!pid) { spawn(group_exit_worker, &exit_code); busy_worker(NULL); }
	waited(pid, exit_code << 8);
	pid = fork();
	assert((pid >= 0) && "fork error");
	if (!pid) { spawn(group_fatal_worker, 0); busy_worker(NULL); }
	waited(pid, SIGTERM);
	printf("PASS _exit and fatal signals terminate every peer\n");
}

static void repeated_join_test(void)
{
	int i;

	for (i=0; i<128; i++) {
		join(spawn(value_worker, (void *)(long)i), i);
	}
	printf("PASS 128 create/return/futex-wait cycles\n");
}

int main(int argc, char **argv)
{
	process_id = getpid();
	/* Creation and the resource-sharing policy. */
	identity_test();
	private_files_test();

	/* Normal exits and parent relationships. */
	creator_exit_test();
	child_process_wait_test();
	leader_exits_before_worker_test();
	orphan_adoption_test();

	/* Signal handling and group-wide control. */
	shared_signal_handler_test();
	interrupted_waiter_test();
	group_stop_test();
	group_exit_test();

	/* Exec and repeated cleanup. */
	repeated_join_test();
	printf("ALL THREAD GROUP TESTS PASSED\n");
	return 0;
}
