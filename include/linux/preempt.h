#ifndef _LINUX_PREEMPT_H
#define _LINUX_PREEMPT_H

/* Defer automatic preemption in legacy kernel services.
 * CPU mode and interrupt state are unchanged; voluntary scheduling is allowed.
 * On one CPU, preventing involuntary switches preserves their old locking;
 * hardware interrupt users still need IRQ-safe critical sections. */
extern void preempt_defer(void);
extern void preempt_resume(void);

#endif
