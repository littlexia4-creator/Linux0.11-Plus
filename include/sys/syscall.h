#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

/* Syscall numbers for this kernel, shared by wrappers and user programs. */
#define __NR_setup	0	/* used only by init, to get system going */
#define __NR_exit	1	/* terminate only the calling thread */
#define __NR_fork	2
#define __NR_read	3
#define __NR_write	4
#define __NR_open	5
#define __NR_close	6
#define __NR_waitpid	7
#define __NR_creat	8
#define __NR_link	9
#define __NR_unlink	10
#define __NR_execve	11
#define __NR_chdir	12
#define __NR_time	13
#define __NR_mknod	14
#define __NR_chmod	15
#define __NR_chown	16
#define __NR_break	17
#define __NR_stat	18
#define __NR_lseek	19
#define __NR_getpid	20
#define __NR_mount	21
#define __NR_umount	22
#define __NR_setuid	23
#define __NR_getuid	24
#define __NR_stime	25
#define __NR_ptrace	26
#define __NR_alarm	27
#define __NR_fstat	28
#define __NR_pause	29
#define __NR_utime	30
#define __NR_stty	31
#define __NR_gtty	32
#define __NR_access	33
#define __NR_nice	34
#define __NR_ftime	35
#define __NR_sync	36
#define __NR_kill	37
#define __NR_rename	38
#define __NR_mkdir	39
#define __NR_rmdir	40
#define __NR_dup	41
#define __NR_pipe	42
#define __NR_times	43
#define __NR_prof	44
#define __NR_brk	45
#define __NR_setgid	46
#define __NR_getgid	47
#define __NR_signal	48
#define __NR_geteuid	49
#define __NR_getegid	50
#define __NR_acct	51
#define __NR_phys	52
#define __NR_lock	53
#define __NR_ioctl	54
#define __NR_fcntl	55
#define __NR_mpx	56
#define __NR_setpgid	57
#define __NR_ulimit	58
#define __NR_uname	59
#define __NR_umask	60
#define __NR_chroot	61
#define __NR_ustat	62
#define __NR_dup2	63
#define __NR_getppid	64
#define __NR_getpgrp	65
#define __NR_setsid	66
#define __NR_sigaction	67
#define __NR_sgetmask	68
#define __NR_ssetmask	69
#define __NR_setreuid	70
#define __NR_setregid	71
#define __NR_gettid	72	/* calling task's thread ID */
#define __NR_clone	73	/* user threads sharing the address space */
#define __NR_yield_demo	74	/* run the kernel-thread demo */
#define __NR_exit_group	75	/* terminate all threads in this process */
#define __NR_futex	76	/* minimal process-local wait/wake */
#define __NR_strncpy_test	77	/* run the in-kernel strncpy regression test */
#define __NR_sigprocmask	78	/* atomically update the calling thread's mask */
#define __NR_preempt_demo	79	/* demonstrate kernel-worker preemption */

#define SYS_setup	__NR_setup
#define SYS_exit	__NR_exit
#define SYS_fork	__NR_fork
#define SYS_read	__NR_read
#define SYS_write	__NR_write
#define SYS_open	__NR_open
#define SYS_close	__NR_close
#define SYS_waitpid	__NR_waitpid
#define SYS_creat	__NR_creat
#define SYS_link	__NR_link
#define SYS_unlink	__NR_unlink
#define SYS_execve	__NR_execve
#define SYS_chdir	__NR_chdir
#define SYS_time	__NR_time
#define SYS_mknod	__NR_mknod
#define SYS_chmod	__NR_chmod
#define SYS_chown	__NR_chown
#define SYS_break	__NR_break
#define SYS_stat	__NR_stat
#define SYS_lseek	__NR_lseek
#define SYS_getpid	__NR_getpid
#define SYS_mount	__NR_mount
#define SYS_umount	__NR_umount
#define SYS_setuid	__NR_setuid
#define SYS_getuid	__NR_getuid
#define SYS_stime	__NR_stime
#define SYS_ptrace	__NR_ptrace
#define SYS_alarm	__NR_alarm
#define SYS_fstat	__NR_fstat
#define SYS_pause	__NR_pause
#define SYS_utime	__NR_utime
#define SYS_stty	__NR_stty
#define SYS_gtty	__NR_gtty
#define SYS_access	__NR_access
#define SYS_nice	__NR_nice
#define SYS_ftime	__NR_ftime
#define SYS_sync	__NR_sync
#define SYS_kill	__NR_kill
#define SYS_rename	__NR_rename
#define SYS_mkdir	__NR_mkdir
#define SYS_rmdir	__NR_rmdir
#define SYS_dup	__NR_dup
#define SYS_pipe	__NR_pipe
#define SYS_times	__NR_times
#define SYS_prof	__NR_prof
#define SYS_brk	__NR_brk
#define SYS_setgid	__NR_setgid
#define SYS_getgid	__NR_getgid
#define SYS_signal	__NR_signal
#define SYS_geteuid	__NR_geteuid
#define SYS_getegid	__NR_getegid
#define SYS_acct	__NR_acct
#define SYS_phys	__NR_phys
#define SYS_lock	__NR_lock
#define SYS_ioctl	__NR_ioctl
#define SYS_fcntl	__NR_fcntl
#define SYS_mpx	__NR_mpx
#define SYS_setpgid	__NR_setpgid
#define SYS_ulimit	__NR_ulimit
#define SYS_uname	__NR_uname
#define SYS_umask	__NR_umask
#define SYS_chroot	__NR_chroot
#define SYS_ustat	__NR_ustat
#define SYS_dup2	__NR_dup2
#define SYS_getppid	__NR_getppid
#define SYS_getpgrp	__NR_getpgrp
#define SYS_setsid	__NR_setsid
#define SYS_sigaction	__NR_sigaction
#define SYS_sgetmask	__NR_sgetmask
#define SYS_ssetmask	__NR_ssetmask
#define SYS_setreuid	__NR_setreuid
#define SYS_setregid	__NR_setregid
#define SYS_gettid	__NR_gettid
#define SYS_clone	__NR_clone
#define SYS_yield_demo	__NR_yield_demo
#define SYS_exit_group	__NR_exit_group
#define SYS_futex	__NR_futex
#define SYS_strncpy_test	__NR_strncpy_test
#define SYS_sigprocmask	__NR_sigprocmask
#define SYS_preempt_demo	__NR_preempt_demo

/* Also declare it here for the guest's original installed <unistd.h>. */
long syscall(long number, ...);

#endif
