/*
 * thr.c -- user-thread test for the clone() syscall (__NR_clone 72).
 *
 * Build and run inside the guest:
 *   gcc -o thr thr.c
 *   ./thr
 *
 * Two threads share this process's address space; both increment the
 * SAME counter.  If the kernel's clone() really shares memory, the
 * final count is 10; with plain fork() each task would count to 5 in
 * its own private copy.
 */
#define __NR_clone 72

static int sys_clone(void (*fn)(void *), void *arg, void *stack_top)
{
	long __res;
	__asm__ volatile ("int $0x80"
		: "=a" (__res)
		: "0" ((long) __NR_clone), "b" ((long) fn),
		  "c" ((long) arg), "d" ((long) stack_top));
	if (__res >= 0)
		return (int) __res;
	return -1;
}

static volatile int counter = 0;

static char stack_a[4096];
static char stack_b[4096];

static void thr_fn(void *arg)
{
	int i;

	for (i = 0; i < 5; i++) {
		counter++;
		printf("<%c> counter=%d\n", (int) (long) arg, counter);
	}
	exit(0);		/* never return: our [esp] slot holds 0 */
}

int main(void)
{
	int a, b, st;

	a = sys_clone(thr_fn, (void *) (long) 'A', stack_a + sizeof(stack_a));
	b = sys_clone(thr_fn, (void *) (long) 'B', stack_b + sizeof(stack_b));
	printf("parent: cloned A=%d B=%d\n", a, b);
	wait(&st);
	wait(&st);
	printf("parent: counter=%d (10 means truly shared)\n", counter);
	return 0;
}
