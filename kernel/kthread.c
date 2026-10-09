/*
 *  kernel/kthread.c
 *
 *  Kernel threads for Linux 0.11: cooperative ring-0 tasks that share
 *  the kernel address space.  The scheduling state of every task lives
 *  in its embedded TCB (struct tcb, include/linux/sched.h) -- the code
 *  in sched.c operates on tcb.state/tcb.counter/tcb.priority.
 *
 *  A kernel thread is a task_struct that never enters user mode.  Its
 *  TSS points eip at an assembly trampoline which calls the C kthread()
 *  wrapper. The wrapper runs fn(arg) and passes its result to kthread_exit().
 *  Threads yield voluntarily (the 0.11 timer never preempts CPL0 code) and
 *  are joined with kthread_join(), which also reclaims the task slot.
 *
 *  Threads do not use do_exit(): that path assumes user memory
 *  (lsll on the LDT, free_page_tables on LDT bases).  kthread_exit()
 *  instead marks the TCB zombie and wakes joiners, and reaping is
 *  exclusively done by kthread_join(), never by sys_waitpid.
 */

#include <errno.h>
#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

/*
 * First code a kernel thread executes, entered by TSS task switch with
 * an empty stack and EBX pointing to kthread(). CALL supplies the return
 * address required by the C calling convention; kthread() never returns.
 */
__asm__(".align 2\n"
"_kthread_entry_asm:\n\t"
	"call *%ebx");			/* enter the C wrapper */

extern void kthread_entry_asm(void);	/* its address only */

/* Worker invocation and exit handling belong in C, after assembly entry. */
static void kthread(void)
{
	long ret = current->kthread.entry(current->kthread.arg);

	kthread_exit(ret);
}

struct task_struct *kthread_create(long (*fn)(void *), void *arg,
					  const char *name)
{
	struct task_struct *p;
	int nr;

	if ((nr = find_empty_process()) < 0)
		return NULL;
	p = alloc_task(nr, task[0]);	/* kernel template, no caller's files */
	if (!p)
		return NULL;

	p->tcb.counter = p->tcb.priority = 15;
	strncpy(p->kthread.name, name, sizeof(p->kthread.name) - 1);
	p->kthread.name[sizeof(p->kthread.name) - 1] = '\0';
	p->kthread.entry = fn;
	p->kthread.arg = arg;
	p->kthread.wait_head = NULL;	/* join() sleepers queue here */

	p->tss.eip = (unsigned long) kthread_entry_asm;
	p->tss.ebx = (unsigned long) kthread;
	p->tss.eflags = 0x202;		/* IF must be set: a task switch loads
					   EFLAGS wholesale from the TSS */
	p->tss.esp = p->tss.esp0;
	p->tss.cs = 0x08;		/* kernel segments only */
	p->tss.ss = 0x10;
	p->tss.ds = p->tss.es = p->tss.fs = p->tss.gs = 0x10;
	/* No user address space. Interrupt handlers preserve the incoming
	 * FS selector, so they do not load these absent user descriptors. */
	p->ldt[1].a = 0; p->ldt[1].b = 0;
	p->ldt[2].a = 0; p->ldt[2].b = 0;
	start_task(nr, p);
	return p;
}

int kernel_thread(long (*fn)(void *), void *arg, const char *name)
{
	struct task_struct *p = kthread_create(fn, arg, name);

	return p ? p->tgid : -EAGAIN;
}

void kthread_exit(long code)
{
	current->exit_code = code;
	current->tcb.state = TASK_ZOMBIE;
	wake_up(&current->kthread.wait_head);
	for (;;)
		schedule();
}

int kthread_join(struct task_struct *t)
{
	int i, code;

	while (t->tcb.state != TASK_ZOMBIE)
		sleep_on(&t->kthread.wait_head);
	code = t->exit_code;
	for (i = 1; i < NR_TASKS; i++)
		if (task[i] == t) {
			task[i] = NULL;
			/* sched_init()'s convention: an unused slot's GDT
			   TSS/LDT descriptors must be zeroed.  A stale
			   descriptor would keep pointing into the page we
			   are about to free (a known latent bug of 0.11's
			   release(), which we do not copy). */
			(gdt+(i<<1)+FIRST_TSS_ENTRY)->a = 0;
			(gdt+(i<<1)+FIRST_TSS_ENTRY)->b = 0;
			(gdt+(i<<1)+FIRST_LDT_ENTRY)->a = 0;
			(gdt+(i<<1)+FIRST_LDT_ENTRY)->b = 0;
			break;
		}
	free_page((unsigned long) t);
	return code;
}
