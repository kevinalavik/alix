#include <sys/timer.h>
#include <core/alix.h>
#include <cpu/idt.h>
#include <cpu/instr.h>
#include <cpu/smp.h>
#include <lib/atomic.h>
#include <sys/apic.h>

static uint32_t timer_reload;
static volatile uint32_t timer_calibrating;
static volatile uint32_t timer_vector_installed;
static volatile uint64_t timer_ticks[MAX_CPUS];
static timer_tick_handler_t timer_tick_handler;

#define PIT_HZ 1193182ULL
#define TIMER_CAL_MS 10ULL
#define TIMER_DIVIDE 3
#define PIT_CALIBRATE_MS TIMER_CAL_MS
#define PIT_CALIBRATE_COUNT ((PIT_HZ * PIT_CALIBRATE_MS) / 1000ULL)
#define TIMER_APIC_MAX 0xFFFFFFFFu

#define PIT_CH2_ONESHOT(X) \
	X(0x43, 0xB0)              \
	X(0x42, pit_count & 0xff)  \
	X(0x42, pit_count >> 8)

static uint32_t timer_program_apic(uint32_t reload)
{
	if (reload == 0)
		reload = 1;

	lapic_write(APIC_TIMER_DIVIDE, TIMER_DIVIDE);
	lapic_write(APIC_LVT_TIMER, TIMER_VECTOR | APIC_LVT_TIMER_PERIODIC);
	lapic_write(APIC_TIMER_INITCNT, reload);

	return reload;
}

static struct cpu_info *timer_current_cpu(void)
{
	uint32_t lapic_id = lapic_read(APIC_ID) >> 24;

	for (uint32_t i = 0; i < cpu_count(); i++) {
		struct cpu_info *cpu = cpu_get(i);

		if (cpu != NULL && cpu->lapic_id == lapic_id)
			return cpu;
	}

	return cpu_current();
}

static uint32_t timer_calibrate_reload(uint32_t hz)
{
	uint16_t pit_count = (uint16_t)PIT_CALIBRATE_COUNT;
	uint8_t port61 = inb(0x61);
	uint32_t current;
	uint64_t elapsed;
	uint64_t reload;

	if (hz == 0)
		return 0;

	lapic_write(APIC_TIMER_DIVIDE, TIMER_DIVIDE);
	lapic_write(APIC_LVT_TIMER, TIMER_VECTOR | APIC_LVT_TIMER_PERIODIC);
	lapic_write(APIC_TIMER_INITCNT, TIMER_APIC_MAX);

#define TIMER_OUTB(port, value) outb((port), (uint8_t)(value));
	PIT_CH2_ONESHOT(TIMER_OUTB)
#undef TIMER_OUTB

	port61 &= ~0x02;
	port61 &= ~0x01;
	outb(0x61, port61);
	port61 |= 0x01;
	outb(0x61, port61);

	while ((inb(0x61) & 0x20) == 0)
		;

	current = lapic_read(APIC_TIMER_CURRCNT);
	elapsed = TIMER_APIC_MAX - current;
	reload = (elapsed * 100ULL) / hz;

	outb(0x61, port61);

	if (reload == 0)
		reload = 1;

	return (uint32_t)reload;
}

static interrupt_frame_t *timer_irq(interrupt_frame_t *frame)
{
	struct cpu_info *cpu = timer_current_cpu();
	uint32_t index = cpu ? cpu->index : 0;

	if (index < MAX_CPUS)
		atomic_fetch_add(&timer_ticks[index], 1, __ATOMIC_RELAXED);

	apic_send_eoi();

	if (timer_tick_handler != NULL)
		return timer_tick_handler(frame);

	return frame;
}

void timer_on_tick(timer_tick_handler_t handler)
{
	timer_tick_handler = handler;
}

uint64_t timer_cpu_ticks(uint32_t cpu_index)
{
	if (cpu_index >= MAX_CPUS)
		return 0;

	return atomic_load(&timer_ticks[cpu_index], __ATOMIC_RELAXED);
}

void timer_init(uint32_t hz)
{
	struct cpu_info *cpu = cpu_current();
	uint32_t reload;

	if (!cpu)
		return;

	uint32_t expected = 0;
	if (atomic_compare_exchange(&timer_vector_installed, &expected, 1,
								__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		irq_install_vector(TIMER_VECTOR, timer_irq, NULL, cpu->lapic_id);

	reload = atomic_load(&timer_reload, __ATOMIC_ACQUIRE);
	if (reload == 0) {
		expected = 0;
		if (atomic_compare_exchange(&timer_calibrating, &expected, 1,
									__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			reload = timer_calibrate_reload(hz);
			if (reload == 0)
				reload = 1;
			atomic_store(&timer_reload, reload, __ATOMIC_RELEASE);
			atomic_store(&timer_calibrating, 0, __ATOMIC_RELEASE);
		} else {
			while ((reload = atomic_load(&timer_reload, __ATOMIC_ACQUIRE)) == 0)
				atomic_cpu_relax();
		}
	}

	timer_program_apic(reload);
}
