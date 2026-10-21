/* Join a peer in this process; status may be NULL. */
#define __LIBRARY__
#include <unistd.h>

_syscall2(int,thread_join,int,tid,int *,status)
