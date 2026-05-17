#include <mm/mm.h>
#define KLOG_NS "pfndb"
#include <log/klog.h>
#include <debug/panic.h>
#include <lib/string.h>

static page_t *mem_map;
static uint64_t max_pfn;
static uint64_t pfndb_phys;

static const char *memmap_type_name(uint64_t type)
{
	switch (type) {
	case LIMINE_MEMMAP_USABLE:
		return "usable";
	case LIMINE_MEMMAP_RESERVED:
		return "reserved";
	case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
		return "acpi";
	case LIMINE_MEMMAP_ACPI_NVS:
		return "acpi-nvs";
	case LIMINE_MEMMAP_BAD_MEMORY:
		return "bad";
	case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
		return "boot";
	case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
		return "kernel";
	case LIMINE_MEMMAP_FRAMEBUFFER:
		return "fb";
	case LIMINE_MEMMAP_RESERVED_MAPPED:
		return "reserved-mapped";
	default:
		return "unknown";
	}
}

static void calc_max_pfn(struct limine_memmap_response *memmap)
{
	max_pfn = 0;

	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *e = memmap->entries[i];
		klogvv("memmap[%llu]: %s base=0x%llx len=0x%llx end=0x%llx",
			   (unsigned long long)i, memmap_type_name(e->type),
			   (unsigned long long)e->base, (unsigned long long)e->length,
			   (unsigned long long)(e->base + e->length));
		if (e->type != LIMINE_MEMMAP_USABLE)
			continue;

		uint64_t end_pfn = PAGE_ALIGN_UP(e->base + e->length) >> PAGE_SHIFT;
		if (end_pfn > max_pfn)
			max_pfn = end_pfn;
	}
}

static void reserve_pfndb_storage(struct limine_memmap_response *memmap)
{
	uint64_t size = (uint64_t)(max_pfn + 1) * sizeof(page_t);
	size = PAGE_ALIGN_UP(size);

	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *e = memmap->entries[i];
		if (e->type != LIMINE_MEMMAP_USABLE || e->length < size)
			continue;

		pfndb_phys = e->base;
		e->base += size;
		e->length -= size;
		klogv("reserved db storage: phys=0x%llx size=0x%llx pages=%llu",
			  (unsigned long long)pfndb_phys, (unsigned long long)size,
			  (unsigned long long)(size >> PAGE_SHIFT));
		return;
	}

	kpanic(NULL,
		   "pfndb: no usable region large enough for database (%llu bytes)",
		   size);
}

void pfndb_mark_range(uint64_t base, uint64_t length, uint64_t new_flags)
{
	uint64_t start_pfn = base >> PAGE_SHIFT;
	uint64_t end_pfn = PAGE_ALIGN_UP(base + length) >> PAGE_SHIFT;
	const uint64_t state_mask =
		PAGE_FREE | PAGE_USED | PAGE_RESERVED | PAGE_POISON | PAGE_SLAB;

	if (start_pfn > max_pfn)
		return;
	if (end_pfn > max_pfn + 1)
		end_pfn = max_pfn + 1;

	klogvv("mark pfn [0x%llx,0x%llx) flags=0x%llx",
		   (unsigned long long)start_pfn, (unsigned long long)end_pfn,
		   (unsigned long long)new_flags);

	for (uint64_t pfn = start_pfn; pfn < end_pfn; pfn++)
		mem_map[pfn].flags = (mem_map[pfn].flags & ~state_mask) | new_flags;
}

void pfndb_init(struct limine_memmap_response *memmap)
{
	uint64_t usable_pages = 0;

	calc_max_pfn(memmap);
	reserve_pfndb_storage(memmap);

	mem_map = (page_t *)PHYS_TO_VIRT(pfndb_phys);

	for (uint64_t pfn = 0; pfn <= max_pfn; pfn++) {
		page_t *p = &mem_map[pfn];
		p->flags = PAGE_RESERVED;
		p->order = 0;
		p->private = 0;
		atomic_init(&p->refcount, 0);
		p->list.next = (pfn < max_pfn) ? &mem_map[pfn + 1] : NULL;
		p->list.prev = (pfn > 0) ? &mem_map[pfn - 1] : NULL;
	}

	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *e = memmap->entries[i];
		if (e->type != LIMINE_MEMMAP_USABLE)
			continue;
		pfndb_mark_range(e->base, e->length, PAGE_FREE);
		usable_pages += PAGE_ALIGN_UP(e->length) >> PAGE_SHIFT;
	}

	klog("pages=%llu db=0x%llx", max_pfn + 1, pfndb_phys);
	klogv("max_pfn=0x%llx usable=%llu entry=%zu", max_pfn, usable_pages,
		  sizeof(page_t));
}

page_t *pfndb_getdb(void)
{
	return mem_map;
}
uint64_t pfndb_getmax(void)
{
	return max_pfn;
}

uint64_t pfndb_pfnaddr(uint64_t pfn)
{
	if (pfn > max_pfn)
		return 0;
	return PFN_PHYS(pfn);
}

page_t *pfndb_getptr(uint64_t pfn)
{
	if (pfn > max_pfn)
		return NULL;
	return &mem_map[pfn];
}

uint64_t pfndb_getpfn(const page_t *page)
{
	if (!page || !mem_map)
		return (uint64_t)-1;
	if (page < mem_map || page > &mem_map[max_pfn])
		return (uint64_t)-1;
	return (uint64_t)(page - mem_map);
}

uint64_t pfndb_page_to_phys(const page_t *page)
{
	uint64_t pfn = pfndb_getpfn(page);
	if (pfn == (uint64_t)-1)
		return 0;
	return PFN_PHYS(pfn);
}

page_t *pfndb_phys_to_page(uint64_t phys)
{
	uint64_t pfn = PHYS_PFN(phys);
	if (pfn > max_pfn)
		return NULL;
	return &mem_map[pfn];
}
