#include <cpu/idt.h>
#include <core/alix.h>
#define KLOG_NS "idt"
#include <log/klog.h>
#include <debug/panic.h>
#include <sys/apic.h>
#include <stddef.h>

#define IDT_TRAP 0xF
#define IDT_INTERRUPT 0xE

#define IDT_EXCEPTION_LIST(X)         \
	X(0, "Division By Zero")          \
	X(1, "Debug")                     \
	X(2, "NMI")                       \
	X(3, "Breakpoint")                \
	X(4, "Overflow")                  \
	X(5, "Bound Range Exceeded")      \
	X(6, "Invalid Opcode")            \
	X(7, "Device Not Available")      \
	X(8, "Double Fault")              \
	X(9, "Reserved")                  \
	X(10, "Invalid TSS")              \
	X(11, "Segment Not Present")      \
	X(12, "Stack-Segment Fault")      \
	X(13, "General Protection Fault") \
	X(14, "Page Fault")               \
	X(15, "Reserved")                 \
	X(16, "X87 Exception")            \
	X(17, "Alignment Check")          \
	X(18, "Machine Check")            \
	X(19, "SIMD Exception")           \
	X(20, "Virtualization Exception") \
	X(21, "Control Protection")       \
	X(22, "Reserved")                 \
	X(23, "Reserved")                 \
	X(24, "Reserved")                 \
	X(25, "Reserved")                 \
	X(26, "Reserved")                 \
	X(27, "Reserved")                 \
	X(28, "Hypervisor Injection")     \
	X(29, "VMM Communication")        \
	X(30, "Security")                 \
	X(31, "Reserved")

#define IRQ_VECTOR_COUNT 256

static const char *exception_str[32] = {
#define IDT_EXCEPTION_STR(v, s) [v] = s,
	IDT_EXCEPTION_LIST(IDT_EXCEPTION_STR)
#undef IDT_EXCEPTION_STR
};

__attribute__((aligned(16))) idt_entry_t idt[256];
idtr_t idtr = {
	.limit = (uint16_t)((sizeof(idt_entry_t) * 256) - 1),
	.base = (uint64_t)&idt[0],
};

extern void *isr_stubs[256];

static irq_handler_t vector_handlers[IRQ_VECTOR_COUNT];

static void irq_set_vector(uint8_t vector, irq_callback callback,
						   interrupt_frame_t *ctx, uint8_t lapic_id)
{
	irq_handler_t *h = &vector_handlers[vector];

	h->vector = vector;
	h->lapic_id = lapic_id;
	h->callback = callback;
	h->ctx = ctx;
}

void irq_install(uint8_t irq, irq_callback callback, interrupt_frame_t *ctx,
				 uint8_t lapic_id)
{
	if (irq >= 16) {
		klog("tried to install handler for IRQ#%u (max 16).", irq);
		return;
	}

	irq_set_vector((uint8_t)(IRQ_BASE + irq), callback, ctx, lapic_id);
	ioapic_write_red(irq, (uint8_t)(IRQ_BASE + irq), IOAPIC_FIXED,
					 IOAPIC_ACTIVE_HI, IOAPIC_TRIGGER_EDGE, lapic_id);
}

void irq_install_vector(uint8_t vector, irq_callback callback,
						interrupt_frame_t *ctx, uint8_t lapic_id)
{
	irq_set_vector(vector, callback, ctx, lapic_id);
}

void irq_uninstall_vector(uint8_t vector)
{
	vector_handlers[vector].callback = NULL;
	vector_handlers[vector].ctx = NULL;
	vector_handlers[vector].vector = vector;
	vector_handlers[vector].lapic_id = 0;
}

void irq_uninstall(uint8_t irq)
{
	if (irq >= 16) {
		klog("tried to uninstall handler for IRQ#%u (max 16).", irq);
		return;
	}

	irq_uninstall_vector((uint8_t)(IRQ_BASE + irq));
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
	klog("idtr base=0x%016llx limit=0x%04x", (unsigned long long)idtr.base,
		 idtr.limit);

	for (uint32_t v = 0; v < 256; v++) {
		idt_entry_t *desc = &idt[v];
		ktrace("vec=%03u base=0x%016llx cs=0x%04x ist=%u type=0x%x dpl=%u p=%u",
			   v, (unsigned long long)idt_desc_base(desc), desc->codeseg,
			   desc->ist & 0x7, desc->flags & 0xf, (desc->flags >> 5) & 0x3,
			   !!(desc->flags & 0x80));
	}
}

void idt_init(void)
{
#define IDT_SET_EXCEPTION(vec, name) \
	idt_set_desc(&idt[vec], (uint64_t)isr_stubs[vec], IDT_TRAP, 0);
	IDT_EXCEPTION_LIST(IDT_SET_EXCEPTION)
#undef IDT_SET_EXCEPTION

	for (int v = 32; v < 256; v++)
		idt_set_desc(&idt[v], (uint64_t)isr_stubs[v], IDT_INTERRUPT, 0);

	idt_set_desc(&idt[0x80], (uint64_t)isr_stubs[0x80], IDT_TRAP, 3);
	idt_dump();
	__asm__ volatile("lidt %0" ::"m"(idtr));
	ktrace("loaded: 256 vectors");
}

interrupt_frame_t *irq_dispatch(uint8_t irq, interrupt_frame_t *frame)
{
	irq_handler_t *h = &vector_handlers[IRQ_BASE + irq];

	if (!h->callback) {
		klog("unhandled IRQ#%u.", irq);
		return frame;
	}

	return h->callback(h->ctx ? h->ctx : frame);
}

interrupt_frame_t *irq_dispatch_vector(uint8_t vector, interrupt_frame_t *frame)
{
	irq_handler_t *h = &vector_handlers[vector];

	if (!h->callback) {
		if (vector >= IRQ_BASE)
			klog("unhandled IRQ#%u.", (unsigned)(vector - IRQ_BASE));
		else
			klog("unhandled vector 0x%02x.", vector);
		return frame;
	}

	return h->callback(h->ctx ? h->ctx : frame);
}

interrupt_frame_t *isr_common_handler(interrupt_frame_t *frame)
{
	uint8_t vector = (uint8_t)frame->vector;

	if (vector == 0xFF)
		return frame;

	if (vector < 32)
		kpanic(frame, "%s", exception_str[vector]);

	if (vector == 0x80)
		return frame;

	return irq_dispatch_vector(vector, frame);
}
