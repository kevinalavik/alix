#include <sdi/host.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fs/vfs.h>
#include <lib/string.h>
#include <mm/mm.h>
#include <sdi.h>
#include <user/posix.h>

#define KLOG_NS "sdi-elf"
#include <log/klog.h>

#define EI_NIDENT 16
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_REL 1
#define EM_X86_64 62

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8

#define SHF_ALLOC 0x2

#define SHN_UNDEF 0
#define SHN_ABS 0xfff1

#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_PLT32 4
#define R_X86_64_32 10
#define R_X86_64_32S 11

#define ELF64_R_SYM(info_) ((uint32_t)((info_) >> 32))
#define ELF64_R_TYPE(info_) ((uint32_t)(info_))

typedef struct {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct {
	uint32_t sh_name;
	uint32_t sh_type;
	uint64_t sh_flags;
	uint64_t sh_addr;
	uint64_t sh_offset;
	uint64_t sh_size;
	uint32_t sh_link;
	uint32_t sh_info;
	uint64_t sh_addralign;
	uint64_t sh_entsize;
} elf64_shdr_t;

typedef struct {
	uint32_t st_name;
	unsigned char st_info;
	unsigned char st_other;
	uint16_t st_shndx;
	uint64_t st_value;
	uint64_t st_size;
} elf64_sym_t;

typedef struct {
	uint64_t r_offset;
	uint64_t r_info;
	int64_t r_addend;
} elf64_rela_t;

typedef struct {
	const char *name;
	uint64_t value;
} sdi_elf_export_t;

typedef struct {
	const uint8_t *image;
	size_t image_size;
	const elf64_ehdr_t *ehdr;
	const elf64_shdr_t *sections;
	void **section_bases;
	uint64_t *section_offsets;
	void *module_base;
	uint64_t module_size;
} sdi_elf_context_t;

static const sdi_elf_export_t sdi_elf_exports[] = {
	{ "sdi_alloc", (uint64_t)(uintptr_t)sdi_alloc },
	{ "sdi_attach_driver", (uint64_t)(uintptr_t)sdi_attach_driver },
	{ "sdi_call", (uint64_t)(uintptr_t)sdi_call },
	{ "sdi_class_register", (uint64_t)(uintptr_t)sdi_class_register },
	{ "sdi_class_unregister", (uint64_t)(uintptr_t)sdi_class_unregister },
	{ "sdi_detach_driver", (uint64_t)(uintptr_t)sdi_detach_driver },
	{ "sdi_dma_alloc", (uint64_t)(uintptr_t)sdi_dma_alloc },
	{ "sdi_dma_free", (uint64_t)(uintptr_t)sdi_dma_free },
	{ "sdi_find_driver", (uint64_t)(uintptr_t)sdi_find_driver },
	{ "sdi_free", (uint64_t)(uintptr_t)sdi_free },
	{ "sdi_get_driver_at", (uint64_t)(uintptr_t)sdi_get_driver_at },
	{ "sdi_get_driver_count", (uint64_t)(uintptr_t)sdi_get_driver_count },
	{ "sdi_get_info", (uint64_t)(uintptr_t)sdi_get_info },
	{ "sdi_get_resource", (uint64_t)(uintptr_t)sdi_get_resource },
	{ "sdi_irq_ack", (uint64_t)(uintptr_t)sdi_irq_ack },
	{ "sdi_irq_bind", (uint64_t)(uintptr_t)sdi_irq_bind },
	{ "sdi_irq_mask", (uint64_t)(uintptr_t)sdi_irq_mask },
	{ "sdi_irq_unbind", (uint64_t)(uintptr_t)sdi_irq_unbind },
	{ "sdi_irq_unmask", (uint64_t)(uintptr_t)sdi_irq_unmask },
	{ "sdi_is_initialized", (uint64_t)(uintptr_t)sdi_is_initialized },
	{ "sdi_log", (uint64_t)(uintptr_t)sdi_log },
	{ "sdi_mmio_barrier", (uint64_t)(uintptr_t)sdi_mmio_barrier },
	{ "sdi_mmio_map", (uint64_t)(uintptr_t)sdi_mmio_map },
	{ "sdi_mmio_read", (uint64_t)(uintptr_t)sdi_mmio_read },
	{ "sdi_mmio_unmap", (uint64_t)(uintptr_t)sdi_mmio_unmap },
	{ "sdi_mmio_write", (uint64_t)(uintptr_t)sdi_mmio_write },
	{ "sdi_now_ns", (uint64_t)(uintptr_t)sdi_now_ns },
	{ "sdi_pio_read", (uint64_t)(uintptr_t)sdi_pio_read },
	{ "sdi_pio_write", (uint64_t)(uintptr_t)sdi_pio_write },
	{ "sdi_probe_driver", (uint64_t)(uintptr_t)sdi_probe_driver },
	{ "sdi_register_driver", (uint64_t)(uintptr_t)sdi_register_driver },
	{ "sdi_remove_driver", (uint64_t)(uintptr_t)sdi_remove_driver },
	{ "sdi_resume_driver", (uint64_t)(uintptr_t)sdi_resume_driver },
	{ "sdi_start_driver", (uint64_t)(uintptr_t)sdi_start_driver },
	{ "sdi_stop_driver", (uint64_t)(uintptr_t)sdi_stop_driver },
	{ "sdi_suspend_driver", (uint64_t)(uintptr_t)sdi_suspend_driver },
	{ "sdi_unregister_driver", (uint64_t)(uintptr_t)sdi_unregister_driver },
};

static bool sdi_elf_range_ok(size_t image_size, uint64_t offset, uint64_t size)
{
	return offset <= image_size && size <= image_size - offset;
}

static bool sdi_elf_u64_add_ok(uint64_t a, uint64_t b, uint64_t *out)
{
	*out = a + b;
	return *out >= a;
}

static uint64_t sdi_elf_align_up(uint64_t value, uint64_t align)
{
	if (align <= 1)
		return value;

	return (value + align - 1) & ~(align - 1);
}

static bool sdi_elf_is_power_of_two(uint64_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

static bool sdi_elf_string_equal(const char *lhs, const char *rhs)
{
	size_t lhs_len;
	size_t rhs_len;

	if (!lhs || !rhs)
		return false;

	lhs_len = strlen(lhs);
	rhs_len = strlen(rhs);
	return lhs_len == rhs_len && memcmp(lhs, rhs, lhs_len) == 0;
}

static bool sdi_elf_string_ends_with(const char *str, const char *suffix)
{
	size_t str_len;
	size_t suffix_len;

	if (!str || !suffix)
		return false;

	str_len = strlen(str);
	suffix_len = strlen(suffix);
	if (suffix_len > str_len)
		return false;

	return sdi_elf_string_equal(str + str_len - suffix_len, suffix);
}

static bool sdi_elf_resolve_export(const char *name, uint64_t *out_value)
{
	for (size_t i = 0; i < SDI_ARRAY_COUNT(sdi_elf_exports); i++) {
		if (sdi_elf_string_equal(name, sdi_elf_exports[i].name)) {
			*out_value = sdi_elf_exports[i].value;
			return true;
		}
	}

	return false;
}

static bool sdi_elf_validate_header(const void *image, size_t image_size)
{
	const elf64_ehdr_t *ehdr;
	uint64_t section_bytes;

	if (!sdi_elf_range_ok(image_size, 0, sizeof(elf64_ehdr_t)))
		return false;

	ehdr = image;
	if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
		ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3)
		return false;
	if (ehdr->e_ident[4] != ELFCLASS64 || ehdr->e_ident[5] != ELFDATA2LSB ||
		ehdr->e_ident[6] != EV_CURRENT)
		return false;
	if (ehdr->e_type != ET_REL || ehdr->e_machine != EM_X86_64 ||
		ehdr->e_version != EV_CURRENT)
		return false;
	if (ehdr->e_ehsize < sizeof(elf64_ehdr_t) ||
		ehdr->e_shentsize != sizeof(elf64_shdr_t) || ehdr->e_shnum == 0)
		return false;
	if (!sdi_elf_u64_add_ok(0, (uint64_t)ehdr->e_shnum * ehdr->e_shentsize,
							&section_bytes))
		return false;

	return sdi_elf_range_ok(image_size, ehdr->e_shoff, section_bytes);
}

static int sdi_elf_prepare_sections(sdi_elf_context_t *ctx)
{
	uint64_t total = 0;
	uint64_t max_align = 1;

	ctx->section_bases = kzalloc(sizeof(void *) * ctx->ehdr->e_shnum);
	ctx->section_offsets = kzalloc(sizeof(uint64_t) * ctx->ehdr->e_shnum);
	if (!ctx->section_bases || !ctx->section_offsets)
		return -1;

	for (uint16_t i = 0; i < ctx->ehdr->e_shnum; i++) {
		const elf64_shdr_t *section = &ctx->sections[i];
		uint64_t align = section->sh_addralign ? section->sh_addralign : 1;

		if ((section->sh_flags & SHF_ALLOC) == 0)
			continue;
		if (!sdi_elf_is_power_of_two(align) || align > PAGE_SIZE)
			return -1;
		if (section->sh_type != SHT_NOBITS &&
			!sdi_elf_range_ok(ctx->image_size, section->sh_offset,
							  section->sh_size))
			return -1;

		total = sdi_elf_align_up(total, align);
		ctx->section_offsets[i] = total;
		if (!sdi_elf_u64_add_ok(total, section->sh_size, &total))
			return -1;
		if (align > max_align)
			max_align = align;
	}

	if (total == 0)
		return -1;

	ctx->module_base = kmalloc(total + max_align - 1);
	if (!ctx->module_base)
		return -1;
	ctx->module_base = (void *)(uintptr_t)sdi_elf_align_up(
		(uint64_t)(uintptr_t)ctx->module_base, max_align);
	ctx->module_size = total;
	memset(ctx->module_base, 0, total);
	if (paging_map_range(
			kernel_vas, PAGE_ALIGN_DOWN(ctx->module_base),
			VIRT_TO_PHYS(PAGE_ALIGN_DOWN(ctx->module_base)),
			PAGE_ALIGN_UP((uint64_t)(uintptr_t)ctx->module_base + total) -
				PAGE_ALIGN_DOWN(ctx->module_base),
			PAGE_PRESENT | PAGE_WRITE | PAGE_GLOBAL) != 0)
		return -1;

	for (uint16_t i = 0; i < ctx->ehdr->e_shnum; i++) {
		const elf64_shdr_t *section = &ctx->sections[i];

		if ((section->sh_flags & SHF_ALLOC) == 0)
			continue;

		ctx->section_bases[i] =
			(uint8_t *)ctx->module_base + ctx->section_offsets[i];
		if (section->sh_type != SHT_NOBITS && section->sh_size != 0) {
			memcpy(ctx->section_bases[i], ctx->image + section->sh_offset,
				   section->sh_size);
		}
	}

	return 0;
}

static const char *sdi_elf_symbol_name(const sdi_elf_context_t *ctx,
									   const elf64_shdr_t *symtab,
									   const elf64_sym_t *symbol)
{
	const elf64_shdr_t *strtab;

	if (symtab->sh_link >= ctx->ehdr->e_shnum)
		return NULL;

	strtab = &ctx->sections[symtab->sh_link];
	if (strtab->sh_type != SHT_STRTAB || symbol->st_name >= strtab->sh_size ||
		!sdi_elf_range_ok(ctx->image_size, strtab->sh_offset, strtab->sh_size))
		return NULL;

	return (const char *)ctx->image + strtab->sh_offset + symbol->st_name;
}

static int sdi_elf_symbol_value(const sdi_elf_context_t *ctx,
								const elf64_shdr_t *symtab,
								const elf64_sym_t *symbol, uint64_t *out_value)
{
	const char *name;

	if (symbol->st_shndx == SHN_ABS) {
		*out_value = symbol->st_value;
		return 0;
	}

	if (symbol->st_shndx == SHN_UNDEF) {
		name = sdi_elf_symbol_name(ctx, symtab, symbol);
		if (name && sdi_elf_resolve_export(name, out_value))
			return 0;

		klog("unresolved driver symbol '%s'", name ? name : "<bad>");
		return -1;
	}

	if (symbol->st_shndx >= ctx->ehdr->e_shnum ||
		!ctx->section_bases[symbol->st_shndx]) {
		name = sdi_elf_symbol_name(ctx, symtab, symbol);
		klog("symbol '%s' refers to a non-loaded section",
			 name ? name : "<bad>");
		return -1;
	}

	*out_value = (uint64_t)(uintptr_t)ctx->section_bases[symbol->st_shndx] +
				 symbol->st_value;
	return 0;
}

static bool sdi_elf_fits_i32(int64_t value)
{
	return value >= INT32_MIN && value <= INT32_MAX;
}

static int sdi_elf_apply_relocation(uint8_t *target, uint32_t type,
									uint64_t symbol_value, int64_t addend,
									uint64_t place)
{
	uint64_t value64;
	int64_t relative;

	switch (type) {
	case R_X86_64_NONE:
		return 0;
	case R_X86_64_64:
		value64 = symbol_value + (uint64_t)addend;
		memcpy(target, &value64, sizeof(value64));
		return 0;
	case R_X86_64_PC32:
	case R_X86_64_PLT32:
		relative = (int64_t)(symbol_value + (uint64_t)addend - place);
		if (!sdi_elf_fits_i32(relative))
			return -1;
		{
			int32_t value32 = (int32_t)relative;
			memcpy(target, &value32, sizeof(value32));
		}
		return 0;
	case R_X86_64_32:
		value64 = symbol_value + (uint64_t)addend;
		if (value64 > UINT32_MAX)
			return -1;
		{
			uint32_t value32 = (uint32_t)value64;
			memcpy(target, &value32, sizeof(value32));
		}
		return 0;
	case R_X86_64_32S:
		relative = (int64_t)(symbol_value + (uint64_t)addend);
		if (!sdi_elf_fits_i32(relative))
			return -1;
		{
			int32_t value32 = (int32_t)relative;
			memcpy(target, &value32, sizeof(value32));
		}
		return 0;
	default:
		klog("unsupported relocation type %u", type);
		return -1;
	}
}

static int sdi_elf_apply_relocations(sdi_elf_context_t *ctx)
{
	for (uint16_t i = 0; i < ctx->ehdr->e_shnum; i++) {
		const elf64_shdr_t *relsec = &ctx->sections[i];
		const elf64_shdr_t *symtab;
		const elf64_sym_t *symbols;
		const elf64_rela_t *relocs;
		uint64_t count;

		if (relsec->sh_type != SHT_RELA)
			continue;
		if (relsec->sh_link >= ctx->ehdr->e_shnum ||
			relsec->sh_info >= ctx->ehdr->e_shnum)
			return -1;
		if (relsec->sh_entsize != sizeof(elf64_rela_t) ||
			(relsec->sh_size % sizeof(elf64_rela_t)) != 0)
			return -1;
		if (!ctx->section_bases[relsec->sh_info])
			continue;
		if (!sdi_elf_range_ok(ctx->image_size, relsec->sh_offset,
							  relsec->sh_size))
			return -1;

		symtab = &ctx->sections[relsec->sh_link];
		if (symtab->sh_type != SHT_SYMTAB ||
			symtab->sh_entsize != sizeof(elf64_sym_t) ||
			!sdi_elf_range_ok(ctx->image_size, symtab->sh_offset,
							  symtab->sh_size))
			return -1;

		symbols =
			(const elf64_sym_t *)(const void *)(ctx->image + symtab->sh_offset);
		relocs = (const elf64_rela_t *)(const void *)(ctx->image +
													  relsec->sh_offset);
		count = relsec->sh_size / sizeof(elf64_rela_t);

		for (uint64_t r = 0; r < count; r++) {
			const elf64_rela_t *reloc = &relocs[r];
			uint32_t sym_index = ELF64_R_SYM(reloc->r_info);
			uint32_t type = ELF64_R_TYPE(reloc->r_info);
			uint8_t *target;
			uint64_t symbol_value;
			uint64_t place;

			if (sym_index >= symtab->sh_size / sizeof(elf64_sym_t))
				return -1;
			if (reloc->r_offset >= ctx->sections[relsec->sh_info].sh_size)
				return -1;

			target = (uint8_t *)ctx->section_bases[relsec->sh_info] +
					 reloc->r_offset;
			place = (uint64_t)(uintptr_t)target;
			if (sdi_elf_symbol_value(ctx, symtab, &symbols[sym_index],
									 &symbol_value) != 0)
				return -1;
			if (sdi_elf_apply_relocation(target, type, symbol_value,
										 reloc->r_addend, place) != 0)
				return -1;
		}
	}

	return 0;
}

static int sdi_elf_find_loaded_symbol(const sdi_elf_context_t *ctx,
									  const char *name, uint64_t *out_value)
{
	for (uint16_t i = 0; i < ctx->ehdr->e_shnum; i++) {
		const elf64_shdr_t *symtab = &ctx->sections[i];
		const elf64_sym_t *symbols;
		uint64_t count;

		if (symtab->sh_type != SHT_SYMTAB)
			continue;
		if (symtab->sh_entsize != sizeof(elf64_sym_t) ||
			!sdi_elf_range_ok(ctx->image_size, symtab->sh_offset,
							  symtab->sh_size))
			return -1;

		symbols =
			(const elf64_sym_t *)(const void *)(ctx->image + symtab->sh_offset);
		count = symtab->sh_size / sizeof(elf64_sym_t);
		for (uint64_t s = 0; s < count; s++) {
			const char *symbol_name =
				sdi_elf_symbol_name(ctx, symtab, &symbols[s]);

			if (!symbol_name || !sdi_elf_string_equal(symbol_name, name))
				continue;

			return sdi_elf_symbol_value(ctx, symtab, &symbols[s], out_value);
		}
	}

	return -1;
}

static int sdi_load_driver_image(const void *image, size_t image_size,
								 const char *path)
{
	sdi_elf_context_t ctx;
	uint64_t driver_symbol;

	if (!sdi_elf_validate_header(image, image_size)) {
		klog("'%s' is not a supported ELF64 relocatable SDI driver", path);
		return -1;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.image = image;
	ctx.image_size = image_size;
	ctx.ehdr = image;
	ctx.sections =
		(const elf64_shdr_t *)(const void *)(ctx.image + ctx.ehdr->e_shoff);

	if (sdi_elf_prepare_sections(&ctx) != 0) {
		klog("failed to prepare sections for '%s'", path);
		return -1;
	}
	if (sdi_elf_apply_relocations(&ctx) != 0) {
		klog("failed to relocate '%s'", path);
		return -1;
	}
	if (sdi_elf_find_loaded_symbol(&ctx, "sdi_driver", &driver_symbol) != 0) {
		klog("'%s' does not export sdi_driver", path);
		return -1;
	}

	return sdi_kernel_register_loaded_driver(
		(const sdi_driver_desc_t *)(uintptr_t)driver_symbol, path);
}

static int sdi_read_file(const char *path, void **out_buffer, size_t *out_size)
{
	vnode_t *node;
	uint8_t chunk[1024];
	void *buffer = NULL;
	size_t capacity = 0;
	size_t size = 0;
	size_t offset = 0;
	int rc;

	if (!path || !out_buffer || !out_size)
		return -1;

	rc = vfs_open(vfsroot, (char *)path, O_RDONLY, &node);
	if (rc != 0)
		return -1;

	for (;;) {
		size_t got = 0;

		rc = vfs_read(node, chunk, sizeof(chunk), offset, &got, O_RDONLY);
		if (rc != 0) {
			vfs_close(node, O_RDONLY);
			kfree(buffer);
			return -1;
		}
		if (got == 0)
			break;

		if (size + got > capacity) {
			size_t new_capacity = capacity ? capacity * 2 : 4096;
			void *new_buffer;

			while (new_capacity < size + got)
				new_capacity *= 2;
			new_buffer = krealloc(buffer, new_capacity);
			if (!new_buffer) {
				vfs_close(node, O_RDONLY);
				kfree(buffer);
				return -1;
			}
			buffer = new_buffer;
			capacity = new_capacity;
		}

		memcpy((uint8_t *)buffer + size, chunk, got);
		size += got;
		offset += got;
	}

	vfs_close(node, O_RDONLY);
	*out_buffer = buffer;
	*out_size = size;
	return 0;
}

static void sdi_join_path(char *buffer, size_t buffer_size,
						  const char *directory, const char *name)
{
	size_t used;

	if (buffer_size == 0)
		return;

	strlcpy(buffer, directory ? directory : "", buffer_size);
	used = strlen(buffer);
	if (used != 0 && used + 1 < buffer_size && buffer[used - 1] != '/') {
		buffer[used++] = '/';
		buffer[used] = '\0';
	}
	if (used < buffer_size)
		strlcpy(buffer + used, name ? name : "", buffer_size - used);
}

int sdi_load_drivers(const char *directory)
{
	vnode_t *dir;
	dirent_t entries[8];
	size_t offset = 0;
	size_t loaded = 0;
	int rc;

	if (!directory)
		return -1;

	rc = vfs_open(vfsroot, (char *)directory, O_RDONLY | O_DIRECTORY, &dir);
	if (rc != 0) {
		ktrace("driver directory '%s' not present", directory);
		return 0;
	}

	for (;;) {
		size_t bytes = 0;
		size_t count;

		rc = vfs_getdents(dir, entries, sizeof(entries), offset, &bytes);
		if (rc != 0) {
			vfs_close(dir, O_RDONLY | O_DIRECTORY);
			return -1;
		}
		if (bytes == 0)
			break;

		count = bytes / sizeof(dirent_t);
		for (size_t i = 0; i < count; i++) {
			char path[PATH_MAX];
			void *image;
			size_t image_size;

			offset++;
			if (entries[i].d_type != DT_REG ||
				!sdi_elf_string_ends_with(entries[i].d_name, ".drv"))
				continue;

			sdi_join_path(path, sizeof(path), directory, entries[i].d_name);
			if (sdi_read_file(path, &image, &image_size) != 0) {
				klog("failed to read SDI driver '%s'", path);
				continue;
			}
			if (image_size == 0) {
				kfree(image);
				continue;
			}

			if (sdi_load_driver_image(image, image_size, path) == 0)
				loaded++;
			kfree(image);
		}
	}

	vfs_close(dir, O_RDONLY | O_DIRECTORY);
	ktrace("loaded %zu SDI driver(s) from '%s'", loaded, directory);
	return 0;
}
