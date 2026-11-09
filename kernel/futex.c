/* Futex wait/wake for threads sharing this kernel's fixed VM policy. */
#include <errno.h>
#include <linux/sched.h>
#include <linux/futex.h>
#include <asm/segment.h>

static int do_futex_wake(struct thread_group *g, unsigned long addr, int count)
{
	struct task_struct *waiter;
	int i, woken = 0;

	for (i=1; i<NR_TASKS && woken<count; i++) {
		waiter = task[i];
		if (!waiter || waiter->group != g || waiter->futex_uaddr != addr)
			continue;
		waiter->futex_uaddr = 0;
		wake_up_process(waiter);
		woken++;
	}
	return woken;
}

int wake_up_futex(struct thread_group *g, unsigned long addr, int count)
{
	int result;

	preempt_defer();
	result = do_futex_wake(g, addr, count);
	preempt_resume();
	return result;
}

static int do_futex_wait(struct thread_group *g, unsigned long addr, int expected)
{
	/* The caller holds legacy kernel protection through comparison,
	 * registration and the transition to sleep, preventing lost wakeups. */
	if (get_fs_long((unsigned long *)addr) != (unsigned long)expected)
		return -EAGAIN;
	current->futex_uaddr = addr;
	while (current->futex_uaddr) {
		if ((pending_signals(current) & ~current->blocked) ||
		    g->exiting || (g->exec_task && g->exec_task != current)) {
			current->futex_uaddr = 0;
			return -EINTR;
		}
		current->tcb.state = TASK_SLEEP_INTERRUPTIBLE;
		schedule();
	}
	return 0;
}

static int wait_on_futex(struct thread_group *g, unsigned long addr, int expected)
{
	int result;

	preempt_defer();
	result = do_futex_wait(g, addr, expected);
	preempt_resume();
	return result;
}

int sys_futex(unsigned long addr, int op, int value)
{
	struct thread_group *g = current->group;

	if (!g || !user_range(addr, 4))
		return -EFAULT;
	if (addr & 3)
		return -EINVAL;
	op &= ~FUTEX_PRIVATE_FLAG;
	if (op == FUTEX_WAKE)
		return value < 0 ? -EINVAL : wake_up_futex(g, addr, value);
	if (op != FUTEX_WAIT)
		return -ENOSYS;
	return wait_on_futex(g, addr, value);
}
