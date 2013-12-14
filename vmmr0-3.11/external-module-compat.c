
/*
 * smp_call_function_single() is not exported below 2.6.20.
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)

/* The 'nonatomic' argument was removed in 2.6.27. */

#undef smp_call_function_single

#include <linux/smp.h>

#ifdef CONFIG_SMP
int vmmr0_smp_call_function_single(int cpu, void (*func)(void *info),
				 void *info, int wait)
{
	return smp_call_function_single(cpu, func, info, 0, wait);
}
#else /* !CONFIG_SMP */
int vmmr0_smp_call_function_single(int cpu, void (*func)(void *info),
				 void *info, int wait)
{
	WARN_ON(cpu != 0);
	local_irq_disable();
	func(info);
	local_irq_enable();
	return 0;

}
#endif /* !CONFIG_SMP */

#define smp_call_function_single vmmr0_smp_call_function_single

#endif

/* div64_u64 is fairly new */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)

#ifndef CONFIG_64BIT

/* 64bit divisor, dividend and result. dynamic precision */
uint64_t div64_u64(uint64_t dividend, uint64_t divisor)
{
	uint32_t high, d;

	high = divisor >> 32;
	if (high) {
		unsigned int shift = fls(high);

		d = divisor >> shift;
		dividend >>= shift;
	} else
		d = divisor;

	do_div(dividend, d);

	return dividend;
}

#endif

#endif

/*
 * smp_call_function_mask() is not defined/exported below 2.6.24 on all
 * targets and below 2.6.26 on x86-64
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24) || \
    (defined CONFIG_X86_64 && LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))

#include <linux/smp.h>

struct vmmr0_call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
};

static void vmmr0_ack_smp_call(void *_data)
{
	struct vmmr0_call_data_struct *data = _data;
	/* if wait == 0, data can be out of scope
	 * after atomic_inc(info->started)
	 */
	void (*func) (void *info) = data->func;
	void *info = data->info;
	int wait = data->wait;

	smp_mb();
	atomic_inc(&data->started);
	(*func)(info);
	if (wait) {
		smp_mb();
		atomic_inc(&data->finished);
	}
}

int vmmr0_smp_call_function_mask(cpumask_t mask,
			       void (*func) (void *info), void *info, int wait)
{
#ifdef CONFIG_SMP
	struct vmmr0_call_data_struct data;
	cpumask_t allbutself;
	int cpus;
	int cpu;
	int me;

	me = get_cpu();
	WARN_ON(irqs_disabled());
	allbutself = cpu_online_map;
	cpu_clear(me, allbutself);

	cpus_and(mask, mask, allbutself);
	cpus = cpus_weight(mask);

	if (!cpus)
		goto out;

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	for (cpu = first_cpu(mask); cpu != NR_CPUS; cpu = next_cpu(cpu, mask))
		smp_call_function_single(cpu, vmmr0_ack_smp_call, &data, 0);

	while (atomic_read(&data.started) != cpus) {
		cpu_relax();
		barrier();
	}

	if (!wait)
		goto out;

	while (atomic_read(&data.finished) != cpus) {
		cpu_relax();
		barrier();
	}
out:
	put_cpu();
#endif /* CONFIG_SMP */
	return 0;
}

#include <linux/workqueue.h>

static void vcpu_kick_intr(void *info)
{
}

struct vmmr0_kick {
	int cpu;
	struct work_struct work;
};

static void vmmr0_do_smp_call_function(struct work_struct *work)
{
	int me = get_cpu();
	struct vmmr0_kick *vmmr0_kick = container_of(work, struct vmmr0_kick, work);

	if (vmmr0_kick->cpu != me)
		smp_call_function_single(vmmr0_kick->cpu, vcpu_kick_intr,
					 NULL, 0);
	kfree(vmmr0_kick);
	put_cpu();
}

void vmmr0_queue_smp_call_function(int cpu)
{
	struct vmmr0_kick *vmmr0_kick = kmalloc(sizeof(struct vmmr0_kick), GFP_ATOMIC);

	INIT_WORK(&vmmr0_kick->work, vmmr0_do_smp_call_function);

	schedule_work(&vmmr0_kick->work);
}

void vmmr0_smp_send_reschedule(int cpu)
{
	if (irqs_disabled()) {
		vmmr0_queue_smp_call_function(cpu);
		return;
	}
	smp_call_function_single(cpu, vcpu_kick_intr, NULL, 0);
}
#endif

#include <linux/intel-iommu.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)

int intel_iommu_found()
{
	return 0;
}

#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)

static enum hrtimer_restart vmmr0_hrtimer_wakeup(struct hrtimer *timer)
{
	struct hrtimer_sleeper *t =
		container_of(timer, struct hrtimer_sleeper, timer);
	struct task_struct *task = t->task;

	t->task = NULL;
	if (task)
		wake_up_process(task);

	return HRTIMER_NORESTART;
}

int schedule_hrtimeout(ktime_t *expires, const enum hrtimer_mode mode)
{
	struct hrtimer_sleeper t;

	/*
	 * Optimize when a zero timeout value is given. It does not
	 * matter whether this is an absolute or a relative time.
	 */
	if (expires && !expires->tv64) {
		__set_current_state(TASK_RUNNING);
		return 0;
	}

	/*
	 * A NULL parameter means "inifinte"
	 */
	if (!expires) {
		schedule();
		__set_current_state(TASK_RUNNING);
		return -EINTR;
	}

	hrtimer_init(&t.timer, CLOCK_MONOTONIC, mode);
	t.timer.expires = *expires;

	t.timer.function = vmmr0_hrtimer_wakeup;
	t.task = current;

	hrtimer_start(&t.timer, t.timer.expires, mode);
	if (!hrtimer_active(&t.timer))
		t.task = NULL;

	if (likely(t.task))
		schedule();

	hrtimer_cancel(&t.timer);

	__set_current_state(TASK_RUNNING);

	return !t.task ? 0 : -EINTR;
}

#endif

#ifndef CONFIG_USER_RETURN_NOTIFIER

DEFINE_PER_CPU(struct vmmr0_user_return_notifier *, vmmr0_urn) = NULL;

#endif /* CONFIG_USER_RETURN_NOTIFIER */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0)
void *bsearch(const void *key, const void *base, size_t num, size_t size,
	      int (*cmp)(const void *key, const void *elt))
{
	size_t start = 0, end = num;
	int result;

	while (start < end) {
		size_t mid = start + (end - start) / 2;

		result = cmp(key, base + mid * size);
		if (result < 0)
			end = mid;
		else if (result > 0)
			start = mid + 1;
		else
			return (void *)base + mid * size;
	}

	return NULL;
}
#endif /* < 3.0 */
