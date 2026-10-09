/* Fixed clone policy: shared VM, signal dispositions and thread group. */
#include <errno.h>
#include <signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

static struct thread_completion *completion(struct thread_group *g, long tid)
{
	int i;
	for (i=0; i<NR_TASKS; i++)
		if (g->completion[i].tid == tid)
			return &g->completion[i];
	return NULL;
}

int new_thread_group(struct task_struct *p)
{
	struct thread_group *g;
	int i;

	g = (struct thread_group *) get_free_page();
	if (!g)
		return -ENOMEM;
	g->refs = g->live = 1;
	g->tgid = p->tgid;
	g->base = p->start_code;
	g->leader = p;
	for (i=0; i<32; i++)
		g->action[i] = task_actions(current)[i];
	g->completion[0].tid = p->tid;
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
		    task[i]->tcb.state == TASK_INTERRUPTIBLE)
			wake_up_process(task[i]);
}

void notify_parent(struct thread_group *g)
{
	struct task_struct *parent = g->leader->real_parent;

	if (parent && parent->group) {
		send_group_signal(parent->group, SIGCHLD);
		/* waitpid must also wake when SIGCHLD is blocked/ignored. */
		wake_thread_group(parent->group);
	}
}

void request_group_exit(struct thread_group *g, int code)
{
	int i;
	if (!g->exiting) {
		g->exiting = 1;
		g->exit_code = code;
	}
	g->stopped = 0;
	for (i=1; i<NR_TASKS; i++)
		if (task[i] && task[i]->group == g &&
		    task[i]->tcb.state != TASK_ZOMBIE &&
		    task[i]->tcb.state != TASK_DEAD) {
			task[i]->signal |= 1 << (SIGKILL-1);
			if (task[i]->tcb.state == TASK_STOPPED)
				task[i]->tcb.state = TASK_RUNNING;
		}
	wake_thread_group(g);
}

int do_group_exit(long code)
{
	if (current->group)
		request_group_exit(current->group, code);
	return do_exit(code);
}

/* Called after this thread has closed its own files and inode references. */
void finish_thread(long code)
{
	struct thread_group *g = current->group;
	struct thread_completion *c;
	int i;

	if (!g)
		panic("user exit without a thread group");
	c = completion(g, current->tid);
	if (c) {
		c->status = (code >> 8) & 255;
		c->done = 1;
	}
	for (i=0; i<NR_TASKS; i++)
		if (g->completion[i].joiner == current->tid)
			g->completion[i].joiner = 0;
	g->utime += current->utime;
	g->stime += current->stime;
	if (current == g->leader && !g->exiting)
		g->exit_code = code;
	current->exit_code = code;
	current->tcb.state = current == g->leader ? TASK_ZOMBIE : TASK_DEAD;
	g->live--;
	reparent_children(current);
	if (!g->live) {
		if (g->tgid == 1)
			panic("init exited");
		free_page_tables(g->base, 0x4000000);
		g->alarm = 0;
		if (g->leader->leader) {
			if (g->leader->tty >= 0)
				tty_table[g->leader->tty].pgrp = 0;
			for (i=1; i<NR_TASKS; i++)
				if (task[i] && task[i]->group &&
				    task[i] == task[i]->group->leader &&
				    task[i]->session == current->session)
					send_group_signal(task[i]->group, SIGHUP);
		}
		notify_parent(g);
	}
	wake_thread_group(g);
	/* Never free the stack or the loaded TSS while still executing on it. */
	for (;;)
		schedule();
}

/* Commit exec only after validation and argument copying have succeeded. */
int de_thread(void)
{
	struct thread_group *g = current->group;
	struct task_struct *old;
	int i;

	if (g->exiting || (g->exec_task && g->exec_task != current))
		return -EINTR;
	g->exec_task = current;
	g->stopped = 0;
	for (i=1; i<NR_TASKS; i++)
		if (task[i] && task[i] != current && task[i]->group == g &&
		    task[i]->tcb.state != TASK_ZOMBIE && task[i]->tcb.state != TASK_DEAD) {
			task[i]->signal |= 1 << (SIGKILL-1);
			if (task[i]->tcb.state == TASK_STOPPED)
				task[i]->tcb.state = TASK_RUNNING;
		}
	wake_thread_group(g);
	while (g->live > 1 && !g->exiting) {
		current->tcb.state = TASK_INTERRUPTIBLE;
		schedule();
	}
	if (g->exiting) {
		g->exec_task = NULL;
		return -EINTR;
	}
	old = g->leader;
	current->leader = old->leader;
	g->leader = current;
	current->tid = g->tgid;
	if (old != current)
		old->tcb.state = TASK_DEAD;
	reap_dead_tasks();
	for (i=0; i<NR_TASKS; i++) {
		g->completion[i].tid = g->completion[i].joiner = 0;
		g->completion[i].done = g->completion[i].status = 0;
	}
	g->completion[0].tid = current->tid;
	g->exit_code = g->stop_report = 0;
	g->exec_task = NULL;
	return 0;
}
