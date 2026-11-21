/* System V semaphore sets, operated on by user-space semop(). Syscall entry
 * holds preempt_defer across table access and voluntary sleeps. These tables
 * have no interrupt users, so the single-CPU kernel needs no IRQ masking here. */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sem.h>
#include <asm/segment.h>

struct sem_queue_node {
	struct sem_queue_node *next;
	struct task_struct *task;
	struct sembuf *ops;
	unsigned int nops;
	int result; /* 1 while pending, 0 on success, negative errno on failure */
	int number, zero; /* first unsatisfied operation, for GETNCNT/GETZCNT */
};

struct sem_queue {
	struct sem_queue_node *first, *last;
};

struct sem_array {
	int id;
	struct semid_ds ds;
	unsigned short values[SEMMSL];
	pid_t pids[SEMMSL];
	struct sem_queue waiters;
};

static struct sem_array sets[SEMMNI];
static unsigned long next_id = 1;

static struct sem_array *find_set(int id)
{
	int i;

	for (i=0; i<SEMMNI; i++)
		if (id > 0 && sets[i].id == id)
			return &sets[i];
	return NULL;
}

static void link_waiter(struct sem_array *set, struct sem_queue_node *waiter)
{
	waiter->next = NULL;
	if (set->waiters.last)
		set->waiters.last->next = waiter;
	else
		set->waiters.first = waiter;
	set->waiters.last = waiter;
}

static void unlink_waiter(struct sem_array *set, struct sem_queue_node *waiter)
{
	struct sem_queue_node *p, *previous = NULL;

	for (p=set->waiters.first; p; p=p->next) {
		if (p == waiter) {
			if (previous)
				previous->next = p->next;
			else
				set->waiters.first = p->next;
			if (set->waiters.last == p)
				set->waiters.last = previous;
			return;
		}
		previous = p;
	}
}

static void snapshot_sem_values(struct sem_array *set, int *values)
{
	unsigned int i;

	for (i=0; i<set->ds.sem_nsems; i++)
		values[i] = set->values[i];
}

static void commit_sem_values(struct sem_array *set, const int *values)
{
	unsigned int i;

	for (i=0; i<set->ds.sem_nsems; i++)
		set->values[i] = values[i];
}

static void init_sem_request(struct sem_queue_node *q, struct sembuf *ops,
			     unsigned int nops)
{
	q->task = current;
	q->ops = ops;
	q->nops = nops;
}

/* All-or-nothing, in array order.
 * Returns
	0 on success,
	1 if the caller must wait,
	-EAGAIN if IPC_NOWAIT prevents waiting,
	-ERANGE if a value would exceed SEMVMX. */
static int try_semop(struct sem_array *set, struct sem_queue_node *q)
{
	int values[SEMMSL];
	int number, change;
	unsigned int i;

	snapshot_sem_values(set, values);
	for (i=0; i<q->nops; i++) {
		number = q->ops[i].sem_num;
		change = q->ops[i].sem_op;
		if ((!change && values[number]) || values[number] + change < 0) {
			q->number = number;
			q->zero = !change;
			return q->ops[i].sem_flg & IPC_NOWAIT ? -EAGAIN : 1;
		}
		if (values[number] + change > SEMVMX)
			return -ERANGE;
		values[number] += change;
	}
	commit_sem_values(set, values);
	for (i=0; i<q->nops; i++)
		set->pids[q->ops[i].sem_num] = q->task->tgid;
	set->ds.sem_otime = CURRENT_TIME;
	return 0;
}

/* Like Linux, complete eligible queued operations before waking their callers.
 * This prevents a new caller from stealing a permit already granted to a waiter.
 * A blocked head does not prevent unrelated operations later in the queue. */
static void update_queue(struct sem_array *set)
{
	struct sem_queue_node *q = set->waiters.first, *next;
	int result;

	while (q) {
		next = q->next;
		result = try_semop(set, q);
		if (result != 1) {
			unlink_waiter(set, q);
			q->result = result;
			wake_up_task(q->task);
			/* Successful changes may enable an earlier zero/negative wait. */
			if (result == 0) {
				q = set->waiters.first; /* Restart from the head of the queue. */
				continue;
			}
		}
		q = next;
	}
}

int sys_semop(int id, struct sembuf *user_ops, unsigned int nops)
{
	struct sembuf ops[SEMOPM];
	struct sem_array *set;
	struct sem_queue_node q;
	struct thread_group *g;
	unsigned int i;
	/* Until per-set permissions are added, only root may use IPC sets. */
	if (current->euid)
		return -EPERM;
	if (id < 0 || !nops)
		return -EINVAL;
	if (nops > SEMOPM)
		return -E2BIG;
	if (!user_range((unsigned long)user_ops, nops * sizeof(*user_ops)))
		return -EFAULT;
	/* The caller's array must remain a snapshot even while this task sleeps. */
	for (i=0; i<nops; i++) {
		ops[i].sem_num = get_fs_word(&user_ops[i].sem_num);
		ops[i].sem_op = (short)get_fs_word((unsigned short *)&user_ops[i].sem_op);
		ops[i].sem_flg = (short)get_fs_word((unsigned short *)&user_ops[i].sem_flg);
		if (ops[i].sem_flg & ~(IPC_NOWAIT | SEM_UNDO))
			return -EINVAL;
		if (ops[i].sem_flg & SEM_UNDO)
			return -ENOSYS; /* Never silently omit exit-time undo accounting. */
	}
	set = find_set(id);
	if (!set)
		return -EINVAL;
	for (i=0; i<nops; i++)
		if (ops[i].sem_num >= set->ds.sem_nsems)
			return -EFBIG;
	init_sem_request(&q, ops, nops);
	q.result = try_semop(set, &q);
	if (q.result == 0) {
		update_queue(set);
	} else if (q.result == 1) {
		link_waiter(set, &q);
		while (q.result == 1) {
			g = current->group;
			if ((pending_signals(current) & ~current->blocked) ||
				(g && (g->exiting || (g->exec_task && g->exec_task != current)))) {
				unlink_waiter(set, &q);
				return -EINTR;
			}
			/* Comparison, registration and sleep retain syscall serialization. */
			current->tcb.state = TASK_SLEEP_INTERRUPTIBLE;
			schedule();
		}
	}

	return q.result;
}

int sys_semget(key_t key, int nsems, int flags)
{
	struct sem_array *set = NULL;
	struct ipc_perm *p;
	int i;

	if (current->euid)
		return -EPERM;
	if (nsems < 0 || nsems > SEMMSL)
		return -EINVAL;
	for (i=0; i<SEMMNI; i++) {
		if (!sets[i].id) {
			if (!set)
				set = &sets[i];
		} else if (key != IPC_PRIVATE && sets[i].ds.sem_perm.__key == key) {
			if ((flags & (IPC_CREAT | IPC_EXCL)) == (IPC_CREAT | IPC_EXCL))
				return -EEXIST;
			if (nsems > sets[i].ds.sem_nsems)
				return -EINVAL;
			return sets[i].id;
		}
	}
	if (key != IPC_PRIVATE && !(flags & IPC_CREAT))
		return -ENOENT;
	if (!nsems)
		return -EINVAL;
	/* IDs do not wrap, preventing stale IDs from referring to new sets. */
	if (!set || next_id > 0x7fffffffUL)
		return -ENOSPC;
	set->id = next_id++;
	set->ds.sem_nsems = nsems;
	set->ds.sem_otime = 0;
	set->ds.sem_ctime = CURRENT_TIME;
	p = &set->ds.sem_perm;
	p->__key = key;
	p->uid = p->cuid = 0;
	p->gid = p->cgid = 0;
	p->mode = flags & 0777;
	p->__seq++;
	set->waiters.first = set->waiters.last = NULL;
	for (i=0; i<nsems; i++) {
		set->values[i] = 0;
		set->pids[i] = 0;
	}
	return set->id;
}

static void copy_from_user(void *to, const void *from, unsigned int size)
{
	unsigned int i;

	for (i=0; i<size; i++)
		((char *)to)[i] = get_fs_byte((char *)from + i);
}

static void copy_to_user(void *to, const void *from, unsigned int size)
{
	unsigned int i;

	verify_area(to, size);
	for (i=0; i<size; i++)
		put_fs_byte(((char *)from)[i], (char *)to + i);
}

int sys_semctl(int id, int number, struct semctl_args *control)
{
	struct sem_array *set = find_set(id);
	struct semid_ds ds;
	struct sem_queue_node *q, *next;
	unsigned short values[SEMMSL];
	unsigned long arg;
	int command, i, count = 0;
	unsigned int nsems;

	if (current->euid)
		return -EPERM;
	if (!set)
		return -EINVAL;
	if (!user_range((unsigned long)control, sizeof(*control)))
		return -EFAULT;
	command = get_fs_long((unsigned long *)&control->command);
	arg = get_fs_long(&control->argument);
	/* A user-memory fault may sleep for disk I/O even inside a legacy guard.
	 * Revalidate IDs after input copies; never act on a reused table slot. */
	set = find_set(id);
	if (!set)
		return -EIDRM;
	nsems = set->ds.sem_nsems;
	if (command == IPC_RMID || command == IPC_SET) {
		if (command == IPC_RMID) {
			set->id = 0;
			for (q=set->waiters.first; q; q=next) {
				next = q->next;
				q->result = -EIDRM;
				wake_up_task(q->task);
			}
			set->waiters.first = set->waiters.last = NULL;
			return 0;
		}
		if (!user_range(arg, sizeof(ds)))
			return -EFAULT;
		copy_from_user(&ds, (void *)arg, sizeof(ds));
		set = find_set(id);
		if (!set)
			return -EIDRM;
		set->ds.sem_perm.uid = ds.sem_perm.uid;
		set->ds.sem_perm.gid = ds.sem_perm.gid;
		set->ds.sem_perm.mode = ds.sem_perm.mode & 0777;
		set->ds.sem_ctime = CURRENT_TIME;
		return 0;
	}
	if (command != IPC_STAT && command != GETALL && command != SETALL &&
	    command != GETVAL && command != SETVAL && command != GETPID &&
	    command != GETNCNT && command != GETZCNT)
		return -EINVAL;
	if (command == IPC_STAT) {
		if (!user_range(arg, sizeof(ds)))
			return -EFAULT;
		/* Snapshot every byte, including zeroed padding, before user writes
		 * can fault and sleep. Do not retain a live table pointer across I/O. */
		for (i=0; i<sizeof(ds); i++)
			((char *)&ds)[i] = ((char *)&set->ds)[i];
		copy_to_user((void *)arg, &ds, sizeof(ds));
		return 0;
	}
	if (command == GETALL || command == SETALL) {
		if (!user_range(arg, nsems * sizeof(unsigned short)))
			return -EFAULT;
		if (command == GETALL) {
			for (i=0; i<nsems; i++)
				values[i] = set->values[i];
			copy_to_user((void *)arg, values,
				nsems * sizeof(unsigned short));
			return 0;
		}
		for (i=0; i<nsems; i++) {
			values[i] = get_fs_word((unsigned short *)arg + i);
			if (values[i] > SEMVMX)
				return -ERANGE;
		}
		set = find_set(id);
		if (!set)
			return -EIDRM;
		for (i=0; i<nsems; i++) {
			set->values[i] = values[i];
			set->pids[i] = current->tgid;
		}
	} else {
		if (number < 0 || number >= nsems)
			return -EINVAL;
		switch (command) {
		case GETVAL: return set->values[number];
		case GETPID: return set->pids[number];
		case GETNCNT:
		case GETZCNT:
			for (q=set->waiters.first; q; q=q->next)
				if (q->number == number && q->zero == (command == GETZCNT))
					count++;
			return count;
		case SETVAL:
			if (arg > SEMVMX)
				return -ERANGE;
			set->values[number] = arg;
			set->pids[number] = current->tgid;
		}
	}
	set->ds.sem_ctime = CURRENT_TIME;
	update_queue(set);
	return 0;
}
