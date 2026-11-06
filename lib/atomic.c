#include "atomic.h"

int __atomic_exchange_int(volatile int *word, int value)
{
	/* Memory xchg is atomic and orders memory accesses on i386. */
	__asm__ volatile ("xchgl %0,%1"
		: "=r" (value), "=m" (*word)
		: "0" (value), "m" (*word));
	return value;
}

unsigned int __atomic_load_uint(volatile unsigned int *word)
{
	return *word;
}

int __atomic_compare_exchange_uint(volatile unsigned int *word,
				   unsigned int *expected, unsigned int desired)
{
	unsigned int previous;

	/* lock cmpxchgl %edx,(%ecx): GAS 1.38 lacks the CMPXCHG mnemonic.
	 * EAX supplies the expected value and receives the observed value.
	 * Encode the i486 instruction directly; do not emulate it with a lock
	 * that could deadlock if sem_post() interrupts its owner in a handler. */
	__asm__ volatile (".byte 0xf0,0x0f,0xb1,0x11"
		: "=a" (previous), "=m" (*word)
		: "0" (*expected), "c" (word), "d" (desired), "m" (*word));
	if (previous == *expected)
		return 1;
	*expected = previous;
	return 0;
}

unsigned int __atomic_fetch_add_uint(volatile unsigned int *word, unsigned int value)
{
	unsigned int previous = __atomic_load_uint(word);

	while (!__atomic_compare_exchange_uint(word, &previous, previous + value))
		;
	return previous;
}

unsigned int __atomic_fetch_and_uint(volatile unsigned int *word, unsigned int value)
{
	unsigned int previous = __atomic_load_uint(word);

	while (!__atomic_compare_exchange_uint(word, &previous, previous & value))
		;
	return previous;
}

unsigned int __atomic_fetch_or_uint(volatile unsigned int *word, unsigned int value)
{
	unsigned int previous = __atomic_load_uint(word);

	while (!__atomic_compare_exchange_uint(word, &previous, previous | value))
		;
	return previous;
}
