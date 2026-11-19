/* One producer and two kernel-thread consumers share a single item slot. */
#include <errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/semaphore.h>

#define ITEMS 10
#define CONSUMERS 2
#define WAIT_TICKS (2 * HZ)

struct handoff {
	struct semaphore empty;
	struct semaphore token;
	int item;
	int stop;
};

static long producer(void *argument)
{
	struct handoff *handoff = argument;
	int i;

	for (i=1; i<=ITEMS; i++) {
		down(&handoff->empty); /* Wait for the consumer to free the slot. */
		handoff->item = i;
		printk("producer: put %d\n", i);
		up(&handoff->token);    /* One item is ready. */
	}
	/* Wait for the final item to be consumed before waking both readers. */
	down(&handoff->empty);
	handoff->stop = 1;
	for (i=0; i<CONSUMERS; i++)
		up(&handoff->token);
	return ITEMS;
}

static long consumer(void *argument)
{
	struct handoff *handoff = argument;
	int item, consumed = 0;

	for (;;) {
		if (down_timeout(&handoff->token, WAIT_TICKS) == -ETIME) {
			printk("%s: waiting for producer\n", current->kthread.name);
			continue;
		}
		if (handoff->stop)
		    break;
		item = handoff->item;
		if (item) {
			consumed++;
			printk("%s: got %d\n", current->kthread.name, item);
		}
		up(&handoff->empty);
	}
	return consumed;
}

static void stop_consumers(struct handoff *handoff,
	struct task_struct **readers, int started)
{
	int i;

	/* No producer exists; wake and join every consumer already started. */
	handoff->stop = 1;
	for (i=0; i<started; i++)
		up(&handoff->token);
	for (i=0; i<started; i++)
		kthread_join(readers[i]);
}

int sys_kernel_sem_demo(void)
{
	struct handoff handoff;
	struct task_struct *writer, *readers[CONSUMERS];
	int produced, counts[CONSUMERS], i, started = 0;

	sema_init(&handoff.empty, 1); /* One empty slot. */
	sema_init(&handoff.token, 0);  /* No item is ready yet. */
	handoff.item = handoff.stop = 0;
	for (i=0; i<CONSUMERS; i++) {
		readers[i] = kthread_run(consumer, &handoff,
			i ? "consumer B" : "consumer A");
		if (!readers[i]) {
			stop_consumers(&handoff, readers, started);
			return -EAGAIN;
		}

		started++;
	}
	writer = kthread_run(producer, &handoff, "producer");
	if (!writer) {
		stop_consumers(&handoff, readers, started);
		return -EAGAIN;
	}
	produced = kthread_join(writer);
	for (i=0; i<CONSUMERS; i++)
		counts[i] = kthread_join(readers[i]);
	/* Workers must finish before the stack-backed handoff is reclaimed. */
	if (produced != ITEMS || counts[0] < 0 || counts[1] < 0 ||
	    counts[0] + counts[1] != ITEMS )
		return -EIO;
	printk("Consumed: A=%d B=%d total=%d\n",
		counts[0], counts[1], produced);

	return 0;
}
