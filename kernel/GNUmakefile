.SUFFIXES:
override OUTPUT := alix
override VERSION := $(shell \
    git -C "$(dir $(abspath $(lastword $(MAKEFILE_LIST))))" rev-parse --short HEAD 2>/dev/null \
    || echo unknown)$(shell \
    git -C "$(dir $(abspath $(lastword $(MAKEFILE_LIST))))" \
    diff --quiet --ignore-submodules HEAD 2>/dev/null \
    && echo -clean || echo -dirty)


PREFIX := /usr/local

TOOLCHAIN :=
TOOLCHAIN_PREFIX :=
ifneq ($(TOOLCHAIN),)
    ifeq ($(TOOLCHAIN_PREFIX),)
        TOOLCHAIN_PREFIX := $(TOOLCHAIN)-
    endif
endif

ifneq ($(TOOLCHAIN_PREFIX),)
    CC := $(TOOLCHAIN_PREFIX)gcc
else
    CC := cc
endif

LD := $(TOOLCHAIN_PREFIX)ld
PYTHON ?= python3

ifeq ($(TOOLCHAIN),llvm)
    CC := clang
    LD := ld.lld
endif

CFLAGS := -g -O2 -pipe
CPPFLAGS := -DALIX_VERSION=\"$(VERSION)\"
NASMFLAGS := -g
LDFLAGS :=

ifneq ($(filter-out clean distclean,$(or $(MAKECMDGOALS),default)),)
    ifeq ($(wildcard .deps-obtained),)
        $(error Please run the ./build/get-deps script first)
    endif
endif

override CC_IS_CLANG := $(shell ! $(CC) --version 2>/dev/null | grep -q '^Target: '; echo $$?)
ifeq ($(CC_IS_CLANG),1)
    override CC += \
        -target x86_64-unknown-none-elf
endif

override CFLAGS += \
    -Wall \
    -Wextra \
    -std=c99 \
    -ffreestanding \
    -fno-omit-frame-pointer \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-PIC \
    -ffunction-sections \
    -fdata-sections \
    -m64 \
    -march=x86-64 \
    -mabi=sysv \
    -mno-80387 \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mno-red-zone \
    -mcmodel=kernel

override CPPFLAGS := \
    -I include \
    -I src \
    -I limine-protocol/include \
    -I flanterm/src \
    -isystem freestnd-c-hdrs/include \
    $(CPPFLAGS) \
    -MMD \
    -MP

override NASMFLAGS := \
    -f elf64 \
    $(patsubst -g,-g -F dwarf,$(NASMFLAGS)) \
    -Wall

override LDFLAGS += \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -z max-page-size=0x1000 \
    --gc-sections \
    -T build/linker.ld

override SRCFILES := $(shell find -L src cc-runtime/src flanterm/src -type f 2>/dev/null | LC_ALL=C sort)
override CFILES := $(filter %.c,$(SRCFILES))
override ASFILES := $(filter %.S,$(SRCFILES))
override NASMFILES := $(filter %.asm,$(SRCFILES))
override OBJ := $(addprefix obj/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) $(NASMFILES:.asm=.asm.o))
override HEADER_DEPS := $(addprefix obj/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))
override PRELINK := bin/$(OUTPUT).prelink
override KALLSYMS_SRC := obj/generated/kallsyms.c
override KALLSYMS_OBJ := $(KALLSYMS_SRC).o
override CONFIG_HEADER := obj/generated/autoconf.h
override KCONFIGLIB := kconfiglib/kconfiglib.py
override KCONFIG_MENU := kconfiglib/menuconfig.py
override KCONFIG_ENV := PYTHONPATH="$(abspath kconfiglib)" KCONFIG_CONFIG=".config" srctree="$(CURDIR)"

.PHONY: all
all: bin/$(OUTPUT)

-include $(HEADER_DEPS)

bin/$(OUTPUT): GNUmakefile build/linker.ld $(OBJ) $(KALLSYMS_OBJ)
	mkdir -p "$(dir $@)"
	$(LD) $(LDFLAGS) $(OBJ) $(KALLSYMS_OBJ) -o $@

$(PRELINK): GNUmakefile build/linker.ld $(OBJ)
	mkdir -p "$(dir $@)"
	$(LD) $(LDFLAGS) --defsym=kallsyms=0 --defsym=kallsyms_count=0 $(OBJ) -o $@

$(KALLSYMS_SRC): $(PRELINK) build/gen-kallsyms.sh
	mkdir -p "$(dir $@)"
	sh build/gen-kallsyms.sh "$(PRELINK)" > "$@"

$(KALLSYMS_OBJ): $(KALLSYMS_SRC) GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -include "$(CONFIG_HEADER)" -c $< -o $@

$(KCONFIGLIB) $(KCONFIG_MENU):
	@echo "error: Kconfiglib is missing; run ./build/get-deps from kernel/ or make menuconfig from the repository root" >&2
	@false

$(CONFIG_HEADER): Kconfig .config build/kconfig.py $(KCONFIGLIB)
	$(KCONFIG_ENV) $(PYTHON) build/kconfig.py sync Kconfig .config "$(CONFIG_HEADER)"

.config: Kconfig build/kconfig.py $(KCONFIGLIB)
	$(KCONFIG_ENV) $(PYTHON) build/kconfig.py defconfig Kconfig .config "$(CONFIG_HEADER)"

$(OBJ): $(CONFIG_HEADER)

obj/%.c.o: %.c GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -include "$(CONFIG_HEADER)" -c $< -o $@

obj/%.S.o: %.S GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -include "$(CONFIG_HEADER)" -c $< -o $@

obj/%.asm.o: %.asm GNUmakefile
	mkdir -p "$(dir $@)"
	nasm $(NASMFLAGS) $< -o $@

.PHONY: menuconfig
menuconfig: $(KCONFIG_MENU)
	$(KCONFIG_ENV) $(PYTHON) "$(KCONFIG_MENU)" Kconfig
	$(KCONFIG_ENV) $(PYTHON) build/kconfig.py sync Kconfig .config "$(CONFIG_HEADER)"

.PHONY: defconfig
defconfig: $(KCONFIGLIB)
	$(KCONFIG_ENV) $(PYTHON) build/kconfig.py defconfig Kconfig .config "$(CONFIG_HEADER)"

.PHONY: clean
clean:
	rm -rf bin obj

.PHONY: distclean
distclean: clean
	rm -rf .deps-obtained .cache compile_commands.json freestnd-c-hdrs cc-runtime limine-protocol kconfiglib

.PHONY: install
install: all
	install -d "$(DESTDIR)$(PREFIX)/share/$(OUTPUT)"
	install -m 644 bin/$(OUTPUT) "$(DESTDIR)$(PREFIX)/share/$(OUTPUT)/"

.PHONY: uninstall
uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/share/$(OUTPUT)/$(OUTPUT)"
	-rmdir "$(DESTDIR)$(PREFIX)/share/$(OUTPUT)"
