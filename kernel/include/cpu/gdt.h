#ifndef CPU_GDT_H
#define CPU_GDT_H

#include <stdint.h>

#define GDT_KERNEL_CODE_SELECTOR 0x08
#define GDT_KERNEL_DATA_SELECTOR 0x10
#define GDT_USER_CODE_SELECTOR 0x1b
#define GDT_USER_DATA_SELECTOR 0x23
#define GDT_TSS_SELECTOR 0x28

typedef struct {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_mid;
	uint8_t access;
	uint8_t limit_flags;
	uint8_t base_high;
} __attribute__((packed)) gdt_descriptor_t;

typedef struct {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed)) gdtr_t;

typedef struct {
	gdt_descriptor_t entries[7];
} __attribute__((packed)) gdt_t;

void gdt_init();
void gdt_init_cpu(uint32_t cpu_index);
void gdt_tss_init(uint64_t rsp0);
void gdt_tss_init_cpu(uint32_t cpu_index, uint64_t rsp0);
void gdt_set_kernel_stack(uint64_t rsp0);

#endif // CPU_GDT_H
