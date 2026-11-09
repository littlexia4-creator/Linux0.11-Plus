/* Kernel-thread conditional-rescheduling demo, called through syscall 80. */

#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>

/*
 * Each worker uses HZ / 2 ticks (0.5 seconds of CPU time), exceeding its
 * initial 15-tick counter.
 */
static long cond_worker(void *unused)
{
	/*
	 * The timer interrupt updates stime asynchronously. volatile ensures
	 * each read through cpu_ticks reloads the counter from memory.
	 * volatile ensures fresh reads without relying on cond_resched(),
	 * which does nothing when CONFIG_PREEMPT is enabled.
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

int sys_cond_resched_demo(void)
{
	struct task_struct *a, *b;
	int ra, rb;

#ifdef CONFIG_PREEMPT
	printk("demo2: CPU-bound workers; CONFIG_PREEMPT enabled, cond_resched is a no-op\n");
#else
	printk("demo2: CPU-bound workers; CONFIG_PREEMPT disabled, cond_resched enabled\n");
#endif
	a = kthread_run(cond_worker, NULL, "condA");
	if (!a)
		return -EAGAIN;
	b = kthread_run(cond_worker, NULL, "condB");
	if (!b) {
		kthread_join(a);
		return -EAGAIN;
	}
	ra = kthread_join(a);
	rb = kthread_join(b);
	printk("demo2: scheduling passes A=%d B=%d\n", ra, rb);
	return 0;
}
