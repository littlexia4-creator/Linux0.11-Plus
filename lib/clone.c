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
#include <stdarg.h>

struct clone_args {
	int (*fn)(void *);
	void *arg;
	int *ctid;
	int *ptid;
};

static void clone_start(void *arg)
{
	struct clone_args *args = arg;

	_exit(args->fn(args->arg));
}

int clone(int (*fn)(void *), void *stack_top, int flags, void *arg, ...)
{
	struct clone_args *args;
	int *ptid = NULL, *ctid = NULL;
	va_list ap;
	register int res;

#if defined(CLONE_VM) && defined(CLONE_SIGHAND) && defined(CLONE_THREAD)
	/* Reject unsupported policies before writing the child's startup data. */
	if ((flags & (CLONE_VM | CLONE_SIGHAND | CLONE_THREAD)) !=
	    (CLONE_VM | CLONE_SIGHAND | CLONE_THREAD) ||
	    (flags & ~(CLONE_VM | CLONE_SIGHAND | CLONE_THREAD |
	    CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID)) ||
	    !fn || !stack_top) {
		errno = EINVAL;
		return -1;
	}
#endif
	va_start(ap, arg);
	if (flags & (CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID))
		ptid = va_arg(ap, int *);
	if (flags & CLONE_CHILD_CLEARTID) {
		(void)va_arg(ap, void *); /* TLS placeholder; CLONE_SETTLS is unsupported. */
		ctid = va_arg(ap, int *);
	}
	va_end(ap);
	/* Keep startup data on the child's stack after the parent returns. */
	args = (struct clone_args *) stack_top - 1;
	args->fn = fn;
	args->arg = arg;
	args->ctid = ctid;
	args->ptid = ptid;
	/* GCC 1.40 has no memory clobber and permits at most seven operands.
	 * One aggregate input covers the startup stores. Load edi/ebp from
	 * it, preserving both registers for the parent's C calling convention. */
	__asm__ volatile ("pushl %%ebp\n\tpushl %%edi\n\t"
		"movl 8(%%ecx),%%edi\n\tmovl 12(%%ecx),%%ebp\n\t"
		"int $0x80\n\tpopl %%edi\n\tpopl %%ebp"
		:"=a" (res)
		:"0" (__NR_clone),"b" ((long) clone_start),"c" (args),
		"d" (args),"S" (flags),"m" (*args));
	if (res>=0)
		return res;
	errno = -res;
	return -1;
}
