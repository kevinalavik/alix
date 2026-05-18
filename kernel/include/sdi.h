/*
 * SDI - Simple Driver Interface
 * Version: 1.0.0, First Revision
 * SPDX-License-Identifier: 0BSD
 *
 * Copyright (C) 2026 Kevin Alavik
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * This header is intentionally standalone. Drivers include only sdi.h.
 * The SDI core is implemented by sdi.c. The kernel supplies host callbacks to
 * sdi_init(), then uses the built-in SDI registry and dispatch functions.
 */

#ifndef SDI_H
#define SDI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Version and build constants
 * ------------------------------------------------------------------------- */

#define SDI_VERSION_MAJOR 1u
#define SDI_VERSION_MINOR 0u
#define SDI_VERSION_PATCH 0u

#define SDI_VERSION_ENCODE(major_, minor_, patch_) \
    ((((uint32_t)(major_) & 0xffu) << 24) | \
     (((uint32_t)(minor_) & 0xfffu) << 12) | \
     ((uint32_t)(patch_) & 0xfffu))

#define SDI_ABI_VERSION SDI_VERSION_ENCODE(SDI_VERSION_MAJOR, SDI_VERSION_MINOR, SDI_VERSION_PATCH)
#define SDI_NULL_HANDLE ((sdi_handle_t)0u)
#define SDI_MAX_DRIVER_NAME 64u
#define SDI_MAX_HOST_NAME 64u
#define SDI_MAX_REGISTERED_DRIVERS 64u
#define SDI_ARRAY_COUNT(a_) ((uint32_t)(sizeof(a_) / sizeof((a_)[0])))

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define SDI_STATIC_ASSERT(cond_, msg_) _Static_assert((cond_), #msg_)
#else
#define SDI_STATIC_ASSERT(cond_, msg_) typedef char sdi_static_assertion_##msg_[(cond_) ? 1 : -1]
#endif

#define SDI_ABI_FIELDS uint32_t size; uint32_t version
#define SDI_INIT_STRUCT(type_) { (uint32_t)sizeof(type_), SDI_ABI_VERSION }
#define SDI_INIT_PTR(ptr_) do { (ptr_)->size = (uint32_t)sizeof(*(ptr_)); (ptr_)->version = SDI_ABI_VERSION; } while (0)

/* -------------------------------------------------------------------------
 * Base data model
 * ------------------------------------------------------------------------- */

typedef uint64_t sdi_u64;
typedef uint32_t sdi_u32;
typedef uint16_t sdi_u16;
typedef uint8_t  sdi_u8;
typedef int64_t  sdi_i64;
typedef int32_t  sdi_i32;
typedef uintptr_t sdi_uptr;
typedef size_t sdi_size_t;
typedef sdi_i32 sdi_status_t;
typedef sdi_u32 sdi_flags_t;
typedef sdi_u64 sdi_handle_t;
typedef sdi_u64 sdi_caps_t;
typedef sdi_u64 sdi_bus_addr_t;
typedef sdi_u64 sdi_phys_addr_t;

typedef sdi_handle_t sdi_driver_handle_t;
typedef sdi_handle_t sdi_device_t;
typedef sdi_handle_t sdi_bus_t;
typedef sdi_handle_t sdi_mmio_t;
typedef sdi_handle_t sdi_irq_t;
typedef sdi_handle_t sdi_dma_t;
typedef sdi_handle_t sdi_classdev_t;
typedef sdi_handle_t sdi_timer_t;

typedef struct sdi_abi_header { SDI_ABI_FIELDS; } sdi_abi_header_t;

/* -------------------------------------------------------------------------
 * Status codes
 * ------------------------------------------------------------------------- */

enum sdi_status_code {
    SDI_OK              = 0,
    SDI_ERR_UNKNOWN     = -1,
    SDI_ERR_NOMEM       = -2,
    SDI_ERR_INVAL       = -3,
    SDI_ERR_UNSUPPORTED = -4,
    SDI_ERR_BUSY        = -5,
    SDI_ERR_TIMEOUT     = -6,
    SDI_ERR_IO          = -7,
    SDI_ERR_NO_DEVICE   = -8,
    SDI_ERR_PERMISSION  = -9,
    SDI_ERR_AGAIN       = -10,
    SDI_ERR_DEAD        = -11,
    SDI_ERR_RANGE       = -12,
    SDI_ERR_ALIGNMENT   = -13,
    SDI_ERR_PROTOCOL    = -14,
    SDI_ERR_EXISTS      = -15,
    SDI_ERR_NOT_FOUND   = -16,
    SDI_ERR_CANCELLED   = -17,
    SDI_ERR_SHORT       = -18,
    SDI_ERR_NOT_READY   = -19
};

static inline int sdi_failed(sdi_status_t st) { return st < 0; }
static inline int sdi_succeeded(sdi_status_t st) { return st >= 0; }

/* -------------------------------------------------------------------------
 * Capabilities
 * ------------------------------------------------------------------------- */

/* Capabilities are macros, not enum constants, so 64-bit flags remain C11-portable. */
#define SDI_CAP_CORE         (UINT64_C(1) << 0)
#define SDI_CAP_LOG          (UINT64_C(1) << 1)
#define SDI_CAP_ALLOC        (UINT64_C(1) << 2)
#define SDI_CAP_TIME         (UINT64_C(1) << 3)
#define SDI_CAP_CALL         (UINT64_C(1) << 4)
#define SDI_CAP_REGISTRY     (UINT64_C(1) << 5)
#define SDI_CAP_DEVICE       (UINT64_C(1) << 6)
#define SDI_CAP_MMIO         (UINT64_C(1) << 7)
#define SDI_CAP_PIO          (UINT64_C(1) << 8)
#define SDI_CAP_IRQ          (UINT64_C(1) << 9)
#define SDI_CAP_DMA          (UINT64_C(1) << 10)
#define SDI_CAP_CLASS        (UINT64_C(1) << 11)
#define SDI_CAP_TIMER        (UINT64_C(1) << 12)
#define SDI_CAP_POWER        (UINT64_C(1) << 13)
#define SDI_CAP_HOTPLUG      (UINT64_C(1) << 14)
#define SDI_CAP_BUS_PCI      (UINT64_C(1) << 20)
#define SDI_CAP_BUS_USB      (UINT64_C(1) << 21)
#define SDI_CAP_BUS_PLATFORM (UINT64_C(1) << 22)
#define SDI_CAP_BUS_ACPI     (UINT64_C(1) << 23)
#define SDI_CAP_BUS_DT       (UINT64_C(1) << 24)
#define SDI_CAP_BUS_VIRTIO   (UINT64_C(1) << 25)
#define SDI_CAP_CLASS_CHAR   (UINT64_C(1) << 32)
#define SDI_CAP_CLASS_BLOCK  (UINT64_C(1) << 33)
#define SDI_CAP_CLASS_NET    (UINT64_C(1) << 34)
#define SDI_CAP_CLASS_INPUT  (UINT64_C(1) << 35)
#define SDI_CAP_CLASS_FB     (UINT64_C(1) << 36)
#define SDI_CAP_DMA_COHERENT (UINT64_C(1) << 48)
#define SDI_CAP_IOMMU        (UINT64_C(1) << 49)
#define SDI_CAP_DMA_64BIT    (UINT64_C(1) << 50)
#define SDI_CAP_REQUIRED_CORE \
    (SDI_CAP_CORE | SDI_CAP_LOG | SDI_CAP_ALLOC | SDI_CAP_TIME | SDI_CAP_CALL | SDI_CAP_REGISTRY)

/* Allocation flags for sdi_alloc(). */
#define SDI_ALLOC_ZERO         (UINT32_C(1) << 0)
#define SDI_ALLOC_ATOMIC       (UINT32_C(1) << 1)
#define SDI_ALLOC_PAGE_ALIGNED (UINT32_C(1) << 2)

/* -------------------------------------------------------------------------
 * Logging
 * ------------------------------------------------------------------------- */

enum sdi_log_level {
    SDI_LOG_TRACE = 0,
    SDI_LOG_DEBUG = 1,
    SDI_LOG_INFO  = 2,
    SDI_LOG_WARN  = 3,
    SDI_LOG_ERROR = 4,
    SDI_LOG_FATAL = 5
};

/* -------------------------------------------------------------------------
 * Host callbacks supplied by the kernel to sdi_init()
 * ------------------------------------------------------------------------- */

struct sdi_host_ops;
typedef void (*sdi_host_log_fn)(void *user, sdi_u32 level, const char *component, const char *message);
typedef void *(*sdi_host_alloc_fn)(void *user, sdi_size_t size, sdi_flags_t flags);
typedef void (*sdi_host_free_fn)(void *user, void *ptr);
typedef sdi_u64 (*sdi_host_now_ns_fn)(void *user);
typedef sdi_status_t (*sdi_host_call_fn)(void *user,
                                         sdi_handle_t target,
                                         sdi_u32 opcode,
                                         const void *in_data,
                                         sdi_size_t in_size,
                                         void *out_data,
                                         sdi_size_t *inout_out_size);

typedef struct sdi_host_ops {
    SDI_ABI_FIELDS;
    const char *host_name;
    sdi_caps_t caps;
    sdi_u32 max_drivers;
    void *user;
    sdi_host_log_fn log;
    sdi_host_alloc_fn alloc;
    sdi_host_free_fn free;
    sdi_host_now_ns_fn now_ns;
    sdi_host_call_fn call;
} sdi_host_ops_t;

typedef struct sdi_init_options {
    SDI_ABI_FIELDS;
    sdi_u32 max_drivers;
    sdi_flags_t flags;
} sdi_init_options_t;

typedef struct sdi_info {
    SDI_ABI_FIELDS;
    sdi_u32 abi_major;
    sdi_u32 abi_minor;
    sdi_u32 abi_patch;
    sdi_u32 pointer_bits;
    sdi_caps_t caps;
    sdi_u32 max_drivers;
    sdi_u32 driver_count;
    char host_name[SDI_MAX_HOST_NAME];
} sdi_info_t;

/* -------------------------------------------------------------------------
 * Matching and driver lifecycle
 * ------------------------------------------------------------------------- */

enum sdi_match_kind {
    SDI_MATCH_NONE     = 0,
    SDI_MATCH_PCI      = 1,
    SDI_MATCH_USB      = 2,
    SDI_MATCH_PLATFORM = 3,
    SDI_MATCH_ACPI     = 4,
    SDI_MATCH_DT       = 5,
    SDI_MATCH_VIRTIO   = 6,
    SDI_MATCH_SOFT     = 7
};

#define SDI_MATCH_ANY_U64 UINT64_C(0xffffffffffffffff)
#define SDI_MATCH_ANY_U32 UINT32_C(0xffffffff)

typedef struct sdi_match_id {
    SDI_ABI_FIELDS;
    sdi_u32 kind;
    sdi_u32 flags;
    sdi_u64 vendor;
    sdi_u64 device;
    sdi_u64 subvendor;
    sdi_u64 subdevice;
    sdi_u32 class_code;
    sdi_u32 revision;
    const char *compatible;
    const void *driver_data;
} sdi_match_id_t;

typedef struct sdi_probe_info {
    SDI_ABI_FIELDS;
    sdi_device_t device;
    sdi_bus_t bus;
    sdi_match_id_t match;
    sdi_u32 resource_count;
    sdi_u32 property_count;
    void *host_private;
} sdi_probe_info_t;

typedef struct sdi_probe_result {
    SDI_ABI_FIELDS;
    sdi_u32 score;       /* 0 = no match, 1000+ = exact match. */
    sdi_flags_t flags;
    const char *reason;
} sdi_probe_result_t;

typedef struct sdi_attach_args {
    SDI_ABI_FIELDS;
    sdi_device_t device;
    const sdi_match_id_t *match;
    void *host_private;
} sdi_attach_args_t;

typedef sdi_status_t (*sdi_driver_init_fn)(void);
typedef sdi_status_t (*sdi_driver_fini_fn)(void);
typedef sdi_status_t (*sdi_driver_probe_fn)(const sdi_probe_info_t *info, sdi_probe_result_t *out_result);
typedef sdi_status_t (*sdi_driver_attach_fn)(const sdi_attach_args_t *args, sdi_handle_t *out_instance);
typedef sdi_status_t (*sdi_driver_start_fn)(sdi_handle_t instance);
typedef sdi_status_t (*sdi_driver_stop_fn)(sdi_handle_t instance);
typedef sdi_status_t (*sdi_driver_suspend_fn)(sdi_handle_t instance, sdi_u32 state);
typedef sdi_status_t (*sdi_driver_resume_fn)(sdi_handle_t instance);
typedef sdi_status_t (*sdi_driver_detach_fn)(sdi_handle_t instance);
typedef sdi_status_t (*sdi_driver_remove_fn)(sdi_handle_t instance);

typedef struct sdi_driver_ops {
    SDI_ABI_FIELDS;
    sdi_driver_init_fn init;
    sdi_driver_fini_fn fini;
    sdi_driver_probe_fn probe;
    sdi_driver_attach_fn attach;
    sdi_driver_start_fn start;
    sdi_driver_stop_fn stop;
    sdi_driver_suspend_fn suspend;
    sdi_driver_resume_fn resume;
    sdi_driver_detach_fn detach;
    sdi_driver_remove_fn remove;
} sdi_driver_ops_t;

typedef struct sdi_driver_desc {
    SDI_ABI_FIELDS;
    const char *name;
    const char *vendor;
    const char *description;
    sdi_u32 abi_major;
    sdi_u32 abi_minor;
    sdi_u32 abi_patch;
    sdi_caps_t required_caps;
    sdi_caps_t optional_caps;
    const sdi_match_id_t *matches;
    sdi_u32 match_count;
    const sdi_driver_ops_t *ops;
    void *driver_private;
} sdi_driver_desc_t;

/* -------------------------------------------------------------------------
 * Generic SDI protocol opcodes for sdi_call()
 * ------------------------------------------------------------------------- */

enum sdi_opcode {
    SDI_OP_NOP              = 0x00000000u,
    SDI_OP_DEVICE_INFO      = 0x00000100u,
    SDI_OP_GET_RESOURCE     = 0x00000101u,
    SDI_OP_MMIO_MAP         = 0x00000200u,
    SDI_OP_MMIO_UNMAP       = 0x00000201u,
    SDI_OP_MMIO_READ        = 0x00000202u,
    SDI_OP_MMIO_WRITE       = 0x00000203u,
    SDI_OP_MMIO_BARRIER     = 0x00000204u,
    SDI_OP_PIO_READ         = 0x00000280u,
    SDI_OP_PIO_WRITE        = 0x00000281u,
    SDI_OP_IRQ_BIND         = 0x00000300u,
    SDI_OP_IRQ_UNBIND       = 0x00000301u,
    SDI_OP_IRQ_ACK          = 0x00000302u,
    SDI_OP_IRQ_MASK         = 0x00000303u,
    SDI_OP_IRQ_UNMASK       = 0x00000304u,
    SDI_OP_DMA_ALLOC        = 0x00000400u,
    SDI_OP_DMA_FREE         = 0x00000401u,
    SDI_OP_DMA_MAP          = 0x00000402u,
    SDI_OP_DMA_UNMAP        = 0x00000403u,
    SDI_OP_DMA_SYNC_DEVICE  = 0x00000404u,
    SDI_OP_DMA_SYNC_CPU     = 0x00000405u,
    SDI_OP_CLASS_REGISTER   = 0x00000500u,
    SDI_OP_CLASS_UNREGISTER = 0x00000501u,
    SDI_OP_TIMER_AFTER_NS   = 0x00000600u,
    SDI_OP_TIMER_CANCEL     = 0x00000601u,
    SDI_OP_POWER_SET        = 0x00000700u,
    SDI_OP_POWER_GET        = 0x00000701u
};

#define SDI_OP_VENDOR_BASE 0x80000000u

enum sdi_resource_type {
    SDI_RES_NONE = 0,
    SDI_RES_MMIO = 1,
    SDI_RES_PIO  = 2,
    SDI_RES_IRQ  = 3,
    SDI_RES_DMA  = 4,
    SDI_RES_MEM  = 5
};

typedef struct sdi_resource {
    SDI_ABI_FIELDS;
    sdi_u32 type;
    sdi_u32 index;
    sdi_u64 start;
    sdi_u64 length;
    sdi_flags_t flags;
} sdi_resource_t;

typedef struct sdi_get_resource_req { SDI_ABI_FIELDS; sdi_u32 index; sdi_u32 type; } sdi_get_resource_req_t;

typedef struct sdi_mmio_map_req { SDI_ABI_FIELDS; sdi_u32 resource_index; sdi_flags_t flags; } sdi_mmio_map_req_t;
typedef struct sdi_mmio_map_resp { SDI_ABI_FIELDS; sdi_mmio_t mmio; sdi_u64 length; } sdi_mmio_map_resp_t;
typedef struct sdi_mmio_io { SDI_ABI_FIELDS; sdi_u64 offset; sdi_u32 width; sdi_u32 reserved; sdi_u64 value; } sdi_mmio_io_t;
typedef struct sdi_pio_io { SDI_ABI_FIELDS; sdi_u16 port; sdi_u8 width; sdi_u8 reserved; sdi_u32 value; } sdi_pio_io_t;

typedef sdi_status_t (*sdi_irq_handler_fn)(sdi_handle_t instance, sdi_irq_t irq);
typedef struct sdi_irq_bind_req { SDI_ABI_FIELDS; sdi_u32 resource_index; sdi_flags_t flags; sdi_irq_handler_fn handler; sdi_handle_t instance; } sdi_irq_bind_req_t;
typedef struct sdi_irq_bind_resp { SDI_ABI_FIELDS; sdi_irq_t irq; } sdi_irq_bind_resp_t;

enum sdi_dma_direction { SDI_DMA_TO_DEVICE = 1, SDI_DMA_FROM_DEVICE = 2, SDI_DMA_BIDIR = 3 };
typedef struct sdi_dma_segment { sdi_bus_addr_t bus_addr; sdi_u64 length; } sdi_dma_segment_t;
typedef struct sdi_dma_alloc_req { SDI_ABI_FIELDS; sdi_u64 byte_count; sdi_flags_t flags; sdi_u32 direction; } sdi_dma_alloc_req_t;
typedef struct sdi_dma_alloc_resp { SDI_ABI_FIELDS; sdi_dma_t dma; void *cpu_addr; sdi_u32 segment_count; sdi_dma_segment_t segments[8]; } sdi_dma_alloc_resp_t;
typedef struct sdi_dma_map_req { SDI_ABI_FIELDS; void *cpu_addr; sdi_u64 byte_count; sdi_u32 direction; sdi_flags_t flags; } sdi_dma_map_req_t;
typedef struct sdi_dma_map_resp { SDI_ABI_FIELDS; sdi_dma_t dma; sdi_u32 segment_count; sdi_dma_segment_t segments[8]; } sdi_dma_map_resp_t;

enum sdi_class_type { SDI_CLASS_CHAR = 1, SDI_CLASS_BLOCK = 2, SDI_CLASS_NET = 3, SDI_CLASS_INPUT = 4, SDI_CLASS_FB = 5 };
typedef struct sdi_class_desc { SDI_ABI_FIELDS; sdi_u32 class_type; sdi_flags_t flags; const char *name; const void *ops; sdi_handle_t instance; } sdi_class_desc_t;
typedef struct sdi_class_register_resp { SDI_ABI_FIELDS; sdi_classdev_t classdev; } sdi_class_register_resp_t;

/* -------------------------------------------------------------------------
 * Built-in SDI core functions implemented by sdi.c
 * ------------------------------------------------------------------------- */

sdi_status_t sdi_init(const sdi_host_ops_t *host, const sdi_init_options_t *options);
sdi_status_t sdi_shutdown(void);
int sdi_is_initialized(void);
sdi_status_t sdi_get_info(sdi_info_t *out_info);

sdi_status_t sdi_register_driver(const sdi_driver_desc_t *driver, sdi_driver_handle_t *out_handle);
sdi_status_t sdi_unregister_driver(sdi_driver_handle_t handle);
sdi_status_t sdi_find_driver(const char *name, sdi_driver_handle_t *out_handle);
sdi_status_t sdi_get_driver_count(sdi_u32 *out_count);
sdi_status_t sdi_get_driver_at(sdi_u32 index, const sdi_driver_desc_t **out_driver, sdi_driver_handle_t *out_handle);

sdi_status_t sdi_probe_driver(sdi_driver_handle_t handle, const sdi_probe_info_t *info, sdi_probe_result_t *out_result);
sdi_status_t sdi_attach_driver(sdi_driver_handle_t handle, const sdi_attach_args_t *args, sdi_handle_t *out_instance);
sdi_status_t sdi_start_driver(sdi_driver_handle_t handle, sdi_handle_t instance);
sdi_status_t sdi_stop_driver(sdi_driver_handle_t handle, sdi_handle_t instance);
sdi_status_t sdi_suspend_driver(sdi_driver_handle_t handle, sdi_handle_t instance, sdi_u32 state);
sdi_status_t sdi_resume_driver(sdi_driver_handle_t handle, sdi_handle_t instance);
sdi_status_t sdi_detach_driver(sdi_driver_handle_t handle, sdi_handle_t instance);
sdi_status_t sdi_remove_driver(sdi_driver_handle_t handle, sdi_handle_t instance);

sdi_status_t sdi_log(sdi_u32 level, const char *component, const char *message);
void *sdi_alloc(sdi_size_t size, sdi_flags_t flags);
void sdi_free(void *ptr);
sdi_u64 sdi_now_ns(void);
sdi_status_t sdi_call(sdi_handle_t target, sdi_u32 opcode, const void *in_data, sdi_size_t in_size, void *out_data, sdi_size_t *inout_out_size);

/* Convenience wrappers over sdi_call(). Kernels may support only a subset. */
sdi_status_t sdi_get_resource(sdi_device_t device, sdi_u32 index, sdi_u32 type, sdi_resource_t *out_resource);
sdi_status_t sdi_mmio_map(sdi_device_t device, sdi_u32 resource_index, sdi_flags_t flags, sdi_mmio_t *out_mmio, sdi_u64 *out_length);
sdi_status_t sdi_mmio_unmap(sdi_mmio_t mmio);
sdi_status_t sdi_mmio_read(sdi_mmio_t mmio, sdi_u64 offset, sdi_u32 width, sdi_u64 *out_value);
sdi_status_t sdi_mmio_write(sdi_mmio_t mmio, sdi_u64 offset, sdi_u32 width, sdi_u64 value);
sdi_status_t sdi_mmio_barrier(sdi_mmio_t mmio);
sdi_status_t sdi_pio_read(sdi_u16 port, sdi_u8 width, sdi_u32 *out_value);
sdi_status_t sdi_pio_write(sdi_u16 port, sdi_u8 width, sdi_u32 value);
sdi_status_t sdi_irq_bind(sdi_device_t device, sdi_u32 resource_index, sdi_flags_t flags, sdi_irq_handler_fn handler, sdi_handle_t instance, sdi_irq_t *out_irq);
sdi_status_t sdi_irq_unbind(sdi_irq_t irq);
sdi_status_t sdi_irq_ack(sdi_irq_t irq);
sdi_status_t sdi_irq_mask(sdi_irq_t irq);
sdi_status_t sdi_irq_unmask(sdi_irq_t irq);
sdi_status_t sdi_dma_alloc(sdi_device_t device, sdi_u64 size, sdi_u32 direction, sdi_flags_t flags, sdi_dma_alloc_resp_t *out_dma);
sdi_status_t sdi_dma_free(sdi_dma_t dma);
sdi_status_t sdi_class_register(sdi_device_t device, const sdi_class_desc_t *desc, sdi_classdev_t *out_classdev);
sdi_status_t sdi_class_unregister(sdi_classdev_t classdev);

#ifdef __cplusplus
}
#endif

#endif /* SDI_H */
