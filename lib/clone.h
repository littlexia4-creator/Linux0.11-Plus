#ifndef _LIB_CLONE_H
#define _LIB_CLONE_H

/* Alias of clone(): the same optional arguments and -1/errno convention. */
int __clone(int (*fn)(void *), void *stack_top, int flags, void *arg, ...);

/* Internal entry: return the TID or a negative error, without touching errno. */
int __clone_raw(int (*fn)(void *), void *stack_top, int flags, void *arg,
		int *parent_tid, int *child_tid);

#endif
