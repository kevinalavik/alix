#ifndef SYS_ACPI_H
#define SYS_ACPI_H

#include <stdint.h>
#include <limine.h>

#define RSDP_SIG "RSD PTR "

typedef struct rsdp {
	char sig[8];
	uint8_t check;
	char oemid[6];
	uint8_t revision;
	uint32_t rsdt_addr;
} __attribute__((packed)) rsdp_t;

typedef struct xsdp {
	rsdp_t _0;
	uint32_t length;
	uint64_t xsdt_addr;
	uint8_t ext_check;
	uint8_t reserved[3];
} __attribute__((packed)) xsdp_t;

typedef struct sdt_header {
	char sig[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char oemid[6];
	char oem_table_id[8];
	uint32_t oem_revision;
	uint32_t creator_id;
	uint32_t creator_revision;
} __attribute__((packed)) sdt_header_t;

void acpi_init(struct limine_rsdp_response *rsdp_response);
sdt_header_t *acpi_get_table(const char *sig);

#endif // SYS_ACPI_H
