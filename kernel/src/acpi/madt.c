#include <acpi/madt.h>
#define KLOG_NS "madt"
#include <log/klog.h>

void madt_init()
{
	sdt_header_t *h = acpi_get_table("APIC");
	if (h == NULL) {
		klog("MADT/APIC table not found");
		return;
	}

	madt_header_t *madt = (madt_header_t *)h;
	klog("MADT found: LAPIC at %p, flags=%u", madt->lapic_addr, madt->flags);
}