/*
 * User-space front end for this repository's Linux 0.11 kernel preemptible demo.
 * Host: run sh run.sh to rebuild the kernel and install these files.
 * Guest: sh build_pre.sh; ./preempt_demo
 *
 * A user-space-only busy loop cannot demonstrate kernel preemption:
 * original Linux 0.11 already preempted user-mode execution. Syscall 79
 * runs two kernel workers instead. Each uses half a CPU second without
 * yielding or sleeping, and records whether its peer started but has not
 * finished. A guarded run provides the comparison in the same kernel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>

static int run_pair(const char *label, int guarded)
{
	long result = syscall(SYS_preempt_demo, guarded);

	if (result < 0) {
		perror("preempt demo syscall (boot the rebuilt Linux 0.11 kernel)");
		exit(EXIT_FAILURE);
	}
	printf("%s: A saw unfinished B: %s; B saw unfinished A: %s\n",
		label, result & 1 ? "yes" : "no", result & 2 ? "yes" : "no");
	return (int)result;
}

int main(void)
{
	int guarded, preemptible;

	printf("Two kernel workers, 0.5 CPU seconds each, no voluntary scheduling.\n");
	fflush(stdout);
	guarded = run_pair("Guarded    ", 1);
	preemptible = run_pair("Preemptible", 0);
	if (guarded != 0 || preemptible != 3) {
		printf("Expected no overlap when guarded, and both peers observed when preemptible.\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
