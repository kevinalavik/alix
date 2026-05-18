/*
 * SDI - Simple Driver Interface
 * Built-in SDI core implementation.
 *
 * The kernel compiles this file into the kernel or SDI module. Before any
 * driver is registered, the kernel calls sdi_init() with a filled
 * sdi_host_ops_t table. SDI then provides built-in registry, logging,
 * allocation, time, dispatch, and lifecycle helper functions.
 */

#include "sdi.h"

#ifndef SDI_REGISTRY_CAPACITY
#define SDI_REGISTRY_CAPACITY SDI_MAX_REGISTERED_DRIVERS
#endif

#if SDI_REGISTRY_CAPACITY > SDI_MAX_REGISTERED_DRIVERS
#error "SDI_REGISTRY_CAPACITY cannot exceed SDI_MAX_REGISTERED_DRIVERS"
#endif

typedef struct sdi_registry_entry {
	sdi_driver_handle_t handle;
	const sdi_driver_desc_t *driver;
	int used;
} sdi_registry_entry_t;

static sdi_host_ops_t g_sdi_host;
static sdi_registry_entry_t g_sdi_registry[SDI_REGISTRY_CAPACITY];
static sdi_u32 g_sdi_max_drivers;
static sdi_u32 g_sdi_driver_count;
static sdi_driver_handle_t g_sdi_next_handle = 1u;
static int g_sdi_initialized = 0;

static sdi_size_t sdi_strlen_(const char *s)
{
	sdi_size_t n = 0u;
	if (s == 0) {
		return 0u;
	}
	while (s[n] != '\0') {
		++n;
	}
	return n;
}

static int sdi_streq_(const char *a, const char *b)
{
	sdi_size_t i = 0u;
	if (a == 0 || b == 0) {
		return 0;
	}
	for (;;) {
		if (a[i] != b[i]) {
			return 0;
		}
		if (a[i] == '\0') {
			return 1;
		}
		++i;
	}
}

static void sdi_copy_str_(char *dst, sdi_size_t cap, const char *src)
{
	sdi_size_t i;
	if (dst == 0 || cap == 0u) {
		return;
	}
	if (src == 0) {
		dst[0] = '\0';
		return;
	}
	for (i = 0u; i + 1u < cap && src[i] != '\0'; ++i) {
		dst[i] = src[i];
	}
	dst[i] = '\0';
}

static sdi_status_t sdi_check_abi_(sdi_u32 size, sdi_u32 min_size,
								   sdi_u32 version)
{
	if (size < min_size) {
		return SDI_ERR_SHORT;
	}
	if (((version >> 24) & 0xffu) != SDI_VERSION_MAJOR) {
		return SDI_ERR_UNSUPPORTED;
	}
	return SDI_OK;
}

static sdi_registry_entry_t *sdi_entry_from_handle_(sdi_driver_handle_t handle)
{
	sdi_u32 i;
	if (handle == SDI_NULL_HANDLE) {
		return 0;
	}
	for (i = 0u; i < g_sdi_max_drivers; ++i) {
		if (g_sdi_registry[i].used && g_sdi_registry[i].handle == handle) {
			return &g_sdi_registry[i];
		}
	}
	return 0;
}

static sdi_status_t sdi_require_init_(void)
{
	return g_sdi_initialized ? SDI_OK : SDI_ERR_NOT_READY;
}

sdi_status_t sdi_init(const sdi_host_ops_t *host,
					  const sdi_init_options_t *options)
{
	sdi_u32 i;
	sdi_u32 requested;

	if (host == 0) {
		return SDI_ERR_INVAL;
	}
	if (sdi_check_abi_(host->size, (sdi_u32)sizeof(sdi_host_ops_t),
					   host->version) != SDI_OK) {
		return SDI_ERR_INVAL;
	}
	if (host->log == 0 || host->alloc == 0 || host->free == 0 ||
		host->now_ns == 0 || host->call == 0) {
		return SDI_ERR_INVAL;
	}
	if ((host->caps & SDI_CAP_REQUIRED_CORE) != SDI_CAP_REQUIRED_CORE) {
		return SDI_ERR_UNSUPPORTED;
	}

	g_sdi_host = *host;
	requested = host->max_drivers;
	if (options != 0 && options->max_drivers != 0u) {
		requested = options->max_drivers;
	}
	if (requested == 0u || requested > SDI_REGISTRY_CAPACITY) {
		requested = SDI_REGISTRY_CAPACITY;
	}

	g_sdi_max_drivers = requested;
	g_sdi_driver_count = 0u;
	g_sdi_next_handle = 1u;
	for (i = 0u; i < SDI_REGISTRY_CAPACITY; ++i) {
		g_sdi_registry[i].used = 0;
		g_sdi_registry[i].handle = SDI_NULL_HANDLE;
		g_sdi_registry[i].driver = 0;
	}
	g_sdi_initialized = 1;
	return SDI_OK;
}

sdi_status_t sdi_shutdown(void)
{
	sdi_u32 i;
	if (!g_sdi_initialized) {
		return SDI_OK;
	}
	for (i = 0u; i < g_sdi_max_drivers; ++i) {
		if (g_sdi_registry[i].used && g_sdi_registry[i].driver != 0 &&
			g_sdi_registry[i].driver->ops != 0 &&
			g_sdi_registry[i].driver->ops->fini != 0) {
			(void)g_sdi_registry[i].driver->ops->fini();
		}
		g_sdi_registry[i].used = 0;
		g_sdi_registry[i].handle = SDI_NULL_HANDLE;
		g_sdi_registry[i].driver = 0;
	}
	g_sdi_initialized = 0;
	g_sdi_driver_count = 0u;
	g_sdi_max_drivers = 0u;
	return SDI_OK;
}

int sdi_is_initialized(void)
{
	return g_sdi_initialized;
}

sdi_status_t sdi_get_info(sdi_info_t *out_info)
{
	if (out_info == 0) {
		return SDI_ERR_INVAL;
	}
	SDI_INIT_PTR(out_info);
	out_info->abi_major = SDI_VERSION_MAJOR;
	out_info->abi_minor = SDI_VERSION_MINOR;
	out_info->abi_patch = SDI_VERSION_PATCH;
	out_info->pointer_bits = (sdi_u32)(sizeof(void *) * 8u);
	out_info->caps =
		g_sdi_initialized ? g_sdi_host.caps : SDI_CAP_REQUIRED_CORE;
	out_info->max_drivers = g_sdi_max_drivers;
	out_info->driver_count = g_sdi_driver_count;
	sdi_copy_str_(out_info->host_name, SDI_MAX_HOST_NAME,
				  g_sdi_initialized ? g_sdi_host.host_name : "SDI");
	return SDI_OK;
}

sdi_status_t sdi_register_driver(const sdi_driver_desc_t *driver,
								 sdi_driver_handle_t *out_handle)
{
	sdi_u32 i;
	sdi_status_t st;
	if (sdi_require_init_() != SDI_OK) {
		return SDI_ERR_NOT_READY;
	}
	if (driver == 0 || out_handle == 0 || driver->name == 0 ||
		driver->ops == 0) {
		return SDI_ERR_INVAL;
	}
	if (sdi_check_abi_(driver->size, (sdi_u32)sizeof(sdi_driver_desc_t),
					   driver->version) != SDI_OK) {
		return SDI_ERR_INVAL;
	}
	if (sdi_check_abi_(driver->ops->size, (sdi_u32)sizeof(sdi_driver_ops_t),
					   driver->ops->version) != SDI_OK) {
		return SDI_ERR_INVAL;
	}
	if (driver->abi_major != SDI_VERSION_MAJOR) {
		return SDI_ERR_UNSUPPORTED;
	}
	if ((driver->required_caps & ~g_sdi_host.caps) != 0u) {
		return SDI_ERR_UNSUPPORTED;
	}
	if (sdi_strlen_(driver->name) == 0u) {
		return SDI_ERR_INVAL;
	}
	for (i = 0u; i < g_sdi_max_drivers; ++i) {
		if (g_sdi_registry[i].used &&
			sdi_streq_(g_sdi_registry[i].driver->name, driver->name)) {
			return SDI_ERR_EXISTS;
		}
	}
	for (i = 0u; i < g_sdi_max_drivers; ++i) {
		if (!g_sdi_registry[i].used) {
			if (driver->ops->init != 0) {
				st = driver->ops->init();
				if (st != SDI_OK) {
					return st;
				}
			}
			g_sdi_registry[i].used = 1;
			g_sdi_registry[i].driver = driver;
			g_sdi_registry[i].handle = g_sdi_next_handle++;
			if (g_sdi_next_handle == SDI_NULL_HANDLE) {
				g_sdi_next_handle = 1u;
			}
			++g_sdi_driver_count;
			*out_handle = g_sdi_registry[i].handle;
			return SDI_OK;
		}
	}
	return SDI_ERR_NOMEM;
}

sdi_status_t sdi_unregister_driver(sdi_driver_handle_t handle)
{
	sdi_registry_entry_t *entry;
	if (sdi_require_init_() != SDI_OK) {
		return SDI_ERR_NOT_READY;
	}
	entry = sdi_entry_from_handle_(handle);
	if (entry == 0) {
		return SDI_ERR_NOT_FOUND;
	}
	if (entry->driver != 0 && entry->driver->ops != 0 &&
		entry->driver->ops->fini != 0) {
		sdi_status_t st = entry->driver->ops->fini();
		if (st != SDI_OK) {
			return st;
		}
	}
	entry->used = 0;
	entry->handle = SDI_NULL_HANDLE;
	entry->driver = 0;
	--g_sdi_driver_count;
	return SDI_OK;
}

sdi_status_t sdi_find_driver(const char *name, sdi_driver_handle_t *out_handle)
{
	sdi_u32 i;
	if (sdi_require_init_() != SDI_OK) {
		return SDI_ERR_NOT_READY;
	}
	if (name == 0 || out_handle == 0) {
		return SDI_ERR_INVAL;
	}
	for (i = 0u; i < g_sdi_max_drivers; ++i) {
		if (g_sdi_registry[i].used &&
			sdi_streq_(g_sdi_registry[i].driver->name, name)) {
			*out_handle = g_sdi_registry[i].handle;
			return SDI_OK;
		}
	}
	return SDI_ERR_NOT_FOUND;
}

sdi_status_t sdi_get_driver_count(sdi_u32 *out_count)
{
	if (out_count == 0) {
		return SDI_ERR_INVAL;
	}
	*out_count = g_sdi_driver_count;
	return SDI_OK;
}

sdi_status_t sdi_get_driver_at(sdi_u32 index,
							   const sdi_driver_desc_t **out_driver,
							   sdi_driver_handle_t *out_handle)
{
	sdi_u32 i, seen = 0u;
	if (sdi_require_init_() != SDI_OK) {
		return SDI_ERR_NOT_READY;
	}
	if (out_driver == 0 || out_handle == 0) {
		return SDI_ERR_INVAL;
	}
	for (i = 0u; i < g_sdi_max_drivers; ++i) {
		if (g_sdi_registry[i].used) {
			if (seen == index) {
				*out_driver = g_sdi_registry[i].driver;
				*out_handle = g_sdi_registry[i].handle;
				return SDI_OK;
			}
			++seen;
		}
	}
	return SDI_ERR_NOT_FOUND;
}

sdi_status_t sdi_probe_driver(sdi_driver_handle_t handle,
							  const sdi_probe_info_t *info,
							  sdi_probe_result_t *out_result)
{
	sdi_registry_entry_t *entry = sdi_entry_from_handle_(handle);
	if (entry == 0 || entry->driver == 0 || entry->driver->ops == 0) {
		return SDI_ERR_NOT_FOUND;
	}
	if (entry->driver->ops->probe == 0) {
		return SDI_ERR_UNSUPPORTED;
	}
	if (info == 0 || out_result == 0) {
		return SDI_ERR_INVAL;
	}
	SDI_INIT_PTR(out_result);
	out_result->score = 0u;
	return entry->driver->ops->probe(info, out_result);
}

sdi_status_t sdi_attach_driver(sdi_driver_handle_t handle,
							   const sdi_attach_args_t *args,
							   sdi_handle_t *out_instance)
{
	sdi_registry_entry_t *entry = sdi_entry_from_handle_(handle);
	if (entry == 0 || entry->driver == 0 || entry->driver->ops == 0) {
		return SDI_ERR_NOT_FOUND;
	}
	if (entry->driver->ops->attach == 0) {
		return SDI_ERR_UNSUPPORTED;
	}
	if (args == 0 || out_instance == 0) {
		return SDI_ERR_INVAL;
	}
	return entry->driver->ops->attach(args, out_instance);
}

#define SDI_LIFECYCLE_CALL(fn_, unsupported_ok_)                           \
	do {                                                                   \
		sdi_registry_entry_t *entry = sdi_entry_from_handle_(handle);      \
		if (entry == 0 || entry->driver == 0 || entry->driver->ops == 0) { \
			return SDI_ERR_NOT_FOUND;                                      \
		}                                                                  \
		if (entry->driver->ops->fn_ == 0) {                                \
			return (unsupported_ok_) ? SDI_OK : SDI_ERR_UNSUPPORTED;       \
		}                                                                  \
		return entry->driver->ops->fn_(instance);                          \
	} while (0)

sdi_status_t sdi_start_driver(sdi_driver_handle_t handle, sdi_handle_t instance)
{
	SDI_LIFECYCLE_CALL(start, 0);
}
sdi_status_t sdi_stop_driver(sdi_driver_handle_t handle, sdi_handle_t instance)
{
	SDI_LIFECYCLE_CALL(stop, 1);
}
sdi_status_t sdi_resume_driver(sdi_driver_handle_t handle,
							   sdi_handle_t instance)
{
	SDI_LIFECYCLE_CALL(resume, 1);
}
sdi_status_t sdi_detach_driver(sdi_driver_handle_t handle,
							   sdi_handle_t instance)
{
	SDI_LIFECYCLE_CALL(detach, 1);
}
sdi_status_t sdi_remove_driver(sdi_driver_handle_t handle,
							   sdi_handle_t instance)
{
	SDI_LIFECYCLE_CALL(remove, 1);
}

sdi_status_t sdi_suspend_driver(sdi_driver_handle_t handle,
								sdi_handle_t instance, sdi_u32 state)
{
	sdi_registry_entry_t *entry = sdi_entry_from_handle_(handle);
	if (entry == 0 || entry->driver == 0 || entry->driver->ops == 0) {
		return SDI_ERR_NOT_FOUND;
	}
	if (entry->driver->ops->suspend == 0) {
		return SDI_OK;
	}
	return entry->driver->ops->suspend(instance, state);
}

sdi_status_t sdi_log(sdi_u32 level, const char *component, const char *message)
{
	if (sdi_require_init_() != SDI_OK) {
		return SDI_ERR_NOT_READY;
	}
	if (message == 0) {
		return SDI_ERR_INVAL;
	}
	g_sdi_host.log(g_sdi_host.user, level, component, message);
	return SDI_OK;
}

void *sdi_alloc(sdi_size_t size, sdi_flags_t flags)
{
	if (!g_sdi_initialized || size == 0u) {
		return 0;
	}
	return g_sdi_host.alloc(g_sdi_host.user, size, flags);
}

void sdi_free(void *ptr)
{
	if (g_sdi_initialized && ptr != 0) {
		g_sdi_host.free(g_sdi_host.user, ptr);
	}
}

sdi_u64 sdi_now_ns(void)
{
	if (!g_sdi_initialized) {
		return 0u;
	}
	return g_sdi_host.now_ns(g_sdi_host.user);
}

sdi_status_t sdi_call(sdi_handle_t target, sdi_u32 opcode, const void *in_data,
					  sdi_size_t in_size, void *out_data,
					  sdi_size_t *inout_out_size)
{
	if (sdi_require_init_() != SDI_OK) {
		return SDI_ERR_NOT_READY;
	}
	return g_sdi_host.call(g_sdi_host.user, target, opcode, in_data, in_size,
						   out_data, inout_out_size);
}

sdi_status_t sdi_get_resource(sdi_device_t device, sdi_u32 index, sdi_u32 type,
							  sdi_resource_t *out_resource)
{
	sdi_get_resource_req_t req;
	SDI_INIT_PTR(&req);
	sdi_size_t out_size = sizeof(*out_resource);
	if (out_resource == 0) {
		return SDI_ERR_INVAL;
	}
	req.index = index;
	req.type = type;
	return sdi_call(device, SDI_OP_GET_RESOURCE, &req, sizeof(req),
					out_resource, &out_size);
}

sdi_status_t sdi_mmio_map(sdi_device_t device, sdi_u32 resource_index,
						  sdi_flags_t flags, sdi_mmio_t *out_mmio,
						  sdi_u64 *out_length)
{
	sdi_mmio_map_req_t req;
	SDI_INIT_PTR(&req);
	sdi_mmio_map_resp_t resp;
	SDI_INIT_PTR(&resp);
	sdi_size_t out_size = sizeof(resp);
	sdi_status_t st;
	if (out_mmio == 0) {
		return SDI_ERR_INVAL;
	}
	req.resource_index = resource_index;
	req.flags = flags;
	st = sdi_call(device, SDI_OP_MMIO_MAP, &req, sizeof(req), &resp, &out_size);
	if (st == SDI_OK) {
		*out_mmio = resp.mmio;
		if (out_length != 0) {
			*out_length = resp.length;
		}
	}
	return st;
}

sdi_status_t sdi_mmio_unmap(sdi_mmio_t mmio)
{
	return sdi_call(mmio, SDI_OP_MMIO_UNMAP, 0, 0u, 0, 0);
}

sdi_status_t sdi_mmio_read(sdi_mmio_t mmio, sdi_u64 offset, sdi_u32 width,
						   sdi_u64 *out_value)
{
	sdi_mmio_io_t io;
	SDI_INIT_PTR(&io);
	sdi_size_t out_size = sizeof(io);
	sdi_status_t st;
	if (out_value == 0) {
		return SDI_ERR_INVAL;
	}
	io.offset = offset;
	io.width = width;
	st = sdi_call(mmio, SDI_OP_MMIO_READ, &io, sizeof(io), &io, &out_size);
	if (st == SDI_OK) {
		*out_value = io.value;
	}
	return st;
}

sdi_status_t sdi_mmio_write(sdi_mmio_t mmio, sdi_u64 offset, sdi_u32 width,
							sdi_u64 value)
{
	sdi_mmio_io_t io;
	SDI_INIT_PTR(&io);
	io.offset = offset;
	io.width = width;
	io.value = value;
	return sdi_call(mmio, SDI_OP_MMIO_WRITE, &io, sizeof(io), 0, 0);
}

sdi_status_t sdi_mmio_barrier(sdi_mmio_t mmio)
{
	return sdi_call(mmio, SDI_OP_MMIO_BARRIER, 0, 0u, 0, 0);
}

sdi_status_t sdi_pio_read(sdi_u16 port, sdi_u8 width, sdi_u32 *out_value)
{
	sdi_pio_io_t io;
	SDI_INIT_PTR(&io);
	sdi_size_t out_size = sizeof(io);
	sdi_status_t st;
	if (out_value == 0) {
		return SDI_ERR_INVAL;
	}
	io.port = port;
	io.width = width;
	st = sdi_call(SDI_NULL_HANDLE, SDI_OP_PIO_READ, &io, sizeof(io), &io,
				  &out_size);
	if (st == SDI_OK) {
		*out_value = io.value;
	}
	return st;
}

sdi_status_t sdi_pio_write(sdi_u16 port, sdi_u8 width, sdi_u32 value)
{
	sdi_pio_io_t io;
	SDI_INIT_PTR(&io);
	io.port = port;
	io.width = width;
	io.value = value;
	return sdi_call(SDI_NULL_HANDLE, SDI_OP_PIO_WRITE, &io, sizeof(io), 0, 0);
}

sdi_status_t sdi_irq_bind(sdi_device_t device, sdi_u32 resource_index,
						  sdi_flags_t flags, sdi_irq_handler_fn handler,
						  sdi_handle_t instance, sdi_irq_t *out_irq)
{
	sdi_irq_bind_req_t req;
	SDI_INIT_PTR(&req);
	sdi_irq_bind_resp_t resp;
	SDI_INIT_PTR(&resp);
	sdi_size_t out_size = sizeof(resp);
	sdi_status_t st;
	if (handler == 0 || out_irq == 0) {
		return SDI_ERR_INVAL;
	}
	req.resource_index = resource_index;
	req.flags = flags;
	req.handler = handler;
	req.instance = instance;
	st = sdi_call(device, SDI_OP_IRQ_BIND, &req, sizeof(req), &resp, &out_size);
	if (st == SDI_OK) {
		*out_irq = resp.irq;
	}
	return st;
}

sdi_status_t sdi_irq_unbind(sdi_irq_t irq)
{
	return sdi_call(irq, SDI_OP_IRQ_UNBIND, 0, 0u, 0, 0);
}
sdi_status_t sdi_irq_ack(sdi_irq_t irq)
{
	return sdi_call(irq, SDI_OP_IRQ_ACK, 0, 0u, 0, 0);
}
sdi_status_t sdi_irq_mask(sdi_irq_t irq)
{
	return sdi_call(irq, SDI_OP_IRQ_MASK, 0, 0u, 0, 0);
}
sdi_status_t sdi_irq_unmask(sdi_irq_t irq)
{
	return sdi_call(irq, SDI_OP_IRQ_UNMASK, 0, 0u, 0, 0);
}

sdi_status_t sdi_dma_alloc(sdi_device_t device, sdi_u64 size, sdi_u32 direction,
						   sdi_flags_t flags, sdi_dma_alloc_resp_t *out_dma)
{
	sdi_dma_alloc_req_t req;
	SDI_INIT_PTR(&req);
	sdi_size_t out_size = sizeof(*out_dma);
	if (out_dma == 0 || size == 0u) {
		return SDI_ERR_INVAL;
	}
	req.byte_count = size;
	req.direction = direction;
	req.flags = flags;
	return sdi_call(device, SDI_OP_DMA_ALLOC, &req, sizeof(req), out_dma,
					&out_size);
}

sdi_status_t sdi_dma_free(sdi_dma_t dma)
{
	return sdi_call(dma, SDI_OP_DMA_FREE, 0, 0u, 0, 0);
}

sdi_status_t sdi_class_register(sdi_device_t device,
								const sdi_class_desc_t *desc,
								sdi_classdev_t *out_classdev)
{
	sdi_class_register_resp_t resp;
	SDI_INIT_PTR(&resp);
	sdi_size_t out_size = sizeof(resp);
	sdi_status_t st;
	if (desc == 0 || out_classdev == 0) {
		return SDI_ERR_INVAL;
	}
	st = sdi_call(device, SDI_OP_CLASS_REGISTER, desc, sizeof(*desc), &resp,
				  &out_size);
	if (st == SDI_OK) {
		*out_classdev = resp.classdev;
	}
	return st;
}

sdi_status_t sdi_class_unregister(sdi_classdev_t classdev)
{
	return sdi_call(classdev, SDI_OP_CLASS_UNREGISTER, 0, 0u, 0, 0);
}
