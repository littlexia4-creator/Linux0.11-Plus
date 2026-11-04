#ifndef _BITS_SIGTHREAD_H
#define _BITS_SIGTHREAD_H

#ifndef _SIGNAL_H
#error "Include <signal.h> before <bits/sigthread.h>."
#endif

/* Return an error number directly; the mask belongs to the calling thread. */
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);

#endif /* _BITS_SIGTHREAD_H */
