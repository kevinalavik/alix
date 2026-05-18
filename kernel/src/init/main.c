#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <cpu/instr.h>
#include <dev/uart.h>
#include <dev/pit.h>
#include <boot/liminereq.h>
#include <core/alix.h>
#include <debug/panic.h>
#define KLOG_NS "alix"
#include <log/klog.h>
#include <cpu/gdt.h>
#include <cpu/smp.h>
#include <sys/apic.h>
#include <sys/timer.h>
#include <sys/time.h>
#include <flanterm.h>
#include <flanterm_backends/fb.h>
#include <lib/kprintf.h>
#include <cpu/idt.h>
#include <fs/initrd.h>
#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <mm/mm.h>
#include <lib/string.h>
#include <sys/acpi.h>
#include <sys/sched.h>
#include <acpi/madt.h>
#include <sdi/host.h>

uint64_t boot_tsc = 0;
struct flanterm_context *ft_ctx = NULL;
uint64_t hhdm_offset = 0;

static bool kmain_string_equal(const char *lhs, const char *rhs)
{
	size_t lhs_len;
	size_t rhs_len;

	if (!lhs || !rhs)
		return false;

	lhs_len = strlen(lhs);
	rhs_len = strlen(rhs);
	return lhs_len == rhs_len && memcmp(lhs, rhs, lhs_len) == 0;
}

static bool kmain_string_ends_with(const char *str, const char *suffix)
{
	size_t str_len;
	size_t suffix_len;

	if (!str || !suffix)
		return false;

	str_len = strlen(str);
	suffix_len = strlen(suffix);
	if (suffix_len > str_len)
		return false;

	return kmain_string_equal(str + str_len - suffix_len, suffix);
}

static const char *kmain_basename(const char *path)
{
	const char *last = path;

	if (!path)
		return "";

	for (const char *cursor = path; *cursor; cursor++) {
		if (*cursor == '/')
			last = cursor + 1;
	}

	return last;
}

static const struct limine_file *kmain_find_cpio_module(void)
{
	struct limine_module_response *response = module_request.response;

	if (!response || response->module_count == 0 || !response->modules)
		return NULL;

	for (uint64_t i = 0; i < response->module_count; i++) {
		const struct limine_file *module = response->modules[i];

		if (module && module->path &&
			kmain_string_ends_with(kmain_basename(module->path), ".cpio"))
			return module;

		if (module && module->string &&
			kmain_string_ends_with(kmain_basename(module->string), ".cpio"))
			return module;
	}

	return NULL;
}

void kmain(void)
{
	struct limine_framebuffer *framebuffer;
	const struct limine_file *initrd_module;

	if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
		kpanic(NULL, "unsupported limine base revision");
	}

	if (framebuffer_request.response == NULL ||
		framebuffer_request.response->framebuffer_count < 1) {
		kpanic(NULL, "no framebuffer");
	}
	if (memmap_request.response == NULL) {
		kpanic(NULL, "no memory map");
	}
	if (executable_address_request.response == NULL) {
		kpanic(NULL, "no executable address response");
	}
	if (paging_mode_request.response == NULL ||
		paging_mode_request.response->mode != LIMINE_PAGING_MODE_X86_64_4LVL) {
		kpanic(NULL, "expected Limine x86_64 4-level paging mode");
	}
	if (hhdm_request.response == NULL) {
		kpanic(NULL, "no HHDM offset");
	}

	framebuffer = framebuffer_request.response->framebuffers[0];
	hhdm_offset = hhdm_request.response->offset;

	ft_ctx = flanterm_fb_init(
		NULL, NULL, framebuffer->address, framebuffer->width,
		framebuffer->height, framebuffer->pitch, framebuffer->red_mask_size,
		framebuffer->red_mask_shift, framebuffer->green_mask_size,
		framebuffer->green_mask_shift, framebuffer->blue_mask_size,
		framebuffer->blue_mask_shift, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, 0, 0, 1, 0, 0, 0, 0);

	if (ft_ctx == NULL) {
		kpanic(NULL, "failed to initialize flanterm");
	}

	uart_init();
	klog("uart0: 16550 ready");
	time_init();
	pit_init();
	klog("timecounter: %s", time_source_name());

	boot_tsc = rdtsc();
	klog_init();
	klog("framebuffer: %llux%llu pitch=%llu bpp=%u addr=%p",
		 (unsigned long long)framebuffer->width,
		 (unsigned long long)framebuffer->height,
		 (unsigned long long)framebuffer->pitch, framebuffer->bpp,
		 framebuffer->address);

	klog("hhdm offset=%p", (void *)hhdm_offset);
	klog("limine paging mode=%llu",
		 (unsigned long long)paging_mode_request.response->mode);
	gdt_init();
	idt_init();

	pfndb_init(memmap_request.response);
	pmm_init();
	paging_init(memmap_request.response, framebuffer,
				executable_address_request.response);
	klog("kernel VAS: %p (pml4=%p)", kernel_vas, kernel_vas->pml4);
	kheap_init();
	if (sdi_kernel_init() != 0) {
		kpanic(NULL, "failed to initialize SDI");
	}

	if (rsdp_request.response == NULL) {
		kpanic(NULL, "no RSDP information");
	}
	acpi_init(rsdp_request.response);
	madt_init();
	apic_init();

	if (mp_request.response == NULL) {
		kpanic(NULL, "no SMP information");
	}

	smp_init(mp_request.response);
	sched_init();
	vfs_init();
	tmpfs_init();
	if (vfs_mount(NULL, NULL, NULL, "tmpfs", NULL) != 0) {
		kpanic(NULL, "failed to mount tmpfs root");
	}

	initrd_module = kmain_find_cpio_module();
	if (!initrd_module) {
		kpanic(NULL, "no .cpio initrd module found");
	}

	klog("found initrd module path='%s' size=%llu",
		 initrd_module->path ? initrd_module->path : "<none>",
		 (unsigned long long)initrd_module->size);
	if (initrd_unpack(initrd_module->address, initrd_module->size) != 0) {
		kpanic(NULL, "failed to unpack initrd");
	}
	if (sdi_load_drivers("/drivers") != 0) {
		kpanic(NULL, "failed to load SDI drivers");
	}

	timer_init(1000);
	smp_wait_all_online();

	klog("--------------------------------------------------");
	klog("kernel v" ALIX_VERSION " initialized.");
	sti();
	for (;;)
		hlt();
}
