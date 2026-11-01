/*
 *  linux/lib/_exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

volatile void _exit(int exit_code)
{
	__asm__ volatile ("int $0x80"::"a" (__NR_exit_group),"b" (exit_code));
}
