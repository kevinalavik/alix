#include <mm/mm.h>
#define KLOG_NS "paging"
#include <log/klog.h>
#include <debug/panic.h>
#include <cpu/instr.h>
#include <string.h>
#include <stdbool.h>

static vas_t _kvas;
vas_t *kernel_vas = &_kvas;
static vas_t *current_vas;
static uint64_t page_table_count;

static vad_t kernel_image_vad;
static vad_t hhdm_vad;
static vad_t framebuffer_vad;

static inline uint16_t pml4_index(uint64_t virt)
{
	return (virt >> 39) & 0x1ff;
}

static inline uint16_t pdpt_index(uint64_t virt)
{
	return (virt >> 30) & 0x1ff;
}

static inline uint16_t pd_index(uint64_t virt)
{
	return (virt >> 21) & 0x1ff;
}

static inline uint16_t pt_index(uint64_t virt)
{
	return (virt >> 12) & 0x1ff;
}

static inline ptable_t *table_virt(uint64_t phys)
{
	return PHYS_TO_VIRT(phys & PAGE_ADDR_MASK);
}

static uint64_t flags_to_pte(uint64_t flags)
{
	uint64_t entry = PAGE_PRESENT;

	if (flags & PAGE_WRITE)
		entry |= PAGE_WRITE;
	if (flags & PAGE_USER)
		entry |= PAGE_USER;
	if (flags & PAGE_GLOBAL)
		entry |= PAGE_GLOBAL;
	if (flags & PAGE_NX)
		entry |= PAGE_NX;

	return entry;
}

static uint64_t alloc_table(void)
{
	page_t *page = pmm_alloc_page();

	if (!page)
		return 0;

	uint64_t phys = page_to_phys(page);
	memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
	page_table_count++;
	klogvv("page table alloc: phys=0x%llx count=%llu", (unsigned long long)phys,
		   (unsigned long long)page_table_count);
	return phys;
}

static ptable_t *next_table(ptable_t *table, uint16_t index, uint64_t flags,
							_Bool create)
{
	uint64_t entry = table[index];

	if (entry & PAGE_PRESENT)
		return table_virt(entry);

	if (!create)
		return NULL;

	uint64_t phys = alloc_table();
	if (!phys)
		return NULL;

	table[index] = phys | PAGE_PRESENT | PAGE_WRITE;
	if (flags & PAGE_USER)
		table[index] |= PAGE_USER;
	return table_virt(phys);
}

static ptable_t *lookup_pt(vas_t *vas, uint64_t virt, _Bool create,
						   uint64_t flags)
{
	ptable_t *pml4 = vas->pml4;
	ptable_t *pdpt = next_table(pml4, pml4_index(virt), flags, create);
	if (!pdpt)
		return NULL;

	ptable_t *pd = next_table(pdpt, pdpt_index(virt), flags, create);
	if (!pd)
		return NULL;

	return next_table(pd, pd_index(virt), flags, create);
}

static ptable_t *lookup_leaf(vas_t *vas, uint64_t virt, _Bool create,
							 uint64_t flags)
{
	ptable_t *pt = lookup_pt(vas, virt, create, flags);
	if (!pt)
		return NULL;

	return &pt[pt_index(virt)];
}

static int map_kernel_segment(const char *name, uint64_t virt_start,
							  uint64_t virt_end, uint64_t kernel_phys_base,
							  uint64_t kernel_virt_base, uint64_t flags)
{
	uint64_t start = PAGE_ALIGN_DOWN(virt_start);
	uint64_t end = PAGE_ALIGN_UP(virt_end);
	uint64_t phys;

	if (end <= start)
		return 0;

	phys = kernel_phys_base + (start - kernel_virt_base);
	klogv("%s: virt=0x%llx phys=0x%llx size=0x%llx flags=0x%llx", name,
		  (unsigned long long)start, (unsigned long long)phys,
		  (unsigned long long)(end - start), (unsigned long long)flags);
	return paging_map_range(kernel_vas, start, phys, end - start, flags);
}

static uint64_t memmap_top(struct limine_memmap_response *memmap)
{
	uint64_t top = 0;

	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *e = memmap->entries[i];
		uint64_t end = e->base + e->length;
		if (end > top)
			top = end;
	}

	return PAGE_ALIGN_UP(top);
}

static int map_hhdm(struct limine_memmap_response *memmap)
{
	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *e = memmap->entries[i];
		uint64_t start = PAGE_ALIGN_DOWN(e->base);
		uint64_t end = PAGE_ALIGN_UP(e->base + e->length);

		if (end <= start)
			continue;

		klogvv("map hhdm: virt=0x%llx phys=0x%llx size=0x%llx",
			   (unsigned long long)(hhdm_offset + start),
			   (unsigned long long)start, (unsigned long long)(end - start));
		if (paging_map_range(
				kernel_vas, hhdm_offset + start, start, end - start,
				PAGE_PRESENT | PAGE_WRITE | PAGE_GLOBAL | PAGE_NX) != 0)
			return -1;
	}

	return 0;
}

static int map_pfndb(void)
{
	uint64_t phys = VIRT_TO_PHYS((uint64_t)pfndb_getdb());
	uint64_t start = PAGE_ALIGN_DOWN(phys);
	uint64_t end = PAGE_ALIGN_UP(phys + (pfndb_getmax() + 1) * sizeof(page_t));

	klogv("map pfndb: virt=0x%llx phys=0x%llx size=0x%llx",
		  (unsigned long long)(hhdm_offset + start), (unsigned long long)start,
		  (unsigned long long)(end - start));
	return paging_map_range(kernel_vas, hhdm_offset + start, start, end - start,
							PAGE_PRESENT | PAGE_WRITE | PAGE_GLOBAL | PAGE_NX);
}

static int map_framebuffer(struct limine_framebuffer *framebuffer)
{
	if (!framebuffer || !framebuffer->address)
		return 0;

	uint64_t virt = (uint64_t)framebuffer->address;
	uint64_t size = framebuffer->pitch * framebuffer->height;
	uint64_t start = PAGE_ALIGN_DOWN(virt);
	uint64_t end = PAGE_ALIGN_UP(virt + size);
	uint64_t phys = start;

	if (virt >= hhdm_offset)
		phys = VIRT_TO_PHYS(start);

	klogv("map fb: virt=0x%llx phys=0x%llx size=0x%llx",
		  (unsigned long long)start, (unsigned long long)phys,
		  (unsigned long long)(end - start));
	return paging_map_range(kernel_vas, start, phys, end - start,
							PAGE_PRESENT | PAGE_WRITE | PAGE_GLOBAL | PAGE_NX);
}

static void add_static_vad(vad_t *vad, uint64_t start, uint64_t end,
						   uint64_t flags)
{
	vad->start = start;
	vad->end = end;
	vad->flags = flags;
	vad->next = NULL;

	klogvv("vad add: [0x%llx,0x%llx) flags=0x%llx", (unsigned long long)start,
		   (unsigned long long)end, (unsigned long long)flags);
	if (vas_add(kernel_vas, vad) != 0)
		kpanic(NULL, "paging: vad overlap for [0x%llx, 0x%llx)",
			   (unsigned long long)start, (unsigned long long)end);
}

static void destroy_level(ptable_t *table, int level)
{
	if (level <= 1)
		return;

	for (uint64_t i = 0; i < 512; i++) {
		uint64_t entry = table[i];
		page_t *page;

		if (!(entry & PAGE_PRESENT))
			continue;
		destroy_level(table_virt(entry), level - 1);

		page = phys_to_page(entry & PAGE_ADDR_MASK);
		if (page && !PageReserved(page))
			pmm_free(page);
	}
}

ptable_t *ptable_create(void)
{
	uint64_t phys = alloc_table();
	ptable_t *pt;

	if (!phys)
		return NULL;

	pt = table_virt(phys);
	if (kernel_vas->pml4) {
		for (uint64_t i = 256; i < 512; i++)
			pt[i] = kernel_vas->pml4[i];
	}

	return pt;
}

void ptable_destroy(ptable_t *pt)
{
	page_t *root;

	if (!pt)
		return;

	for (uint64_t i = 0; i < 256; i++) {
		uint64_t entry = pt[i];
		page_t *page;

		if (!(entry & PAGE_PRESENT))
			continue;

		destroy_level(table_virt(entry), 3);
		page = phys_to_page(entry & PAGE_ADDR_MASK);
		if (page && !PageReserved(page))
			pmm_free(page);
	}

	root = phys_to_page(VIRT_TO_PHYS(pt));
	if (root && !PageReserved(root))
		pmm_free(root);
}

vas_t *paging_current_vas(void)
{
	return current_vas;
}

void vas_switch(vas_t *vas)
{
	if (!vas || !vas->pml4)
		kpanic(NULL, "paging: invalid vas switch");

	klogvv("switch vas: pml4=0x%llx",
		   (unsigned long long)VIRT_TO_PHYS(vas->pml4));
	write_cr3(VIRT_TO_PHYS(vas->pml4));
	current_vas = vas;
}

int paging_map_page(vas_t *vas, uint64_t virt, uint64_t phys, uint64_t flags)
{
	ptable_t *leaf;

	if (!vas || !vas->pml4)
		return -1;
	if ((virt & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1)))
		return -1;

	leaf = lookup_leaf(vas, virt, 1, flags);
	if (!leaf)
		return -1;

	*leaf = (phys & PAGE_ADDR_MASK) | flags_to_pte(flags);
	if (vas == current_vas)
		invlpg(virt);
	return 0;
}

int paging_map_range(vas_t *vas, uint64_t virt, uint64_t phys, uint64_t size,
					 uint64_t flags)
{
	uint64_t pages = PAGE_ALIGN_UP(size) >> PAGE_SHIFT;
	uint64_t pte_flags;
	_Bool flush;

	if (!size)
		return 0;
	if (!vas || !vas->pml4)
		return -1;
	if ((virt & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1)))
		return -1;

	pte_flags = flags_to_pte(flags);
	flush = vas == current_vas;

	klogvv("map range: virt=0x%llx phys=0x%llx size=0x%llx flags=0x%llx",
		   (unsigned long long)virt, (unsigned long long)phys,
		   (unsigned long long)size, (unsigned long long)flags);

	while (pages > 0) {
		ptable_t *pt = lookup_pt(vas, virt, 1, flags);
		uint64_t index;
		uint64_t run;

		if (!pt)
			return -1;

		index = pt_index(virt);
		run = 512 - index;
		if (run > pages)
			run = pages;

		for (uint64_t i = 0; i < run; i++) {
			pt[index + i] = (phys & PAGE_ADDR_MASK) | pte_flags;
			if (flush)
				invlpg(virt);
			virt += PAGE_SIZE;
			phys += PAGE_SIZE;
		}

		pages -= run;
	}

	return 0;
}

void paging_unmap_page(vas_t *vas, uint64_t virt)
{
	ptable_t *leaf;

	if (!vas || !vas->pml4 || (virt & (PAGE_SIZE - 1)))
		return;

	leaf = lookup_leaf(vas, virt, 0, 0);
	if (!leaf)
		return;

	klogvv("unmap page: virt=0x%llx phys=0x%llx", (unsigned long long)virt,
		   (unsigned long long)(*leaf & PAGE_ADDR_MASK));
	*leaf = 0;
	if (vas == current_vas)
		invlpg(virt);
}

uint64_t paging_virt_to_phys(vas_t *vas, uint64_t virt)
{
	ptable_t *leaf;

	if (!vas || !vas->pml4)
		return 0;

	leaf = lookup_leaf(vas, virt, 0, 0);
	if (!leaf || !(*leaf & PAGE_PRESENT))
		return 0;

	return (*leaf & PAGE_ADDR_MASK) | (virt & (PAGE_SIZE - 1));
}

uint64_t paging_get_flags(vas_t *vas, uint64_t virt)
{
	ptable_t *leaf;

	if (!vas || !vas->pml4)
		return 0;

	leaf = lookup_leaf(vas, virt, 0, 0);
	if (!leaf || !(*leaf & PAGE_PRESENT))
		return 0;

	return *leaf &
		   (PAGE_PRESENT | PAGE_WRITE | PAGE_USER | PAGE_GLOBAL | PAGE_NX);
}

void paging_init(struct limine_memmap_response *memmap,
				 struct limine_framebuffer *framebuffer,
				 struct limine_executable_address_response *kernel_addr)
{
	uint64_t pml4_phys;

	if (!memmap)
		kpanic(NULL, "paging: missing memory map");
	if (!kernel_addr)
		kpanic(NULL, "paging: missing executable address response");
	if (kernel_addr->virtual_base != (uint64_t)__kernel_start)
		kpanic(NULL, "paging: linker base 0x%llx != Limine base 0x%llx",
			   (unsigned long long)(uint64_t)__kernel_start,
			   (unsigned long long)kernel_addr->virtual_base);

	write_cr4(read_cr4() | X86_CR4_PGE);

	memset(&_kvas, 0, sizeof(_kvas));
	page_table_count = 0;
	pml4_phys = alloc_table();
	if (!pml4_phys)
		kpanic(NULL, "paging: failed to allocate kernel PML4");

	_kvas.pml4 = table_virt(pml4_phys);
	_kvas.user_start = VAS_USER_START;

	kernel_vas = &_kvas;

	if (map_hhdm(memmap) != 0)
		kpanic(NULL, "paging: failed to map HHDM");
	if (map_pfndb() != 0)
		kpanic(NULL, "paging: failed to map PFNDB");

	if (map_kernel_segment("map boot requests", (uint64_t)__kernel_start,
						   (uint64_t)__text_start, kernel_addr->physical_base,
						   kernel_addr->virtual_base,
						   PAGE_PRESENT | PAGE_WRITE | PAGE_GLOBAL) != 0)
		kpanic(NULL, "paging: failed to map Limine request segment");
	if (map_kernel_segment("map kernel text", (uint64_t)__text_start,
						   (uint64_t)__text_end, kernel_addr->physical_base,
						   kernel_addr->virtual_base,
						   PAGE_PRESENT | PAGE_GLOBAL) != 0)
		kpanic(NULL, "paging: failed to map kernel text");
	if (map_kernel_segment("map kernel rodata", (uint64_t)__rodata_start,
						   (uint64_t)__rodata_end, kernel_addr->physical_base,
						   kernel_addr->virtual_base,
						   PAGE_PRESENT | PAGE_GLOBAL | PAGE_NX) != 0)
		kpanic(NULL, "paging: failed to map kernel rodata");
	if (map_kernel_segment(
			"map kernel data", (uint64_t)__data_start, (uint64_t)__kernel_end,
			kernel_addr->physical_base, kernel_addr->virtual_base,
			PAGE_PRESENT | PAGE_WRITE | PAGE_GLOBAL | PAGE_NX) != 0)
		kpanic(NULL, "paging: failed to map kernel data");
	if (map_framebuffer(framebuffer) != 0)
		kpanic(NULL, "paging: failed to map framebuffer");

	add_static_vad(&hhdm_vad, hhdm_offset, hhdm_offset + memmap_top(memmap),
				   PAGE_PRESENT | PAGE_WRITE | PAGE_GLOBAL | PAGE_NX |
					   VAD_HHDM | VAD_KERNEL);
	add_static_vad(&kernel_image_vad, (uint64_t)__kernel_start,
				   (uint64_t)__kernel_end,
				   PAGE_PRESENT | PAGE_WRITE | PAGE_GLOBAL | VAD_KERNEL);
	if (framebuffer && framebuffer->address) {
		uint64_t start = PAGE_ALIGN_DOWN((uint64_t)framebuffer->address);
		uint64_t end = PAGE_ALIGN_UP((uint64_t)framebuffer->address +
									 framebuffer->pitch * framebuffer->height);
		if (!vas_find(kernel_vas, start))
			add_static_vad(&framebuffer_vad, start, end,
						   PAGE_PRESENT | PAGE_WRITE | PAGE_GLOBAL | PAGE_NX |
							   VAD_DEVICE | VAD_KERNEL);
	}

	vas_switch(kernel_vas);
	klogv("global pages on");
	klogv("pml4=0x%llx tables=%llu kernel=%p-%p hhdm=%p",
		  (unsigned long long)VIRT_TO_PHYS(_kvas.pml4),
		  (unsigned long long)page_table_count, __kernel_start, __kernel_end,
		  (void *)hhdm_offset);
}
