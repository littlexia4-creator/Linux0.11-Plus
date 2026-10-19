/* User-space wrapper for the task's unique thread ID. */
#define __LIBRARY__
#include <unistd.h>

_syscall0(int,gettid)
