#include "atomic.h"

int __atomic_exchange_int(volatile int *word, int value)
{
	/* Memory xchg is atomic and orders memory accesses on i386. */
	__asm__ volatile ("xchgl %0,%1"
		: "=r" (value), "=m" (*word)
		: "0" (value), "m" (*word));
	return value;
}
