#include <acpi/madt.h>
#define KLOG_NS "madt"
#include <log/klog.h>
#include <debug/panic.h>
#include <core/alix.h>
#include <cpu/instr.h>

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MADT_BOUNDED_APPEND(array, count, limit, item) \
	(((count) < (limit)) ? ((array)[(count)++] = (item), 1) : 0)
#define MADT_HANDLE_STRUCT(type, struct_t, array, count, limit, overflow_msg,  \
						   overflow_arg, success_msg, ...) \
	case MADT_##type: {                                                           \
		struct_t *item = (struct_t *)entry;                                       \
		if (!MADT_BOUNDED_APPEND(array, count, limit, item)) {                    \
			klog(overflow_msg, overflow_arg);                                     \
			break;                                                                \
		}                                                                         \
		klog(success_msg, __VA_ARGS__);                                           \
		break;                                                                    \
	}

madt_t *madt = NULL;

uintptr_t lapic_base = 0;

madt_lapic_t *lapics[MAX_CPUS];
madt_ioapic_t *ioapics[MAX_CPUS];

size_t lapic_count = 0;
size_t ioapic_count = 0;

madt_iso_t *isos[16];
size_t iso_count = 0;

madt_lapic_nmi_t *nmis[224];
size_t nmi_count = 0;

static inline const char *madt_type_to_str(madt_type_t type)
{
	switch (type) {
#define MADT_TYPE_CASE(name, value, desc) case MADT_##name: return desc;
	MADT_ENTRY_TYPES(MADT_TYPE_CASE)
#undef MADT_TYPE_CASE
	default:
		return "unknown";
	}
}

void madt_init()
{
	madt = (madt_t *)acpi_get_table("APIC");
	if (madt == NULL) {
		kpanic(NULL, "MADT not found, cant continue without APIC support");
		return;
	}

	if (madt->flags & 1) {
		klog("Masking 8259 PIC");
		outb(0x21, 0xff);
		outb(0xa1, 0xff);
	}

	lapic_base = madt->lapic_addr;

	uint8_t *entry = madt->structures;
	uint8_t *end = (uint8_t *)madt + madt->hdr.length;
	for (; entry + sizeof(madt_header_t) <= end;) {
		madt_header_t *mhdr = (madt_header_t *)entry;
		if (mhdr->len < sizeof(madt_header_t) || entry + mhdr->len > end) {
			klog("Malformed MADT entry type %u (%s) with length %u", mhdr->type,
				 madt_type_to_str(mhdr->type), mhdr->len);
			break;
		}

		switch (mhdr->type) {
		MADT_HANDLE_STRUCT(LAPIC, madt_lapic_t, lapics, lapic_count,
						   ARRAY_LEN(lapics),
						   "Reached maximum allowed CPUs, processor #%u will be left disabled.",
						   item->id,
						   "Registered LAPIC for processor #%u with _UID %u (%s)",
						   item->id, item->uid,
						   (item->flags & 1) ? "enabled" : "disabled")
		MADT_HANDLE_STRUCT(IOAPIC, madt_ioapic_t, ioapics, ioapic_count,
						   ARRAY_LEN(ioapics),
						   "Reached maximum allowed IOAPIC controllers, IOAPIC #%u will be unused.",
						   item->id,
						   "Registered IOAPIC #%u located at 0x%llx (gsi base=%llx)",
						   item->id, item->addr, item->gsi_base)
		MADT_HANDLE_STRUCT(ISO, madt_iso_t, isos, iso_count, ARRAY_LEN(isos),
						   "Reached maximum allowed interrupt source overrides, bus %u source %u will be ignored.",
						   item->bus,
						   "Interrupt source override on bus %u with source %u (gsi=%u, flags=%x)",
						   item->bus, item->src, item->gsi, item->flags)
		MADT_HANDLE_STRUCT(LAPIC_NMI, madt_lapic_nmi_t, nmis, nmi_count,
						   ARRAY_LEN(nmis),
						   "Reached maximum allowed NMIs, LINT#%u on processor _UID %u will be ignored.",
						   item->acpi_uid,
						   "NMI for LINT#%u on processor with _UID %u, flags %x",
						   item->LINTn, item->acpi_uid, item->flags)
		case MADT_LAPIC_OVERRIDE: {
			madt_lapic_override_t *override = (madt_lapic_override_t *)entry;
			lapic_base = override->addr;
			klog("Overridden LAPIC base address: 0x%llx",
				 (unsigned long long)override->addr);
			break;
		}
		case MADT_NMI_SRC:
		case MADT_LX2APIC:
		case MADT_LX2APIC_NMI:
		default:
			klog("Unhandled MADT Entry with type %u (%s)", mhdr->type,
				 madt_type_to_str(mhdr->type));
			break;
		}

		entry += mhdr->len;
	}
	klog("MADT summary: lapics=%zu ioapics=%zu isos=%zu nmis=%zu", lapic_count,
		 ioapic_count, iso_count, nmi_count);
}
