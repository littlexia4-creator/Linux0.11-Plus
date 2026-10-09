/*
 * kernel/kthread_demo.c -- kernel-thread demo, invoked through syscall 73.
 */

#include <errno.h>

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

void demo1()
{
	struct task_struct *a, *b;
	int ra, rb;
	a = kthread_create(worker, NULL, "kworkerA");
	b = kthread_create(worker, NULL, "kworkerB");
	printk("kthread demo: created kworkerA(pid %d) kworkerB(pid %d)\n",
		a->pid, b->pid);
	ra = kthread_join(a);
	rb = kthread_join(b);
	printk("kthread demo done: kworkerA returned %d, kworkerB returned %d\n",
		ra, rb);
}

/*
 * Each worker uses HZ / 2 ticks (0.5 seconds of CPU time), exceeding its
 * initial 15-tick counter.
 */
static long cond_worker(void *unused)
{
	/*
	 * The timer interrupt updates stime asynchronously. volatile ensures
	 * each read through cpu_ticks reloads the counter from memory.
	 * Our compiler also reloads it after cond_resched(), but volatile
	 * ensures fresh reads without relying on that function call.
	 */
	volatile long *cpu_ticks = &current->stime;
	unsigned long start, calls = 0, scheduled = 0;

	printk("[%s] start\n", current->kthread.name);
	start = *cpu_ticks;
	/* HZ / 2 = 50 ticks = 0.5 seconds at HZ = 100. */
	while ((unsigned long) *cpu_ticks - start < HZ / 2) {
		calls++;
		if (cond_resched()) {
			scheduled++;
			printk("[%s] resumed after scheduling pass %lu\n",
				current->kthread.name, scheduled);
		}
	}
	printk("[%s] done: calls=%lu, scheduling passes=%lu\n",
		current->kthread.name, calls, scheduled);
	return scheduled;
}

static int demo2(void)
{
	struct task_struct *a, *b;
	int ra, rb;

	printk("demo2: CPU-bound workers with cond_resched\n");
	a = kthread_create(cond_worker, NULL, "condA");
	if (!a)
		return -EAGAIN;
	b = kthread_create(cond_worker, NULL, "condB");
	if (!b) {
		kthread_join(a);
		return -EAGAIN;
	}
	ra = kthread_join(a);
	rb = kthread_join(b);
	printk("demo2: scheduling passes A=%d B=%d\n", ra, rb);
	return 0;
}

/*
 * Syscall 73: run the kernel-thread demo on demand (post-login),
 * where the system is fully booted and the scheduler settles.
 */
int sys_kthread_demo(void)
{
	// demo1();
	demo2();
	return 0;
}
