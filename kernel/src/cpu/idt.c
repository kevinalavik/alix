#include <cpu/idt.h>
#define KLOG_NS "idt"
#include <log/klog.h>
#include <debug/panic.h>
#include <stddef.h>

#define IDT_TRAP 0xF
#define IDT_INTERRUPT 0xE

#define APIC_SPURIOUS_VECTOR 0xFF

static const char *_exception_str[32] = {
	"Division By Zero",
	"Debug",
	"NMI",
	"Breakpoint",
	"Overflow",
	"Bound Range Exceeded",
	"Invalid Opcode",
	"Device Not Available",
	"Double Fault",
	"Reserved",
	"Invalid TSS",
	"Segment Not Present",
	"Stack-Segment Fault",
	"General Protection Fault",
	"Page Fault",
	"Reserved",
	"X87 Exception",
	"Alignment Check",
	"Machine Check",
	"SIMD Exception",
	"Virtualization Exception",
	"Control Protection",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Hypervisor Injection",
	"VMM Communication",
	"Security",
	"Reserved",
};

__attribute__((aligned(16))) idt_entry_t idt[256];
idtr_t idtr = {
	.limit = (uint16_t)((sizeof(idt_entry_t) * 256) - 1),
	.base = (uint64_t)&idt[0],
};

extern void *isr_stubs[256];

irq_handler_t irq_handlers[224] = { 0 };

void irq_install(uint8_t irq, irq_callback callback, interrupt_frame_t *ctx,
				 maybe_unused uint8_t lapic_id)
{
	if (irq > 16) {
		klog("tried to install handler for IRQ#%u (max 16).", irq);
		return;
	}

	irq_handler_t *h = &irq_handlers[irq];
	if (h->callback) {
		klog("overwriting IRQ#%u callback 0x%llx with 0x%llx.", irq,
			 h->callback, callback);
	}

	h->callback = callback;
	h->ctx = ctx;
	/* TODO: IOAPIC setup */
}

void irq_uninstall(uint8_t irq)
{
	if (irq > 16) {
		klog("tried to uninstall handler for IRQ#%u (max 16).", irq);
		return;
	}

	irq_handler_t *h = &irq_handlers[irq];
	h->callback = NULL;
	h->ctx = NULL;
}

interrupt_frame_t *irq_dispatch(uint8_t irq, interrupt_frame_t *frame)
{
	irq_handler_t *h = &irq_handlers[irq];
	if (!h->callback) {
		klog("unhandled IRQ#%u.", irq);
		return frame;
	}
	interrupt_frame_t *next = h->callback(h->ctx ? h->ctx : frame);
	return next ? next : frame;
}

void idt_set_desc(idt_entry_t *desc, uint64_t offset, uint8_t type, uint8_t dpl)
{
	desc->base_low = offset & 0xFFFF;
	desc->codeseg = 0x08;
	desc->ist = 0;
	desc->flags = (uint8_t)((1 << 7) | (dpl << 5) | type);
	desc->base_mid = (offset >> 16) & 0xFFFF;
	desc->base_high = (offset >> 32) & 0xFFFFFFFF;
	desc->reserved = 0;
}

static uint64_t idt_desc_base(const idt_entry_t *desc)
{
	return ((uint64_t)desc->base_low) | ((uint64_t)desc->base_mid << 16) |
		   ((uint64_t)desc->base_high << 32);
}

static void idt_dump(void)
{
	klogvvv("idtr base=0x%016llx limit=0x%04x", (unsigned long long)idtr.base,
			idtr.limit);

	for (uint32_t v = 0; v < 256; v++) {
		idt_entry_t *desc = &idt[v];
		klogvvv(
			"vec=%03u base=0x%016llx cs=0x%04x ist=%u type=0x%x dpl=%u p=%u", v,
			(unsigned long long)idt_desc_base(desc), desc->codeseg,
			desc->ist & 0x7, desc->flags & 0xf, (desc->flags >> 5) & 0x3,
			!!(desc->flags & 0x80));
	}
}

void idt_init(void)
{
	for (int v = 0; v < 32; v++)
		idt_set_desc(&idt[v], (uint64_t)isr_stubs[v], IDT_TRAP, 0);

	for (int v = 32; v < 256; v++)
		idt_set_desc(&idt[v], (uint64_t)isr_stubs[v], IDT_INTERRUPT, 0);

	idt_set_desc(&idt[0x80], (uint64_t)isr_stubs[0x80], IDT_TRAP, 3);
	idt_dump();
	__asm__ volatile("lidt %0" ::"m"(idtr));
	klogvv("loaded: 256 vectors");
}

interrupt_frame_t *isr_common_handler(interrupt_frame_t *frame)
{
	if (frame->vector == APIC_SPURIOUS_VECTOR)
		return frame;

	if (frame->vector == 0x80) {
		return frame; /* TODO: handle system call */
	}

	if (frame->vector < IRQ_BASE) {
		kpanic(frame, "%s", _exception_str[frame->vector]);
	}

	interrupt_frame_t *next =
		irq_dispatch((uint8_t)(frame->vector - IRQ_BASE), frame);
	/* TODO: send eoi (aka apic stuff) */
	return next;
}
