#include <cpu/gdt.h>
#include <lib/string.h>
#include <sys/alix.h>

#define KLOG_NS "gdt"
#include <sys/klog.h>

/* TODO: smp support */

#define GDT_DEBUG 1

#define GDT_P 0b10000000
#define GDT_DPL0 0b00000000
#define GDT_DPL3 0b01100000
#define GDT_S 0b00010000

#define GDT_CODE 0b00001000
#define GDT_DATA 0b00000000

#define GDT_RW 0b00000010
#define GDT_A 0b00000001

#define GDT_KERNEL_CODE (GDT_P | GDT_DPL0 | GDT_S | GDT_CODE | GDT_RW)
#define GDT_KERNEL_DATA (GDT_P | GDT_DPL0 | GDT_S | GDT_DATA | GDT_RW)

#define GDT_USER_CODE (GDT_P | GDT_DPL3 | GDT_S | GDT_CODE | GDT_RW)
#define GDT_USER_DATA (GDT_P | GDT_DPL3 | GDT_S | GDT_DATA | GDT_RW)

#define GDT_FLAG_GRAN_4K 0b10000000
#define GDT_FLAG_64BIT 0b00100000

#define _ENTRY(base, limit, access, flags)                         \
	((gdt_descriptor_t){                                           \
		(uint16_t)((limit) & 0xffff), (uint16_t)((base) & 0xffff), \
		(uint8_t)(((base) >> 16) & 0xff), (uint8_t)(access),       \
		(uint8_t)((((limit) >> 16) & 0x0f) | ((flags) & 0xf0)),    \
		(uint8_t)(((base) >> 24) & 0xff) })

static const gdt_t
	gdt_template = { .entries = {
						 /* null */
						 _ENTRY(0, 0, 0, 0),

						 /* kernel code */
						 _ENTRY(0, 0xfffff, GDT_KERNEL_CODE,
								GDT_FLAG_GRAN_4K | GDT_FLAG_64BIT),

						 /* kernel data */
						 _ENTRY(0, 0xfffff, GDT_KERNEL_DATA, GDT_FLAG_GRAN_4K),

						 /* user code */
						 _ENTRY(0, 0xfffff, GDT_USER_CODE,
								GDT_FLAG_GRAN_4K | GDT_FLAG_64BIT),

						 /* user data */
						 _ENTRY(0, 0xfffff, GDT_USER_DATA, GDT_FLAG_GRAN_4K),

						 /* TSS, filled per CPU */
						 _ENTRY(0, 0, 0, 0),
						 _ENTRY(0, 0, 0, 0),
					 } };

static gdt_t cpu_gdt[MAX_CPUS];
static gdtr_t cpu_gdtr[MAX_CPUS];

typedef struct {
	uint32_t reserved0;
	uint64_t rsp[3];
	uint64_t reserved1;
	uint64_t ist[7];
	uint64_t reserved2;
	uint16_t reserved3;
	uint16_t iopb;
} __attribute__((packed)) tss_t;

static tss_t cpu_tss[MAX_CPUS];

static const char *gdt_entry_name(size_t index)
{
	switch (index) {
	case 0:
		return "null";
	case 1:
		return "kcode";
	case 2:
		return "kdata";
	case 3:
		return "ucode";
	case 4:
		return "udata";
	case 5:
		return "tsslo";
	case 6:
		return "tsshi";
	default:
		return "unk";
	}
}

static uint32_t gdt_desc_base(const gdt_descriptor_t *desc)
{
	return ((uint32_t)desc->base_low) | ((uint32_t)desc->base_mid << 16) |
		   ((uint32_t)desc->base_high << 24);
}

static uint32_t gdt_desc_limit(const gdt_descriptor_t *desc)
{
	return ((uint32_t)desc->limit_low) |
		   ((uint32_t)(desc->limit_flags & 0x0f) << 16);
}

static uint8_t gdt_desc_flags(const gdt_descriptor_t *desc)
{
	return desc->limit_flags & 0xf0;
}

static void gdt_dump_cpu(uint32_t cpu_index)
{
	uint64_t *raw = (uint64_t *)cpu_gdt[cpu_index].entries;

	klog("cpu%u: gdtr base=0x%016llx limit=0x%04x", cpu_index,
		 (unsigned long long)cpu_gdtr[cpu_index].base,
		 cpu_gdtr[cpu_index].limit);

	for (size_t i = 0; i < 7; i++) {
		gdt_descriptor_t *desc = &cpu_gdt[cpu_index].entries[i];

		if (i == 6) {
			klog("cpu%u: %02zu %-5s raw=0x%016llx", cpu_index, i,
				 gdt_entry_name(i), (unsigned long long)raw[i]);
			continue;
		}

		klog(
			"cpu%u: %02zu %-5s base=0x%08x limit=0x%05x flags=0x%02x access=0x%02x",
			cpu_index, i, gdt_entry_name(i), gdt_desc_base(desc),
			gdt_desc_limit(desc), gdt_desc_flags(desc), desc->access);
	}
}

static void gdt_set_tss_descriptor(uint32_t cpu_index, tss_t *tss)
{
	if (cpu_index >= MAX_CPUS)
		cpu_index = 0;

	uint64_t base = (uint64_t)tss;
	uint64_t limit = sizeof(*tss) - 1;
	uint64_t low = 0;

	low |= limit & 0xffff;
	low |= (base & 0xffff) << 16;
	low |= ((base >> 16) & 0xff) << 32;
	low |= 0x89ULL << 40;
	low |= ((limit >> 16) & 0x0f) << 48;
	low |= ((base >> 24) & 0xff) << 56;

	uint64_t *raw = (uint64_t *)cpu_gdt[cpu_index].entries;

	raw[5] = low;
	raw[6] = base >> 32;
}

void gdt_init(void)
{
	gdt_init_cpu(0);
}

void gdt_init_cpu(uint32_t cpu_index)
{
	if (cpu_index >= MAX_CPUS)
		cpu_index = 0;

	memcpy(&cpu_gdt[cpu_index], &gdt_template, sizeof(gdt_template));

	cpu_gdtr[cpu_index].limit = sizeof(cpu_gdt[cpu_index].entries) - 1;
	cpu_gdtr[cpu_index].base = (uint64_t)&cpu_gdt[cpu_index].entries;

	klog("cpu%u: loading", cpu_index);
	gdt_dump_cpu(cpu_index);

	__asm__ volatile("lgdt %[g]\n"
					 "pushq $0x08\n"
					 "lea 1f(%%rip), %%rax\n"
					 "pushq %%rax\n"
					 "lretq\n"
					 "1:\n"
					 "mov $0x10, %%ax\n"
					 "mov %%ax, %%ds\n"
					 "mov %%ax, %%es\n"
					 "mov %%ax, %%ss\n"
					 "mov %%ax, %%fs\n"
					 "mov %%ax, %%gs\n"
					 :
					 : [g] "m"(cpu_gdtr[cpu_index])
					 : "rax", "memory");

	klog("cpu%u: loaded", cpu_index);
}

void gdt_tss_init_cpu(uint32_t cpu_index, uint64_t rsp0)
{
	if (cpu_index >= MAX_CPUS)
		cpu_index = 0;

	tss_t *tss = &cpu_tss[cpu_index];

	memset(tss, 0, sizeof(*tss));

	tss->rsp[0] = rsp0;
	tss->iopb = sizeof(*tss);

	gdt_set_tss_descriptor(cpu_index, tss);

	klog("cpu%u: tss base=0x%016llx rsp0=0x%016llx", cpu_index,
		 (unsigned long long)tss, (unsigned long long)rsp0);

	__asm__ volatile("ltr %%ax" : : "a"((uint16_t)0x28) : "memory");

	klog("cpu%u: tss loaded", cpu_index);
}

void gdt_tss_init(uint64_t rsp0)
{
	gdt_tss_init_cpu(0, rsp0);
}

void gdt_set_kernel_stack(uint64_t rsp0)
{
	uint32_t index = 0;

	if (index >= MAX_CPUS)
		index = 0;

	cpu_tss[index].rsp[0] = rsp0;

	klog("cpu%u: kernel stack rsp0=0x%016llx", index, (unsigned long long)rsp0);
}