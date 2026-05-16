#include <mm/mm.h>
#define KLOG_NS "kheap"
#include <log/klog.h>
#include <debug/panic.h>
#include <string.h>

#define LARGE_MAGIC 0x4B484541504C4741ull
#define LARGE_PAGE_TAG 0x4B48454150000000ull
#define LARGE_PAGE_TAG_MASK 0xffffffffffffff00ull

static const uint32_t size_classes[] = { 8,	  16,  32,	 64,  128,
										 256, 512, 1024, 2048 };

#define NUM_SIZE_CLASSES (sizeof(size_classes) / sizeof(size_classes[0]))

static slab_cache_t caches[NUM_SIZE_CLASSES];

static uint64_t total_alloc_count;
static uint64_t total_free_count;

static page_t *page_from_alloc_ptr(const void *ptr)
{
	uint64_t virt = (uint64_t)ptr;
	uint64_t page_base;

	if (virt < hhdm_offset)
		return NULL;

	page_base = PAGE_ALIGN_DOWN(virt);
	return phys_to_page(VIRT_TO_PHYS(page_base));
}

static _Bool page_is_large_alloc(const page_t *page)
{
	return page && !PageSlab(page) &&
		   (page->private & LARGE_PAGE_TAG_MASK) == LARGE_PAGE_TAG;
}

static slab_t *slab_from_page(page_t *page)
{
	return (slab_t *)PHYS_TO_VIRT(page_to_phys(page));
}

static slab_t *slab_create(slab_cache_t *cache)
{
	page_t *page = pmm_alloc_page();
	if (!page)
		return NULL;

	SetPageFlag(page, PAGE_SLAB);
	page->private = (uint64_t)(uintptr_t)cache;

	slab_t *slab = slab_from_page(page);

	uint32_t hdr =
		(sizeof(slab_t) + cache->obj_align - 1) & ~(cache->obj_align - 1);
	uint32_t avail = PAGE_SIZE - hdr;
	uint32_t total = avail / cache->obj_size;

	slab->next = NULL;
	slab->prev = NULL;
	slab->inuse = 0;
	slab->total = total;
	slab->page = page;

	uint8_t *base = (uint8_t *)slab + hdr;
	slab->freelist = base;

	for (uint32_t i = 0; i < total; i++) {
		void **slot = (void **)(base + i * cache->obj_size);
		*slot = (i + 1 < total) ? (base + (i + 1) * cache->obj_size) : NULL;
	}

	klogvv("slab create cache=%s size=%u align=%u page=0x%llx objs=%u",
		   cache->name, cache->obj_size, cache->obj_align,
		   (unsigned long long)page_to_phys(page), total);
	return slab;
}

static void slab_destroy(slab_t *slab)
{
	page_t *page = slab->page;
	slab_cache_t *cache = (slab_cache_t *)(uintptr_t)page->private;

	klogvv("slab destroy cache=%s page=0x%llx inuse=%u/%u",
		   cache ? cache->name : "?", (unsigned long long)page_to_phys(page),
		   slab->inuse, slab->total);
	ClearPageFlag(page, PAGE_SLAB);
	page->private = -1;
	pmm_free(page);
}

static void slab_list_push(slab_t **head, slab_t *slab)
{
	slab->prev = NULL;
	slab->next = *head;
	if (*head)
		(*head)->prev = slab;
	*head = slab;
}

static void slab_list_remove(slab_t **head, slab_t *slab)
{
	if (slab->prev)
		slab->prev->next = slab->next;
	else
		*head = slab->next;
	if (slab->next)
		slab->next->prev = slab->prev;
	slab->next = NULL;
	slab->prev = NULL;
}

slab_cache_t *slab_cache_create(const char *name, uint32_t size, uint32_t align)
{
	if (!align || (align & (align - 1)))
		kpanic(NULL, "slab: align must be power of two");

	slab_cache_t *cache = kmalloc(sizeof(slab_cache_t));
	if (!cache)
		return NULL;

	cache->name = name;
	cache->obj_size = (size + align - 1) & ~(align - 1);
	cache->obj_align = align;
	cache->partial = NULL;
	cache->full = NULL;
	cache->alloc_count = 0;
	cache->free_count = 0;
	klogv("cache create %s size=%u align=%u", name, cache->obj_size,
		  cache->obj_align);
	return cache;
}

void slab_cache_destroy(slab_cache_t *cache)
{
	slab_t *s;
	for (s = cache->partial; s;) {
		slab_t *n = s->next;
		slab_destroy(s);
		s = n;
	}
	for (s = cache->full; s;) {
		slab_t *n = s->next;
		slab_destroy(s);
		s = n;
	}
	kfree(cache);
}

void *slab_alloc(slab_cache_t *cache)
{
	if (!cache->partial) {
		slab_t *slab = slab_create(cache);
		if (!slab)
			return NULL;
		slab_list_push(&cache->partial, slab);
	}

	slab_t *slab = cache->partial;
	void *obj = slab->freelist;
	slab->freelist = *(void **)obj;
	slab->inuse++;
	cache->alloc_count++;

	if (!slab->freelist) {
		slab_list_remove(&cache->partial, slab);
		slab_list_push(&cache->full, slab);
		klogvv("slab full cache=%s page=0x%llx", cache->name,
			   (unsigned long long)page_to_phys(slab->page));
	}

	klogvv("alloc cache=%s size=%u ptr=%p inuse=%u/%u", cache->name,
		   cache->obj_size, obj, slab->inuse, slab->total);
	return obj;
}

void slab_free(slab_cache_t *cache, void *ptr)
{
	uint64_t phys = VIRT_TO_PHYS(PAGE_ALIGN_DOWN(ptr));
	page_t *page = pfndb_phys_to_page(phys);
	slab_t *slab = slab_from_page(page);

	_Bool was_full = (slab->freelist == NULL && slab->inuse == slab->total);
	if (was_full) {
		slab_list_remove(&cache->full, slab);
		slab_list_push(&cache->partial, slab);
		klogvv("slab partial cache=%s page=0x%llx", cache->name,
			   (unsigned long long)page_to_phys(slab->page));
	}

	*(void **)ptr = slab->freelist;
	slab->freelist = ptr;
	slab->inuse--;
	cache->free_count++;
	klogvv("free cache=%s ptr=%p inuse=%u/%u", cache->name, ptr, slab->inuse,
		   slab->total);
}

static slab_cache_t *cache_for_size(uint64_t size)
{
	for (uint32_t i = 0; i < NUM_SIZE_CLASSES; i++) {
		if (size <= size_classes[i])
			return &caches[i];
	}
	return NULL;
}

typedef struct {
	uint64_t magic;
	uint64_t size;
	uint8_t order;
	uint8_t reserved[7];
} large_hdr_t;

void kheap_init(void)
{
	for (uint32_t i = 0; i < NUM_SIZE_CLASSES; i++) {
		uint32_t sz = size_classes[i];
		uint32_t align = sz < 16 ? sz : 16;
		caches[i].name = "kmalloc";
		caches[i].obj_size = sz;
		caches[i].obj_align = align;
		caches[i].partial = NULL;
		caches[i].full = NULL;
		caches[i].alloc_count = 0;
		caches[i].free_count = 0;
	}

	total_alloc_count = 0;
	total_free_count = 0;
	klog("slab caches=%zu max=%u", NUM_SIZE_CLASSES,
		 size_classes[NUM_SIZE_CLASSES - 1]);
}

void *kmalloc(uint64_t size)
{
	if (!size)
		return NULL;

	slab_cache_t *cache = cache_for_size(size);
	void *ptr;

	if (cache) {
		ptr = slab_alloc(cache);
	} else {
		uint64_t total = size + sizeof(large_hdr_t);
		uint8_t order = 0;
		while ((PAGE_SIZE << order) < total)
			order++;
		if (order >= PMM_MAX_ORDER)
			return NULL;

		page_t *page = pmm_alloc(order);
		if (!page)
			return NULL;

		large_hdr_t *hdr = PHYS_TO_VIRT(page_to_phys(page));
		hdr->magic = LARGE_MAGIC;
		hdr->size = size;
		hdr->order = order;
		page->private = LARGE_PAGE_TAG | order;
		ptr = (uint8_t *)hdr + sizeof(large_hdr_t);
		klogv("large alloc size=%llu order=%u ptr=%p phys=0x%llx",
			  (unsigned long long)size, order, ptr,
			  (unsigned long long)page_to_phys(page));
	}

	if (ptr) {
		total_alloc_count++;
		klogvv("kmalloc size=%llu ptr=%p allocs=%llu",
			   (unsigned long long)size, ptr,
			   (unsigned long long)total_alloc_count);
	}

	return ptr;
}

void *kzalloc(uint64_t size)
{
	void *ptr = kmalloc(size);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

void *krealloc(void *ptr, uint64_t new_size)
{
	if (!ptr)
		return kmalloc(new_size);
	if (!new_size) {
		kfree(ptr);
		return NULL;
	}

	void *new_ptr = kmalloc(new_size);
	if (!new_ptr)
		return NULL;

	page_t *page = page_from_alloc_ptr(ptr);
	if (!page)
		kpanic(NULL, "krealloc: invalid heap pointer %p", ptr);

	uint64_t old_size;
	if (PageSlab(page)) {
		slab_cache_t *cache = (slab_cache_t *)(uintptr_t)page->private;
		if (!cache)
			kpanic(NULL, "krealloc: slab page at %p has no cache", ptr);
		old_size = cache->obj_size;
	} else if (page_is_large_alloc(page)) {
		large_hdr_t *hdr =
			(large_hdr_t *)((uint8_t *)ptr - sizeof(large_hdr_t));
		if (hdr->magic != LARGE_MAGIC)
			kpanic(NULL, "krealloc: bad large-allocation header at %p", ptr);
		old_size = hdr->size;
	} else {
		kpanic(NULL, "krealloc: pointer %p is not a kheap allocation", ptr);
	}

	memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
	kfree(ptr);
	return new_ptr;
}

void kfree(void *ptr)
{
	if (!ptr)
		return;

	page_t *page = page_from_alloc_ptr(ptr);
	if (!page)
		kpanic(NULL, "kfree: invalid heap pointer %p", ptr);

	if (PageSlab(page)) {
		slab_cache_t *cache = (slab_cache_t *)(uintptr_t)page->private;
		if (!cache)
			kpanic(NULL, "kfree: slab page at %p has no cache", ptr);
		slab_free(cache, ptr);
	} else if (page_is_large_alloc(page)) {
		large_hdr_t *hdr =
			(large_hdr_t *)((uint8_t *)ptr - sizeof(large_hdr_t));
		if (hdr->magic != LARGE_MAGIC)
			kpanic(NULL, "kfree: bad magic at %p - double free or corruption",
				   ptr);
		hdr->magic = 0;
		page->private = 0;
		klogv("large free ptr=%p size=%llu order=%u", ptr,
			  (unsigned long long)hdr->size, hdr->order);
		pmm_free(page);
	} else {
		kpanic(NULL, "kfree: pointer %p is not a kheap allocation", ptr);
	}

	total_free_count++;
	klogvv("kfree ptr=%p frees=%llu", ptr, (unsigned long long)total_free_count);
}

uint64_t kheap_alloc_count(void)
{
	return total_alloc_count;
}
uint64_t kheap_free_count(void)
{
	return total_free_count;
}
