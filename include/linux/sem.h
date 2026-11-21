#ifndef _LINUX_SEM_H
#define _LINUX_SEM_H

#include <sys/sem.h>

/* Bounded tables for Linux 0.11; SEMVMX follows Linux's value limit. */
#define SEMMNI 16
#define SEMMSL 16
#define SEMOPM 32
#define SEMVMX 32767

/* The C syscall dispatcher accepts three arguments. semctl's public wrapper
 * packs its command and fourth argument without changing that kernel ABI. */
struct semctl_args {
	int command;
	unsigned long argument;
};

#endif
