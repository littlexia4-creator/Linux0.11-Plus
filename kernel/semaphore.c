/* Counting semaphores following Linux's queued, direct-handoff model.
 * Linux uses a spinlock with IRQ save/restore. On one CPU, disabling local
 * IRQs also prevents timer preemption, protecting the count and waiter queue. */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <asm/system.h>

struct semaphore_waiter {
	struct semaphore_waiter *next;
	struct semaphore_waiter *next_timed;
	struct semaphore *sem;
	struct task_struct *task;
	int granted;
	int timed_out;
	long remaining;
};

/* Stack-backed waiters are linked here only while their timeout is active. */
static struct semaphore_waiter *timed_waiters;

static void init_waiter(struct semaphore_waiter *waiter, struct semaphore *sem)
{
	waiter->task = current;
	waiter->granted = 0;
	waiter->timed_out = 0;
	waiter->sem = sem;
}

void sema_init(struct semaphore *sem, int value)
{
	if (value < 0)
		panic("negative semaphore count");
	sem->count = value;
	sem->first = sem->last = NULL;
}

/* Semaphore queue operations run with IRQs disabled. */
static void add_waiter(struct semaphore *sem, struct semaphore_waiter *waiter)
{
	waiter->next = NULL;
	if (sem->last)
		sem->last->next = waiter;
	else
		sem->first = waiter;
	sem->last = waiter;
}

/* Removal handles signal-interrupted and timed-out waiters. */
static void remove_waiter(struct semaphore *sem, struct semaphore_waiter *waiter)
{
	struct semaphore_waiter *previous = NULL, *p;

	for (p=sem->first; p; p=p->next) {
		if (p == waiter) {
			if (previous)
				previous->next = p->next;
			else
				sem->first = p->next;
			if (sem->last == p)
				sem->last = previous;
			return;
		}
		previous = p;
	}
}

/* Both queues are protected by disabled local interrupts. */
static void add_timed_waiter(struct semaphore_waiter *waiter, long timeout)
{
	waiter->remaining = timeout;
	waiter->next_timed = timed_waiters;
	timed_waiters = waiter;
}

static void remove_timed_waiter(struct semaphore_waiter *waiter)
{
	struct semaphore_waiter **p;

	for (p = &timed_waiters; *p; p = &(*p)->next_timed)
		if (*p == waiter) {
			*p = waiter->next_timed;
			return;
		}
}

void semaphore_timer_tick(void)
{
	struct semaphore_waiter **p, *waiter;

	for (p = &timed_waiters;(waiter = *p) != NULL;*p = waiter->next_timed) {
		if (--waiter->remaining > 0) {
			p = &waiter->next_timed;
			continue;
		}

		remove_waiter(waiter->sem, waiter);
		waiter->timed_out = 1;
		wake_up_task(waiter->task);
	}
}

static int wait_for_permit(struct semaphore *sem, int interruptible,
			   int timed, long timeout, unsigned long flags)
{
	struct semaphore_waiter current_waiter;
	struct thread_group *g;
	int result = 0;

	if (in_hardirq() || !irqs_enabled_flags(flags) || current == task[0])
		panic("semaphore wait in non-sleepable context");
	init_waiter(&current_waiter, sem);
	if (timed && timeout <= 0) {
		return -ETIME;
	}
	add_waiter(sem, &current_waiter);
	if (timed)
		add_timed_waiter(&current_waiter, timeout);
	for (;;) {
		/* up() removes and grants under the same IRQ protection. A grant
		 * wins over a simultaneous signal, so a permit cannot be lost. */
		if (current_waiter.granted)
			break;
		if (current_waiter.timed_out) {
			result = -ETIME;
			break;
		}
		g = current->group;
		if (interruptible && ((pending_signals(current) & ~current->blocked) ||
		    (g && (g->exiting || (g->exec_task && g->exec_task != current))))) {
			remove_waiter(sem, &current_waiter);
			if (timed)
				remove_timed_waiter(&current_waiter);
			result = -EINTR;
			break;
		}
		current->tcb.state = interruptible ? TASK_SLEEP_INTERRUPTIBLE :
			TASK_SLEEP_UNINTERRUPTIBLE;
		/* Publish the sleep state before enabling IRQs. A concurrent up()
		 * sets RUNNING and granted; schedule() then cannot lose its wakeup.
		 * Never keep IRQs disabled while waiting for a future interrupt. */
		local_irq_restore(flags);
		schedule();
		local_irq_save(flags);
	}
	current->tcb.state = TASK_RUNNING;
	return result;
}

static int acquire_permit(struct semaphore *sem, int interruptible,
			  int timed, long timeout)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	if (sem->count) {
		sem->count--;
		local_irq_restore(flags);
		return 0;
	}

	ret = wait_for_permit(sem, interruptible, timed, timeout, flags);
	local_irq_restore(flags);
	return ret;
}

void down(struct semaphore *sem)
{
	acquire_permit(sem, 0, 0, 0);
}

int down_interruptible(struct semaphore *sem)
{
	return acquire_permit(sem, 1, 0, 0);
}

int down_timeout(struct semaphore *sem, long timeout)
{
	return acquire_permit(sem, 0, 1, timeout);
}

int down_trylock(struct semaphore *sem)
{
	unsigned long flags;
	int unavailable;

	local_irq_save(flags);
	unavailable = !sem->count;
	if (!unavailable)
		sem->count--;
	local_irq_restore(flags);
	return unavailable;
}

void up(struct semaphore *sem)
{
	struct semaphore_waiter *waiter;
	unsigned long flags;

	local_irq_save(flags);
	waiter = sem->first;
	if (!waiter) {
		sem->count++;
	} else {
		remove_waiter(sem, waiter);
		remove_timed_waiter(waiter);
		/* Hand the permit to this waiter instead of incrementing count.
		 * A newly arriving task cannot steal it before the waiter runs. */
		waiter->granted = 1;
		wake_up_task(waiter->task);
	}
	local_irq_restore(flags);
}
