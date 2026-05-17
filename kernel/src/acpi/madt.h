#ifndef ACPI_MADT_H
#define ACPI_MADT_H

#include <sys/acpi.h>

typedef struct madt_header {
	sdt_header_t h;
	uint32_t lapic_addr;
	uint32_t flags;
} __attribute__((packed)) madt_header_t;

void madt_init();

#endif // ACPI_MADT_H