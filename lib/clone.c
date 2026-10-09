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
#include <sched.h>
#include "clone.h"

struct clone_args {
	int (*fn)(void *);
	void *arg;
};

static void clone_start(void *arg)
{
	struct clone_args *args = arg;

	_exit(args->fn(args->arg));
}

static int valid_clone(int (*fn)(void *), void *stack_top, int flags)
{
	/* Reject unsupported policies before writing the child's startup data. */
	if (flags != (CLONE_VM | CLONE_SIGHAND | CLONE_THREAD) ||
	    !fn || !stack_top)
		return 0;
	return 1;
}

int __clone_raw(int (*fn)(void *), void *stack_top, int flags, void *arg)
{
	struct clone_args *args;
	register int res;

	if (!valid_clone(fn, stack_top, flags))
		return -EINVAL;
	/* Keep startup data on the child's stack after the parent returns. */
	args = (struct clone_args *) stack_top - 1;
	args->fn = fn;
	args->arg = arg;
	/* GCC 1.40 needs the function address cast and an explicit memory input
	 * for both stores (no "memory" clobber). One aggregate input also keeps
	 * this within its seven-operand inline-assembly limit. */
	__asm__ volatile ("int $0x80"
		:"=a" (res)
		:"0" (__NR_clone),"b" ((long) clone_start),"c" (args),
		"d" (args),"S" (flags),"m" (*args));
	return res;
}

int __clone(int (*fn)(void *), void *stack_top, int flags, void *arg)
{
	int res = __clone_raw(fn, stack_top, flags, arg);

	if (res>=0)
		return res;
	errno = -res;
	return -1;
}

/* Both toolchains prefix C symbols with '_'. Export the same entry address
 * under both names, using an assembler alias for PE/COFF and i386 a.out. */
__asm__(".globl _clone\n\t.set _clone,___clone");
