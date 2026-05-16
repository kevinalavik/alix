#include <mm/mm.h>
#define KLOG_NS "vmm"
#include <log/klog.h>
#include <debug/panic.h>
#include <cpu/instr.h>
#include <string.h>

#define PTE_PRESENT (1ull << 0)
#define PTE_WRITE (1ull << 1)
#define PTE_USER (1ull << 2)
#define PTE_GLOBAL (1ull << 8)
#define PTE_NX (1ull << 63)
#define PTE_ADDR_MASK 0x000ffffffffff000ull

typedef uint64_t pte_t;

static vmm_space_t kernel_space;
static vmm_space_t *current_space;
static _Bool nx_enabled;
static uint64_t page_table_count;

static vm_area_t kernel_image_area;
static vm_area_t hhdm_area;
static vm_area_t framebuffer_area;

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

static inline pte_t *table_virt(uint64_t phys)
{
	return PHYS_TO_VIRT(phys & PTE_ADDR_MASK);
}

static const char *prot_name(uint64_t prot, char buf[8])
{
	buf[0] = (prot & VMM_PROT_READ) ? 'r' : '-';
	buf[1] = (prot & VMM_PROT_WRITE) ? 'w' : '-';
	buf[2] = (prot & VMM_PROT_EXEC) ? 'x' : '-';
	buf[3] = (prot & VMM_PROT_USER) ? 'u' : 'k';
	buf[4] = (prot & VMM_PROT_GLOBAL) ? 'g' : '-';
	buf[5] = '\0';
	return buf;
}

static void log_range_v(const char *what, uint64_t virt, uint64_t phys,
						uint64_t size, uint64_t prot)
{
	char pbuf[8];

	klogv("%s: virt=0x%llx phys=0x%llx size=0x%llx prot=%s", what,
		  (unsigned long long)virt, (unsigned long long)phys,
		  (unsigned long long)size, prot_name(prot, pbuf));
}

static void log_range_vv(const char *what, uint64_t virt, uint64_t phys,
						 uint64_t size, uint64_t prot)
{
	char pbuf[8];

	klogvv("%s: virt=0x%llx phys=0x%llx size=0x%llx prot=%s", what,
		   (unsigned long long)virt, (unsigned long long)phys,
		   (unsigned long long)size, prot_name(prot, pbuf));
}

static void enable_nx_if_available(void)
{
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;

	cpuid(0x80000001u, 0, &eax, &ebx, &ecx, &edx);
	if (!(edx & (1u << 20))) {
		klogv("nx: unsupported");
		return;
	}

	wrmsr(X86_MSR_EFER, rdmsr(X86_MSR_EFER) | X86_EFER_NXE);
	nx_enabled = 1;
	klogv("nx: enabled");
}

static uint64_t prot_to_pte(uint64_t prot)
{
	uint64_t flags = PTE_PRESENT;

	if (prot & VMM_PROT_WRITE)
		flags |= PTE_WRITE;
	if (prot & VMM_PROT_USER)
		flags |= PTE_USER;
	if (prot & VMM_PROT_GLOBAL)
		flags |= PTE_GLOBAL;
	if (nx_enabled && !(prot & VMM_PROT_EXEC))
		flags |= PTE_NX;

	return flags;
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

static pte_t *next_table(pte_t *table, uint16_t index, uint64_t prot,
						 _Bool create)
{
	pte_t entry = table[index];

	if (entry & PTE_PRESENT)
		return table_virt(entry);

	if (!create)
		return NULL;

	uint64_t phys = alloc_table();
	if (!phys)
		return NULL;

	table[index] = phys | PTE_PRESENT | PTE_WRITE |
				   ((prot & VMM_PROT_USER) ? PTE_USER : 0);
	return table_virt(phys);
}

static pte_t *lookup_leaf(vmm_space_t *space, uint64_t virt, _Bool create,
						  uint64_t prot)
{
	pte_t *pml4 = table_virt(space->pml4_phys);
	pte_t *pdpt = next_table(pml4, pml4_index(virt), prot, create);
	if (!pdpt)
		return NULL;

	pte_t *pd = next_table(pdpt, pdpt_index(virt), prot, create);
	if (!pd)
		return NULL;

	pte_t *pt = next_table(pd, pd_index(virt), prot, create);
	if (!pt)
		return NULL;

	return &pt[pt_index(virt)];
}

static int map_kernel_segment(const char *name, uint64_t virt_start,
							  uint64_t virt_end, uint64_t kernel_phys_base,
							  uint64_t kernel_virt_base, uint64_t prot)
{
	uint64_t start = PAGE_ALIGN_DOWN(virt_start);
	uint64_t end = PAGE_ALIGN_UP(virt_end);

	if (end <= start)
		return 0;

	log_range_v(name, start, kernel_phys_base + (start - kernel_virt_base),
				end - start, prot);
	return vmm_map_range(&kernel_space, start,
						 kernel_phys_base + (start - kernel_virt_base),
						 end - start, prot);
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

		log_range_vv("map hhdm", hhdm_offset + start, start, end - start,
					 VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_GLOBAL);
		if (vmm_map_range(
				&kernel_space, hhdm_offset + start, start, end - start,
				VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_GLOBAL) != 0)
			return -1;
	}

	return 0;
}

static int map_pfndb(void)
{
	uint64_t phys = VIRT_TO_PHYS((uint64_t)pfndb_getdb());
	uint64_t start = PAGE_ALIGN_DOWN(phys);
	uint64_t end = PAGE_ALIGN_UP(phys + (pfndb_getmax() + 1) * sizeof(page_t));

	log_range_v("map pfndb", hhdm_offset + start, start, end - start,
				VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_GLOBAL);
	return vmm_map_range(&kernel_space, hhdm_offset + start, start, end - start,
						 VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_GLOBAL);
}

static int map_framebuffer(struct limine_framebuffer *framebuffer)
{
	if (!framebuffer || !framebuffer->address)
		return 0;

	uint64_t virt = (uint64_t)framebuffer->address;
	uint64_t size = framebuffer->pitch * framebuffer->height;
	uint64_t start = PAGE_ALIGN_DOWN(virt);
	uint64_t end = PAGE_ALIGN_UP(virt + size);
	uint64_t phys = (virt >= hhdm_offset) ? VIRT_TO_PHYS(start) : start;

	log_range_v("map framebuffer", start, phys, end - start,
				VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_GLOBAL);
	return vmm_map_range(&kernel_space, start, phys, end - start,
						 VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_GLOBAL);
}

static void add_static_area(vm_area_t *area, uint64_t start, uint64_t end,
							uint64_t prot, uint64_t flags)
{
	area->start = start;
	area->end = end;
	area->prot = prot;
	area->flags = flags;
	area->offset = 0;
	area->object = NULL;
	area->prev = NULL;
	area->next = NULL;

	klogvv("vad add: [0x%llx,0x%llx) prot=0x%llx flags=0x%llx",
		   (unsigned long long)start, (unsigned long long)end,
		   (unsigned long long)prot, (unsigned long long)flags);
	if (vmm_add_area(&kernel_space, area) != 0)
		kpanic(NULL, "vmm: VAD overlap for [0x%llx, 0x%llx)",
			   (unsigned long long)start, (unsigned long long)end);
}

vmm_space_t *vmm_kernel_space(void)
{
	return &kernel_space;
}

vmm_space_t *vmm_current_space(void)
{
	return current_space;
}

void vmm_switch(vmm_space_t *space)
{
	if (!space || !space->pml4_phys)
		kpanic(NULL, "vmm: invalid address space switch");

	klogvv("switch address space: pml4=0x%llx",
		   (unsigned long long)space->pml4_phys);
	write_cr3(space->pml4_phys);
	current_space = space;
}

int vmm_map_page(vmm_space_t *space, uint64_t virt, uint64_t phys,
				 uint64_t prot)
{
	if (!space || !space->pml4_phys)
		return -1;
	if ((virt & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1)))
		return -1;

	pte_t *leaf = lookup_leaf(space, virt, 1, prot);
	if (!leaf)
		return -1;

	*leaf = (phys & PTE_ADDR_MASK) | prot_to_pte(prot);
	if (space == current_space)
		invlpg(virt);
	return 0;
}

int vmm_map_range(vmm_space_t *space, uint64_t virt, uint64_t phys,
				  uint64_t size, uint64_t prot)
{
	uint64_t pages = PAGE_ALIGN_UP(size) >> PAGE_SHIFT;

	if (!size)
		return 0;
	if ((virt & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1)))
		return -1;

	log_range_vv("map range", virt, phys, size, prot);
	for (uint64_t i = 0; i < pages; i++) {
		if (vmm_map_page(space, virt + (i << PAGE_SHIFT),
						 phys + (i << PAGE_SHIFT), prot) != 0)
			return -1;
	}

	return 0;
}

void vmm_unmap_page(vmm_space_t *space, uint64_t virt)
{
	pte_t *leaf;

	if (!space || !space->pml4_phys || (virt & (PAGE_SIZE - 1)))
		return;

	leaf = lookup_leaf(space, virt, 0, 0);
	if (!leaf)
		return;

	klogvv("unmap page: virt=0x%llx phys=0x%llx", (unsigned long long)virt,
		   (unsigned long long)(*leaf & PTE_ADDR_MASK));
	*leaf = 0;
	if (space == current_space)
		invlpg(virt);
}

uint64_t vmm_virt_to_phys(vmm_space_t *space, uint64_t virt)
{
	pte_t *leaf;

	if (!space || !space->pml4_phys)
		return 0;

	leaf = lookup_leaf(space, virt, 0, 0);
	if (!leaf || !(*leaf & PTE_PRESENT))
		return 0;

	return (*leaf & PTE_ADDR_MASK) | (virt & (PAGE_SIZE - 1));
}

vm_area_t *vmm_find_area(vmm_space_t *space, uint64_t virt)
{
	for (vm_area_t *area = space ? space->areas : NULL; area;
		 area = area->next) {
		if (virt >= area->start && virt < area->end)
			return area;
		if (virt < area->start)
			return NULL;
	}

	return NULL;
}

int vmm_add_area(vmm_space_t *space, vm_area_t *area)
{
	vm_area_t *cur;
	vm_area_t *prev = NULL;

	if (!space || !area || area->start >= area->end)
		return -1;
	if ((area->start & (PAGE_SIZE - 1)) || (area->end & (PAGE_SIZE - 1)))
		return -1;

	for (cur = space->areas; cur && cur->start < area->start; cur = cur->next)
		prev = cur;

	if (prev && area->start < prev->end)
		return -1;
	if (cur && area->end > cur->start)
		return -1;

	area->prev = prev;
	area->next = cur;
	if (prev)
		prev->next = area;
	else
		space->areas = area;
	if (cur)
		cur->prev = area;

	return 0;
}

void paging_init(struct limine_memmap_response *memmap,
				 struct limine_framebuffer *framebuffer,
				 struct limine_executable_address_response *kernel_addr)
{
	if (!memmap)
		kpanic(NULL, "vmm: missing memory map");
	if (!kernel_addr)
		kpanic(NULL, "vmm: missing executable address response");
	if (kernel_addr->virtual_base != (uint64_t)__kernel_start)
		kpanic(NULL, "vmm: linker base 0x%llx != Limine base 0x%llx",
			   (unsigned long long)(uint64_t)__kernel_start,
			   (unsigned long long)kernel_addr->virtual_base);

	enable_nx_if_available();

	memset(&kernel_space, 0, sizeof(kernel_space));
	page_table_count = 0;
	kernel_space.pml4_phys = alloc_table();
	kernel_space.lower_bound = 0xffff800000000000ull;
	kernel_space.upper_bound = 0xffffffffffffffffull;
	if (!kernel_space.pml4_phys)
		kpanic(NULL, "vmm: failed to allocate kernel PML4");

	if (map_hhdm(memmap) != 0)
		kpanic(NULL, "vmm: failed to map HHDM");
	if (map_pfndb() != 0)
		kpanic(NULL, "vmm: failed to map PFNDB");

	if (map_kernel_segment("map boot requests", (uint64_t)__kernel_start,
						   (uint64_t)__text_start, kernel_addr->physical_base,
						   kernel_addr->virtual_base,
						   VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_GLOBAL) !=
		0)
		kpanic(NULL, "vmm: failed to map Limine request segment");
	if (map_kernel_segment(
			"map kernel text", (uint64_t)__text_start, (uint64_t)__text_end,
			kernel_addr->physical_base, kernel_addr->virtual_base,
			VMM_PROT_READ | VMM_PROT_EXEC | VMM_PROT_GLOBAL) != 0)
		kpanic(NULL, "vmm: failed to map kernel text");
	if (map_kernel_segment("map kernel rodata", (uint64_t)__rodata_start,
						   (uint64_t)__rodata_end, kernel_addr->physical_base,
						   kernel_addr->virtual_base,
						   VMM_PROT_READ | VMM_PROT_GLOBAL) != 0)
		kpanic(NULL, "vmm: failed to map kernel rodata");
	if (map_kernel_segment(
			"map kernel data", (uint64_t)__data_start, (uint64_t)__kernel_end,
			kernel_addr->physical_base, kernel_addr->virtual_base,
			VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_GLOBAL) != 0)
		kpanic(NULL, "vmm: failed to map kernel data");
	if (map_framebuffer(framebuffer) != 0)
		kpanic(NULL, "vmm: failed to map framebuffer");

	add_static_area(&hhdm_area, hhdm_offset, hhdm_offset + memmap_top(memmap),
					VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_GLOBAL,
					VMM_AREA_HHDM | VMM_AREA_KERNEL);
	add_static_area(
		&kernel_image_area, (uint64_t)__kernel_start, (uint64_t)__kernel_end,
		VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_EXEC | VMM_PROT_GLOBAL,
		VMM_AREA_KERNEL);
	if (framebuffer && framebuffer->address) {
		uint64_t start = PAGE_ALIGN_DOWN((uint64_t)framebuffer->address);
		uint64_t end = PAGE_ALIGN_UP((uint64_t)framebuffer->address +
									 framebuffer->pitch * framebuffer->height);
		if (!vmm_find_area(&kernel_space, start))
			add_static_area(&framebuffer_area, start, end,
							VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_GLOBAL,
							VMM_AREA_DEVICE | VMM_AREA_KERNEL);
	}

	vmm_switch(&kernel_space);
	klog("4-level paging enabled");
	klogv("pml4=0x%llx tables=%llu kernel=[%p,%p) hhdm=%p",
		  (unsigned long long)kernel_space.pml4_phys,
		  (unsigned long long)page_table_count, __kernel_start, __kernel_end,
		  (void *)hhdm_offset);
}
