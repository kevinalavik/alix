#include <mm/mm.h>
#define KLOG_NS "pmm"
#include <log/klog.h>
#include <debug/panic.h>
#include <string.h>

#define MAX_ORDER PMM_MAX_ORDER

static struct {
	page_t *head;
	uint64_t count;
} free_lists[MAX_ORDER];

static uint64_t free_page_count;
static uint64_t total_page_count;

static inline void free_list_push(uint8_t order, page_t *page)
{
	page->list.prev = NULL;
	page->list.next = free_lists[order].head;
	if (free_lists[order].head)
		free_lists[order].head->list.prev = page;
	free_lists[order].head = page;
	free_lists[order].count++;
}

static inline void free_list_remove(uint8_t order, page_t *page)
{
	if (page->list.prev)
		page->list.prev->list.next = page->list.next;
	else
		free_lists[order].head = page->list.next;
	if (page->list.next)
		page->list.next->list.prev = page->list.prev;
	page->list.next = NULL;
	page->list.prev = NULL;
	free_lists[order].count--;
}

static inline uint64_t buddy_pfn(uint64_t pfn, uint8_t order)
{
	return pfn ^ (1ull << order);
}

static void free_block(page_t *page)
{
	uint64_t pfn = page_to_pfn(page);
	uint8_t order = page->order;
	uint8_t start_order = order;

	while (order < MAX_ORDER - 1) {
		uint64_t buddy = buddy_pfn(pfn, order);
		if (buddy > pfndb_getmax())
			break;

		page_t *buddy_page = pfn_to_page(buddy);
		if (!PageFree(buddy_page) || buddy_page->order != order)
			break;

		klogvv("coalesce pfn=0x%llx buddy=0x%llx order=%u",
			   (unsigned long long)pfn, (unsigned long long)buddy, order);
		free_list_remove(order, buddy_page);

		if (buddy < pfn) {
			pfn = buddy;
			page = buddy_page;
		}

		ClearPageFlag(page, PAGE_LARGE_HEAD);

		order++;
		page->order = order;
	}

	SetPageFlag(page, PAGE_FREE);
	ClearPageFlag(page, PAGE_USED | PAGE_LARGE_HEAD | PAGE_LARGE_BODY);
	page->order = order;

	if (order > 0)
		SetPageFlag(page, PAGE_LARGE_HEAD);

	if (order != start_order)
		klogvv("free block merged: pfn=0x%llx order=%u->%u",
			   (unsigned long long)page_to_pfn(page), start_order, order);

	free_list_push(order, page);
}

void pmm_init(void)
{
	memset(free_lists, 0, sizeof(free_lists));
	free_page_count = 0;
	total_page_count = 0;

	page_t *page;
	uint64_t pfn;

	pfndb_for_each_free(page, pfn)
	{
		page->order = 0;
		ClearPageFlag(page, PAGE_FREE);
		free_block(page);
		free_page_count++;
		total_page_count++;
	}

	klog("free=%llu pages (%llu MiB)", free_page_count,
		 (free_page_count << PAGE_SHIFT) >> 20);

	for (uint8_t o = 0; o < MAX_ORDER; o++) {
		if (free_lists[o].count)
			klogvv("order[%u]=%llu", o, free_lists[o].count);
	}
}

page_t *pmm_alloc(uint8_t order)
{
	if (order >= MAX_ORDER)
		return NULL;

	klogvv("alloc request order=%u free=%llu", order, free_page_count);

	for (uint8_t o = order; o < MAX_ORDER; o++) {
		if (!free_lists[o].head)
			continue;

		page_t *page = free_lists[o].head;
		uint64_t pfn = page_to_pfn(page);
		free_list_remove(o, page);
		klogvv("alloc source pfn=0x%llx order=%u target=%u",
			   (unsigned long long)pfn, o, order);

		while (o > order) {
			o--;
			page_t *buddy = pfn_to_page(page_to_pfn(page) + (1ull << o));
			buddy->order = o;
			buddy->flags = PAGE_FREE | PAGE_LARGE_HEAD;
			free_list_push(o, buddy);
			klogvv("split buddy pfn=0x%llx order=%u",
				   (unsigned long long)page_to_pfn(buddy), o);
		}

		uint64_t count = 1ull << order;
		page->order = order;
		SetPageFlag(page, PAGE_USED);
		ClearPageFlag(page, PAGE_FREE);
		if (order > 0) {
			SetPageFlag(page, PAGE_LARGE_HEAD);
			for (uint64_t i = 1; i < count; i++) {
				page_t *body = pfn_to_page(page_to_pfn(page) + i);
				body->order = order;
				body->flags = PAGE_USED | PAGE_LARGE_BODY;
				atomic_store_explicit(&body->refcount, 1, memory_order_relaxed);
			}
		}

		atomic_store_explicit(&page->refcount, 1, memory_order_relaxed);
		free_page_count -= count;
		klogvv("allocated pfn=0x%llx phys=0x%llx order=%u free=%llu",
			   (unsigned long long)page_to_pfn(page),
			   (unsigned long long)page_to_phys(page), order, free_page_count);
		return page;
	}

	klogv("alloc failed: order=%u free=%llu", order, free_page_count);
	return NULL;
}

page_t *pmm_alloc_page(void)
{
	return pmm_alloc(0);
}

void pmm_free(page_t *page)
{
	if (!page)
		return;

	if (PageReserved(page) || PagePoisoned(page))
		kpanic(NULL,
			   "pmm: attempt to free reserved/poisoned page at PFN 0x%llx",
			   page_to_pfn(page));

	if (PageFree(page))
		kpanic(NULL, "pmm: double-free at PFN 0x%llx", page_to_pfn(page));

	if (page_ref_dec_and_test(page)) {
		uint64_t count = 1ull << page->order;
		uint64_t pfn = page_to_pfn(page);
		uint8_t order = page->order;

		klogvv("free pfn=0x%llx phys=0x%llx order=%u", (unsigned long long)pfn,
			   (unsigned long long)page_to_phys(page), order);

		if (page->order > 0) {
			for (uint64_t i = 1; i < count; i++) {
				page_t *body = pfn_to_page(page_to_pfn(page) + i);
				body->flags = 0;
				atomic_store_explicit(&body->refcount, 0, memory_order_relaxed);
			}
		}

		free_page_count += count;
		free_block(page);
		klogvv("freed pfn=0x%llx order=%u free=%llu", (unsigned long long)pfn,
			   order, free_page_count);
	}
}

uint64_t pmm_free_pages(void)
{
	return free_page_count;
}
uint64_t pmm_total_pages(void)
{
	return total_page_count;
}
