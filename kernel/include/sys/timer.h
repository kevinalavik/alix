#ifndef SYS_TIMER_H
#define SYS_TIMER_H

#include <cpu/idt.h>
#include <stdint.h>

#define TIMER_VECTOR 0xF0

typedef interrupt_frame_t *(*timer_tick_handler_t)(interrupt_frame_t *frame);

void timer_init(uint32_t hz);
void timer_on_tick(timer_tick_handler_t handler);
uint64_t timer_cpu_ticks(uint32_t cpu_index);

#endif
