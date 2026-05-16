#ifndef MM_VMM_H
#define MM_VMM_H

#include <limine.h>
#include <stdint.h>
#include <mm/page.h>

#define VMM_PROT_READ _PAGE_BIT(0)
#define VMM_PROT_WRITE _PAGE_BIT(1)
#define VMM_PROT_EXEC _PAGE_BIT(2)
#define VMM_PROT_USER _PAGE_BIT(3)
#define VMM_PROT_GLOBAL _PAGE_BIT(4)

#define VMM_AREA_ANON _PAGE_BIT(0)
#define VMM_AREA_FILE _PAGE_BIT(1)
#define VMM_AREA_STACK _PAGE_BIT(2)
#define VMM_AREA_HEAP _PAGE_BIT(3)
#define VMM_AREA_KERNEL _PAGE_BIT(4)
#define VMM_AREA_HHDM _PAGE_BIT(5)
#define VMM_AREA_DEVICE _PAGE_BIT(6)

typedef struct vm_area vm_area_t;
typedef struct vmm_space vmm_space_t;

struct vm_area {
	uint64_t start;
	uint64_t end;
	uint64_t prot;
	uint64_t flags;
	uint64_t offset;
	void *object;
	vm_area_t *prev;
	vm_area_t *next;
};

struct vmm_space {
	uint64_t pml4_phys;
	uint64_t lower_bound;
	uint64_t upper_bound;
	vm_area_t *areas;
};

void paging_init(struct limine_memmap_response *memmap,
				 struct limine_framebuffer *framebuffer,
				 struct limine_executable_address_response *kernel_addr);
vmm_space_t *vmm_kernel_space(void);
vmm_space_t *vmm_current_space(void);
void vmm_switch(vmm_space_t *space);
int vmm_map_page(vmm_space_t *space, uint64_t virt, uint64_t phys,
				 uint64_t prot);
int vmm_map_range(vmm_space_t *space, uint64_t virt, uint64_t phys,
				  uint64_t size, uint64_t prot);
void vmm_unmap_page(vmm_space_t *space, uint64_t virt);
uint64_t vmm_virt_to_phys(vmm_space_t *space, uint64_t virt);
vm_area_t *vmm_find_area(vmm_space_t *space, uint64_t virt);
int vmm_add_area(vmm_space_t *space, vm_area_t *area);

#endif // MM_VMM_H
