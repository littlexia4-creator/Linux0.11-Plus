/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>
#include <errno.h>

#define SIGBIT(n) (1UL << ((n)-1))
#define STOPMASK (SIGBIT(SIGSTOP)|SIGBIT(SIGTSTP)|SIGBIT(SIGTTIN)|SIGBIT(SIGTTOU))
#define IS_STOP_SIGNAL(sig) (SIGBIT(sig) & STOPMASK)

/* SIGCONT resumes stopped members even when blocked or ignored. */
static void continue_thread_group(struct thread_group *g)
{
	int i;

	clear_group_pending(g, STOPMASK);
	g->stopped = 0;
	for (i=1; i<NR_TASKS; i++)
		if (task[i] && task[i]->group == g) {
			clear_task_pending(task[i], STOPMASK);
			if (task[i]->tcb.state == TASK_STOPPED)
				task[i]->tcb.state = TASK_RUNNING;
		}
}

void send_group_signal(struct thread_group *g, int sig)
{
	if (!g || !g->live || !sig)
		return;
	if (sig == SIGKILL) {
		request_group_exit(g, SIGKILL);
		return;
	}
	if (sig == SIGCONT) {
		continue_thread_group(g);
	} else if (IS_STOP_SIGNAL(sig)) {
		clear_group_pending(g, SIGBIT(SIGCONT));
	}
	if (g->action[sig-1].sa_handler != SIG_IGN)
		set_group_pending(g, SIGBIT(sig));
}

/* Called only on return to user mode, with the interrupted registers saved. */
int get_signal(void)
{
	struct thread_group *g = current->group;
	unsigned long pending, bit, flags;
	int sig, result = 0;

	for (;;) {
		if (g && (g->exiting || (g->exec_task && g->exec_task != current)))
			do_exit(g->exiting ? g->exit_code : 0);
		if (!g || !g->stopped)
			break;
		current->tcb.state = TASK_STOPPED;
		schedule();
	}
	/* An IRQ may post a process signal while we select/clear a bit. */
	__asm__ volatile ("pushfl; popl %0; cli" : "=r" (flags) : : "memory");
	pending = pending_signals(current) & ~current->blocked;
	for (sig=1; sig<=32; sig++) {
		bit = SIGBIT(sig);
		if (pending & bit) {
			if (current->signal & bit)
				clear_task_pending(current, bit);
			else
				clear_group_pending(g, bit);
			result = sig;
			break;
		}
	}
	__asm__ volatile ("pushl %0; popfl" : : "r" (flags) : "memory", "cc");
	return result;
}

int sys_sgetmask()
{
	return current->blocked;
}

int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & ~(SIGBIT(SIGKILL)|SIGBIT(SIGSTOP));
	return old;
}

static inline void save_old(char * from,char * to)
{
	int i;

	verify_area(to, sizeof(struct sigaction));
	for (i=0 ; i< sizeof(struct sigaction) ; i++) {
		put_fs_byte(*from,to);
		from++;
		to++;
	}
}

static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}

int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL || signum==SIGSTOP)
		return -1;
	tmp.sa_handler = (void (*)(int)) handler;
	tmp.sa_mask = 0;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
	tmp.sa_restorer = (void (*)(void)) restorer;
	handler = (long) task_actions(current)[signum-1].sa_handler;
	task_actions(current)[signum-1] = tmp;
	if (tmp.sa_handler == SIG_IGN && current->group)
		clear_group_pending(current->group, SIGBIT(signum));
	return handler;
}

int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp, new_action;

	if (signum<1 || signum>32 || signum==SIGKILL || signum==SIGSTOP)
		return -1;
	if ((action && !user_range((unsigned long)action, sizeof(*action))) ||
	    (oldaction && !user_range((unsigned long)oldaction, sizeof(*oldaction))))
		return -EFAULT;
	tmp = task_actions(current)[signum-1];
	if (action) {
		get_new((char *) action, (char *) &new_action);
		if (new_action.sa_flags & SA_NOMASK)
			new_action.sa_mask = 0;
		else
			new_action.sa_mask |= SIGBIT(signum);
		new_action.sa_mask &= ~(SIGBIT(SIGKILL)|SIGBIT(SIGSTOP));
		task_actions(current)[signum-1] = new_action;
		if (new_action.sa_handler == SIG_IGN && current->group)
			clear_group_pending(current->group, SIGBIT(signum));
	}
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
	return 0;
}

/* Matches the saved-register/iret layout in system_call.s. Writing through
 * this pointer is observable; assignments to by-value C arguments are not. */
struct user_return_frame {
	unsigned long eax, ebx, ecx, edx, fs, es, ds;
	unsigned long eip, cs, eflags, esp, ss;
};

void do_signal(long signr, struct user_return_frame *regs)
{
	unsigned long sa_handler;
	long old_eip=regs->eip;
	struct sigaction action = task_actions(current)[signr-1];
	struct sigaction *sa = &action;
	int longs;
	unsigned long * tmp_esp;

	sa_handler = (unsigned long) sa->sa_handler;
	if (sa_handler==1)
		return;
	if (!sa_handler) {
		if (signr==SIGCHLD || signr==SIGCONT)
			return;
		if (IS_STOP_SIGNAL(signr)) {
			current->group->stopped = signr;
			current->group->stop_report = signr;
			notify_parent(current->group);
			while (current->group->stopped && !current->group->exiting &&
			       !current->group->exec_task) {
				current->tcb.state = TASK_STOPPED;
				schedule();
			}
			if (current->group->exiting || current->group->exec_task)
				do_exit(current->group->exit_code);
			return;
		}
		do_group_exit(signr);
	}
	if (sa->sa_flags & SA_ONESHOT)
		task_actions(current)[signr-1].sa_handler = NULL;
	longs = (sa->sa_flags & SA_NOMASK)?7:8;
	if (regs->esp < longs*4 || !user_range(regs->esp-longs*4, longs*4))
		do_group_exit(SIGSEGV);
	verify_area((void *)(regs->esp-longs*4),longs*4);
	regs->eip = sa_handler;
	regs->esp -= longs*4;
	tmp_esp=(unsigned long *)regs->esp;
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++);
	put_fs_long(regs->eax,tmp_esp++);
	put_fs_long(regs->ecx,tmp_esp++);
	put_fs_long(regs->edx,tmp_esp++);
	put_fs_long(regs->eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	current->blocked |= sa->sa_mask;
}
