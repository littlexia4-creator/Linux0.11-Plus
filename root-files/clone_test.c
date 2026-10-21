/* Linux 0.11 guest regression test for the fixed clone thread-group policy.
 * gcc -O -o clone_test clone_test.c -Llib -lsyscall
 * Run from /usr/root: ./clone_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "sched.h"

extern int gettid(void);
extern int thread_join(int tid, int *status);
extern volatile void exit_group(int status);

static char stacks[4][4096];
static volatile int ready, go, nested_tid, forked_pid, join_target;
static volatile int hits, hit_tid, child_signals;
static int process_id, parent_id, shared_fd;
static int wake_pipe[2], ready_pipe[2];
static char exec_pid[20], exec_parent[20], exec_child[20], exec_gate[20];

static void check(int ok, char *what)
{
	if (!ok) {
		printf("FAIL: %s (errno=%d)\n", what, errno);
		fflush(stdout);
		exit_group(1);
	}
}

static long ticks(void)
{
	long result;
	__asm__ volatile ("int $0x80" : "=a" (result) : "0" (43), "b" (0));
	return result;
}

static int mask(int value)
{
	int result;
	__asm__ volatile ("int $0x80" : "=a" (result) : "0" (69), "b" (value));
	return result;
}

static void until(volatile int *value)
{
	long start = ticks();
	while (!*value && ticks() - start < 500)
		;
	check(*value != 0, "thread handshake timed out");
}

static int spawn(int (*fn)(void *), void *arg, int stack)
{
	int tid = clone(fn, stacks[stack] + sizeof(stacks[stack]),
			CLONE_VM | CLONE_SIGHAND | CLONE_THREAD, arg);
	check(tid > 0, "clone");
	return tid;
}

static void join(int tid, int expected)
{
	int result, status = -1;
	do { result = thread_join(tid, &status); } while (result < 0 && errno == EINTR);
	check(result == 0 && status == expected, "thread_join status");
}

static void waited(int pid, int expected)
{
	int result, status = -1;
	do { result = waitpid(pid, &status, 0); } while (result < 0 && errno == EINTR);
	check(result == pid && status == expected, "process wait status");
}

static void read_byte(int fd)
{
	int n;
	char byte;
	do { n = read(fd, &byte, 1); } while (n < 0 && errno == EINTR);
	check(n == 1, "pipe handshake");
}

static void child_handler(int sig) { child_signals++; }
static void handler(int sig) { hits++; hit_tid = gettid(); }

static int identity(void *arg)
{
	check(getpid() == process_id && getppid() == parent_id &&
	      gettid() != getpid(), "thread identity and process parent");
	ready = 1;
	until(&go);
	check(getppid() == parent_id, "parent after thread creator exits");
	return (int)(long)arg;
}

static int nested(void *arg)
{
	nested_tid = spawn(identity, (void *)19, 1);
	return 17;
}

static int fork_worker(void *arg)
{
	int pid = fork();
	check(pid >= 0, "fork from worker");
	if (!pid) {
		read_byte(wake_pipe[0]);
		check(getppid() == process_id, "child parent after creating thread exits");
		_exit(11);
	}
	forked_pid = pid;
	return 0;
}

static int signal_worker(void *arg)
{
	struct sigaction action;
	action.sa_handler = handler;
	action.sa_mask = 0;
	action.sa_flags = 0;
	action.sa_restorer = 0;
	check(sigaction(SIGUSR1, &action, 0) == 0, "install shared handler");
	mask(0);
	ready = 1;
	until(&go);
	return 0;
}

static int private_files(void *arg)
{
	check(close(shared_fd) == 0 && chdir("/") == 0, "thread file/fs copies");
	return 0;
}

static int after_leader(void *arg)
{
	join(getpid(), 23);
	check(getppid() == parent_id, "parent survives leader exit");
	check(write(ready_pipe[1], "R", 1) == 1, "leader exit handshake");
	read_byte(wake_pipe[0]);
	return 7;
}

static int group_exit_worker(void *arg) { exit_group(42); return 99; }
static int fatal_worker(void *arg) { kill(getpid(), SIGTERM); return 0; }
static int busy_worker(void *arg) { for (;;) getpid(); return 0; }
static int value_worker(void *arg) { return (int)(long)arg; }

/* Bypass libsyscall to test the kernel's own flags check. Null entry and
 * stack addresses are intentional: invalid flags must fail before setup. */
static int raw_clone_flags(int flags)
{
	int result;
	__asm__ volatile ("int $0x80" : "=a" (result) : "0" (73),
			"b" (0), "c" (0), "d" (0), "S" (flags));
	return result;
}

static void clone_flags_test(void)
{
	int required = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD;
	int flags, i;
	int unsupported[] = { 0x200, 0x400, SIGCHLD, 0x40000000 };

	/* All proper subsets of the required flags must be rejected. */
	for (i=0; i<7; i++) {
		flags = ((i & 1) ? CLONE_VM : 0) |
			((i & 2) ? CLONE_SIGHAND : 0) |
			((i & 4) ? CLONE_THREAD : 0);
		errno = 0;
		check(clone(value_worker, (void *)1, flags, 0) == -1 && errno == EINVAL,
		      "wrapper rejects incomplete flags before writing stack");
		check(raw_clone_flags(flags) == -EINVAL, "kernel rejects incomplete flags");
	}
	for (i=0; i<sizeof(unsupported)/sizeof(unsupported[0]); i++) {
		flags = required | unsupported[i];
		errno = 0;
		check(clone(value_worker, (void *)1, flags, 0) == -1 && errno == EINVAL,
		      "wrapper rejects unsupported flags");
		check(raw_clone_flags(flags) == -EINVAL, "kernel rejects unsupported flags");
	}
	check(raw_clone_flags(required) == -EFAULT, "valid flags reach address validation");
	printf("PASS explicit clone flags and kernel validation\n");
}

static int interrupted_join(void *arg)
{
	int status, result;
	mask(0);
	ready = 1;
	result = thread_join(join_target, &status);
	check(result == -1 && errno == EINTR, "join interrupted by a signal");
	return 0;
}

static int exec_worker(void *arg)
{
	char *argv[7];
	char *env[1];
	if (arg)
		join(getpid(), 23);
	argv[0] = "clone_test"; argv[1] = "exec";
	argv[2] = exec_pid; argv[3] = exec_parent;
	argv[4] = exec_child; argv[5] = exec_gate; argv[6] = 0;
	env[0] = 0;
	execve("/usr/root/clone_test", argv, env);
	exit_group(91);
	return 0;
}

static int bad_exec(void *arg)
{
	char *argv[2];
	char *env[1];
	argv[0] = "missing"; argv[1] = 0; env[0] = 0;
	check(execve("/no-such-file", argv, env) == -1 && errno == ENOENT,
	      "failed exec preserves group");
	return 0;
}

int main(int argc, char **argv)
{
	int tid, pid, status, i, oldmask, child_pid, child_parent;
	long start;
	char byte;
	struct stat before, after;
	struct sigaction action;

	if (argc == 6 && !strcmp(argv[1], "exec")) {
		check(getpid() == atoi(argv[2]) && gettid() == getpid() &&
		      getppid() == atoi(argv[3]), "nonleader exec preserves process identity");
		check(sigaction(SIGUSR1, 0, &action) == 0 && action.sa_handler == SIG_DFL,
		      "exec resets caught signals");
		check(sigaction(SIGUSR2, 0, &action) == 0 && action.sa_handler == SIG_IGN,
		      "exec preserves ignored signals");
		check(write(atoi(argv[5]), "G", 1) == 1, "wake child after exec");
		waited(atoi(argv[4]), 12 << 8);
		for (i=0; i<12; i++) {
			pid = fork();
			check(pid >= 0, "fork after nonleader exec");
			if (!pid) _exit(3);
			waited(pid, 3 << 8);
		}
		exit(41);
	}
	process_id = getpid();
	parent_id = getppid();
	clone_flags_test();
	signal(SIGCHLD, child_handler);
	tid = spawn(identity, (void *)37, 0);
	until(&ready);
	errno = 0;
	check(waitpid(tid, &status, 1) == -1 && errno == ECHILD,
	      "waitpid rejects a peer TID");
	go = 1;
	join(tid, 37);
	check(child_signals == 0, "thread exit must not send SIGCHLD");
	errno = 0;
	check(thread_join(tid, &status) == -1 && errno == ESRCH, "one join per thread");
	check(thread_join(gettid(), 0) == -1 && errno == EDEADLK, "self join");
	signal(SIGCHLD, SIG_DFL);
	printf("PASS identity, no per-thread SIGCHLD, join versus waitpid\n");

	ready = go = 0;
	tid = spawn(nested, 0, 0);
	join(tid, 17);
	until(&ready);
	go = 1;
	join(nested_tid, 19);
	check(pipe(wake_pipe) == 0, "creator exit pipe");
	tid = spawn(fork_worker, 0, 0);
	join(tid, 0);
	/* Allocation reaps the creator's task and can reuse its task slot. */
	for (i=0; i<8; i++) {
		tid = spawn(value_worker, 0, 0);
		join(tid, 0);
	}
	check(write(wake_pipe[1], "G", 1) == 1, "wake child after creator exit");
	waited(forked_pid, 11 << 8);
	close(wake_pipe[0]); close(wake_pipe[1]);
	printf("PASS peer join after creator exit and group-wide child waiting\n");

	check(pipe(wake_pipe) == 0 && pipe(ready_pipe) == 0, "orphan pipes");
	fflush(stdout);
	pid = fork();
	check(pid >= 0, "fork orphan parent");
	if (!pid) {
		child_pid = fork();
		check(child_pid >= 0, "fork orphan child");
		if (!child_pid) {
			read_byte(wake_pipe[0]);
			check(getppid() == 1, "orphan adopted by init");
			check(write(ready_pipe[1], "R", 1) == 1, "orphan confirmation");
			_exit(0);
		}
		_exit(0);
	}
	waited(pid, 0);
	for (i=0; i<8; i++) {
		pid = fork();
		check(pid >= 0, "reuse orphan parent's task slot");
		if (!pid) _exit(0);
		waited(pid, 0);
	}
	check(write(wake_pipe[1], "G", 1) == 1, "wake orphan");
	read_byte(ready_pipe[0]);
	for (i=0; i<2; i++) { close(wake_pipe[i]); close(ready_pipe[i]); }
	printf("PASS orphan parent pointer after parent reaping and slot reuse\n");

	ready = go = 0;
	oldmask = mask(1 << (SIGUSR1-1));
	tid = spawn(signal_worker, 0, 0);
	until(&ready);
	check(sigaction(SIGUSR1, 0, &action) == 0 && action.sa_handler == handler,
	      "signal dispositions shared");
	check(kill(getpid(), SIGUSR1) == 0, "process signal");
	until(&hits);
	check(hits == 1 && hit_tid == tid, "one eligible signal recipient");
	check(mask(1 << (SIGUSR1-1)) == (1 << (SIGUSR1-1)), "masks remain per-thread");
	go = 1;
	join(tid, 0);
	mask(oldmask);
	check(kill(getpid(), SIGUSR1) == 0 && hits == 2 && hit_tid == gettid(),
	      "handler survives installing thread exit");
	printf("PASS shared handlers, per-thread masks, process signal delivery\n");

	ready = go = 0;
	oldmask = mask(1 << (SIGUSR1-1));
	join_target = spawn(identity, 0, 0);
	until(&ready);
	ready = 0;
	tid = spawn(interrupted_join, 0, 1);
	until(&ready);
	/* Allow the joiner to enter its interruptible kernel wait. */
	start = ticks();
	while (ticks() - start < 50) ;
	check(thread_join(join_target, 0) == -1 && errno == EINVAL,
	      "concurrent join rejected");
	check(kill(getpid(), SIGUSR1) == 0, "interrupt joiner");
	join(tid, 0);
	mask(oldmask);
	go = 1;
	join(join_target, 0);
	printf("PASS interrupted join releases its exclusive claim\n");

	check(stat(".", &before) == 0, "stat cwd");
	shared_fd = open("thread.tmp", O_CREAT|O_TRUNC|O_RDWR, 0600);
	check(shared_fd >= 0 && write(shared_fd, "A", 1) == 1, "open test file");
	lseek(shared_fd, 0, 0);
	tid = spawn(private_files, 0, 0);
	join(tid, 0);
	check(read(shared_fd, &byte, 1) == 1 && byte == 'A', "fd table remains private");
	check(stat(".", &after) == 0 && before.st_ino == after.st_ino &&
	      before.st_dev == after.st_dev, "cwd remains private");
	close(shared_fd);
	unlink("thread.tmp");
	printf("PASS no implicit CLONE_FILES or CLONE_FS\n");

	check(pipe(wake_pipe) == 0 && pipe(ready_pipe) == 0, "pipes");
	fflush(stdout);
	pid = fork();
	check(pid >= 0, "fork leader test");
	if (!pid) {
		parent_id = getppid();
		spawn(after_leader, 0, 0);
		_exit(23);
	}
	read_byte(ready_pipe[0]);
	check(waitpid(pid, &status, 1) == 0, "parent waits for last live thread");
	check(write(wake_pipe[1], "G", 1) == 1, "wake survivor");
	waited(pid, 23 << 8);
	for (i=0; i<2; i++) { close(wake_pipe[i]); close(ready_pipe[i]); }
	printf("PASS leader exits first; one process status after final thread\n");

	fflush(stdout);
	pid = fork();
	check(pid >= 0, "fork group exit");
	if (!pid) { spawn(group_exit_worker, 0, 0); for (;;) getpid(); }
	waited(pid, 42 << 8);
	fflush(stdout);
	pid = fork();
	check(pid >= 0, "fork fatal signal");
	if (!pid) { spawn(fatal_worker, 0, 0); for (;;) getpid(); }
	waited(pid, SIGTERM);
	printf("PASS exit_group and fatal signals terminate every peer\n");

	fflush(stdout);
	check(pipe(ready_pipe) == 0, "stop handshake pipe");
	pid = fork();
	check(pid >= 0, "fork stop test");
	if (!pid) {
		spawn(busy_worker, 0, 0);
		write(ready_pipe[1], "R", 1);
		for (;;) getpid();
	}
	read_byte(ready_pipe[0]);
	check(kill(pid, SIGSTOP) == 0, "group stop");
	check(waitpid(pid, &status, 2) == pid && status == ((SIGSTOP << 8) | 0x7f),
	      "process stop notification");
	check(kill(pid, SIGCONT) == 0 && kill(pid, SIGKILL) == 0, "continue and kill");
	waited(pid, SIGKILL);
	close(ready_pipe[0]); close(ready_pipe[1]);
	printf("PASS group stop, continue and kill\n");

	tid = spawn(bad_exec, 0, 0);
	join(tid, 0);
	signal(SIGUSR2, SIG_IGN);
	for (i=0; i<2; i++) {
		check(pipe(wake_pipe) == 0, "exec child pipe");
		fflush(stdout);
		pid = fork();
		check(pid >= 0, "fork exec test");
		if (!pid) {
			child_parent = getpid();
			child_pid = fork();
			check(child_pid >= 0, "fork child before exec");
			if (!child_pid) {
				read_byte(wake_pipe[0]);
				check(getppid() == child_parent, "child parent after nonleader exec");
				_exit(12);
			}
			sprintf(exec_child, "%d", child_pid);
			sprintf(exec_gate, "%d", wake_pipe[1]);
			sprintf(exec_pid, "%d", getpid());
			sprintf(exec_parent, "%d", getppid());
			spawn(exec_worker, (void *)(long)i, 0);
			if (i) _exit(23);
			for (;;) getpid();
		}
		waited(pid, 41 << 8);
		close(wake_pipe[0]); close(wake_pipe[1]);
	}
	printf("PASS failed exec and nonleader exec with preserved TGID\n");

	for (i=0; i<128; i++) {
		tid = spawn(value_worker, (void *)(long)i, 0);
		join(tid, i);
	}
	printf("PASS 128 create/return/join cycles\n");
	printf("ALL THREAD GROUP TESTS PASSED\n");
	exit(0);
}
