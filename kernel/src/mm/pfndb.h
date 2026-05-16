#ifndef MM_PFNDB_H
#define MM_PFNDB_H

#include <limine.h>
#include <stdint.h>
#include <mm/page.h>

#define page_to_pfn(page) pfndb_getpfn(page)
#define pfn_to_page(pfn) pfndb_getptr(pfn)
#define page_to_phys(page) pfndb_page_to_phys(page)
#define phys_to_page(phys) pfndb_phys_to_page(phys)

#define pfndb_for_each_free(page, pfn)                                 \
	for ((pfn) = 0, (page) = pfndb_getptr(0); (pfn) <= pfndb_getmax(); \
		 (pfn)++, (page) = pfndb_getptr(pfn))                          \
		if (PageFree(page))

void pfndb_init(struct limine_memmap_response *memmap);
void pfndb_mark_range(uint64_t base, uint64_t length, uint64_t flags);
page_t *pfndb_getdb(void);
uint64_t pfndb_getmax(void);
uint64_t pfndb_pfnaddr(uint64_t pfn);
page_t *pfndb_getptr(uint64_t pfn);
uint64_t pfndb_getpfn(const page_t *page);
uint64_t pfndb_page_to_phys(const page_t *page);
page_t *pfndb_phys_to_page(uint64_t phys);

#endif // MM_PFNDB_H
