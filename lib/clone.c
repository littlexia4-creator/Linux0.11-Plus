/*
 *  linux/lib/clone.c
 *
 *  User-space wrapper for the clone() syscall (thread creation),
 *  following the lib/open.c / lib/_exit.c convention: this runs at
 *  CPL3 (e.g. from init() after move_to_user_mode) and traps into
 *  the kernel through int $0x80. Returns the new thread's TID; the new
 *  thread shares the caller's TGID (the process ID returned by getpid()).
 *  When fn(arg) returns, its result becomes the thread's exit status.
 */
#define __LIBRARY__
#include <unistd.h>
#include <errno.h>

struct clone_args {
	int (*fn)(void *);
	void *arg;
};

static void clone_start(void *arg)
{
	struct clone_args *args = arg;

	_exit(args->fn(args->arg));
}

int clone(int (*fn)(void *), void *arg, void *stack_top)
{
	struct clone_args *args;
	register int res;

	if (!fn || !stack_top) {
		errno = EINVAL;
		return -1;
	}
	/* Keep startup data on the child's stack after the parent returns. */
	args = (struct clone_args *) stack_top - 1;
	args->fn = fn;
	args->arg = arg;
	/* GCC 1.40 needs the function address cast and explicit memory inputs
	 * to keep these stores before the syscall (no "memory" clobber). */
	__asm__ volatile ("int $0x80"
		:"=a" (res)
		:"0" (__NR_clone),"b" ((long) clone_start),"c" (args),
		"d" (args),"m" (args->fn),"m" (args->arg));
	if (res>=0)
		return res;
	errno = -res;
	return -1;
}
