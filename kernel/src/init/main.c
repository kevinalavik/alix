#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cpu/instr.h>
#include <dev/uart.h>
#include <dev/pit.h>
#include <api/time.h>
#include <boot/liminereq.h>
#include <core/alix.h>
#include <debug/panic.h>
#define KLOG_NS "alix"
#include <log/klog.h>
#include <cpu/gdt.h>
#include <cpu/smp.h>
#include <flanterm.h>
#include <flanterm_backends/fb.h>
#include <lib/kprintf.h>
#include <cpu/idt.h>
#include <mm/mm.h>
#include <lib/string.h>
#include <sys/acpi.h>
#include <acpi/madt.h>

uint64_t boot_tsc = 0;
struct flanterm_context *ft_ctx = NULL;
uint64_t hhdm_offset = 0;

void kmain(void)
{
	struct limine_framebuffer *framebuffer;

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
	klogv("framebuffer: %llux%llu pitch=%llu bpp=%u addr=%p",
		  (unsigned long long)framebuffer->width,
		  (unsigned long long)framebuffer->height,
		  (unsigned long long)framebuffer->pitch, framebuffer->bpp,
		  framebuffer->address);

	klogv("hhdm offset=%p", (void *)hhdm_offset);
	klogv("limine paging mode=%llu",
		  (unsigned long long)paging_mode_request.response->mode);
	gdt_init();
	idt_init();

	/* memory stuff*/
	pfndb_init(memmap_request.response);
	pmm_init();
	paging_init(memmap_request.response, framebuffer,
				executable_address_request.response);
	klog("kernel VAS: %p (pml4=%p)", kernel_vas, kernel_vas->pml4);
	kheap_init();

	/* smp stuff and apic */
	if (mp_request.response == NULL) {
		kpanic(NULL, "no SMP information");
	}

	smp_init(mp_request.response);
	smp_wait_all_online();

	if (!cpu_current()->is_bsp) {
		nointloop();
		__builtin_unreachable();
	}

	if (rsdp_request.response == NULL) {
		kpanic(NULL, "no RSDP information");
	}
	acpi_init(rsdp_request.response);
	madt_init();

	klog("--------------------------------------------------");
	klog("kernel v" ALIX_VERSION " initialized.");
	hlt();
}
