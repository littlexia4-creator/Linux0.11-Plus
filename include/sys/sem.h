#ifndef _SYS_SEM_H
#define _SYS_SEM_H

#include <sys/ipc.h>

#define SEM_UNDO 0x1000 /* Reserved: semop currently returns ENOSYS for this flag. */
#define GETPID  11
#define GETVAL  12
#define GETALL  13
#define GETNCNT 14
#define GETZCNT 15
#define SETVAL  16
#define SETALL  17

struct sembuf {
	unsigned short sem_num;
	short sem_op;
	short sem_flg;
};

struct semid_ds {
	struct ipc_perm sem_perm;
	time_t sem_otime;
	time_t sem_ctime;
	unsigned long sem_nsems;
};

/* Like glibc, callers define union semun for semctl's optional argument. */
#define _SEM_SEMUN_UNDEFINED 1

int semget(key_t key, int nsems, int flags);
int semop(int semid, struct sembuf *ops, size_t nops);
int semctl(int semid, int semnum, int command, ...);

#endif
