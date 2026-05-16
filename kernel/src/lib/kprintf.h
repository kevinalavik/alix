#ifndef LIB_KPRINTF_H
#define LIB_KPRINTF_H

#include <stdarg.h>
#include <stddef.h>

/*
 * my little kprintf impl, this thing supports (should be stable, but not guaranteed lmaooo):
 *   %s
 *   %c
 *   %d / %i
 *   %u
 *   %x / %X
 *   %p
 *   %zu / %zx
 *   %llu / %llx / %llX
 *   %%
 *
 * and a small width/zero-pad/left-align subset such as %06llu and %-5s.
 */

int kvsnprintf(char *buf, size_t bufsz, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t bufsz, const char *fmt, ...);

void vkprintf(const char *fmt, va_list ap);
void kprintf(const char *fmt, ...);

#endif // LIB_KPRINTF_H
