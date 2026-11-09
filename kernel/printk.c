/*
 *  linux/kernel/printk.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * When in kernel-mode, we cannot use printf, as fs is liable to
 * point to 'interesting' things. Make a printf with fs-saving, and
 * all is well.
 */
#include <stdarg.h>
#include <stddef.h>

#include <linux/kernel.h>
#include <linux/preempt.h>
#include <asm/segment.h>

extern int vsprintf(char * buf, const char * fmt, va_list args);

int printk(const char *fmt, ...)
{
	va_list args;
	int i;
	unsigned long fs;
	/* A blocked console writer must not share its formatting buffer with
	 * another task or an interrupt that also prints. */
	char buf[1024];

	preempt_defer();
	va_start(args, fmt);
	i=vsprintf(buf,fmt,args);
	va_end(args);
	fs = get_fs();
	set_fs(get_ds());
	tty_write(0, buf, i);
	set_fs(fs);
	preempt_resume();
	return i;
}
