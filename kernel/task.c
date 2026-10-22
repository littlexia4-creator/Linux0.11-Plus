/*
 *  linux/kernel/task.c  (fork.c in the original 0.11 tree)
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'task.c' contains the task-creation machinery: copy_process() for
 * the 'fork' system call, plus shared allocation and startup helpers.
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h>

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
 * Reserve the slot before further setup, but keep the task unrunnable
 * until start_task(). On failure the caller must clear task[nr] and
 * free the page.
 */
struct task_struct *alloc_task(int nr, const struct task_struct *template)
{
	struct task_struct *p;

	p = (struct task_struct *) get_free_page();
	if (!p)
		return NULL;
	*p = *template;
	p->tcb.state = TASK_UNINTERRUPTIBLE;
	p->tid = last_tid;
	p->tgid = p->tid;	/* fork and kernel threads start a new process */
	p->real_parent = current;
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

/* Call only after registers, memory and resource ownership are ready. */
void start_task(int nr, struct task_struct *p)
{
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->tcb.state = TASK_RUNNING;	/* do this last, just in case */
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

int find_empty_process(void)
{
	int i;

	repeat:
		if (last_tid >= 0x7fffffff) last_tid=1;
		else ++last_tid;
		for(i=0 ; i<NR_TASKS ; i++)
			/* Reserve the TGID even after its leader is reaped. */
			if (task[i] && (task[i]->tid == last_tid ||
			    task[i]->tgid == last_tid)) goto repeat;
	for(i=1 ; i<NR_TASKS ; i++)
		if (!task[i])
			return i;
	return -EAGAIN;
}
