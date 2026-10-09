/*
 *  linux/lib/clone.c
 *
 *  User-space wrapper for the clone() syscall (thread creation),
 *  following the lib/open.c / lib/_exit.c convention: this runs at
 *  CPL3 (e.g. from init() after move_to_user_mode) and traps into
 *  the kernel through int $0x80.
 */
#define __LIBRARY__
#include <unistd.h>

int clone(void (*fn)(void *), void *arg, void *stack_top)
{
	register int res;

	__asm__("int $0x80"
		:"=a" (res)
		:"0" (__NR_clone),"b" (fn),"c" (arg),
		"d" (stack_top));
	if (res>=0)
		return res;
	errno = -res;
	return -1;
}
