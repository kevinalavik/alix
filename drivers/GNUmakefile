.SUFFIXES:

DRIVER_CC ?= cc
DRIVER_MAINS := $(shell find . -mindepth 2 -maxdepth 2 -type f -name main.c 2>/dev/null | LC_ALL=C sort)
DRIVER_DIRS := $(patsubst %/,%,$(patsubst ./%,%,$(dir $(DRIVER_MAINS))))
DRIVER_OUTPUTS := $(addprefix bin/,$(addsuffix .drv,$(DRIVER_DIRS)))
DRIVER_OBJDIR := bin/obj

DRIVER_CFLAGS := \
	-g \
	-O2 \
	-pipe \
	-Wall \
	-Wextra \
	-std=c99 \
	-ffreestanding \
	-fno-builtin \
	-nostdinc \
	-fno-stack-protector \
	-fno-stack-check \
	-fno-lto \
	-fno-PIC \
	-fno-pie \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables \
	-m64 \
	-march=x86-64 \
	-mabi=sysv \
	-mno-80387 \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-mno-red-zone \
	-mcmodel=large

DRIVER_CPPFLAGS := \
	-I ../kernel/include \
	-isystem ../kernel/freestnd-c-hdrs/include

DRIVER_DEPFLAGS := -MMD -MP
DRIVER_LDFLAGS := -nostdlib -r

.PHONY: all
all: $(DRIVER_OUTPUTS)

.PHONY: print-outputs
print-outputs:
	@$(if $(DRIVER_OUTPUTS),printf '%s\n' $(addprefix drivers/,$(DRIVER_OUTPUTS)),:)

define DRIVER_template
DRIVER_SRCS_$1 := $$(shell find $1 -type f -name '*.c' 2>/dev/null | LC_ALL=C sort)
DRIVER_OBJS_$1 := $$(patsubst %.c,$$(DRIVER_OBJDIR)/%.o,$$(DRIVER_SRCS_$1))
DRIVER_DEPS += $$(DRIVER_OBJS_$1:.o=.d)

bin/$1.drv: $$(DRIVER_OBJS_$1)
	mkdir -p "$$(dir $$@)"
	$$(DRIVER_CC) $$(DRIVER_LDFLAGS) $$^ -o $$@
endef

$(foreach driver,$(DRIVER_DIRS),$(eval $(call DRIVER_template,$(driver))))

$(DRIVER_OBJDIR)/%.o: %.c ../kernel/include/sdi.h
	mkdir -p "$(dir $@)"
	$(DRIVER_CC) $(DRIVER_CFLAGS) $(DRIVER_CPPFLAGS) $(DRIVER_DEPFLAGS) -c $< -o $@

-include $(DRIVER_DEPS)

.PHONY: clean
clean:
	rm -rf bin
