/* Futex wait/wake for threads sharing this kernel's fixed VM policy. */
#include <errno.h>
#include <linux/sched.h>
#include <linux/futex.h>
#include <asm/segment.h>

int futex_wake(struct thread_group *g, unsigned long addr, int count)
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

static int futex_wait(struct thread_group *g, unsigned long addr, int expected)
{
	/* On this uniprocessor, kernel context is not preemptible. There is
	 * no sleeping operation between comparison and waiter registration,
	 * so a wake cannot be lost between those two steps. */
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

int sys_futex(unsigned long addr, int op, int value)
{
	struct thread_group *g = current->group;

	if (!g || !user_range(addr, 4))
		return -EFAULT;
	if (addr & 3)
		return -EINVAL;
	op &= ~FUTEX_PRIVATE_FLAG;
	if (op == FUTEX_WAKE)
		return value < 0 ? -EINVAL : futex_wake(g, addr, value);
	if (op != FUTEX_WAIT)
		return -ENOSYS;
	return futex_wait(g, addr, value);
}
