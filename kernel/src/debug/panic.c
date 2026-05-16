#include <debug/panic.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cpu/instr.h>
#include <debug/kallsyms.h>
#include <log/klog.h>
#include <lib/kprintf.h>

struct panic_frame {
	uint64_t next_rbp;
	uint64_t ret_addr;
};

static const size_t panic_backtrace_limit = 16;

static void panic_log_addr(const char *label, uint64_t addr)
{
	uint64_t offset = 0;
	const struct kallsyms_entry *sym = kallsyms_lookup(addr, &offset);

	if (sym != NULL && sym->name != NULL) {
		if (sym->size != 0) {
			klog_ns("panic", "%s[<%016llx>] %s+0x%llx/0x%llx", label,
					(unsigned long long)addr, sym->name,
					(unsigned long long)offset, (unsigned long long)sym->size);
		} else {
			klog_ns("panic", "%s[<%016llx>] %s+0x%llx", label,
					(unsigned long long)addr, sym->name,
					(unsigned long long)offset);
		}
		return;
	}

	klog_ns("panic", "%s[<%016llx>]", label, (unsigned long long)addr);
}

static void panic_log_trace(size_t depth, uint64_t addr)
{
	uint64_t offset = 0;
	const struct kallsyms_entry *sym = kallsyms_lookup(addr, &offset);

	if (sym != NULL && sym->name != NULL) {
		if (sym->size != 0) {
			klog_ns("panic", "  #%zu [<%016llx>] %s+0x%llx/0x%llx", depth,
					(unsigned long long)addr, sym->name,
					(unsigned long long)offset, (unsigned long long)sym->size);
		} else {
			klog_ns("panic", "  #%zu [<%016llx>] %s+0x%llx", depth,
					(unsigned long long)addr, sym->name,
					(unsigned long long)offset);
		}
		return;
	}

	klog_ns("panic", "  #%zu [<%016llx>]", depth, (unsigned long long)addr);
}

static void panic_log_message(const char *fmt, va_list ap)
{
	char msg[KLOG_MSG_MAX];

	kvsnprintf(msg, sizeof(msg), fmt, ap);
	klog_ns("panic", "Kernel panic: %s", msg);
}

static bool panic_frame_is_sane(const struct panic_frame *frame,
								const struct panic_frame *prev)
{
	uint64_t cur = (uint64_t)(uintptr_t)frame;
	uint64_t next = frame->next_rbp;

	if (frame == NULL)
		return false;

	if ((cur & 0xfULL) != 0)
		return false;

	if (next <= cur)
		return false;

	if (prev != NULL && next <= (uint64_t)(uintptr_t)prev)
		return false;

	if (next == 0 || (next & 0xfULL) != 0)
		return false;

	return true;
}

static void panic_dump_backtrace(interrupt_frame_t *regs)
{
	klog_ns("panic", "Call Trace:");

	if (regs == NULL)
		return;

	/* Entry #0 is the RIP at the point of the fault */
	panic_log_trace(0, regs->rip);

	/* Walk the rbp chain from the interrupted context upward */
	const struct panic_frame *frame =
		(const struct panic_frame *)(uintptr_t)regs->rbp;
	const struct panic_frame *prev = NULL;

	for (size_t depth = 1; frame != NULL && depth < panic_backtrace_limit;
		 depth++) {
		if (!panic_frame_is_sane(frame, prev))
			break;

		if (frame->ret_addr == 0)
			break;

		panic_log_trace(depth, frame->ret_addr);

		prev = frame;
		frame = (const struct panic_frame *)(uintptr_t)frame->next_rbp;
	}
}

static void panic_dump_regs(interrupt_frame_t *regs)
{
	if (regs == NULL)
		return;

	klog_ns("panic", "RAX: %016llx  RBX: %016llx  RCX: %016llx  RDX: %016llx",
			(unsigned long long)regs->rax, (unsigned long long)regs->rbx,
			(unsigned long long)regs->rcx, (unsigned long long)regs->rdx);
	klog_ns("panic", "RSI: %016llx  RDI: %016llx  RBP: %016llx  RSP: %016llx",
			(unsigned long long)regs->rsi, (unsigned long long)regs->rdi,
			(unsigned long long)regs->rbp, (unsigned long long)regs->rsp);
	klog_ns("panic", "R8:  %016llx  R9:  %016llx  R10: %016llx  R11: %016llx",
			(unsigned long long)regs->r8, (unsigned long long)regs->r9,
			(unsigned long long)regs->r10, (unsigned long long)regs->r11);
	klog_ns("panic", "R12: %016llx  R13: %016llx  R14: %016llx  R15: %016llx",
			(unsigned long long)regs->r12, (unsigned long long)regs->r13,
			(unsigned long long)regs->r14, (unsigned long long)regs->r15);
	klog_ns("panic", "CS:  %016llx  SS:  %016llx  DS:  %016llx  ES:  %016llx",
			(unsigned long long)regs->cs, (unsigned long long)regs->ss,
			(unsigned long long)regs->ds, (unsigned long long)regs->es);
	klog_ns("panic", "CR0: %016llx  CR2: %016llx  CR3: %016llx  CR4: %016llx",
			(unsigned long long)regs->cr0, (unsigned long long)regs->cr2,
			(unsigned long long)regs->cr3, (unsigned long long)regs->cr4);
	klog_ns("panic", "RFLAGS: %016llx  ERR: %016llx  VEC: %llu",
			(unsigned long long)regs->rflags, (unsigned long long)regs->err,
			(unsigned long long)regs->vector);
	panic_log_addr("RIP: ", regs->rip);
	klog_ns("panic", "CPU: BSP");
}

void vkpanic(interrupt_frame_t *regs, const char *fmt, va_list ap)
{
	cli();

	if (regs == NULL)
		regs = __builtin_frame_address(0);

	panic_log_message(fmt, ap);
	panic_dump_regs(regs);
	panic_dump_backtrace(regs);
	klog_ns("panic", "---[ end Kernel panic ]---");

	cli();
	for (;;)
		hlt();
}

void kpanic(interrupt_frame_t *regs, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (regs == NULL)
		regs = __builtin_frame_address(0);
	vkpanic(regs, fmt, ap);
	va_end(ap);
}