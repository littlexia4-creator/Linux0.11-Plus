#include <errno.h>
#include <linux/sched.h>

struct demo_pair {
	volatile int started[2];
	volatile int finished[2];
	int guarded;
};

struct worker_arg {
	struct demo_pair *pair;
	int id;
};

static long busy_worker(void *arg)
{
	struct worker_arg *worker = arg;
	struct demo_pair *pair = worker->pair;
	int id = worker->id, peer = 1 - id, saw_peer = 0;
	/* do_timer() updates stime asynchronously, so reread it every time. */
	volatile long *cpu_ticks = &current->stime;
	unsigned long start;

	if (pair->guarded)
		preempt_defer();
	pair->started[id] = 1;
	start = *cpu_ticks;
	/* Half a CPU second exceeds the initial 15-tick slice. No calls,
	 * sleeps or voluntary scheduling occur inside this busy loop. */
	while ((unsigned long)*cpu_ticks - start < HZ / 2) {
		if (pair->started[peer] && !pair->finished[peer])
			saw_peer = 1;
	}
	/* Finish before releasing protection or entering kthread_exit(),
	 * so their scheduling cannot be mistaken for interleaved work. */
	pair->finished[id] = 1;
	if (pair->guarded)
		preempt_resume();
	return saw_peer;
}

/* guarded = 1 preserves legacy behavior; 
 * guarded = 0 permits preemption.
 * Return bit 0 if A saw unfinished B, and bit 1 if B saw unfinished A. */
int sys_preempt_demo(int guarded)
{
	struct demo_pair pair;
	struct worker_arg args[2];
	struct task_struct *a, *b;
	int i, a_saw_b, b_saw_a;

	if (guarded != 0 && guarded != 1)
		return -EINVAL;
	pair.guarded = guarded;
	for (i = 0; i < 2; i++) {
		pair.started[i] = pair.finished[i] = 0;
		args[i].pair = &pair;
		args[i].id = i;
	}
	a = kthread_run(busy_worker, &args[0], "preemptA");
	if (!a)
		return -ENOMEM;
	b = kthread_run(busy_worker, &args[1], "preemptB");
	if (!b) {
		kthread_join(a);
		return -ENOMEM;
	}
	/* Both workers finish before their arguments leave this stack frame.
	 * Joining also reclaims their task slots, so the demo can be repeated. */
	a_saw_b = kthread_join(a);
	b_saw_a = kthread_join(b);
	return a_saw_b | (b_saw_a << 1);
}
