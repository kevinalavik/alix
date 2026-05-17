#include <dev/pit.h>
#include <stdint.h>
#include <cpu/instr.h>
#include <core/alix.h>
#include <sys/time.h>

#define PIT_HZ 1193182ULL
#define PIT_CALIBRATE_MS 50ULL
#define PIT_CALIBRATE_COUNT ((PIT_HZ * PIT_CALIBRATE_MS) / 1000ULL)

extern uint64_t boot_tsc;

static uint64_t pit_tsc_hz;

static uint64_t pit_tsc_delta_to_us(uint64_t delta)
{
	if (pit_tsc_hz == 0)
		return 0;

	return (delta / pit_tsc_hz) * 1000000ULL +
		   ((delta % pit_tsc_hz) * 1000000ULL) / pit_tsc_hz;
}

static uint64_t pit_time_read_us(void)
{
	uint64_t now;

	if (boot_tsc == 0)
		return 0;

	now = rdtsc();

	if (now < boot_tsc)
		return 0;

	return pit_tsc_delta_to_us(now - boot_tsc);
}

static uint64_t pit_time_read_hz(void)
{
	return pit_tsc_hz;
}

static const struct time_source pit_tsc_time_source = {
	.name = "pit-tsc",
	.read_us = pit_time_read_us,
	.read_hz = pit_time_read_hz,
	.stable = true,
};

uint64_t pit_calibrate_tsc(void)
{
	uint64_t start;
	uint64_t end;
	uint16_t count = (uint16_t)PIT_CALIBRATE_COUNT;
	uint8_t port61;

	/*
	 * PIT channel 2, mode 0, lobyte/hibyte.
	 */
	outb(0x43, 0xB0);

	outb(0x42, count & 0xff);
	outb(0x42, count >> 8);

	/*
	 * Disable speaker, reset gate.
	 */
	port61 = inb(0x61);
	port61 &= ~0x02;
	port61 &= ~0x01;
	outb(0x61, port61);

	/*
	 * Start timer by raising gate.
	 */
	port61 |= 0x01;
	outb(0x61, port61);

	start = rdtsc();

	while ((inb(0x61) & 0x20) == 0) {
		/* wait */
	}

	end = rdtsc();

	return ((end - start) * PIT_HZ) / count;
}

void pit_init(void)
{
	pit_tsc_hz = pit_calibrate_tsc();

	if (pit_tsc_hz != 0)
		time_register_source(&pit_tsc_time_source);
}
