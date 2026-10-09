/* Unlike the guest's legacy _exit(), terminate the whole thread group. */
#define __LIBRARY__
#include <unistd.h>

volatile void exit_group(int status)
{
	__asm__ volatile ("int $0x80"::"a" (__NR_exit_group),"b" (status));
	for (;;)
		;
}
