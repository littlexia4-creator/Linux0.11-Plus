/*
 * yield_demo.c -- trigger the kernel-thread demo via syscall 74.
 *
 * Deploy + build + run inside the guest:
 *   ./run.sh stages this file at /usr/root/yield_demo.c.
 *   cd /usr/root
 *   gcc yield_demo.c
 *   ./a.out
 */
#include <stdio.h>

#define __NR_yield_demo 74

int main(void)
{
	long __res;

	printf("main: issuing syscall\n");
	__asm__ volatile ("int $0x80"
		: "=a" (__res)
		: "0" ((long) __NR_yield_demo));
	if (__res < 0) {
		printf("main: syscall failed (%ld)\n", __res);
		return 1;
	}
	printf("main: syscall issued\n");
	return 0;
}
