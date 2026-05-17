#ifndef SYS_SCHED_H
#define SYS_SCHED_H

#include <stdint.h>
#include <stddef.h>

#include <lib/rq.h>

typedef struct vas vas_t;
typedef struct pcb pcb_t;

#define SCHED_QUANTUM_TICKS 2
#define SCHED_NAME_MAX 32

#define SCHED_INVALID_TID UINT64_MAX
#define SCHED_INVALID_PID UINT64_MAX
#define SCHED_IDLE_PID 0
#define SCHED_PID_MIN 1
#define SCHED_PID_MAX 65535

#define SCHED_ID_NONE UINT64_MAX
#define SCHED_STACK_SIZE 0x4000
#define SCHED_KERNEL_RFLAGS 0x202

#define SCHED_TICK_EXPIRED(t) ((t)->ticks_left == 0)
#define SCHED_TICK_RESET(t) ((t)->ticks_left = SCHED_QUANTUM_TICKS)

#define SCHED_THREAD_STATUS_X(X) \
	X(THREAD_RUNNABLE, "runnable") \
	X(THREAD_RUNNING, "running") \
	X(THREAD_BLOCKED, "blocked") \
	X(THREAD_ZOMBIE, "zombie")

#define SCHED_PROC_TYPE_X(X) \
	X(PROC_KERNEL, "kernel") \
	X(PROC_USER, "user") \
	X(PROC_IDLE, "idle")

#define SCHED_THREAD_IS(t, state) ((t) && (t)->status == (state))
#define SCHED_PROC_IS(p, kind) ((p) && (p)->type == (kind))

#define SCHED_IS_RUNNABLE(t) SCHED_THREAD_IS((t), THREAD_RUNNABLE)
#define SCHED_IS_RUNNING(t) SCHED_THREAD_IS((t), THREAD_RUNNING)
#define SCHED_IS_BLOCKED(t) SCHED_THREAD_IS((t), THREAD_BLOCKED)
#define SCHED_IS_ZOMBIE(t) SCHED_THREAD_IS((t), THREAD_ZOMBIE)

#define PROC_IS_KERNEL(p) SCHED_PROC_IS((p), PROC_KERNEL)
#define PROC_IS_USER(p) SCHED_PROC_IS((p), PROC_USER)
#define PROC_IS_IDLE(p) SCHED_PROC_IS((p), PROC_IDLE)

typedef enum {
#define SCHED_ENUM_VALUE(id, name) id,
	SCHED_THREAD_STATUS_X(SCHED_ENUM_VALUE)
#undef SCHED_ENUM_VALUE
} thread_status_t;

typedef enum {
#define SCHED_ENUM_VALUE(id, name) id,
	SCHED_PROC_TYPE_X(SCHED_ENUM_VALUE)
#undef SCHED_ENUM_VALUE
} proc_type_t;

typedef void (*sched_entry_t)(void *arg);

typedef struct tcb {
	uint64_t tid;
	char name[SCHED_NAME_MAX];

	thread_status_t status;

	uint32_t ticks_left;
	uint64_t total_ticks;

	uint64_t rsp;
	uint64_t rip;

	rq_node_t rq_node;
	struct tcb *next;

	pcb_t *parent;
	sched_entry_t entry;
	void *arg;
	void *stack;
	size_t stack_size;
	uint64_t cs;
	uint64_t ss;
	uint64_t rflags;
	uint32_t cpu_index;
} tcb_t;

struct pcb {
	uint64_t pid;
	uint64_t ppid;
	char name[SCHED_NAME_MAX];
	proc_type_t type;

	tcb_t *thread;
	struct pcb *next;
	vas_t *vas;
	uint32_t thread_count;
};

void sched_init(void);
tcb_t *sched_current(void);
void sched_tick(void);
void sched_yield(void);
void sched_reap(void);
pcb_t *spawn_proc(const char *name, proc_type_t type, vas_t *vas);
tcb_t *spawn_thread(const char *name, pcb_t *pcb, sched_entry_t entry,
					void *arg);
void thread_block(tcb_t *thread);
void thread_wake(tcb_t *thread);
void sched_exit(void) __attribute__((__noreturn__));

#endif // SYS_SCHED_H
