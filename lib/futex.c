/* Minimal process-local futex wait/wake wrapper. */
#define __LIBRARY__
#include <unistd.h>
#include <linux/futex.h>

_syscall3(int,futex,volatile int *,uaddr,int,op,int,value)
