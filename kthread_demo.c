/*
 * kthread_demo.c -- trigger the kernel-thread demo via syscall 73.
 *
 * Deploy + build + run inside the guest:
 *   MSYS_NO_PATHCONV=1 python tools/minix.py hdc-0.11.img put kthread_demo.c /kthread_demo.c
 *   gcc -o kthread_demo kthread_demo.c
 *   ./kthread_demo
 */
#define __NR_kthread_demo 73

int main(void)
{
	long __res;

	__asm__ volatile ("int $0x80"
		: "=a" (__res)
		: "0" ((long) __NR_kthread_demo));
	if (__res < 0) {
		printf("kthread_demo: syscall failed (%ld)\n", __res);
		return 1;
	}
	printf("kthread_demo: syscall issued; watch the kernel threads run\n");
	return 0;
}
