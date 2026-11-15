/* User-space entry for the scheduler's voluntary yield syscall. */
#define __LIBRARY__
#include <unistd.h>
#include <sched.h>

_syscall0(int,sched_yield)
