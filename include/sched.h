#ifndef _USER_SCHED_H
#define _USER_SCHED_H

#define CLONE_VM       0x00000100 /* share the address space */
#define CLONE_SIGHAND  0x00000800 /* share signal dispositions, not masks */
#define CLONE_THREAD   0x00010000 /* join the caller's thread group */
#define CLONE_PARENT_SETTID  0x00100000 /* publish TID before the child can run */
#define CLONE_CHILD_CLEARTID 0x00200000 /* clear child_tid and futex-wake on exit */

/* CLONE_VM | CLONE_SIGHAND | CLONE_THREAD is required. Only PARENT_SETTID
 * and CHILD_CLEARTID may be added. Optional arguments follow Linux's order:
 * parent_tid, tls (unused placeholder), child_tid. Pass parent_tid when either
 * optional flag is set, and all three when CHILD_CLEARTID is set. Enabled TID
 * pointers must be non-NULL, aligned 32-bit words. CHILD_CLEARTID does not
 * initialize its word: initialize it before clone, or use the same word for
 * PARENT_SETTID. The wrapper passes fn(arg)'s return value to _exit(); result
 * retention and joining belong in user space. */
int clone(int (*fn)(void *), void *stack_top, int flags, void *arg, ...);

#endif
