/* Level3: default joinable user threads over clone and clear-TID futexes. */
#include "pthread.h"
#include <sched.h>
#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include "clone.h"
#include "atomic.h"
#include "sysdep.h"

#define THREAD_SLOTS 64
#define STACK_SIZE 16384

struct pthread_descriptor {
	pthread_t handle;
	volatile int tid;
	int joining;
	void *(*start)(void *);
	void *arg;
	void *result;
	char stack[STACK_SIZE];
};

/* BSS storage avoids the non-reentrant guest heap allocator. A finished
 * joinable thread still owns its slot until another thread joins it.
 * Leaving threads joinable and unjoined would accumulate resources until
 * the server exits. Here, retained slots can exhaust THREAD_SLOTS and make
 * pthread_create() return EAGAIN. */
static struct pthread_descriptor threads[THREAD_SLOTS];
static pthread_t last_handle;
static volatile int library_lock;

static int futex_wait(volatile int *word, int expected)
{
	return __syscall3(SYS_futex, (long)word, FUTEX_WAIT, expected);
}

static void lock_library(void)
{
	while (atomic_exchange_acquire(&library_lock, 1))
		futex_wait(&library_lock, 1);
}

static void unlock_library(void)
{
	atomic_exchange_release(&library_lock, 0);
	__syscall3(SYS_futex, (long)&library_lock, FUTEX_WAKE, 1);
}

static struct pthread_descriptor *find_thread(pthread_t handle)
{
	int i;

	for (i = 0; i < THREAD_SLOTS; i++)
		if (threads[i].handle == handle)
			return &threads[i];
	return 0;
}

static pthread_t new_handle(void)
{
	/* Keep zero invalid and avoid live handles even if the counter wraps. */
	do {
		last_handle++;
	} while (!last_handle || find_thread(last_handle));
	return last_handle;
}

static int start_thread(void *arg)
{
	struct pthread_descriptor *thread = arg;

	/* Retain the full pointer; the raw exit status cannot carry this result. */
	thread->result = thread->start(thread->arg);
	return 0;
}

int pthread_create(pthread_t *handle, const pthread_attr_t *attr,
		   void *(*start_routine)(void *), void *arg)
{
	struct pthread_descriptor *thread = 0;
	void *stack_top;
	int i, result;
	int flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD |
		CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;

	if (!handle || attr || !start_routine)
		return EINVAL;
	lock_library();
	for (i = 0; i < THREAD_SLOTS; i++)
		if (!threads[i].handle) {
			thread = &threads[i];
			break;
		}
	if (!thread) {
		unlock_library();
		return EAGAIN;
	}
	thread->handle = new_handle();
	thread->tid = thread->joining = 0;
	thread->start = start_routine;
	thread->arg = arg;
	thread->result = 0;
	stack_top = (void *)((unsigned long)(thread->stack + STACK_SIZE) & ~15UL);
	/* Publish/clear the same word. A child may exit before __clone_raw returns;
	 * never overwrite tid with its return value or release its slot here. */
	result = __clone_raw(start_thread, stack_top, flags, thread,
			     (int *)&thread->tid, (int *)&thread->tid);
	if (result < 0) {
		thread->handle = 0;
		unlock_library();
		return result == -ENOMEM ? EAGAIN : -result;
	}
	*handle = thread->handle;
	unlock_library();
	return 0;
}

int pthread_join(pthread_t handle, void **retval)
{
	struct pthread_descriptor *thread;
	int tid, result;

	if (!handle)
		return ESRCH;
	lock_library();
	thread = find_thread(handle);
	if (!thread) {
		unlock_library();
		return ESRCH;
	}
	if (thread->tid == __syscall3(SYS_gettid, 0, 0, 0)) {
		unlock_library();
		return EDEADLK;
	}
	if (thread->joining) {
		unlock_library();
		return EINVAL;
	}
	thread->joining = 1;
	unlock_library();
	while ((tid = thread->tid) != 0) {
		result = futex_wait(&thread->tid, tid);
		if (result < 0 && result != -EAGAIN && result != -EINTR) {
			lock_library();
			thread->joining = 0;
			unlock_library();
			return -result;
		}
	}
	/* Reacquiring orders result reads after the kernel's clear-TID store.
	 * The kernel no longer uses the user stack; it reaps its own task stack. */
	lock_library();
	if (retval)
		*retval = thread->result;
	thread->handle = 0;
	thread->joining = 0;
	unlock_library();
	return 0;
}
