#ifndef _LIB_SYSDEP_H
#define _LIB_SYSDEP_H

/* Raw i386 syscall: return negative errors without changing errno.
 * The external call also orders memory accesses with GCC 1.40. */
long __syscall3(long number, long first, long second, long third);

#endif
