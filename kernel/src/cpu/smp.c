#include <cpu/smp.h>
#include <sys/apic.h>
#include <core/alix.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>
#include <cpu/instr.h>
#include <sys/timer.h>
#include <mm/vmm.h>
#include <lib/atomic.h>
#include <lib/spinlock.h>

#define KLOG_NS "smp"
#include <log/klog.h>

static struct cpu_info cpu_infos[MAX_CPUS];
static uint32_t smp_cpu_count;
static spinlock_t smp_log_lock = SPINLOCK_INIT;

static void cpu_set_current(struct cpu_info *cpu)
{
	write_fs_base((uint64_t)(uintptr_t)cpu);
}

static void smp_log_cpu(struct cpu_info *cpu, const char *state)
{
	spinlock_lock(&smp_log_lock);
	klog("cpu%u: lapic=%u acpi=%u %s", cpu->index, cpu->lapic_id,
		 cpu->processor_id, state);
	spinlock_unlock(&smp_log_lock);
}

static noreturn void smp_ap_entry(struct limine_mp_info *mp_info)
{
	struct cpu_info *cpu =
		(struct cpu_info *)(uintptr_t)mp_info->extra_argument;

	cli();
	vas_switch(kernel_vas);
	gdt_init_cpu(cpu->index);
	cpu_set_current(cpu);
	idt_init();
	apic_cpu_init((uint8_t)cpu->index);
	timer_init(100);
	smp_log_cpu(cpu, "online");
	atomic_store(&cpu->online, 1, __ATOMIC_RELEASE);
	sti();
	for (;;)
		hlt();
	__builtin_unreachable();
}

struct cpu_info *cpu_current(void)
{
	uint64_t fs_base = read_fs_base();

	if (fs_base == 0)
		return NULL;

	return (struct cpu_info *)(uintptr_t)fs_base;
}

struct cpu_info *cpu_get(uint32_t index)
{
	if (index >= smp_cpu_count)
		return NULL;

	return &cpu_infos[index];
}

uint32_t cpu_count(void)
{
	return smp_cpu_count;
}

void smp_wait_all_online(void)
{
	for (uint32_t i = 0; i < smp_cpu_count; i++) {
		struct cpu_info *cpu = &cpu_infos[i];

		while (atomic_load(&cpu->online, __ATOMIC_ACQUIRE) == 0)
			atomic_cpu_relax();
	}

	klog("all %u CPUs online", smp_cpu_count);
}

void smp_init(struct limine_mp_response *mp_response)
{
	uint64_t limine_cpu_count = mp_response->cpu_count;

	if (limine_cpu_count > MAX_CPUS) {
		klog("limine reported %llu CPUs, clamping to %u",
			 (unsigned long long)limine_cpu_count, MAX_CPUS);
		limine_cpu_count = MAX_CPUS;
	}

	smp_cpu_count = (uint32_t)limine_cpu_count;

	for (uint32_t i = 0; i < smp_cpu_count; i++) {
		struct limine_mp_info *mp_info = mp_response->cpus[i];
		struct cpu_info *cpu = &cpu_infos[i];

		cpu->index = i;
		cpu->processor_id = mp_info->processor_id;
		cpu->lapic_id = mp_info->lapic_id;
		cpu->is_bsp = (mp_info->lapic_id == mp_response->bsp_lapic_id);
		cpu->online = cpu->is_bsp ? 1 : 0;
		cpu->mp_info = mp_info;

		mp_info->extra_argument = (uint64_t)(uintptr_t)cpu;
	}

	for (uint32_t i = 0; i < smp_cpu_count; i++) {
		struct cpu_info *cpu = &cpu_infos[i];

		if (cpu->is_bsp) {
			cpu_set_current(cpu);
			smp_log_cpu(cpu, "bsp online");
			continue;
		}

		atomic_store(&cpu->mp_info->goto_address, smp_ap_entry,
					 __ATOMIC_SEQ_CST);
	}
}
