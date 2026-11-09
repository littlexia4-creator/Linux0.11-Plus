/* Kernel-thread yielding and joining demo, called through syscall 74. */

#include <linux/sched.h>
#include <linux/kernel.h>

static long worker(void *unused)
{
	int i;

	for (i = 1; i <= 10; i++) {
		printk("[%s] step %d\n", current->kthread.name, i);
		/* Give up the remaining counter after each step. */
		yield();
	}
	return i - 1;
}

static void kthread_yield_demo(void)
{
	struct task_struct *a, *b;
	int ra, rb;
	a = kthread_run(worker, NULL, "kworkerA");
	b = kthread_run(worker, NULL, "kworkerB");
	printk("kthread demo: created kworkerA(tid %d) kworkerB(tid %d)\n",
		a->tid, b->tid);
	ra = kthread_join(a);
	rb = kthread_join(b);
	printk("kthread demo done: kworkerA returned %d, kworkerB returned %d\n",
		ra, rb);
}

/*
 * Syscall 74: run the kernel-thread demo on demand (post-login),
 * where the system is fully booted and the scheduler settles.
 */
int sys_yield_demo(void)
{
	kthread_yield_demo();
	return 0;
}
