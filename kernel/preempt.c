/* Single-CPU legacy kernel preemption guards. */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

static unsigned int preempt_defer_count(void)
{
	return current->preempt_defer_depth;
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
}
