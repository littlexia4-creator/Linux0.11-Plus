/* User-space i386 entry for syscall(number, ...). */
#include <sys/syscall.h>
#include <errno.h>

long syscall(long number, ...)
{
	long result;

	/* Read argument slots directly, as an assembly syscall stub does: the
	 * caller supplies only the arguments its syscall uses. Save the C ABI's
	 * callee-saved registers, including the compiler's frame pointer, before
	 * loading ebx/ecx/edx/esi/edi/ebp. GCC 1.40 has no memory clobber; the
	 * memory operand keeps the number's stack slot available to the asm. */
	__asm__ volatile ("pushl %%ebp\n\tpushl %%edi\n\t"
		"pushl %%esi\n\tpushl %%ebx\n\t"
		"movl 4(%%eax),%%ebx\n\tmovl 8(%%eax),%%ecx\n\t"
		"movl 12(%%eax),%%edx\n\tmovl 16(%%eax),%%esi\n\t"
		"movl 20(%%eax),%%edi\n\tmovl 24(%%eax),%%ebp\n\t"
		"movl (%%eax),%%eax\n\tint $0x80\n\t"
		"popl %%ebx\n\tpopl %%esi\n\tpopl %%edi\n\tpopl %%ebp"
		: "=a" (result)
		: "0" (&number), "m" (number));
	/* Other negative values can be successful results, e.g. signal masks. */
	if ((unsigned long)result >= (unsigned long)-4095) {
		errno = -result;
		return -1;
	}
	return result;
}
