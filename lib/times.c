/* User-space wrapper for the times() syscall. */
#define __LIBRARY__
#include <unistd.h>

_syscall1(time_t,times,struct tms *,tbuf)
