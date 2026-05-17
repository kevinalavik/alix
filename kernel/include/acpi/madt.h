#ifndef ACPI_MADT_H
#define ACPI_MADT_H

#include <sys/acpi.h>

#define MADT_ENTRY_TYPES(X) \
	X(LAPIC, 0x00, "apic") \
	X(IOAPIC, 0x01, "ioapic") \
	X(ISO, 0x02, "interrupt source override") \
	X(NMI_SRC, 0x03, "non-maskable interrupt source") \
	X(LAPIC_NMI, 0x04, "non-maskable interrupt") \
	X(LAPIC_OVERRIDE, 0x05, "lapic address override") \
	X(LX2APIC, 0x09, "local x2apic") \
	X(LX2APIC_NMI, 0x0a, "local x2apic nmi") \
	X(GICC, 0x0b, "gicc") \
	X(GICD, 0x0c, "gicd") \
	X(GIC_MSI_FRAME, 0x0d, "gic msi frame") \
	X(GICR, 0x0e, "gicr") \
	X(GIC_ITS, 0x0f, "gic its")

typedef enum {
#define MADT_TYPE_ENUM(name, value, desc) MADT_##name = value,
	MADT_ENTRY_TYPES(MADT_TYPE_ENUM)
#undef MADT_TYPE_ENUM
} madt_type_t;

typedef struct {
	uint8_t type;
	uint8_t len;
} __attribute__((packed)) madt_header_t;

typedef enum {
	POLARITY_BUS = 0,
	POLARITY_ACTIVE_HI = 1,
	POLARITY_ACTIVE_LO = 3,
	TRIGGER_BUS = 0,
	TRIGGER_EDGE = 1,
	TRIGGER_LEVEL = 3
} mps_inti_flags_t;

typedef struct {
	madt_header_t hdr;
	uint8_t uid;
	uint8_t id;
	uint32_t flags;
} __attribute__((packed)) madt_lapic_t;

typedef struct {
	madt_header_t hdr;
	uint8_t id;
	uint8_t reserved;
	uint32_t addr;
	uint32_t gsi_base;
} __attribute__((packed)) madt_ioapic_t;

typedef struct {
	madt_header_t hdr;
	uint8_t bus;
	uint8_t src;
	uint32_t gsi;
	uint16_t flags;
} __attribute__((packed)) madt_iso_t;

typedef struct {
	madt_header_t hdr;
	uint16_t flags;
	uint32_t gsi;
} __attribute__((packed)) madt_nmi_src_t;

typedef struct {
	madt_header_t hdr;
	uint8_t acpi_uid;
	uint16_t flags;
	uint8_t LINTn;
} __attribute__((packed)) madt_lapic_nmi_t;

typedef struct {
	madt_header_t hdr;
	uint16_t reserved;
	uint64_t addr;
} __attribute__((packed)) madt_lapic_override_t;

typedef struct {
	madt_header_t hdr;
	uint16_t reserved;
	uint32_t id;
	uint32_t flags;
	uint32_t acpi_uid;
} __attribute__((packed)) madt_lx2apic_t;

typedef struct {
	madt_header_t hdr;
	uint16_t flags;
	uint32_t acpi_uid;
	uint8_t LINTn;
	uint16_t reserved0;
	uint8_t reserved1;
} __attribute__((packed)) madt_lx2apic_nmi_t;

typedef struct {
	sdt_header_t hdr;
	uint32_t lapic_addr;
	uint32_t flags;
	uint8_t structures[];
} __attribute__((packed)) madt_t;

void madt_init();

#endif // ACPI_MADT_H
