#ifndef MM_PAGE_H
#define MM_PAGE_H

#include <lib/atomic.h>
#include <stdint.h>

#define PAGE_SIZE 0x1000u
#define PAGE_SHIFT 12u
#define PAGE_MASK (~(uint64_t)(PAGE_SIZE - 1))

#define PAGE_ALIGN_DOWN(x) ((uint64_t)(x) & PAGE_MASK)
#define PAGE_ALIGN_UP(x) (((uint64_t)(x) + PAGE_SIZE - 1) & PAGE_MASK)

#define PG_free 0
#define PG_used 1
#define PG_reserved 2
#define PG_shared 3
#define PG_cow 4
#define PG_poison 5
#define PG_slab 6
#define PG_large_head 10
#define PG_large_body 11

#define _PAGE_BIT(n) (1ul << (n))

#define PAGE_FREE _PAGE_BIT(PG_free)
#define PAGE_USED _PAGE_BIT(PG_used)
#define PAGE_RESERVED _PAGE_BIT(PG_reserved)
#define PAGE_SHARED _PAGE_BIT(PG_shared)
#define PAGE_COW _PAGE_BIT(PG_cow)
#define PAGE_POISON _PAGE_BIT(PG_poison)
#define PAGE_SLAB _PAGE_BIT(PG_slab)
#define PAGE_LARGE_HEAD _PAGE_BIT(PG_large_head)
#define PAGE_LARGE_BODY _PAGE_BIT(PG_large_body)

#define PAGE_ALLOCATED (PAGE_USED | PAGE_RESERVED)

#define PFN_PHYS(pfn) ((uint64_t)(pfn) << PAGE_SHIFT)
#define PHYS_PFN(phys) ((uint64_t)(phys) >> PAGE_SHIFT)

typedef struct page page_t;

struct page {
	struct {
		page_t *next;
		page_t *prev;
	} list;

	atomic_int refcount;
	uint8_t order;
	uint64_t flags;
	uint64_t private;
} __attribute__((aligned(64)));

extern uint64_t hhdm_offset;

#define PHYS_TO_VIRT(p) ((void *)((uint64_t)(p) + hhdm_offset))
#define VIRT_TO_PHYS(v) ((uint64_t)(v) - hhdm_offset)

extern char __kernel_start[];
extern char __limine_requests_start[];
extern char __limine_requests_end[];
extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern char __kernel_end[];

#define PageFree(p) ((p)->flags & PAGE_FREE)
#define PageUsed(p) ((p)->flags & PAGE_USED)
#define PageReserved(p) ((p)->flags & PAGE_RESERVED)
#define PageShared(p) ((p)->flags & PAGE_SHARED)
#define PageCow(p) ((p)->flags & PAGE_COW)
#define PagePoisoned(p) ((p)->flags & PAGE_POISON)
#define PageSlab(p) ((p)->flags & PAGE_SLAB)
#define PageLargeHead(p) ((p)->flags & PAGE_LARGE_HEAD)
#define PageLargeBody(p) ((p)->flags & PAGE_LARGE_BODY)

#define SetPageFlag(p, f) ((p)->flags |= (f))
#define ClearPageFlag(p, f) ((p)->flags &= ~(f))

static inline void page_ref_inc(page_t *page)
{
	atomic_fetch_add_explicit(&page->refcount, 1, memory_order_relaxed);
}

static inline _Bool page_ref_dec_and_test(page_t *page)
{
	return atomic_fetch_sub_explicit(&page->refcount, 1,
									 memory_order_release) == 1;
}

static inline int page_ref_count(const page_t *page)
{
	return atomic_load_explicit(&page->refcount, memory_order_relaxed);
}

static inline void page_addprivate(page_t *page)
{
	page->private ++;
	if (page->private > 0)
		SetPageFlag(page, PAGE_SHARED);
}

static inline void page_subprivate(page_t *page)
{
	page->private --;
	if (page->private <= 0)
		ClearPageFlag(page, PAGE_SHARED);
}

#endif // MM_PAGE_H
