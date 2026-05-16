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
#define KLOG_NS "kernel"
#include <sys/klog.h>
#include <cpu/gdt.h>

uint64_t boot_tsc = 0;

void kmain(void)
{
	if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
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

	klog("hello from alix (" ALIX_VERSION ")");

	gdt_init();

	if (framebuffer_request.response == NULL ||
		framebuffer_request.response->framebuffer_count < 1) {
		klog("error: no framebuffer");
		nointloop(); /* no point in continuing, this is a modern OS we neeeed graphics :^) */
	}

	struct limine_framebuffer *framebuffer =
		framebuffer_request.response->framebuffers[0];

	volatile uint32_t *fb_ptr = framebuffer->address;

	for (size_t y = 0; y < framebuffer->height; y++) {
		for (size_t x = 0; x < framebuffer->width; x++) {
			uint32_t nX = x * 255 / framebuffer->width;
			uint32_t nY = y * 255 / framebuffer->height;

			fb_ptr[y * (framebuffer->pitch / 4) + x] = (nY << 8) | nX;
		}
	}

	hlt();
}