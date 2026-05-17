#ifndef CPU_SMP_H
#define CPU_SMP_H

#include <limine.h>
#include <stdbool.h>
#include <stdint.h>
#include <lib/rq.h>

typedef struct tcb tcb_t;

struct cpu_info {
	uint32_t index;
	uint32_t processor_id;
	uint32_t lapic_id;
	bool is_bsp;
	volatile uint32_t online;
	struct limine_mp_info *mp_info;

	/* sched */
	volatile uint32_t sched_lock;
	rq_t rq;
	tcb_t *idle_thread;
	tcb_t *current_thread;
	uint32_t sched_thread_count;
};

void smp_init(struct limine_mp_response *mp_response);
void smp_wait_all_online(void);
struct cpu_info *cpu_current(void);
struct cpu_info *cpu_get(uint32_t index);
uint32_t cpu_count(void);

#endif // CPU_SMP_H
