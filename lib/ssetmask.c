/* User-space wrapper for the calling thread's signal mask. */
#define __LIBRARY__
#include <unistd.h>
#include <signal.h>

int ssetmask(int value)
{
	int result;

	/* The return value is a bitmap, so bit 31 must not be treated as errno. */
	__asm__ volatile ("int $0x80" : "=a" (result)
		: "0" (__NR_ssetmask), "b" (value));
	return result;
}
