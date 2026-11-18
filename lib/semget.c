#define __LIBRARY__
#include <unistd.h>
#include <sys/sem.h>

_syscall3(int,semget,key_t,key,int,nsems,int,flags)
