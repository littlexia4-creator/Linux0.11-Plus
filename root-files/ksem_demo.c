/* Trigger the kernel-thread semaphore demo inside Linux 0.11. */
#include <stdio.h>
#include <sys/syscall.h>

int main(void)
{
	if (syscall(SYS_kernel_sem_demo) < 0) {
		perror("kernel semaphore demo");
		return 1;
	}
	return 0;
}
