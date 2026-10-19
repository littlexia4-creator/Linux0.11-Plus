/*
 * Syscall 75: test the repository's strncpy() inside the running kernel.
 * Build with the kernel's GCC 3.2.3 and -O. Without commit 6609047's asm
 * constraint fix, copy_name writes the terminator at dest[30], not dest[15].
 * The 32-byte buffer keeps that erroneous write inside allocated storage.
 */
#include <errno.h>
#include <string.h>
#include <linux/kernel.h>

/* Keep the checks separate from the code whose operands were incorrect. */
static void __attribute__((noinline))
copy_name(char *dest, const char *src)
{
	strncpy(dest, src, 15);
	dest[15] = '\0';
}

int sys_strncpy_test(void)
{
	char dest[32];
	int i;
	unsigned short cs;

	__asm__ __volatile__("movw %%cs,%0" : "=r" (cs));
	if (cs & 3) {
		printk("[strncpy kernel] FAIL: expected kernel mode\n");
		return -EIO;
	}
	for (i = 0; i < sizeof(dest); i++)
		dest[i] = 'Z';
	copy_name(dest, "abcdefghijklmnop");

	printk("[strncpy kernel] dest[15]=0x%02x dest[30]=0x%02x CPL=%d\n",
		(unsigned char) dest[15], (unsigned char) dest[30], cs & 3);
	if (dest[15] != '\0') {
		printk("[strncpy kernel] FAIL: missing terminator at dest[15]\n");
		return -EIO;
	}
	for (i = 0; i < 15; i++)
		if (dest[i] != 'a' + i) {
			printk("[strncpy kernel] FAIL: copied byte %d\n", i);
			return -EIO;
		}
	for (i = 16; i < sizeof(dest); i++)
		if (dest[i] != 'Z') {
			printk("[strncpy kernel] FAIL: overwritten guard byte %d\n", i);
			return -EIO;
		}
	printk("[strncpy kernel] PASS\n");
	return 0;
}
