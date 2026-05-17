#ifndef BOOT_LIMINEREQ_H
#define BOOT_LIMINEREQ_H

#define LIMINE_REQUEST \
	__attribute__((used, section(".limine_requests"))) static volatile

LIMINE_REQUEST uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

LIMINE_REQUEST struct limine_framebuffer_request framebuffer_request = {
	.id = LIMINE_FRAMEBUFFER_REQUEST_ID,
	.revision = 0,
};
LIMINE_REQUEST struct limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST_ID,
	.revision = 0,
};
LIMINE_REQUEST struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST_ID,
	.revision = 0,
};
LIMINE_REQUEST struct limine_executable_address_request
	executable_address_request = {
		.id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
		.revision = 0,
	};
LIMINE_REQUEST struct limine_paging_mode_request paging_mode_request = {
	.id = LIMINE_PAGING_MODE_REQUEST_ID,
	.revision = 0,
	.mode = LIMINE_PAGING_MODE_X86_64_4LVL,
	.max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
	.min_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
};
LIMINE_REQUEST struct limine_mp_request mp_request = {
	.id = LIMINE_MP_REQUEST_ID,
	.revision = 0,
};

LIMINE_REQUEST struct limine_rsdp_request rsdp_request = {
	.id = LIMINE_RSDP_REQUEST_ID,
	.revision = 0,
};

__attribute__((used,
			   section(".limine_requests_start"))) static volatile uint64_t
	limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) static volatile uint64_t
	limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

#endif // BOOT_LIMINEREQ_H