#ifndef MM_PMM_H
#define MM_PMM_H

#include <stdint.h>
#include <mm/page.h>

#define PMM_MAX_ORDER 11

void pmm_init(void);
page_t *pmm_alloc(uint8_t order);
page_t *pmm_alloc_page(void);
void pmm_free(page_t *page);
uint64_t pmm_free_pages(void);
uint64_t pmm_total_pages(void);

#endif // MM_PMM_H
