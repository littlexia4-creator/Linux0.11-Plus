#define __LIBRARY__
#include <unistd.h>
#include <sys/sem.h>

_syscall3(int,semop,int,semid,struct sembuf *,ops,size_t,nops)
