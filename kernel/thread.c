/* Process lifetime, shared memory ownership and deferred task reclamation. */
#include <errno.h>
#include <signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int new_thread_group(struct task_struct *p)
{
	struct thread_group *g;

	g = (struct thread_group *) get_free_page();
	if (!g)
		return -ENOMEM;
	g->refs = g->live = 1;
	g->tgid = p->tgid;
	g->base = p->start_code;
	g->leader = p;
	p->group = g;
	return 0;
}

void reap_dead_tasks(void)
{
	int i;
	for (i=1; i<NR_TASKS; i++)
		if (task[i] && task[i] != current &&
		    task[i]->tcb.state == TASK_DEAD)
			release(task[i]);
}

void wake_thread_group(struct thread_group *g)
{
	int i;
	for (i=1; i<NR_TASKS; i++)
		if (task[i] && task[i]->group == g &&
		    task[i]->tcb.state == TASK_SLEEP_INTERRUPTIBLE)
			wake_up_process(task[i]);
}

void notify_parent(struct thread_group *g)
{
	struct task_struct *parent = g->leader->real_parent;

	if (parent && parent->group) {
		parent->signal |= 1 << (SIGCHLD-1);
		/* waitpid must also wake when SIGCHLD is blocked/ignored. */
		wake_thread_group(parent->group);
	}
}

/* Called after this thread has closed its own files and inode references. */
void finish_thread(long code)
{
	struct thread_group *g = current->group;
	int i;

	if (!g)
		panic("user exit without a thread group");
	g->utime += current->utime;
	g->stime += current->stime;
	if (current == g->leader)
		g->exit_code = code;
	current->exit_code = code;
	current->tcb.state = current == g->leader ? TASK_ZOMBIE : TASK_DEAD;
	g->live--;
	reparent_children(current);
	if (!g->live) {
		if (g->tgid == 1)
			panic("init exited");
		free_page_tables(g->base, 0x4000000);
		if (g->leader->leader) {
			if (g->leader->tty >= 0)
				tty_table[g->leader->tty].pgrp = 0;
			for (i=1; i<NR_TASKS; i++)
				if (task[i] && task[i]->session == current->session)
					task[i]->signal |= 1 << (SIGHUP-1);
		}
		notify_parent(g);
	}
	wake_thread_group(g);
	/* Never free the stack or the loaded TSS while still executing on it. */
	for (;;)
		schedule();
}
