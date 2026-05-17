#ifndef MM_PAGING_H
#define MM_PAGING_H

#include <limine.h>
#include <stdint.h>

#define PAGE_PRESENT (1ull << 0)
#define PAGE_WRITE (1ull << 1)
#define PAGE_USER (1ull << 2)
#define PAGE_GLOBAL (1ull << 8)
#define PAGE_NX (1ull << 63)
#define PAGE_ADDR_MASK 0x000ffffffffff000ull

typedef uint64_t ptable_t;
struct vas;

ptable_t *ptable_create(void);
void ptable_destroy(ptable_t *pt);

void paging_init(struct limine_memmap_response *memmap,
				 struct limine_framebuffer *framebuffer,
				 struct limine_executable_address_response *kernel_addr);
struct vas *paging_current_vas(void);
int paging_map_page(struct vas *vas, uint64_t virt, uint64_t phys,
					uint64_t flags);
int paging_map_range(struct vas *vas, uint64_t virt, uint64_t phys,
					 uint64_t size, uint64_t flags);
void paging_unmap_page(struct vas *vas, uint64_t virt);
uint64_t paging_virt_to_phys(struct vas *vas, uint64_t virt);
uint64_t paging_get_flags(struct vas *vas, uint64_t virt);

#endif // MM_PAGING_H