/*
 *  linux/kernel/sys.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <sys/times.h>
#include <sys/utsname.h>

int sys_ni_syscall(void)
{
	return -ENOSYS;
}

int sys_ftime()
{
	return -ENOSYS;
}

int sys_break()
{
	return -ENOSYS;
}

int sys_ptrace()
{
	return -ENOSYS;
}

int sys_stty()
{
	return -ENOSYS;
}

int sys_gtty()
{
	return -ENOSYS;
}

int sys_rename()
{
	return -ENOSYS;
}

int sys_prof()
{
	return -ENOSYS;
}

int sys_setregid(int rgid, int egid)
{
	if (rgid>0) {
		if ((current->gid == rgid) || 
		    suser())
			current->gid = rgid;
		else
			return(-EPERM);
	}
	if (egid>0) {
		if ((current->gid == egid) ||
		    (current->egid == egid) ||
		    (current->sgid == egid) ||
		    suser())
			current->egid = egid;
		else
			return(-EPERM);
	}
	return 0;
}

int sys_setgid(int gid)
{
	return(sys_setregid(gid, gid));
}

int sys_acct()
{
	return -ENOSYS;
}

int sys_phys()
{
	return -ENOSYS;
}

int sys_lock()
{
	return -ENOSYS;
}

int sys_mpx()
{
	return -ENOSYS;
}

int sys_ulimit()
{
	return -ENOSYS;
}

int sys_time(long * tloc)
{
	int i;

	i = CURRENT_TIME;
	if (tloc) {
		verify_area(tloc,4);
		put_fs_long(i,(unsigned long *)tloc);
	}
	return i;
}

/*
 * Unprivileged users may change the real user id to the effective uid
 * or vice versa.
 */
int sys_setreuid(int ruid, int euid)
{
	int old_ruid = current->uid;
	
	if (ruid>0) {
		if ((current->euid==ruid) ||
                    (old_ruid == ruid) ||
		    suser())
			current->uid = ruid;
		else
			return(-EPERM);
	}
	if (euid>0) {
		if ((old_ruid == euid) ||
                    (current->euid == euid) ||
		    suser())
			current->euid = euid;
		else {
			current->uid = old_ruid;
			return(-EPERM);
		}
	}
	return 0;
}

int sys_setuid(int uid)
{
	return(sys_setreuid(uid, uid));
}

int sys_stime(long * tptr)
{
	if (!suser())
		return -EPERM;
	startup_time = get_fs_long((unsigned long *)tptr) - jiffies/HZ;
	return 0;
}

int sys_times(struct tms * tbuf)
{
	struct thread_group *g = current->group;
	long utime = g ? g->utime : 0, stime = g ? g->stime : 0;
	int i;
	for (i=0; i<NR_TASKS; i++)
		if (task[i] && task[i]->group == g &&
		    task[i]->tcb.state != TASK_ZOMBIE && task[i]->tcb.state != TASK_DEAD) {
			utime += task[i]->utime;
			stime += task[i]->stime;
		}
	if (tbuf) {
		verify_area(tbuf,sizeof *tbuf);
		put_fs_long(utime,(unsigned long *)&tbuf->tms_utime);
		put_fs_long(stime,(unsigned long *)&tbuf->tms_stime);
		put_fs_long(g ? g->cutime : 0,(unsigned long *)&tbuf->tms_cutime);
		put_fs_long(g ? g->cstime : 0,(unsigned long *)&tbuf->tms_cstime);
	}
	return jiffies;
}

int sys_brk(unsigned long end_data_seg)
{
	int i;
	if (end_data_seg >= current->end_code &&
	    end_data_seg < current->start_stack - 16384)
		for (i=1; i<NR_TASKS; i++)
			if (task[i] && task[i]->group == current->group)
				task[i]->brk = end_data_seg;
	return current->brk;
}

/*
 * Job-control operations select processes and update all their threads.
 * This needs some heave checking ...
 * I just haven't get the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 */
int sys_setpgid(int pid, int pgid)
{
	int i, j;

	if (!pid)
		pid = current->tgid;
	if (!pgid)
		pgid = pid;
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->group && task[i] == task[i]->group->leader &&
		    task[i]->tgid==pid) {
			if (task[i]->leader)
				return -EPERM;
			if (task[i]->session != current->session)
				return -EPERM;
			for (j=1; j<NR_TASKS; j++)
				if (task[j] && task[j]->group == task[i]->group)
					task[j]->pgrp = pgid;
			return 0;
		}
	return -ESRCH;
}

int sys_getpgrp(void)
{
	return current->pgrp;
}

int sys_setsid(void)
{
	int i;
	struct task_struct *leader = current->group->leader;
	if (leader->leader && !suser())
		return -EPERM;
	leader->leader = 1;
	for (i=1; i<NR_TASKS; i++)
		if (task[i] && task[i]->group == current->group) {
			task[i]->session = task[i]->pgrp = current->tgid;
			task[i]->tty = -1;
		}
	return current->pgrp;
}

int sys_uname(struct utsname * name)
{
	static struct utsname thisname = {
		"linux .0","nodename","release ","version ","machine "
	};
	int i;

	if (!name) return -ERROR;
	verify_area(name,sizeof *name);
	for(i=0;i<sizeof *name;i++)
		put_fs_byte(((char *) &thisname)[i],i+(char *) name);
	return 0;
}

int sys_umask(int mask)
{
	int old = current->umask;

	current->umask = mask & 0777;
	return (old);
}
