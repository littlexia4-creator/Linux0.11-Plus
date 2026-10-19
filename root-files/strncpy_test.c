/*
 * Invoke the in-kernel strncpy regression test through syscall 75.
 * First run ./run.sh on the host to build/boot the updated kernel and copy
 * this file into /usr/root. Then, inside Linux 0.11:
 *   cd /usr/root
 *   gcc -O -o strncpy_test strncpy_test.c
 *   ./strncpy_test
 *
 * The kernel uses the repository's include/string.h, prints its checks on
 * the console, and returns 0 on success or -EIO on failure. This program
 * returns 0 on success and 1 on failure; it does not call the guest libc's
 * strncpy(). Rebuild the kernel after changing the repository's header.
 */
#include <stdio.h>
#include <stdlib.h>

/* The guest's installed headers do not contain this added syscall number. */
#define __NR_strncpy_test 75

int main(void)
{
	long result;

	__asm__ volatile ("int $0x80"
		: "=a" (result)
		: "0" ((long) __NR_strncpy_test));
	if (result != 0) {
		printf("Kernel strncpy test failed (syscall returned %ld).\n", result);
		if (result == -1)
			printf("Boot the rebuilt kernel with syscall 75 support.\n");
		exit(EXIT_FAILURE);
	}
	printf("Kernel strncpy test passed.\n");
	exit(EXIT_SUCCESS);
}
