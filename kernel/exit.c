/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

int sys_close(unsigned int fd);

void release(struct task_struct * p)
{
	int i;

	if (!p)
		return;
	if (p == current)
		panic("release of current task");
	for (i=1 ; i<NR_TASKS ; i++)
		if (task[i]==p) {
			task[i]=NULL;
			(gdt+(i<<1)+FIRST_TSS_ENTRY)->a = 0;
			(gdt+(i<<1)+FIRST_TSS_ENTRY)->b = 0;
			(gdt+(i<<1)+FIRST_LDT_ENTRY)->a = 0;
			(gdt+(i<<1)+FIRST_LDT_ENTRY)->b = 0;
			if (p->group && !--p->group->refs)
				free_page((long)p->group);
			free_page((long)p);
			return;
		}
	panic("trying to release non-existent task");
}

/* kill selects processes; one eligible peer consumes a shared signal. */
int sys_kill(int pid,int sig)
{
	struct task_struct *p;
	int i, result = -ESRCH, sent = 0;
	if (sig < 0 || sig > 32)
		return -EINVAL;
	for (i=1; i<NR_TASKS; i++) {
		p = task[i];
		if (!p || !p->group || p != p->group->leader || !p->group->live)
			continue;
		if ((pid>0 && p->tgid!=pid) ||
		    (!pid && p->pgrp!=current->pgrp) ||
		    (pid < -1 && p->pgrp != -pid) ||
		    (pid == -1 && (p->tgid == 1 || p->tgid == current->tgid)))
			continue;
		if (!suser() && current->uid != p->uid && current->euid != p->uid &&
		    current->uid != p->suid && current->euid != p->suid &&
		    !(sig == SIGCONT && current->session == p->session)) {
			result = -EPERM;
			continue;
		}
		send_group_signal(p->group, sig);
		sent = 1;
	}
	return sent ? 0 : result;
}

/* Move children to a live peer, or init when the parent process ends.
 * Run before a task can be reaped; raw task pointers hold no reference. */
void reparent_children(struct task_struct *parent)
{
	struct task_struct *p, *reaper = task[0];
	int i;

	if (parent->group)
		for (i=1; i<NR_TASKS; i++) {
			p = task[i];
			if (p && p != parent && p->group == parent->group &&
			    p->tcb.state != TASK_ZOMBIE && p->tcb.state != TASK_DEAD) {
				reaper = p;
				break;
			}
		}
	/* Find init by identity: a nonleader exec can move its task slot. */
	if (reaper == task[0])
		for (i=1; i<NR_TASKS; i++) {
			p = task[i];
			if (p && p != parent && p->tgid == 1 && p->group &&
			    p->tcb.state != TASK_ZOMBIE && p->tcb.state != TASK_DEAD) {
				reaper = p;
				break;
			}
		}
	for (i=1; i<NR_TASKS; i++) {
		p = task[i];
		if (!p || p->real_parent != parent)
			continue;
		p->real_parent = reaper;
		if (p->group && p == p->group->leader && !p->group->live &&
		    parent->group != reaper->group)
			notify_parent(p->group);
	}
}

int do_exit(long code)
{
	int i;
	for (i=0 ; i<NR_OPEN ; i++)
		if (current->filp[i])
			sys_close(i);
	iput(current->pwd);
	current->pwd=NULL;
	iput(current->root);
	current->root=NULL;
	iput(current->executable);
	current->executable=NULL;
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	finish_thread(code);
	return (-1);	/* just to suppress warnings */
}

/* The raw exit syscall exits one thread; exit_group exits the process. */
int sys_exit(int error_code)
{
	return do_exit((error_code&0xff)<<8);
}

/* Any peer can wait for a child process. Peer threads are never children. */
int sys_waitpid(pid_t pid,unsigned long * status, int options)
{
	struct task_struct *p;
	struct thread_group *g;
	int i, found, code, result;
	if (status) {
		if (!user_range((unsigned long)status, 4))
			return -EFAULT;
		verify_area(status, 4);
	}
repeat:
	found = 0;
	for (i=1; i<NR_TASKS; i++) {
		p = task[i];
		if (!p || !(g=p->group) || p != g->leader ||
		    !p->real_parent || p->real_parent->tgid != current->tgid)
			continue;
		if ((pid>0 && g->tgid!=pid) ||
		    (!pid && p->pgrp!=current->pgrp) ||
		    (pid < -1 && p->pgrp != -pid))
			continue;
		if (!g->live) {
			current->group->cutime += g->utime + g->cutime;
			current->group->cstime += g->stime + g->cstime;
			result = g->tgid;
			code = g->exit_code;
			reap_dead_tasks();
			release(p);
			if (status)
				put_fs_long(code, status);
			return result;
		}
		if (g->stop_report && (options & WUNTRACED)) {
			code = (g->stop_report << 8) | 0x7f;
			g->stop_report = 0;
			if (status)
				put_fs_long(code, status);
			return g->tgid;
		}
		found = 1;
	}
	if (!found)
		return -ECHILD;
	if (options & WNOHANG)
		return 0;
	code = pending_signals(current) & ~current->blocked;
	if (task_actions(current)[SIGCHLD-1].sa_handler == SIG_DFL ||
	    task_actions(current)[SIGCHLD-1].sa_handler == SIG_IGN) {
		code &= ~(1 << (SIGCHLD-1));
		clear_task_pending(current, 1UL << (SIGCHLD-1));
		clear_group_pending(current->group, 1UL << (SIGCHLD-1));
	}
	if (code)
		return -EINTR;
	current->tcb.state=TASK_SLEEP_INTERRUPTIBLE;
	schedule();
	goto repeat;
}


