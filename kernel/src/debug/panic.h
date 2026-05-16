#ifndef DEBUG_PANIC_H
#define DEBUG_PANIC_H

#include <stdarg.h>
#include <core/alix.h>
#include <cpu/idt.h>

void vkpanic(interrupt_frame_t *regs, const char *fmt, va_list ap) noreturn;
void kpanic(interrupt_frame_t *regs, const char *fmt, ...) noreturn;

#endif // DEBUG_PANIC_H
