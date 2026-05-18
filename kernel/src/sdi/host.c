#include <sdi/host.h>

#include <stdbool.h>
#include <stddef.h>

#include <cpu/idt.h>
#include <cpu/instr.h>
#include <cpu/smp.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <mm/kheap.h>
#include <sdi.h>
#include <sys/apic.h>
#include <sys/time.h>

#define KLOG_NS "sdi"
#include <log/klog.h>

#define SDI_ALLOC_SUPPORTED_FLAGS \
	(SDI_ALLOC_ZERO | SDI_ALLOC_ATOMIC | SDI_ALLOC_PAGE_ALIGNED)
#define SDI_ALLOC_MAGIC 0x534449414c4c4f43ull
#define SDI_IRQ_COUNT 16

typedef struct {
	uint64_t magic;
	void *raw;
	sdi_size_t size;
	sdi_flags_t flags;
} sdi_alloc_header_t;

typedef struct {
	bool used;
	uint8_t irq;
	sdi_irq_handler_fn handler;
	sdi_handle_t instance;
} sdi_irq_binding_t;

typedef struct {
	sdi_caps_t cap;
	const char *name;
} sdi_cap_name_t;

static const sdi_cap_name_t sdi_cap_names[] = {
	{ SDI_CAP_CORE, "core" },
	{ SDI_CAP_LOG, "log" },
	{ SDI_CAP_ALLOC, "alloc" },
	{ SDI_CAP_TIME, "time" },
	{ SDI_CAP_CALL, "call" },
	{ SDI_CAP_REGISTRY, "registry" },
	{ SDI_CAP_DEVICE, "device" },
	{ SDI_CAP_MMIO, "mmio" },
	{ SDI_CAP_PIO, "pio" },
	{ SDI_CAP_IRQ, "irq" },
	{ SDI_CAP_DMA, "dma" },
	{ SDI_CAP_CLASS, "class" },
	{ SDI_CAP_TIMER, "timer" },
	{ SDI_CAP_POWER, "power" },
	{ SDI_CAP_HOTPLUG, "hotplug" },
	{ SDI_CAP_BUS_PCI, "bus-pci" },
	{ SDI_CAP_BUS_USB, "bus-usb" },
	{ SDI_CAP_BUS_PLATFORM, "bus-platform" },
	{ SDI_CAP_BUS_ACPI, "bus-acpi" },
	{ SDI_CAP_BUS_DT, "bus-dt" },
	{ SDI_CAP_BUS_VIRTIO, "bus-virtio" },
	{ SDI_CAP_CLASS_CHAR, "class-char" },
	{ SDI_CAP_CLASS_BLOCK, "class-block" },
	{ SDI_CAP_CLASS_NET, "class-net" },
	{ SDI_CAP_CLASS_INPUT, "class-input" },
	{ SDI_CAP_CLASS_FB, "class-fb" },
	{ SDI_CAP_DMA_COHERENT, "dma-coherent" },
	{ SDI_CAP_IOMMU, "iommu" },
	{ SDI_CAP_DMA_64BIT, "dma-64bit" },
};

static sdi_irq_binding_t sdi_irq_bindings[SDI_IRQ_COUNT];
static sdi_caps_t sdi_kernel_caps;

static void sdi_append_text(char *buffer, size_t buffer_size, const char *text)
{
	size_t used;

	if (buffer_size == 0)
		return;

	used = strlen(buffer);
	if (used >= buffer_size)
		return;

	(void)strlcpy(buffer + used, text ? text : "", buffer_size - used);
}

static void sdi_format_caps(sdi_caps_t caps, char *buffer, size_t buffer_size)
{
	sdi_caps_t known = 0;
	sdi_caps_t unknown;
	bool any = false;

	if (buffer_size == 0)
		return;

	buffer[0] = '\0';
	for (size_t i = 0; i < SDI_ARRAY_COUNT(sdi_cap_names); i++) {
		known |= sdi_cap_names[i].cap;
		if ((caps & sdi_cap_names[i].cap) == 0)
			continue;

		if (any)
			sdi_append_text(buffer, buffer_size, ", ");
		sdi_append_text(buffer, buffer_size, sdi_cap_names[i].name);
		any = true;
	}

	unknown = caps & ~known;
	if (unknown != 0) {
		char unknown_text[32];

		if (any)
			sdi_append_text(buffer, buffer_size, ", ");
		ksnprintf(unknown_text, sizeof(unknown_text), "unknown(0x%llx)",
				  (unsigned long long)unknown);
		sdi_append_text(buffer, buffer_size, unknown_text);
		any = true;
	}

	if (!any)
		sdi_append_text(buffer, buffer_size, "none");
}

static void sdi_log_caps(const char *label, sdi_caps_t caps)
{
	char caps_text[256];

	sdi_format_caps(caps, caps_text, sizeof(caps_text));
	klog("  %-10s: %s (0x%llx)", label, caps_text, (unsigned long long)caps);
}

static uint64_t sdi_align_up(uint64_t value, uint64_t align)
{
	return (value + align - 1) & ~(align - 1);
}

static void sdi_kernel_log(void *user, sdi_u32 level, const char *component,
						   const char *message)
{
	const char *level_name;

	(void)user;

	switch (level) {
	case SDI_LOG_TRACE:
		level_name = "trace";
		break;
	case SDI_LOG_DEBUG:
		level_name = "debug";
		break;
	case SDI_LOG_INFO:
		level_name = "info";
		break;
	case SDI_LOG_WARN:
		level_name = "warn";
		break;
	case SDI_LOG_ERROR:
		level_name = "error";
		break;
	case SDI_LOG_FATAL:
		level_name = "fatal";
		break;
	default:
		level_name = "unknown";
		break;
	}

	klog("%s: %s: %s", level_name,
		 component && component[0] != '\0' ? component : "driver",
		 message ? message : "");
}

static void *sdi_kernel_alloc(void *user, sdi_size_t size, sdi_flags_t flags)
{
	sdi_alloc_header_t *header;
	uint8_t *raw;
	uint8_t *user_ptr;
	uint64_t align = (flags & SDI_ALLOC_PAGE_ALIGNED) ? PAGE_SIZE : 16u;
	sdi_size_t total;

	(void)user;

	if (size == 0 || (flags & ~SDI_ALLOC_SUPPORTED_FLAGS) != 0)
		return NULL;

	total = size + sizeof(*header) + align - 1;
	if (total < size)
		return NULL;

	raw = kmalloc(total);
	if (!raw)
		return NULL;

	user_ptr = (uint8_t *)(uintptr_t)sdi_align_up(
		(uint64_t)(uintptr_t)(raw + sizeof(*header)), align);
	header = (sdi_alloc_header_t *)(void *)(user_ptr - sizeof(*header));
	header->magic = SDI_ALLOC_MAGIC;
	header->raw = raw;
	header->size = size;
	header->flags = flags;

	if (flags & SDI_ALLOC_ZERO)
		memset(user_ptr, 0, size);

	return user_ptr;
}

static void sdi_kernel_free(void *user, void *ptr)
{
	sdi_alloc_header_t *header;

	(void)user;

	if (!ptr)
		return;

	header = (sdi_alloc_header_t *)((uint8_t *)ptr - sizeof(*header));
	if (header->magic == SDI_ALLOC_MAGIC) {
		header->magic = 0;
		kfree(header->raw);
		return;
	}

	kfree(ptr);
}

static sdi_u64 sdi_kernel_now_ns(void *user)
{
	(void)user;

	return time_uptime_us() * 1000u;
}

static sdi_status_t sdi_check_payload(const void *payload, sdi_size_t size,
									  sdi_size_t required)
{
	const sdi_abi_header_t *header = payload;

	if (!payload || size < required)
		return SDI_ERR_SHORT;
	if (header->size < required)
		return SDI_ERR_SHORT;
	if ((header->version >> 24) != SDI_VERSION_MAJOR)
		return SDI_ERR_UNSUPPORTED;

	return SDI_OK;
}

static interrupt_frame_t *sdi_irq_dispatch(interrupt_frame_t *frame)
{
	uint8_t vector = frame ? (uint8_t)frame->vector : 0;
	uint8_t irq = vector >= IRQ_BASE ? (uint8_t)(vector - IRQ_BASE) : 0xff;

	if (irq < SDI_IRQ_COUNT && sdi_irq_bindings[irq].used &&
		sdi_irq_bindings[irq].handler) {
		(void)sdi_irq_bindings[irq].handler(sdi_irq_bindings[irq].instance,
											(sdi_irq_t)(irq + 1u));
	}

	apic_send_eoi();
	return frame;
}

static sdi_status_t sdi_host_irq_bind(const void *in_data, sdi_size_t in_size,
									  void *out_data,
									  sdi_size_t *inout_out_size)
{
	const sdi_irq_bind_req_t *req = in_data;
	sdi_irq_bind_resp_t *resp = out_data;
	struct cpu_info *cpu;
	uint8_t irq;

	if (sdi_check_payload(in_data, in_size, sizeof(*req)) != SDI_OK)
		return SDI_ERR_INVAL;
	if (!resp || !inout_out_size || *inout_out_size < sizeof(*resp))
		return SDI_ERR_SHORT;
	if (!req->handler)
		return SDI_ERR_INVAL;
	if (req->resource_index >= SDI_IRQ_COUNT)
		return SDI_ERR_RANGE;

	irq = (uint8_t)req->resource_index;
	if (sdi_irq_bindings[irq].used)
		return SDI_ERR_BUSY;

	sdi_irq_bindings[irq].used = true;
	sdi_irq_bindings[irq].irq = irq;
	sdi_irq_bindings[irq].handler = req->handler;
	sdi_irq_bindings[irq].instance = req->instance;

	cpu = cpu_current();
	irq_install(irq, sdi_irq_dispatch, NULL, cpu ? (uint8_t)cpu->lapic_id : 0);
	ioapic_set_irq_mask(irq, 0);

	memset(resp, 0, sizeof(*resp));
	SDI_INIT_PTR(resp);
	resp->irq = (sdi_irq_t)(irq + 1u);
	*inout_out_size = sizeof(*resp);
	ktrace("bound SDI IRQ%u handle=%llu", irq, (unsigned long long)resp->irq);
	return SDI_OK;
}

static sdi_status_t sdi_irq_from_handle(sdi_handle_t target, uint8_t *out_irq)
{
	if (target == SDI_NULL_HANDLE || target > SDI_IRQ_COUNT)
		return SDI_ERR_NOT_FOUND;
	if (!sdi_irq_bindings[target - 1].used)
		return SDI_ERR_NOT_FOUND;

	*out_irq = (uint8_t)(target - 1);
	return SDI_OK;
}

static sdi_status_t sdi_host_irq_unbind(sdi_handle_t target)
{
	uint8_t irq;
	sdi_status_t st = sdi_irq_from_handle(target, &irq);

	if (st != SDI_OK)
		return st;

	ioapic_set_irq_mask(irq, 1);
	irq_uninstall(irq);
	memset(&sdi_irq_bindings[irq], 0, sizeof(sdi_irq_bindings[irq]));
	return SDI_OK;
}

static sdi_status_t sdi_host_irq_mask(sdi_handle_t target, bool masked)
{
	uint8_t irq;
	sdi_status_t st = sdi_irq_from_handle(target, &irq);

	if (st != SDI_OK)
		return st;

	return ioapic_set_irq_mask(irq, masked ? 1 : 0) == 0 ? SDI_OK :
														   SDI_ERR_NOT_FOUND;
}

static sdi_status_t sdi_host_pio_read(const void *in_data, sdi_size_t in_size,
									  void *out_data,
									  sdi_size_t *inout_out_size)
{
	const sdi_pio_io_t *req = in_data;
	sdi_pio_io_t *resp = out_data;

	if (sdi_check_payload(in_data, in_size, sizeof(*req)) != SDI_OK)
		return SDI_ERR_INVAL;
	if (!resp || !inout_out_size || *inout_out_size < sizeof(*resp))
		return SDI_ERR_SHORT;

	*resp = *req;
	switch (req->width) {
	case 8:
		resp->value = inb(req->port);
		break;
	case 16:
		resp->value = inw(req->port);
		break;
	case 32:
		resp->value = inl(req->port);
		break;
	default:
		return SDI_ERR_INVAL;
	}

	*inout_out_size = sizeof(*resp);
	return SDI_OK;
}

static sdi_status_t sdi_host_pio_write(const void *in_data, sdi_size_t in_size)
{
	const sdi_pio_io_t *req = in_data;

	if (sdi_check_payload(in_data, in_size, sizeof(*req)) != SDI_OK)
		return SDI_ERR_INVAL;

	switch (req->width) {
	case 8:
		outb(req->port, (uint8_t)req->value);
		return SDI_OK;
	case 16:
		outw(req->port, (uint16_t)req->value);
		return SDI_OK;
	case 32:
		outl(req->port, req->value);
		return SDI_OK;
	default:
		return SDI_ERR_INVAL;
	}
}

static sdi_status_t sdi_kernel_call(void *user, sdi_handle_t target,
									sdi_u32 opcode, const void *in_data,
									sdi_size_t in_size, void *out_data,
									sdi_size_t *inout_out_size)
{
	(void)user;

	if (opcode == SDI_OP_NOP) {
		if (inout_out_size)
			*inout_out_size = 0;
		return SDI_OK;
	}

	switch (opcode) {
	case SDI_OP_PIO_READ:
		return sdi_host_pio_read(in_data, in_size, out_data, inout_out_size);
	case SDI_OP_PIO_WRITE:
		return sdi_host_pio_write(in_data, in_size);
	case SDI_OP_IRQ_BIND:
		return sdi_host_irq_bind(in_data, in_size, out_data, inout_out_size);
	case SDI_OP_IRQ_UNBIND:
		return sdi_host_irq_unbind(target);
	case SDI_OP_IRQ_ACK: {
		uint8_t irq;
		sdi_status_t st = sdi_irq_from_handle(target, &irq);

		if (st != SDI_OK)
			return st;
		(void)irq;
	}
		apic_send_eoi();
		return SDI_OK;
	case SDI_OP_IRQ_MASK:
		return sdi_host_irq_mask(target, true);
	case SDI_OP_IRQ_UNMASK:
		return sdi_host_irq_mask(target, false);
	default:
		break;
	}

	return SDI_ERR_UNSUPPORTED;
}

int sdi_kernel_init(void)
{
	sdi_host_ops_t host;
	sdi_status_t st;

	memset(&host, 0, sizeof(host));
	SDI_INIT_PTR(&host);
	host.host_name = "alix";
	host.caps = SDI_CAP_REQUIRED_CORE | SDI_CAP_IRQ | SDI_CAP_PIO;
	sdi_kernel_caps = host.caps;
	host.max_drivers = SDI_MAX_REGISTERED_DRIVERS;
	host.log = sdi_kernel_log;
	host.alloc = sdi_kernel_alloc;
	host.free = sdi_kernel_free;
	host.now_ns = sdi_kernel_now_ns;
	host.call = sdi_kernel_call;

	st = sdi_init(&host, NULL);
	if (st != SDI_OK) {
		klog("failed to initialize SDI core: %d", st);
		return -1;
	}

	klog("initialized SDI v%u.%u.%u caps=0x%llx",
		 (unsigned int)SDI_VERSION_MAJOR, (unsigned int)SDI_VERSION_MINOR,
		 (unsigned int)SDI_VERSION_PATCH, (unsigned long long)host.caps);
	return 0;
}

static const sdi_match_id_t *
sdi_find_soft_match(const sdi_driver_desc_t *driver)
{
	if (!driver || !driver->matches)
		return NULL;

	for (sdi_u32 i = 0; i < driver->match_count; i++) {
		if (driver->matches[i].kind == SDI_MATCH_SOFT)
			return &driver->matches[i];
	}

	return NULL;
}

int sdi_kernel_register_loaded_driver(const sdi_driver_desc_t *driver,
									  const char *path)
{
	sdi_driver_handle_t handle = SDI_NULL_HANDLE;
	sdi_probe_info_t probe;
	sdi_probe_result_t result;
	sdi_attach_args_t attach;
	sdi_handle_t instance = SDI_NULL_HANDLE;
	const sdi_match_id_t *match;
	sdi_caps_t enabled_caps;
	sdi_caps_t missing_caps;
	sdi_status_t st;

	memset(&probe, 0, sizeof(probe));
	memset(&result, 0, sizeof(result));
	memset(&attach, 0, sizeof(attach));
	SDI_INIT_PTR(&probe);
	SDI_INIT_PTR(&result);
	SDI_INIT_PTR(&attach);

	if (!driver) {
		klog("loaded driver '%s' did not export sdi_driver",
			 path ? path : "<unknown>");
		return -1;
	}

	enabled_caps =
		(driver->required_caps | driver->optional_caps) & sdi_kernel_caps;
	missing_caps = driver->required_caps & ~sdi_kernel_caps;
	klog("found SDI driver:");
	klog("  name        : %s", driver->name ? driver->name : "<unnamed>");
	klog("  vendor      : %s", driver->vendor ? driver->vendor : "<unknown>");
	klog("  description : %s",
		 driver->description ? driver->description : "<none>");
	klog("  path        : %s", path ? path : "<memory>");
	sdi_log_caps("required", driver->required_caps);
	sdi_log_caps("optional", driver->optional_caps);
	sdi_log_caps("enabled", enabled_caps);
	sdi_log_caps("missing", missing_caps);

	st = sdi_register_driver(driver, &handle);
	if (st != SDI_OK) {
		klog("failed to register SDI driver '%s' from '%s': %d",
			 driver->name ? driver->name : "<unnamed>",
			 path ? path : "<memory>", st);
		return -1;
	}

	ktrace("registered SDI driver '%s'", driver->name);

	match = sdi_find_soft_match(driver);
	if (match) {
		probe.match = *match;
	} else {
		memset(&probe.match, 0, sizeof(probe.match));
		SDI_INIT_PTR(&probe.match);
	}
	probe.match.kind = match ? match->kind : SDI_MATCH_SOFT;

	st = sdi_probe_driver(handle, &probe, &result);
	if (st == SDI_ERR_UNSUPPORTED) {
		ktrace("driver '%s' has no probe hook; registered only", driver->name);
		return 0;
	}
	if (st != SDI_OK) {
		klog("probe failed for SDI driver '%s': %d", driver->name, st);
		return -1;
	}
	if (result.score == 0) {
		ktrace("driver '%s' did not match the soft SDI test device",
			   driver->name);
		return 0;
	}

	attach.match = match;
	st = sdi_attach_driver(handle, &attach, &instance);
	if (st == SDI_ERR_UNSUPPORTED) {
		ktrace("driver '%s' matched but has no attach hook", driver->name);
		return 0;
	}
	if (st != SDI_OK) {
		klog("attach failed for SDI driver '%s': %d", driver->name, st);
		return -1;
	}

	st = sdi_start_driver(handle, instance);
	if (st == SDI_ERR_UNSUPPORTED)
		return 0;
	if (st != SDI_OK) {
		klog("start failed for SDI driver '%s': %d", driver->name, st);
		return -1;
	}

	ktrace("started SDI driver '%s' instance=%llu", driver->name,
		   (unsigned long long)instance);
	return 0;
}
