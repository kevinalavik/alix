#include <debug/kallsyms.h>

const struct kallsyms_entry *kallsyms_lookup(uint64_t addr, uint64_t *offset)
{
	size_t lo = 0;
	size_t hi = kallsyms_count;
	const struct kallsyms_entry *best = NULL;

	while (lo < hi) {
		size_t mid = lo + ((hi - lo) / 2);
		const struct kallsyms_entry *sym = &kallsyms[mid];

		if (sym->addr <= addr) {
			best = sym;
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}

	if (best == NULL)
		return NULL;

	if (offset != NULL)
		*offset = addr - best->addr;

	return best;
}
