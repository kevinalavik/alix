#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cpu/instr.h>
#include <dev/uart.h>
#include <dev/pit.h>
#include <api/time.h>
#include <sys/liminereq.h>
#include <sys/alix.h>
#define KLOG_NS "alix"
#include <sys/klog.h>
#include <cpu/gdt.h>
#include <flanterm.h>
#include <flanterm_backends/fb.h>
#include <lib/kprintf.h>

uint64_t boot_tsc = 0;
struct flanterm_context *ft_ctx = NULL;

void kmain(void)
{
	if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
		nointloop();
	}

	if (framebuffer_request.response == NULL ||
		framebuffer_request.response->framebuffer_count < 1) {
		klog("error: no framebuffer");
		nointloop(); /* no point in continuing, this is a modern OS we neeeed graphics :^) */
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
		klog("error: failed to initialize flanterm.");
		nointloop();
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

	hlt();
}