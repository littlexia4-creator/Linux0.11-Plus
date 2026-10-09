/*
 *  kernel/kthread.c
 *
 *  Kernel threads for Linux 0.11: cooperative ring-0 tasks that share
 *  the kernel address space.  The scheduling state of every task lives
 *  in its embedded TCB (struct tcb, include/linux/sched.h) -- the code
 *  in sched.c operates on tcb.state/tcb.counter/tcb.priority.
 *
 *  A kernel thread is a task_struct that never enters user mode.  Its
 *  TSS points eip at a small trampoline which calls fn(arg) and hands
 *  the return value to kthread_exit().  Threads yield voluntarily
 *  (the 0.11 timer never preempts CPL0 code) and are joined with
 *  kthread_join(), which also reclaims the task slot.
 *
 *  Threads do not use do_exit(): that path assumes user memory
 *  (lsll on the LDT, free_page_tables on LDT bases).  kthread_exit()
 *  instead marks the TCB zombie and wakes joiners, and reaping is
 *  exclusively done by kthread_join(), never by sys_waitpid.
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

extern int find_empty_process(void);	/* kernel/task.c, sets last_pid */
extern long last_pid;

/*
 * First code a kernel thread executes, entered by TSS task switch.
 * kernel_thread() lays out [fn][arg] at the top of the thread's stack
 * and points tss.esp at it:
 *
 *	esp ->	fn	arg
 *		 ^popped into eax, arg into ebx, then a cdecl call fn(arg).
 */
__asm__(".align 2\n"
"_kthread_trampoline:\n\t"
	"popl %eax\n\t"			/* fn */
	"popl %ebx\n\t"			/* arg */
	"pushl %ebx\n\t"
	"call *%eax\n\t"		/* eax = fn(arg): return value = exit code */
	"pushl %eax\n\t"
	"call _kthread_exit");		/* never returns */

extern void kthread_trampoline(void);	/* its address only */

static struct task_struct *kthread_create(long (*fn)(void *), void *arg,
					  const char *name)
{
	struct task_struct *p;
	int i, nr;
	unsigned long stack_top;

	if ((nr = find_empty_process()) < 0)
		return NULL;
	p = (struct task_struct *) get_free_page();
	if (!p)
		return NULL;
	task[nr] = p;
	*p = *current;			/* clean template (task 0 at boot) */

	p->pid = last_pid;
	p->father = current->pid;
	p->tcb.counter = p->tcb.priority = 15;
	p->signal = 0;
	p->alarm = 0;
	p->utime = p->stime = p->cutime = p->cstime = 0;
	p->start_time = jiffies;
	for (i = 0; i < (int) sizeof(p->kthread.name) - 1 && name[i]; i++)
		p->kthread.name[i] = name[i];
	p->kthread.name[i] = 0;
	p->kthread.entry = fn;
	p->kthread.arg = arg;
	p->kthread.wait_head = NULL;	/* join() sleepers queue here */

	stack_top = PAGE_SIZE + (unsigned long) p;
	((unsigned long *) stack_top)[-1] = (unsigned long) fn;	/* [esp]   */
	((unsigned long *) stack_top)[-2] = (unsigned long) arg;	/* [esp+4] */
	p->tss.back_link = 0;
	p->tss.esp0 = stack_top;	/* CPL0 threads take interrupts on the
					   current stack; kept for consistency */
	p->tss.ss0 = 0x10;
	p->tss.eip = (unsigned long) kthread_trampoline;
	p->tss.eflags = 0x202;		/* IF must be set: a task switch loads
					   EFLAGS wholesale from the TSS */
	p->tss.esp = stack_top - 8;
	p->tss.cs = 0x08;		/* kernel segments only */
	p->tss.ss = 0x10;
	p->tss.ds = p->tss.es = p->tss.fs = p->tss.gs = 0x10;
	/*
	 * Null LDT entries: a kernel thread never runs user code, and
	 * leaving inherited descriptors here poisons the kernel once two
	 * or more threads exist -- user-mode address arithmetic such as
	 * verify_area()/get_base(current->ldt[2]) then aliases onto base
	 * 0 and writes land outside every mapped limit (observed as
	 * Bochs "write beyond limit" storms and page-fault cascades in
	 * block_write under the login shell; a single thread happened to
	 * get away with it).  Trade-off: die()'s lsll on selector 0x0f
	 * would fault on a null entry, but that only matters if a kernel
	 * thread crashes, which is fatal anyway.
	 */
	p->ldt[1].a = 0; p->ldt[1].b = 0;
	p->ldt[2].a = 0; p->ldt[2].b = 0;
	p->tss.ldt = _LDT(nr);
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->tcb.state = TASK_RUNNING;	/* do this last, just in case */
	return p;
}

int kernel_thread(long (*fn)(void *), void *arg, const char *name)
{
	struct task_struct *p = kthread_create(fn, arg, name);

	return p ? p->pid : -EAGAIN;
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

/* ---------------- demo, started from init/main.c before sti() -------- */

/*
 * Voluntary yield.  A bare schedule() is not enough to round-robin
 * between two runnable threads: 0.11 counters only decay through timer
 * ticks, so a tie (both 15) always picks the same task.  Dropping our
 * counter to 0 lets the other runnable thread win, and the recompute
 * pass in schedule() restores us via counter/2 + priority.
 */
static void kthread_yield(void)
{
	current->tcb.counter = 0;
	schedule();
}

static long worker(void *unused)
{
	int i;

	for (i = 1; i <= 5; i++) {
		printk("[%s] step %d\n", current->kthread.name, i);
		kthread_yield();
	}
	return i - 1;
}

static long coordinator(void *unused)
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
	/*
	 * Park forever instead of exiting: the demo runs before init's
	 * first fork, so this thread holds pid 1 / slot 1; a zombie there
	 * would collect orphan reparenting meant for init.  The slot stays
	 * allocated -- that is the documented cost of this demo.
	 */
	sleep_on(&current->kthread.wait_head);	/* never woken */
	return 0;				/* not reached */
}

/*
 * Syscall 73: run the kernel-thread demo on demand (post-login),
 * where the system is fully booted and the scheduler settles.
 */
int sys_kthread_demo(void)
{
	printk("kthread demo: starting coordinator\n");
	kernel_thread(coordinator, NULL, "kcoord");
	return 0;
}
