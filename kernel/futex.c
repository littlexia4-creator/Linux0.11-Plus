/* Futex wait/wake for threads sharing this kernel's fixed VM policy. */
#include <errno.h>
#include <linux/sched.h>
#include <linux/futex.h>
#include <asm/segment.h>

#define FUTEX_BUCKETS 8

struct futex_waiter {
	struct futex_waiter *next;
	struct task_struct *task;
	struct thread_group *group;
	unsigned long addr;
	int woken;
};

struct futex_bucket {
	struct futex_waiter *first, *last;
};

/* Private futexes are keyed by thread group and user address. The queues
 * are accessed from non-preemptible kernel task context. */
static struct futex_bucket futex_buckets[FUTEX_BUCKETS];

static struct futex_bucket *futex_bucket(struct thread_group *g,
					 unsigned long addr)
{
	unsigned long hash = ((unsigned long)g >> 4) ^ (addr >> 2);

	return &futex_buckets[hash & (FUTEX_BUCKETS - 1)];
}

static void queue_futex_waiter(struct futex_bucket *bucket,
			       struct futex_waiter *waiter)
{
	waiter->next = NULL;
	if (bucket->last)
		bucket->last->next = waiter;
	else
		bucket->first = waiter;
	bucket->last = waiter;
}

static void remove_futex_waiter(struct futex_bucket *bucket,
				struct futex_waiter *waiter)
{
	struct futex_waiter *p, *previous = NULL;

	for (p = bucket->first; p; p = p->next) {
		if (p == waiter) {
			if (previous)
				previous->next = p->next;
			else
				bucket->first = p->next;
			if (bucket->last == p)
				bucket->last = previous;
			return;
		}
		previous = p;
	}
}

int do_futex_wake(struct thread_group *g, unsigned long addr, int count)
{
	struct futex_bucket *bucket = futex_bucket(g, addr);
	struct futex_waiter *waiter, *next, *previous = NULL;
	int woken = 0;

	for (waiter = bucket->first; waiter; waiter = next) {
		next = waiter->next;
		if (waiter->group != g || waiter->addr != addr) {
			previous = waiter;
			continue;
		}
		if (previous)
			previous->next = next;
		else
			bucket->first = next;
		if (bucket->last == waiter)
			bucket->last = previous;
		waiter->woken = 1;
		wake_up_task(waiter->task);
		woken++;
		/* Legacy Linux FUTEX_WAKE checks the count after waking. */
		if (woken >= count)
			break;
	}
	return woken;
}

int do_futex_wait(struct thread_group *g, unsigned long addr, int expected)
{
	struct futex_bucket *bucket;
	struct futex_waiter waiter;

	/* Kernel task context is non-preemptible. Comparison, registration
	 * and the transition to sleep cannot be interrupted by another task. */
	if (get_fs_long((unsigned long *)addr) != (unsigned long)expected)
		return -EAGAIN;
	bucket = futex_bucket(g, addr);
	waiter.task = current;
	waiter.group = g;
	waiter.addr = addr;
	waiter.woken = 0;
	queue_futex_waiter(bucket, &waiter);
	while (!waiter.woken) {
		if ((pending_signals(current) & ~current->blocked) ||
		    g->exiting || (g->exec_task && g->exec_task != current)) {
			remove_futex_waiter(bucket, &waiter);
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
		return do_futex_wake(g, addr, value);
	if (op != FUTEX_WAIT)
		return -ENOSYS;
	return do_futex_wait(g, addr, value);
}
