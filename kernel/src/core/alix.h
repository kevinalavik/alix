#ifndef CORE_ALIX_H
#define CORE_ALIX_H

#include <stdint.h>
#include <flanterm.h>

#define MAX_CPUS 256

#define maybe_unused __attribute__((unused))
#define noreturn __attribute__((noreturn))

extern uint64_t boot_tsc;
extern struct flanterm_context *ft_ctx;

#endif // CORE_ALIX_H
