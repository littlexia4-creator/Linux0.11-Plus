/*
 * cond_resched_demo.c -- trigger the conditional-rescheduling demo via syscall 80.
 *
 * Deploy + build + run inside the guest:
 *   ./run.sh stages this file at /usr/root/cond_demo.c (Minix's 14-char limit).
 *   cd /usr/root
 *   gcc cond_demo.c
 *   ./a.out
 */
#include <stdio.h>

#define __NR_cond_resched_demo 80

int main(void)
{
	long __res;

	printf("main: running cond_resched demo\n");
	__asm__ volatile ("int $0x80"
		: "=a" (__res)
		: "0" ((long) __NR_cond_resched_demo));
	if (__res < 0) {
		printf("main: cond_resched demo failed (%ld)\n", __res);
		return 1;
	}
	printf("main: cond_resched demo completed\n");
	return 0;
}
