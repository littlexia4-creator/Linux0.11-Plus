/*
 *  linux/kernel/task.c  (fork.c in the original 0.11 tree)
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'task.c' contains the task-creation routines: copy_process() for
 * the 'fork' syscall (processes) and do_clone() for 'clone' (user
 * threads), plus shared setup and activation for kernel threads.
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h>
#include <sched.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long address);

long last_tid=0;

/*
 * Allocate a task after find_empty_process() selects nr/tid.
 * User tasks inherit current; kernel threads use task[0] as their template.
 * Only task_struct is copied, not the supervisor stack in the same page.
 * Reserve the slot before setup can sleep (e.g. a clone stack page fault),
 * but keep the task unrunnable until explicitly woken. On failure the caller
 * must clear task[nr] and free the page.
 */
struct task_struct *alloc_task(int nr, const struct task_struct *template)
{
	struct task_struct *p;

	p = (struct task_struct *) get_free_page();
	if (!p)
		return NULL;
	*p = *template;
	p->tcb.state = TASK_SLEEP_UNINTERRUPTIBLE;
	p->tid = last_tid;
	p->tgid = p->tid;	/* fork and kernel threads start a new process */
	p->real_parent = current;
	p->group = NULL;
	p->futex_uaddr = 0;
	p->tcb.counter = p->tcb.priority;
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = p->cutime = p->cstime = 0;
	p->start_time = jiffies;
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (unsigned long) p;
	p->tss.ss0 = 0x10;
	p->tss.ldt = _LDT(nr);
	p->tss.trace_bitmap = 0x80000000;
	task[nr] = p;
	return p;
}

/* Install descriptors while the fully initialized task remains asleep. */
void setup_task(int nr, struct task_struct *p)
{
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
}

/* Fork and clone make the child runnable as soon as setup is complete. */
void start_task(int nr, struct task_struct *p)
{
	setup_task(nr, p);
	wake_up_process(p);	/* do this last, just in case */
}

void verify_area(void * addr,int size)
{
	unsigned long start;

	start = (unsigned long) addr;
	size += start & 0xfff;
	start &= 0xfffff000;
	start += get_base(current->ldt[2]);
	while (size>0) {
		size -= 4096;
		write_verify(start);
		start += 4096;
	}
}

int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;

	code_limit=get_limit(0x0f);
	data_limit=get_limit(0x17);
	old_code_base = get_base(current->ldt[1]);
	old_data_base = get_base(current->ldt[2]);
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	if (data_limit < code_limit)
		panic("Bad data_limit");
	new_data_base = new_code_base = nr * 0x4000000;
	p->start_code = new_code_base;
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
		printk("free_page_tables: from copy_mem\n");
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	p = alloc_task(nr, current);
	if (!p)
		return -EAGAIN;
	p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0;
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}
	if (new_thread_group(p)) {
		free_page_tables(p->start_code, get_limit(0x17));
		task[nr] = NULL;
		free_page((long) p);
		return -ENOMEM;
	}
	for (i=0; i<NR_OPEN;i++)
		if ((f=p->filp[i]))
			f->f_count++;
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	start_task(nr, p);
	return p->tgid;
}

/*
 * Raw clone(fn, arg, stack_top, flags) -- create a user thread.
 * ebx/ecx/edx carry fn/arg/stack_top and the saved esi carries flags.
 * The C wrapper exposes clone(fn, stack_top, flags, arg).
 *
 * Same as copy_process except:
 *  - copy_mem() is skipped: the child inherits the parent's LDT
 *    descriptors and start_code, so both tasks use the same page
 *    tables (writes to shared pages are plain writes -- pages stay
 *    RW because copy_page_tables never marks them read-only here).
 *  - the child starts at fn with a fresh user stack; we build its
 *    first frame in already-shared memory through the parent's fs:
 *      [esp]   = 0    invalid return address: the entry must call sys_exit
 *      [esp+4] = arg  cdecl argument for fn(arg)
 *    lib/clone.c supplies an entry wrapper that exits after the callback.
 */
int do_clone(long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i, nr, error;
	struct file *f;

	/* This kernel supports one sharing policy; reject all other masks. */
	if (esi != (CLONE_VM | CLONE_SIGHAND | CLONE_THREAD))
		return -EINVAL;
	if (!current->group || current->group->exiting ||
	    current->group->exec_task)
		return -EAGAIN;
	if (!ebx || (unsigned long) ebx >= get_limit(0x0f) ||
	    edx < 8 || !user_range(edx - 8, 8))
		return -EFAULT;
	/* Write the frame before reserving a task or group reference:
	 * verify_area handles COW, but does not fault in missing pages. */
	verify_area((void *)(edx - 8), 8);
	put_fs_long(0, (unsigned long *) (edx - 8));
	put_fs_long(ecx, (unsigned long *) (edx - 4));
	if ((nr = find_empty_process()) < 0)
		return nr;
	p = alloc_task(nr, current);
	if (!p)
		return -EAGAIN;
	if ((error = join_thread_group(p))) {
		task[nr] = NULL;
		free_page((long) p);
		return error;
	}
	p->tss.eip = ebx;		/* thread entry point */
	p->tss.eflags = eflags;
	p->tss.eax = 0;
	p->tss.esp = edx - 8;		/* thread's private user stack */
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	for (i=0; i<NR_OPEN;i++)
		if ((f=p->filp[i]))
			f->f_count++;
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	start_task(nr, p);
	return p->tid;
}

int find_empty_process(void)
{
	int i, j, used;

	reap_dead_tasks();
	repeat:
		if (last_tid >= 0x7fffffff) last_tid=1;
		else ++last_tid;
		for(i=0 ; i<NR_TASKS ; i++)
			/* Reserve the TGID even after its leader is reaped. */
			if (task[i] && (task[i]->tid == last_tid ||
			    task[i]->tgid == last_tid)) goto repeat;
	for(i=1 ; i<NR_TASKS ; i++) {
		if (task[i])
			continue;
		/* A nonleader exec may retain the original leader's VM slot. */
		used = 0;
		for (j=1; j<NR_TASKS; j++)
			if (task[j] && task[j]->group && task[j]->group->live &&
			    task[j]->group->base == (unsigned long)i * 0x4000000)
				used = 1;
		if (!used)
			return i;
	}
	return -EAGAIN;
}
