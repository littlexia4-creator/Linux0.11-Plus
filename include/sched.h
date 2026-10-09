#ifndef _USER_SCHED_H
#define _USER_SCHED_H

int clone(int (*fn)(void *), void *stack_top, int flags, void *arg);

#endif
