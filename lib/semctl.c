#include <stdarg.h>
#include <sys/syscall.h>
#include <linux/sem.h>

int semctl(int semid, int semnum, int command, ...)
{
	union semun {
		int val;
		struct semid_ds *buf;
		unsigned short *array;
	} arg;
	struct semctl_args control;
	va_list ap;

	control.command = command;
	control.argument = 0;
	if (command == SETVAL || command == GETALL || command == SETALL ||
	    command == IPC_STAT || command == IPC_SET) {
		va_start(ap, command);
		arg = va_arg(ap, union semun);
		va_end(ap);
		if (command == SETVAL)
			control.argument = arg.val;
		else if (command == GETALL || command == SETALL)
			control.argument = (unsigned long)arg.array;
		else
			control.argument = (unsigned long)arg.buf;
	}
	return syscall(SYS_semctl, semid, semnum, &control);
}
