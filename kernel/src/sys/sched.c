#include <sys/sched.h>
#include <cpu/gdt.h>
#include <cpu/smp.h>
#include <core/alix.h>
#include <debug/panic.h>
#include <lib/atomic.h>
#include <lib/spinlock.h>
#include <lib/string.h>
#include <mm/mm.h>
#define KLOG_NS "sched"
#include <log/klog.h>

#define SCHED_TCB_RSP_OFFSET 56
#define SCHED_IDLE_STACK_QWORDS 512
#define SCHED_CONTEXT_QWORDS 16
#define SCHED_CONTEXT_THREAD_ARG_SLOT 5
#define SCHED_STACK_QWORDS (SCHED_STACK_SIZE / sizeof(uint64_t))

typedef char sched_tcb_rsp_offset_must_match
	[(offsetof(tcb_t, rsp) == SCHED_TCB_RSP_OFFSET) ? 1 : -1];

static uint64_t next_pid = SCHED_PID_MIN;
static volatile uint64_t next_tid = 1;
static pcb_t *proc_list;
static spinlock_t proc_lock = SPINLOCK_INIT;

static pcb_t idle_procs[MAX_CPUS];
static tcb_t idle_threads[MAX_CPUS];
static uint64_t idle_stacks[MAX_CPUS][SCHED_IDLE_STACK_QWORDS];
static rq_t zombie_rq[MAX_CPUS];

static void sched_thread_trampoline(tcb_t *thread)
	__attribute__((__noreturn__));

#define SCHED_NAME_CASE(id, name) \
	case id:                      \
		return name;

static const char *sched_thread_status_name(thread_status_t status)
{
	switch (status) {
		SCHED_THREAD_STATUS_X(SCHED_NAME_CASE)
	default:
		return "unknown";
	}
}

static const char *sched_proc_type_name(proc_type_t type)
{
	switch (type) {
		SCHED_PROC_TYPE_X(SCHED_NAME_CASE)
	default:
		return "unknown";
	}
}

#undef SCHED_NAME_CASE

static void sched_cpu_lock(struct cpu_info *cpu)
{
	while (atomic_exchange(&cpu->sched_lock, 1, __ATOMIC_ACQUIRE) != 0) {
		while (atomic_load(&cpu->sched_lock, __ATOMIC_RELAXED) != 0)
			atomic_cpu_relax();
	}
}

static void sched_cpu_unlock(struct cpu_info *cpu)
{
	atomic_store(&cpu->sched_lock, 0, __ATOMIC_RELEASE);
}

static noreturn void sched_idle(void *arg)
{
	(void)arg;
	for (;;)
		__asm__ volatile("pause");
}

static uint64_t sched_make_stack(void (*entry)(tcb_t *thread), tcb_t *thread,
								 uint64_t *stack, size_t qwords)
{
	uint64_t *sp = stack + qwords;

	*--sp = (uint64_t)(uintptr_t)entry;
	for (size_t i = 0; i < SCHED_CONTEXT_QWORDS - 1; i++)
		*--sp = 0;

	sp[SCHED_CONTEXT_THREAD_ARG_SLOT] = (uint64_t)(uintptr_t)thread;
	return (uint64_t)(uintptr_t)sp;
}

static size_t sched_cpu_load(struct cpu_info *cpu)
{
	size_t load;

	if (!cpu || !cpu->idle_thread)
		return (size_t)-1;

	sched_cpu_lock(cpu);
	load = cpu->sched_thread_count;
	if (cpu->current_thread && cpu->current_thread != cpu->idle_thread &&
		SCHED_IS_RUNNING(cpu->current_thread))
		load++;
	sched_cpu_unlock(cpu);

	return load;
}

static struct cpu_info *sched_select_cpu(void)
{
	struct cpu_info *best = NULL;
	size_t best_load = (size_t)-1;

	for (uint32_t i = 0; i < cpu_count(); i++) {
		struct cpu_info *cpu = cpu_get(i);
		size_t load = sched_cpu_load(cpu);

		if (load < best_load) {
			best = cpu;
			best_load = load;
		}
	}

	return best ? best : cpu_current();
}

tcb_t *sched_current(void)
{
	struct cpu_info *cpu = cpu_current();

	return cpu ? cpu->current_thread : NULL;
}

static uint64_t sched_parent_pid(void)
{
	tcb_t *current = sched_current();

	if (!current || !current->parent)
		return SCHED_IDLE_PID;

	return current->parent->pid;
}

static bool sched_pid_in_use(uint64_t pid)
{
	for (pcb_t *proc = proc_list; proc; proc = proc->next) {
		if (proc->pid == pid)
			return true;
	}

	return false;
}

static uint64_t sched_alloc_pid(void)
{
	uint64_t start = next_pid;
	uint64_t pid;

	for (;;) {
		pid = next_pid;
		next_pid++;
		if (next_pid > SCHED_PID_MAX)
			next_pid = SCHED_PID_MIN;

		if (!sched_pid_in_use(pid))
			return pid;

		if (next_pid == start)
			return SCHED_INVALID_PID;
	}
}

static void sched_proc_link_locked(pcb_t *proc)
{
	proc->next = proc_list;
	proc_list = proc;
}

static void sched_proc_unlink_locked(pcb_t *proc)
{
	pcb_t **cur = &proc_list;

	while (*cur) {
		if (*cur == proc) {
			*cur = proc->next;
			proc->next = NULL;
			return;
		}

		cur = &(*cur)->next;
	}
}

static void sched_proc_release_pid(pcb_t *proc)
{
	if (PROC_IS_IDLE(proc))
		return;

	spinlock_lock(&proc_lock);
	sched_proc_unlink_locked(proc);
	spinlock_unlock(&proc_lock);
}

pcb_t *spawn_proc(const char *name, proc_type_t type, vas_t *vas)
{
	pcb_t *proc = kzalloc(sizeof(*proc));

	if (!proc)
		return NULL;

	if (type == PROC_IDLE) {
		proc->pid = SCHED_IDLE_PID;
	} else {
		spinlock_lock(&proc_lock);
		proc->pid = sched_alloc_pid();
		if (proc->pid != SCHED_INVALID_PID)
			sched_proc_link_locked(proc);
		spinlock_unlock(&proc_lock);
	}

	if (proc->pid == SCHED_INVALID_PID) {
		kfree(proc);
		return NULL;
	}

	proc->ppid = sched_parent_pid();
	strlcpy(proc->name, name, sizeof(proc->name));
	proc->type = type;
	proc->thread = NULL;
	if (type == PROC_IDLE)
		proc->next = NULL;
	proc->vas = vas;
	proc->thread_count = 0;

	if (!proc->vas) {
		if (type == PROC_USER)
			proc->vas = vas_create(NULL);
		else
			proc->vas = kernel_vas;
	}

	if (!proc->vas) {
		sched_proc_release_pid(proc);
		kfree(proc);
		return NULL;
	}

	ktrace("spawned %s process %llu (%s) ppid=%llu",
		   sched_proc_type_name(proc->type), (unsigned long long)proc->pid,
		   proc->name, (unsigned long long)proc->ppid);
	return proc;
}

tcb_t *spawn_thread(const char *name, pcb_t *pcb, sched_entry_t entry,
					void *arg)
{
	struct cpu_info *cpu;
	tcb_t *thread;
	void *stack;

	if (!pcb || !entry)
		return NULL;

	if (PROC_IS_USER(pcb)) {
		klog("spawn_thread: user thread spawn needs an iret/user entry path");
		return NULL;
	}

	cpu = sched_select_cpu();
	if (!cpu || !cpu->idle_thread)
		return NULL;

	thread = kzalloc(sizeof(*thread));
	if (!thread)
		return NULL;

	stack = kmalloc(SCHED_STACK_SIZE);
	if (!stack) {
		kfree(thread);
		return NULL;
	}

	thread->tid = atomic_fetch_add(&next_tid, 1, __ATOMIC_RELAXED);
	strlcpy(thread->name, name, sizeof(thread->name));
	thread->status = THREAD_RUNNABLE;
	SCHED_TICK_RESET(thread);
	thread->total_ticks = 0;
	thread->rip = (uint64_t)(uintptr_t)entry;
	rq_node_init(&thread->rq_node, thread);
	thread->next = pcb->thread;
	thread->parent = pcb;
	thread->entry = entry;
	thread->arg = arg;
	thread->stack = stack;
	thread->stack_size = SCHED_STACK_SIZE;
	thread->cs =
		PROC_IS_USER(pcb) ? GDT_USER_CODE_SELECTOR : GDT_KERNEL_CODE_SELECTOR;
	thread->ss =
		PROC_IS_USER(pcb) ? GDT_USER_DATA_SELECTOR : GDT_KERNEL_DATA_SELECTOR;
	thread->rflags = SCHED_KERNEL_RFLAGS;
	thread->cpu_index = cpu->index;
	thread->rsp = sched_make_stack(sched_thread_trampoline, thread, stack,
								   SCHED_STACK_QWORDS);

	pcb->thread = thread;
	pcb->thread_count++;

	sched_cpu_lock(cpu);
	rq_push_back(&cpu->rq, &thread->rq_node, thread);
	cpu->sched_thread_count++;
	sched_cpu_unlock(cpu);
	ktrace("spawned %s thread %llu (%s) for pid=%llu on CPU%u status=%s",
		   sched_proc_type_name(pcb->type), (unsigned long long)thread->tid,
		   thread->name, (unsigned long long)pcb->pid, cpu->index,
		   sched_thread_status_name(thread->status));
	return thread;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
static inline __attribute__((naked)) void
context_switch(maybe_unused tcb_t *prev, maybe_unused tcb_t *next)
{
	__asm__("pushq %r15\n\t"
			"pushq %r14\n\t"
			"pushq %r13\n\t"
			"pushq %r12\n\t"
			"pushq %r11\n\t"
			"pushq %r10\n\t"
			"pushq %r9\n\t"
			"pushq %r8\n\t"
			"pushq %rsi\n\t"
			"pushq %rdi\n\t"
			"pushq %rbp\n\t"
			"pushq %rdx\n\t"
			"pushq %rcx\n\t"
			"pushq %rbx\n\t"
			"pushq %rax\n\t"
			"movq %rsp, 56(%rdi)\n\t"
			"movq 56(%rsi), %rsp\n\t"
			"popq %rax\n\t"
			"popq %rbx\n\t"
			"popq %rcx\n\t"
			"popq %rdx\n\t"
			"popq %rbp\n\t"
			"popq %rdi\n\t"
			"popq %rsi\n\t"
			"popq %r8\n\t"
			"popq %r9\n\t"
			"popq %r10\n\t"
			"popq %r11\n\t"
			"popq %r12\n\t"
			"popq %r13\n\t"
			"popq %r14\n\t"
			"popq %r15\n\t"
			"retq\n\t");
}
#pragma GCC diagnostic pop

static void sched_load_thread_state(tcb_t *thread)
{
	uint16_t data = (uint16_t)thread->ss;
	uint64_t code = thread->cs;

	__asm__ volatile("pushq %1\n\t"
					 "leaq 1f(%%rip), %%rax\n\t"
					 "pushq %%rax\n\t"
					 "lretq\n\t"
					 "1:\n\t"
					 "movw %0, %%ds\n\t"
					 "movw %0, %%es\n\t"
					 "movw %0, %%ss\n\t"
					 "pushq %2\n\t"
					 "popfq\n\t"
					 :
					 : "r"(data), "r"(code), "r"(thread->rflags)
					 : "rax", "memory");
}

static void sched_thread_trampoline(tcb_t *thread)
{
	sched_load_thread_state(thread);
	thread->entry(thread->arg);
	sched_exit();
}

static void sched_proc_unlink_thread(pcb_t *proc, tcb_t *thread)
{
	tcb_t **cur;

	if (!proc)
		return;

	cur = &proc->thread;
	while (*cur) {
		if (*cur == thread) {
			*cur = thread->next;
			if (proc->thread_count > 0)
				proc->thread_count--;
			return;
		}

		cur = &(*cur)->next;
	}
}

static void sched_free_proc(pcb_t *proc)
{
	if (!proc)
		return;

	sched_proc_release_pid(proc);

	if (PROC_IS_USER(proc) && proc->vas)
		vas_destroy(proc->vas);

	ktrace("reaped %s process %llu (%s)", sched_proc_type_name(proc->type),
		   (unsigned long long)proc->pid, proc->name);
	kfree(proc);
}

static void sched_free_thread(tcb_t *thread)
{
	pcb_t *proc;
	struct cpu_info *cpu;

	if (!thread || thread == cpu_current()->idle_thread)
		return;

	cpu = cpu_get(thread->cpu_index);
	if (cpu) {
		sched_cpu_lock(cpu);
		if (cpu->sched_thread_count > 0)
			cpu->sched_thread_count--;
		sched_cpu_unlock(cpu);
	}

	proc = thread->parent;
	sched_proc_unlink_thread(proc, thread);

	ktrace("reaped thread %llu (%s)", (unsigned long long)thread->tid,
		   thread->name);

	if (thread->stack)
		kfree(thread->stack);
	kfree(thread);

	if (proc && proc->thread_count == 0)
		sched_free_proc(proc);
}

static void sched_queue_zombie_locked(struct cpu_info *cpu, tcb_t *thread)
{
	if (!cpu || !thread || thread == cpu->idle_thread)
		return;

	if (!SCHED_IS_ZOMBIE(thread))
		thread->status = THREAD_ZOMBIE;

	rq_push_back(&zombie_rq[cpu->index], &thread->rq_node, thread);
}

static void sched_reap_cpu(struct cpu_info *cpu)
{
	tcb_t *thread;

	if (!cpu)
		return;

	for (;;) {
		sched_cpu_lock(cpu);
		thread = rq_pop_front(&zombie_rq[cpu->index]);
		sched_cpu_unlock(cpu);

		if (!thread)
			return;

		sched_free_thread(thread);
	}
}

void sched_reap(void)
{
	sched_reap_cpu(cpu_current());
}

static tcb_t *sched_next_thread(struct cpu_info *cpu)
{
	tcb_t *next;

	while ((next = rq_pop_front(&cpu->rq)) != NULL) {
		if (SCHED_IS_RUNNABLE(next) && next->rsp != 0)
			return next;

		if (SCHED_IS_ZOMBIE(next))
			sched_queue_zombie_locked(cpu, next);
	}

	return cpu->idle_thread;
}

static void sched_run_next_locked(struct cpu_info *cpu, tcb_t *current)
{
	tcb_t *next = sched_next_thread(cpu);

	if (!next)
		next = cpu->idle_thread;

	if (next == current) {
		current->status = THREAD_RUNNING;
		SCHED_TICK_RESET(current);
		sched_cpu_unlock(cpu);
		return;
	}

	next->status = THREAD_RUNNING;
	SCHED_TICK_RESET(next);
	cpu->current_thread = next;
	sched_cpu_unlock(cpu);
	context_switch(current, next);
}

void sched_tick(void)
{
	struct cpu_info *cpu = cpu_current();
	tcb_t *current;

	if (!cpu || !cpu->idle_thread)
		return;

	sched_reap_cpu(cpu);

	sched_cpu_lock(cpu);
	current = cpu->current_thread ? cpu->current_thread : cpu->idle_thread;
	current->total_ticks++;

	if (SCHED_IS_RUNNING(current)) {
		if (current->ticks_left > 0)
			current->ticks_left--;

		if (!SCHED_TICK_EXPIRED(current) &&
			(current != cpu->idle_thread || rq_empty(&cpu->rq))) {
			sched_cpu_unlock(cpu);
			return;
		}

		if (current != cpu->idle_thread) {
			current->status = THREAD_RUNNABLE;
			SCHED_TICK_RESET(current);
			rq_push_back(&cpu->rq, &current->rq_node, current);
		}
	}

	sched_run_next_locked(cpu, current);
}

void sched_yield(void)
{
	struct cpu_info *cpu = cpu_current();
	tcb_t *current;

	if (!cpu || !cpu->idle_thread)
		return;

	sched_cpu_lock(cpu);
	current = cpu->current_thread ? cpu->current_thread : cpu->idle_thread;

	if (current != cpu->idle_thread && !SCHED_IS_ZOMBIE(current)) {
		current->status = THREAD_RUNNABLE;
		SCHED_TICK_RESET(current);
		rq_push_back(&cpu->rq, &current->rq_node, current);
	}

	sched_run_next_locked(cpu, current);
}

void thread_block(tcb_t *thread)
{
	struct cpu_info *cpu;

	if (!thread)
		return;

	cpu = cpu_get(thread->cpu_index);
	if (!cpu || thread == cpu->idle_thread)
		return;

	sched_cpu_lock(cpu);
	thread->status = THREAD_BLOCKED;
	if (rq_contains(&cpu->rq, thread))
		rq_remove(&cpu->rq, thread);

	if (cpu == cpu_current() && cpu->current_thread == thread) {
		sched_run_next_locked(cpu, thread);
		return;
	}

	sched_cpu_unlock(cpu);
}

void thread_wake(tcb_t *thread)
{
	struct cpu_info *cpu;

	if (!thread)
		return;

	cpu = cpu_get(thread->cpu_index);
	if (!cpu || thread == cpu->idle_thread)
		return;

	sched_cpu_lock(cpu);
	if (SCHED_IS_BLOCKED(thread)) {
		thread->status = THREAD_RUNNABLE;
		SCHED_TICK_RESET(thread);
		if (!rq_contains(&cpu->rq, thread))
			rq_push_back(&cpu->rq, &thread->rq_node, thread);
	}
	sched_cpu_unlock(cpu);
}

void sched_exit(void)
{
	struct cpu_info *cpu = cpu_current();
	tcb_t *current;
	tcb_t *next;

	if (!cpu || !cpu->current_thread)
		kpanic(NULL, "sched: exit without current thread");

	current = cpu->current_thread;
	if (current == cpu->idle_thread)
		kpanic(NULL, "sched: idle thread tried to exit");

	ktrace("thread %llu (%s) exiting", (unsigned long long)current->tid,
		   current->name);
	sched_cpu_lock(cpu);
	sched_queue_zombie_locked(cpu, current);

	next = sched_next_thread(cpu);
	if (next == current || !next)
		next = cpu->idle_thread;

	next->status = THREAD_RUNNING;
	SCHED_TICK_RESET(next);
	cpu->current_thread = next;
	sched_cpu_unlock(cpu);

	context_switch(current, next);
	kpanic(NULL, "sched: zombie thread resumed");
}

void sched_init(void)
{
	struct cpu_info *cpu = cpu_current();
	pcb_t *idle_proc;
	tcb_t *idle_thread;

	if (!cpu)
		kpanic(NULL, "sched: no current CPU");

	if (cpu->idle_thread)
		return;

	idle_proc = &idle_procs[cpu->index];
	idle_thread = &idle_threads[cpu->index];

	cpu->sched_lock = 0;
	rq_init(&cpu->rq);
	rq_init(&zombie_rq[cpu->index]);
	cpu->sched_thread_count = 0;

	idle_proc->pid = SCHED_IDLE_PID;
	idle_proc->ppid = SCHED_IDLE_PID;
	strlcpy(idle_proc->name, "idle", sizeof(idle_proc->name));
	idle_proc->type = PROC_IDLE;
	idle_proc->thread = idle_thread;
	idle_proc->next = NULL;
	idle_proc->vas = kernel_vas;
	idle_proc->thread_count = 1;

	idle_thread->tid = SCHED_IDLE_PID;
	strlcpy(idle_thread->name, "idle", sizeof(idle_thread->name));
	idle_thread->status = THREAD_RUNNING;
	SCHED_TICK_RESET(idle_thread);
	idle_thread->total_ticks = 0;
	idle_thread->rip = (uint64_t)(uintptr_t)sched_idle;
	rq_node_init(&idle_thread->rq_node, idle_thread);
	idle_thread->next = NULL;
	idle_thread->parent = idle_proc;
	idle_thread->entry = sched_idle;
	idle_thread->arg = NULL;
	idle_thread->stack = idle_stacks[cpu->index];
	idle_thread->stack_size = sizeof(idle_stacks[cpu->index]);
	idle_thread->cs = GDT_KERNEL_CODE_SELECTOR;
	idle_thread->ss = GDT_KERNEL_DATA_SELECTOR;
	idle_thread->rflags = SCHED_KERNEL_RFLAGS;
	idle_thread->cpu_index = cpu->index;
	idle_thread->rsp =
		sched_make_stack(sched_thread_trampoline, idle_thread,
						 idle_stacks[cpu->index], SCHED_IDLE_STACK_QWORDS);

	cpu->idle_thread = idle_thread;
	cpu->current_thread = idle_thread;
	klog("setup idle process for CPU%d", cpu->index);
}
