#include "semaphore.h"
#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include "sysdep.h"

static int sem_error(int error)
{
	errno = error;
	return -1;
}

static int lock_sem(sem_t *sem)
{
	int error;

	if (!sem)
		return sem_error(EINVAL);
	error = pthread_mutex_lock(&sem->__lock);
	if (error)
		return sem_error(error);
	if (sem->__value < 0) {
		pthread_mutex_unlock(&sem->__lock);
		return sem_error(EINVAL);
	}
	return 0;
}

static int unlock_sem(sem_t *sem)
{
	int error = pthread_mutex_unlock(&sem->__lock);

	return error ? sem_error(error) : 0;
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
	pthread_mutex_t unlocked = PTHREAD_MUTEX_INITIALIZER;

	if (!sem || value > SEM_VALUE_MAX)
		return sem_error(EINVAL);
	/* Our futex keys belong to one thread group, not shared mappings. */
	if (pshared)
		return sem_error(ENOSYS);
	sem->__lock = unlocked;
	sem->__value = value;
	sem->__waiters = 0;
	return 0;
}

int sem_destroy(sem_t *sem)
{
	if (lock_sem(sem) < 0)
		return -1;
	if (sem->__waiters) {
		unlock_sem(sem);
		return sem_error(EBUSY);
	}
	/* No kernel semaphore or allocated storage needs releasing. */
	sem->__value = -1;
	return unlock_sem(sem);
}

int sem_trywait(sem_t *sem)
{
	if (lock_sem(sem) < 0)
		return -1;
	if (!sem->__value) {
		unlock_sem(sem);
		return sem_error(EAGAIN);
	}
	sem->__value--;
	return unlock_sem(sem);
}

int sem_wait(sem_t *sem)
{
	int result = 0, waiting = 0;

	for (;;) {
		if (lock_sem(sem) < 0)
			return -1;
		if (sem->__value) {
			sem->__value--;
			if (waiting)
				sem->__waiters--;
			return unlock_sem(sem);
		}
		/* Consume an available token before reporting an interruption.
		 * Otherwise a signal could strand the token whose wakeup we received. */
		if (result < 0 && result != -EAGAIN) {
			sem->__waiters--;
			unlock_sem(sem);
			return sem_error(-result);
		}
		if (!waiting) {
			sem->__waiters++;
			waiting = 1;
		}
		if (unlock_sem(sem) < 0)
			return -1;
		/* The kernel compares against zero before sleeping. If sem_post()
		 * added a token after we unlocked, WAIT returns EAGAIN instead of
		 * losing the wakeup. A wakeup only requests another count check. */
		result = __syscall3(SYS_futex, (long)&sem->__value, FUTEX_WAIT, 0);
	}
}

int sem_post(sem_t *sem)
{
	int wake, result;

	if (lock_sem(sem) < 0)
		return -1;
	if (sem->__value == SEM_VALUE_MAX) {
		unlock_sem(sem);
		return sem_error(EOVERFLOW);
	}
	sem->__value++;
	wake = sem->__waiters != 0;
	if (unlock_sem(sem) < 0)
		return -1;
	if (!wake)
		return 0;
	result = __syscall3(SYS_futex, (long)&sem->__value, FUTEX_WAKE, 1);
	return result < 0 ? sem_error(-result) : 0;
}

int sem_getvalue(sem_t *sem, int *value)
{
	if (!value)
		return sem_error(EINVAL);
	if (lock_sem(sem) < 0)
		return -1;
	/* Report zero while threads wait, as Linux does. */
	*value = sem->__value;
	return unlock_sem(sem);
}
