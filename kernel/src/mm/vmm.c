#include <mm/mm.h>
#define KLOG_NS "vmm"
#include <log/klog.h>
#include <debug/panic.h>
#include <lib/string.h>

static vad_t *vad_alloc(uint64_t start, uint64_t end, uint64_t flags)
{
	vad_t *vad = kzalloc(sizeof(vad_t));

	if (!vad)
		return NULL;

	vad->start = start;
	vad->end = end;
	vad->flags = flags;
	vad->next = NULL;
	return vad;
}

static void vad_free(vad_t *vad)
{
	if (vad)
		kfree(vad);
}

static vad_t *vad_clone_segment(vad_t *src, uint64_t start, uint64_t end)
{
	return vad_alloc(start, end, src->flags);
}

static void vad_insert(vas_t *vas, vad_t *vad)
{
	vad_t **cur = &vas->list_head;

	while (*cur && (*cur)->start < vad->start)
		cur = &(*cur)->next;

	vad->next = *cur;
	*cur = vad;
}

static void vad_remove_range(vas_t *vas, uint64_t start, uint64_t end)
{
	vad_t *old = vas->list_head;
	vad_t *new_head = NULL;
	vad_t **tail = &new_head;

	while (old) {
		vad_t *next = old->next;

		if (old->end <= start || old->start >= end) {
			old->next = NULL;
			*tail = old;
			tail = &old->next;
			old = next;
			continue;
		}

		if (old->start < start) {
			vad_t *left = vad_clone_segment(old, old->start, start);
			if (!left)
				kpanic(NULL, "vmm: failed to split left VAD");
			*tail = left;
			tail = &left->next;
		}

		if (old->end > end) {
			vad_t *right = vad_clone_segment(old, end, old->end);
			if (!right)
				kpanic(NULL, "vmm: failed to split right VAD");
			*tail = right;
			tail = &right->next;
		}

		vad_free(old);
		old = next;
	}

	*tail = NULL;
	vas->list_head = new_head;
}

static uint64_t vas_find_free(vas_t *vas, uint64_t hint, size_t length)
{
	uint64_t base = PAGE_ALIGN_UP(hint);
	uint64_t end = base + length;
	vad_t *vad;

	for (vad = vas->list_head; vad; vad = vad->next) {
		if (end <= vad->start)
			break;
		if (base < vad->end) {
			base = PAGE_ALIGN_UP(vad->end);
			end = base + length;
		}
	}

	if (end > VAS_USER_END || end < base)
		return 0;

	return base;
}

static int vas_overlaps(vas_t *vas, uint64_t start, uint64_t end)
{
	for (vad_t *vad = vas->list_head; vad; vad = vad->next) {
		if (vad->start >= end)
			break;
		if (vad->end > start)
			return 1;
	}

	return 0;
}

static void uncommit_range(vas_t *vas, uint64_t start, uint64_t end)
{
	for (uint64_t va = start; va < end; va += PAGE_SIZE) {
		uint64_t phys = paging_virt_to_phys(vas, va);
		page_t *page;

		if (!phys) {
			paging_unmap_page(vas, va);
			continue;
		}

		page = phys_to_page(PAGE_ALIGN_DOWN(phys));
		paging_unmap_page(vas, va);

		if (!page || PageReserved(page) || PagePoisoned(page))
			continue;

		pmm_free(page);
	}
}

static int commit_anon(vas_t *vas, vad_t *vad)
{
	uint64_t flags = vad->flags & VAD_PROT_MASK;

	for (uint64_t va = vad->start; va < vad->end; va += PAGE_SIZE) {
		page_t *page = pmm_alloc_page();
		uint64_t phys;

		if (!page)
			return -1;

		phys = page_to_phys(page);
		memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);

		if (paging_map_page(vas, va, phys, flags) != 0) {
			pmm_free(page);
			return -1;
		}
	}

	return 0;
}

static int remap_page_flags(vas_t *vas, uint64_t va, uint64_t new_flags)
{
	uint64_t phys = paging_virt_to_phys(vas, va);

	if (!phys)
		return 0;

	paging_unmap_page(vas, va);
	return paging_map_page(vas, va, PAGE_ALIGN_DOWN(phys), new_flags);
}

vas_t *vas_create(ptable_t *pt)
{
	vas_t *vas = kzalloc(sizeof(vas_t));

	if (!vas)
		return NULL;

	vas->pml4 = pt ? pt : ptable_create();
	vas->list_head = NULL;
	vas->user_start = VAS_USER_START;

	if (!vas->pml4) {
		kfree(vas);
		return NULL;
	}

	return vas;
}

void vas_destroy(vas_t *vas)
{
	vad_t *vad;

	if (!vas || vas == kernel_vas)
		return;

	vad = vas->list_head;
	while (vad) {
		vad_t *next = vad->next;

		uncommit_range(vas, vad->start, vad->end);
		vad_free(vad);
		vad = next;
	}

	ptable_destroy(vas->pml4);
	kfree(vas);
}

uint64_t vas_map_anon(vas_t *vas, uint64_t hint, size_t length, uint64_t flags)
{
	uint64_t base;
	uint64_t end;
	vad_t *vad;

	if (!vas || !length)
		return 0;

	length = PAGE_ALIGN_UP(length);

	if (flags & VAD_FIXED) {
		base = PAGE_ALIGN_DOWN(hint);
		end = base + length;
		if (end > VAS_USER_END || end < base)
			return 0;
		if (vas_overlaps(vas, base, end))
			return 0;
	} else {
		uint64_t search_hint = hint ? hint : vas->user_start;
		base = vas_find_free(vas, search_hint, length);
		if (!base)
			return 0;
	}

	vad = vad_alloc(base, base + length, flags | VAD_ANONYMOUS | VAD_MAPPED);
	if (!vad)
		return 0;

	if (commit_anon(vas, vad) != 0) {
		uncommit_range(vas, base, base + length);
		vad_free(vad);
		return 0;
	}

	vad_insert(vas, vad);
	vas->user_start = base + length;
	return base;
}

uint64_t vas_map_phys(vas_t *vas, uint64_t hint, uint64_t phys, size_t length,
					  uint64_t flags)
{
	uint64_t base;
	uint64_t end;
	vad_t *vad;

	if (!vas || !length)
		return 0;

	length = PAGE_ALIGN_UP(length);
	phys = PAGE_ALIGN_DOWN(phys);

	if (flags & VAD_FIXED) {
		base = PAGE_ALIGN_DOWN(hint);
		end = base + length;
		if (end > VAS_USER_END || end < base)
			return 0;
		if (vas_overlaps(vas, base, end))
			return 0;
	} else {
		uint64_t search_hint = hint ? hint : vas->user_start;
		base = vas_find_free(vas, search_hint, length);
		if (!base)
			return 0;
	}

	for (size_t off = 0; off < length; off += PAGE_SIZE) {
		uint64_t map_phys = phys + off;
		page_t *page = phys_to_page(map_phys);

		if (page && !PageReserved(page) && !PagePoisoned(page))
			page_ref_inc(page);

		if (paging_map_page(vas, base + off, map_phys, flags & VAD_PROT_MASK) !=
			0) {
			uncommit_range(vas, base, base + off);
			return 0;
		}
	}

	vad = vad_alloc(base, base + length, (flags & ~VAD_ANONYMOUS) | VAD_MAPPED);
	if (!vad) {
		uncommit_range(vas, base, base + length);
		return 0;
	}

	vad_insert(vas, vad);
	return base;
}

int vas_unmap(vas_t *vas, uint64_t start, size_t length)
{
	uint64_t end;

	if (!vas || !length)
		return 0;

	start = PAGE_ALIGN_DOWN(start);
	length = PAGE_ALIGN_UP(length);
	end = start + length;

	for (vad_t *vad = vas->list_head; vad && vad->start < end;
		 vad = vad->next) {
		uint64_t lo;
		uint64_t hi;

		if (vad->end <= start)
			continue;

		lo = vad->start > start ? vad->start : start;
		hi = vad->end < end ? vad->end : end;
		uncommit_range(vas, lo, hi);
	}

	vad_remove_range(vas, start, end);
	return 0;
}

int vas_protect(vas_t *vas, uint64_t start, size_t length, uint64_t new_flags)
{
	uint64_t end;

	if (!vas || !length)
		return 0;

	start = PAGE_ALIGN_DOWN(start);
	length = PAGE_ALIGN_UP(length);
	end = start + length;
	new_flags &= VAD_PROT_MASK;

	for (vad_t *vad = vas->list_head; vad && vad->start < end;
		 vad = vad->next) {
		uint64_t lo;
		uint64_t hi;

		if (vad->end <= start)
			continue;

		lo = vad->start > start ? vad->start : start;
		hi = vad->end < end ? vad->end : end;

		for (uint64_t va = lo; va < hi; va += PAGE_SIZE) {
			if (remap_page_flags(vas, va, new_flags) != 0)
				return -1;
		}

		vad->flags = (vad->flags & ~VAD_PROT_MASK) | new_flags;
	}

	return 0;
}

vad_t *vas_find(vas_t *vas, uint64_t addr)
{
	if (!vas)
		return NULL;

	for (vad_t *vad = vas->list_head; vad; vad = vad->next) {
		if (addr >= vad->start && addr < vad->end)
			return vad;
	}

	return NULL;
}

int vas_range_mapped(vas_t *vas, uint64_t start, size_t length)
{
	uint64_t end;
	uint64_t pos;

	if (!vas || !length)
		return 0;

	start = PAGE_ALIGN_DOWN(start);
	length = PAGE_ALIGN_UP(length);
	end = start + length;
	if (end < start)
		return 0;

	pos = start;
	for (vad_t *vad = vas->list_head; vad && pos < end; vad = vad->next) {
		if (vad->end <= pos)
			continue;
		if (vad->start > pos)
			return 0;
		if (vad->end > pos)
			pos = vad->end;
	}

	return pos >= end;
}

vas_t *vas_adopt(ptable_t *existing_pml4)
{
	vas_t *vas;

	if (!existing_pml4)
		return NULL;

	vas = kzalloc(sizeof(vas_t));
	if (!vas)
		return NULL;

	vas->pml4 = existing_pml4;
	vas->list_head = NULL;
	vas->user_start = VAS_USER_START;
	return vas;
}

int vas_handle_page_fault(vas_t *vas, uint64_t addr, uint64_t err)
{
	uint64_t va;
	vad_t *vad;
	uint64_t phys;
	page_t *old_page;
	uint64_t flags;

	if (!vas)
		return -1;

	va = PAGE_ALIGN_DOWN(addr);
	vad = vas_find(vas, va);
	if (!vad)
		return -1;

	if (!(err & 0x2) || !(err & 0x1))
		return -1;
	if (!(vad->flags & PAGE_WRITE) || (vad->flags & VAD_SHARED))
		return -1;

	phys = paging_virt_to_phys(vas, va);
	if (!phys)
		return -1;

	old_page = phys_to_page(PAGE_ALIGN_DOWN(phys));
	if (!old_page || !PageCow(old_page))
		return -1;

	flags = vad->flags & VAD_PROT_MASK;
	if (page_ref_count(old_page) <= 1) {
		ClearPageFlag(old_page, PAGE_COW);
		return remap_page_flags(vas, va, flags);
	}

	page_t *new_page = pmm_alloc_page();
	uint64_t new_phys;

	if (!new_page)
		return -1;

	new_phys = page_to_phys(new_page);
	memcpy(PHYS_TO_VIRT(new_phys), PHYS_TO_VIRT(PAGE_ALIGN_DOWN(phys)),
		   PAGE_SIZE);

	paging_unmap_page(vas, va);
	pmm_free(old_page);
	if (paging_map_page(vas, va, new_phys, flags) != 0) {
		pmm_free(new_page);
		return -1;
	}

	return 0;
}

int vas_user_access_ok(vas_t *vas, uint64_t addr, size_t len, int write)
{
	uint64_t start;
	uint64_t end;

	if (!vas)
		return -1;
	if (addr < VAS_USER_START || addr >= VAS_USER_END)
		return -1;
	if (len > VAS_USER_END - addr)
		return -1;
	if (len == 0)
		return 0;

	start = PAGE_ALIGN_DOWN(addr);
	end = PAGE_ALIGN_UP(addr + len);

	for (uint64_t va = start; va < end; va += PAGE_SIZE) {
		uint64_t flags = paging_get_flags(vas, va);

		if (!(flags & PAGE_PRESENT))
			return -1;
		if (write && !(flags & PAGE_WRITE)) {
			if (vas_handle_page_fault(vas, va, 0x3) != 0)
				return -1;
			flags = paging_get_flags(vas, va);
			if (!(flags & PAGE_PRESENT) || !(flags & PAGE_WRITE))
				return -1;
		}
	}

	return 0;
}

vas_t *vas_clone(vas_t *src)
{
	vas_t *dst;

	if (!src)
		return NULL;

	dst = vas_create(NULL);
	if (!dst)
		return NULL;

	dst->user_start = src->user_start;

	for (vad_t *vad = src->list_head; vad; vad = vad->next) {
		vad_t *copy = vad_alloc(vad->start, vad->end, vad->flags);
		uint64_t prot = vad->flags & VAD_PROT_MASK;

		if (!copy) {
			vas_destroy(dst);
			return NULL;
		}

		vad_insert(dst, copy);

		for (uint64_t va = vad->start; va < vad->end; va += PAGE_SIZE) {
			uint64_t src_phys = paging_virt_to_phys(src, va);
			page_t *page;

			if (!src_phys)
				continue;

			page = phys_to_page(PAGE_ALIGN_DOWN(src_phys));
			if (!page || PageReserved(page) || PagePoisoned(page)) {
				if (paging_map_page(dst, va, PAGE_ALIGN_DOWN(src_phys), prot) !=
					0) {
					vas_destroy(dst);
					return NULL;
				}
				continue;
			}

			page_ref_inc(page);

			if ((prot & PAGE_WRITE) && !(vad->flags & VAD_SHARED)) {
				uint64_t cow_flags = prot & ~PAGE_WRITE;

				SetPageFlag(page, PAGE_COW);
				if (remap_page_flags(src, va, cow_flags) != 0 ||
					paging_map_page(dst, va, PAGE_ALIGN_DOWN(src_phys),
									cow_flags) != 0) {
					pmm_free(page);
					vas_destroy(dst);
					return NULL;
				}
			} else {
				if (paging_map_page(dst, va, PAGE_ALIGN_DOWN(src_phys), prot) !=
					0) {
					pmm_free(page);
					vas_destroy(dst);
					return NULL;
				}
			}
		}
	}

	return dst;
}

int vas_add(vas_t *vas, vad_t *vad)
{
	if (!vas || !vad || vad->start >= vad->end)
		return -1;
	if ((vad->start & (PAGE_SIZE - 1)) || (vad->end & (PAGE_SIZE - 1)))
		return -1;
	if (vas_overlaps(vas, vad->start, vad->end))
		return -1;

	vad_insert(vas, vad);
	return 0;
}
