#ifndef DEBUG_KALLSYMS_H
#define DEBUG_KALLSYMS_H

#include <stddef.h>
#include <stdint.h>

struct kallsyms_entry {
	uint64_t addr;
	uint64_t size;
	const char *name;
};

extern const struct kallsyms_entry kallsyms[];
extern const size_t kallsyms_count;

const struct kallsyms_entry *kallsyms_lookup(uint64_t addr, uint64_t *offset);

#endif // DEBUG_KALLSYMS_H
