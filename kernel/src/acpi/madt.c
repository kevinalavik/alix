#include <acpi/madt.h>
#define KLOG_NS "madt"
#include <log/klog.h>
#include <debug/panic.h>

void madt_init()
{
	sdt_header_t *h = acpi_get_table("APIC");
	if (h == NULL) {
		kpanic(NULL, "MADT not found, cant continue without APIC support");
		return;
	}

	madt_header_t *madt = (madt_header_t *)h;
	klog("MADT found: LAPIC at %p, flags=%u", madt->lapic_addr, madt->flags);
}