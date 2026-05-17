#ifndef CPU_SMP_H
#define CPU_SMP_H

#include <limine.h>
#include <stdbool.h>
#include <stdint.h>

struct cpu_info {
	uint32_t index;
	uint32_t processor_id;
	uint32_t lapic_id;
	bool is_bsp;
	volatile uint32_t online;
	struct limine_mp_info *mp_info;
};

void smp_init(struct limine_mp_response *mp_response);
void smp_wait_all_online(void);
struct cpu_info *cpu_current(void);
struct cpu_info *cpu_get(uint32_t index);
uint32_t cpu_count(void);

#endif // CPU_SMP_H
