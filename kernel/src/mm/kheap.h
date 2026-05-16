#ifndef MM_KHEAP_H
#define MM_KHEAP_H

#include <stdint.h>
#include <mm/page.h>

typedef struct slab_cache slab_cache_t;
typedef struct slab slab_t;

struct slab {
	slab_t *next;
	slab_t *prev;
	void *freelist;
	uint32_t inuse;
	uint32_t total;
	page_t *page;
};

struct slab_cache {
	const char *name;
	uint32_t obj_size;
	uint32_t obj_align;
	slab_t *partial;
	slab_t *full;
	uint64_t alloc_count;
	uint64_t free_count;
};

slab_cache_t *slab_cache_create(const char *name, uint32_t size,
								uint32_t align);
void slab_cache_destroy(slab_cache_t *cache);
void *slab_alloc(slab_cache_t *cache);
void slab_free(slab_cache_t *cache, void *ptr);

void kheap_init(void);
void *kmalloc(uint64_t size);
void *kzalloc(uint64_t size);
void *krealloc(void *ptr, uint64_t new_size);
void kfree(void *ptr);

uint64_t kheap_alloc_count(void);
uint64_t kheap_free_count(void);

#endif // MM_KHEAP_H
