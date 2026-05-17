#ifndef _LYR_MM_VMM_H
#define _LYR_MM_VMM_H

#include <stdint.h>
#include <stddef.h>
#include <mm/page.h>
#include <mm/paging.h>

#define VAD_PROT_MASK \
	(PAGE_PRESENT | PAGE_WRITE | PAGE_USER | PAGE_GLOBAL | PAGE_NX)
#define VAD_ANONYMOUS (1ull << 12)
#define VAD_FIXED (1ull << 13)
#define VAD_MAPPED (1ull << 14)
#define VAD_SHARED (1ull << 15)
#define VAD_KERNEL (1ull << 16)
#define VAD_HHDM (1ull << 17)
#define VAD_DEVICE (1ull << 18)

#define VAS_USER_START 0x0000000000001000ull
#define VAS_USER_END 0x00007FFFFFFFFFFFull

typedef struct vad {
	uint64_t start;
	uint64_t end;
	uint64_t flags;
	struct vad *next;
} vad_t;

typedef struct vas {
	ptable_t *pml4;
	vad_t *list_head;
	uint64_t user_start;
} vas_t;

extern vas_t *kernel_vas;

vas_t *vas_create(ptable_t *pt);
void vas_destroy(vas_t *vas);

uint64_t vas_map_anon(vas_t *vas, uint64_t hint, size_t length, uint64_t flags);
uint64_t vas_map_phys(vas_t *vas, uint64_t hint, uint64_t phys, size_t length,
					  uint64_t flags);
int vas_unmap(vas_t *vas, uint64_t start, size_t length);
int vas_protect(vas_t *vas, uint64_t start, size_t length, uint64_t new_flags);
vad_t *vas_find(vas_t *vas, uint64_t addr);
int vas_range_mapped(vas_t *vas, uint64_t start, size_t length);
int vas_handle_page_fault(vas_t *vas, uint64_t addr, uint64_t err);
int vas_user_access_ok(vas_t *vas, uint64_t addr, size_t len, int write);

void vas_switch(vas_t *vas);
vas_t *vas_adopt(ptable_t *existing_pml4);
vas_t *vas_clone(vas_t *src);

int vas_add(vas_t *vas, vad_t *vad);

#endif /* _LYR_MM_VMM_H */
