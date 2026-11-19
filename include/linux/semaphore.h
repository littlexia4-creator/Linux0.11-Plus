#ifndef _LINUX_SEMAPHORE_H
#define _LINUX_SEMAPHORE_H

struct semaphore_waiter;

/* Kernel counting semaphore. All count/queue changes use local IRQ protection
 * on this single-CPU kernel. Kernel callers use these directly, without a
 * syscall; System V <sys/sem.h> and POSIX <semaphore.h> are separate APIs. */
struct semaphore {
	unsigned int count;
	struct semaphore_waiter *first;
	struct semaphore_waiter *last;
};

#define DEFINE_SEMAPHORE(name, count) \
	struct semaphore name = { (count), 0, 0 }

/* Initialize before publishing; never reinitialize a semaphore in use. */
extern void sema_init(struct semaphore *sem, int value);
/* Blocking acquisition requires task context with interrupts enabled. */
extern void down(struct semaphore *sem);
extern int down_interruptible(struct semaphore *sem);
/* Linux convention: return 0 on acquisition, 1 if unavailable. */
extern int down_trylock(struct semaphore *sem);
/* Any task or interrupt handler may release a permit. */
extern void up(struct semaphore *sem);

/* Called by the timer interrupt with local IRQs disabled. */
extern void semaphore_timer_tick(void);

#endif
