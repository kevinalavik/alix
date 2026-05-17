#include <sys/acpi.h>
#include <mm/mm.h>
#include <lib/string.h>
#define KLOG_NS "acpi"
#include <log/klog.h>
#include <stdbool.h>

static bool xsdt = false;
static sdt_header_t *root_sdt = NULL;

static sdt_header_t *acpi_map_sdt(uintptr_t phys)
{
	if (phys == 0)
		return NULL;

	return (sdt_header_t *)PHYS_TO_VIRT(phys);
}

sdt_header_t *acpi_get_table(const char *sig)
{
	if (root_sdt == NULL)
		return NULL;

	size_t entry_size = xsdt ? sizeof(uint64_t) : sizeof(uint32_t);
	if (root_sdt->length < sizeof(sdt_header_t)) {
		klog("ACPI root table length invalid: %u", root_sdt->length);
		return NULL;
	}

	size_t entry_count = (root_sdt->length - sizeof(sdt_header_t)) / entry_size;
	uint8_t *entry_base = (uint8_t *)root_sdt + sizeof(sdt_header_t);

	for (size_t i = 0; i < entry_count; i++) {
		uintptr_t phys = xsdt ? (uintptr_t)((uint64_t *)entry_base)[i] :
								(uintptr_t)((uint32_t *)entry_base)[i];
		sdt_header_t *h = acpi_map_sdt(phys);

		if (h == NULL) {
			continue;
		}

		if (memcmp(h->sig, sig, 4) == 0)
			return h;
	}

	return NULL;
}

void acpi_init(struct limine_rsdp_response *rsdp_response)
{
	if (!rsdp_response || !rsdp_response->address)
		return;

	rsdp_t *rsdp = (rsdp_t *)(uintptr_t)rsdp_response->address;
	if (memcmp(rsdp->sig, RSDP_SIG, 8) != 0) {
		klog("RSDP signature invalid");
		return;
	}

	if (rsdp->revision >= 2) {
		xsdt = true;
		root_sdt = acpi_map_sdt((uintptr_t)((xsdp_t *)rsdp)->xsdt_addr);
		klog("XSDT found at %p", root_sdt);
	} else {
		root_sdt = acpi_map_sdt((uintptr_t)rsdp->rsdt_addr);
		klog("RSDT found at %p", root_sdt);
	}

	if (root_sdt == NULL) {
		klog("ACPI root table missing");
		return;
	}

	klog("ACPI tables:");
	size_t entry_size = xsdt ? sizeof(uint64_t) : sizeof(uint32_t);
	if (root_sdt->length < sizeof(sdt_header_t)) {
		klog("ACPI root table length invalid: %u", root_sdt->length);
		return;
	}

	size_t entry_count = (root_sdt->length - sizeof(sdt_header_t)) / entry_size;
	uint8_t *entry_base = (uint8_t *)root_sdt + sizeof(sdt_header_t);

	for (size_t i = 0; i < entry_count; i++) {
		uintptr_t phys = xsdt ? (uintptr_t)((uint64_t *)entry_base)[i] :
								(uintptr_t)((uint32_t *)entry_base)[i];
		sdt_header_t *h = acpi_map_sdt(phys);

		if (h == NULL) {
			klog("- null table pointer");
			continue;
		}

		klog("- %.*s at %p", 4, h->sig, h);
	}
}
