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
#include <flanterm.h>
#include <flanterm_backends/fb.h>
#include <lib/kprintf.h>
#include <cpu/idt.h>

uint64_t boot_tsc = 0;
struct flanterm_context *ft_ctx = NULL;

void kmain(void)
{
	if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
		kpanic(NULL, "unsupported limine base revision");
	}

	if (framebuffer_request.response == NULL ||
		framebuffer_request.response->framebuffer_count < 1) {
		kpanic(NULL, "no framebuffer");
	}

	struct limine_framebuffer *framebuffer =
		framebuffer_request.response->framebuffers[0];

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
	klog("UART (16550) initialized");
	time_init();
	klog("time API initialized");
	pit_init();
	klog("registered %s timer", time_source_name());

	boot_tsc = rdtsc();
	klog_init();

	gdt_init();
	klog("initialized GDT for BSP (cpu0)");

	idt_init();
	klog("initialized IDT");

	hlt();
}
