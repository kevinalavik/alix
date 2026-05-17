#include <sys/apic.h>
#define KLOG_NS "apic"
#include <log/klog.h>
#include <cpu/instr.h>
#include <acpi/madt.h>
#include <stdint.h>
#include <cpu/smp.h>
#include <cpu/idt.h>
#include <mm/vmm.h>

#define IOREGSEL 0x00
#define IOWIN 0x10
#define SPURIOUS_VECTOR 0xFF
#define APIC_LVT_DISABLE 0x10000

static inline uint32_t mmio_read32(uintptr_t addr)
{
	return *(volatile uint32_t *)addr;
}

static inline void mmio_write32(uintptr_t addr, uint32_t val)
{
	*(volatile uint32_t *)addr = val;
}

#define APIC_DISABLE_LVT(reg) lapic_write((reg), APIC_LVT_DISABLE);
#define APIC_LVT_REGS(X) \
	X(APIC_LVT_TIMER)    \
	X(APIC_LVT_THERMAL)  \
	X(APIC_LVT_PERF)     \
	X(APIC_LVT_LINT0)    \
	X(APIC_LVT_LINT1)    \
	X(APIC_LVT_ERROR)

extern uintptr_t lapic_base;
extern madt_lapic_t *lapics[];
extern madt_ioapic_t *ioapics[];
extern size_t lapic_count;
extern size_t ioapic_count;
extern madt_iso_t *isos[16];
extern size_t iso_count;
extern madt_lapic_nmi_t *nmis[224];
extern size_t nmi_count;

uint64_t apic_msr_read(uint64_t offset)
{
	return rdmsr(APIC_BASE_MSR + offset);
}

void apic_msr_write(uint64_t offset, uint64_t val)
{
	wrmsr(APIC_BASE_MSR + offset, val);
}

uint32_t ioapic_read(uintptr_t base, uint8_t regoff)
{
	mmio_write32(base + IOREGSEL, regoff);
	return mmio_read32(base + IOWIN);
}

void ioapic_write(uintptr_t base, uint8_t regoff, uint32_t data)
{
	mmio_write32(base + IOREGSEL, regoff);
	mmio_write32(base + IOWIN, data);
}

uint32_t lapic_read(uint16_t reg)
{
	return mmio_read32(lapic_base + reg);
}

void lapic_write(uint16_t reg, uint32_t val)
{
	mmio_write32(lapic_base + reg, val);
}

void apic_send_eoi(void)
{
	lapic_write(APIC_EOI, 0);
}

void ioapic_write_red(uint32_t gsi, uint8_t vec, uint8_t delivery_mode,
					  uint8_t polarity, uint8_t trigger_mode, uint8_t dest)
{
	union ioapic_redirect_entry redent = { 0 };

	redent.vec = vec;
	redent.delivery_mode = delivery_mode;
	redent.destination_mode = IOAPIC_PHYSICAL_DESTINATION;
	redent.delivery_status = 0;
	redent.pin_polarity = polarity;
	redent.remote_irr = 0;
	redent.trigger_mode = trigger_mode;
	redent.mask = 0;
	redent.reserved = 0;
	redent.dest = dest;

	for (size_t i = 0; i < iso_count; i++) {
		if ((vec - IRQ_BASE) != isos[i]->src)
			continue;

		gsi = isos[i]->gsi;
		if (isos[i]->flags & 2)
			redent.pin_polarity = IOAPIC_ACTIVE_LO;
		if (isos[i]->flags & 8)
			redent.trigger_mode = IOAPIC_TRIGGER_LEVEL;
		break;
	}

	size_t i;
	for (i = 0; i < ioapic_count; i++) {
		uint8_t maxreds =
			(ioapic_read((uintptr_t)PHYS_TO_VIRT(ioapics[i]->addr),
						 IOAPICVER) >>
			 16) &
			0xFF;
		if (ioapics[i]->gsi_base <= gsi && ioapics[i]->gsi_base + maxreds > gsi)
			break;
	}

	if (i == ioapic_count) {
		klogf("No IOAPIC found for GSI %u", gsi);
		return;
	}

	uintptr_t base = (uintptr_t)PHYS_TO_VIRT(ioapics[i]->addr);
	uint32_t pin = gsi - ioapics[i]->gsi_base;
	ioapic_write(base, IOAPICREDTBLL(pin), redent.bytes.low);
	ioapic_write(base, IOAPICREDTBLH(pin), redent.bytes.high);

	klogvv("IOAPIC redir: vec=%u gsi=%u dest_lapic=%u mode=phys", vec, gsi,
		   dest);
}

void apic_cpu_init(uint8_t cpu_index)
{
	uint64_t base_msr = rdmsr(0x1B);

	wrmsr(0x1B, (base_msr | (1ULL << 11)) & ~(1ULL << 10));
	lapic_write(APIC_SPURIOUS_IVR, (1u << 8) | SPURIOUS_VECTOR);
	lapic_write(APIC_DEST_FORMAT, 0xFFFFFFFF);
	lapic_write(APIC_LOCAL_DEST,
				cpu_index < 8 ? (uint32_t)(1u << cpu_index) << 24 : 0);
	APIC_LVT_REGS(APIC_DISABLE_LVT);
	lapic_write(APIC_TPR, 0);
	lapic_write(APIC_ERROR_STATUS, 0);
	(void)lapic_read(APIC_ERROR_STATUS);
}

void apic_init(void)
{
	uintptr_t lapic_virt = (uintptr_t)PHYS_TO_VIRT(lapic_base);

	paging_map_page(kernel_vas, lapic_virt, lapic_base,
					PAGE_PRESENT | PAGE_WRITE);
	lapic_base = lapic_virt;
	apic_cpu_init(0);

	for (size_t i = 0; i < ioapic_count; i++) {
		uintptr_t base = (uintptr_t)PHYS_TO_VIRT(ioapics[i]->addr);
		uint8_t maxreds;

		klogvv("Mapping I/O APIC #%zu (phys 0x%llx -> virt 0x%llx)", i,
			   ioapics[i]->addr, (uint64_t)base);
		paging_map_page(kernel_vas, base, ioapics[i]->addr,
						PAGE_PRESENT | PAGE_WRITE);

		maxreds = (ioapic_read(base, IOAPICVER) >> 16) & 0xFF;
		klogv("Masking %u redirection entries for I/O APIC #%zu", maxreds, i);

		for (uint8_t n = 0; n < maxreds; n++) {
			ioapic_write(base, IOAPICREDTBLL(n), 0x10000);
			ioapic_write(base, IOAPICREDTBLH(n), 0);
		}
	}
}
