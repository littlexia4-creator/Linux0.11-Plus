/* Single-CPU interrupt and preemption bookkeeping. */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

static unsigned int hardirq_depth;

static unsigned int preempt_defer_count(void)
{
	return current->preempt_defer_depth;
}

/* Check with local IRQs disabled. Expired counter stays zero while any
 * protected context defers the switch; no separate request bit is needed. */
static int should_preempt(void)
{
	return !hardirq_depth && !preempt_defer_count() &&
		(current == task[0] || current->tcb.state == TASK_RUNNING) &&
		need_resched();
}

int in_hardirq(void)
{
	return hardirq_depth != 0;
}

void irq_enter(void)
{
	hardirq_depth++;
}

void irq_exit(unsigned long interrupted_flags)
{
	unsigned long flags;

	local_irq_save(flags);
	if (!hardirq_depth)
		panic("unbalanced irq_exit");
	hardirq_depth--;
	/* The handler has finished. Consult the interrupted IF, not the IF
	 * cleared by an interrupt gate; nested handlers must unwind first. */
	if (irqs_enabled_flags(interrupted_flags) && should_preempt())
		schedule();
	local_irq_restore(flags);
}

void preempt_defer(void)
{
	current->preempt_defer_depth++;
	barrier();
}

void preempt_resume(void)
{
	barrier();
	if (!current->preempt_defer_depth)
		panic("unbalanced preempt_resume");
	current->preempt_defer_depth--;
	preempt_check_resched();
}

void preempt_check_resched(void)
{
	unsigned long flags;

	local_irq_save(flags);
	if (irqs_enabled_flags(flags) && should_preempt())
		schedule();
	local_irq_restore(flags);
}
