#include <debug/panic.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cpu/instr.h>
#include <cpu/smp.h>
#include <debug/kallsyms.h>
#include <log/klog.h>
#include <lib/kprintf.h>
#include <lib/spinlock.h>

struct panic_frame {
	uint64_t next_rbp;
	uint64_t ret_addr;
};

static const size_t panic_backtrace_limit = 16;
static spinlock_t panic_lock = SPINLOCK_INIT;

static void panic_log_addr(const char *label, uint64_t addr)
{
	uint64_t offset = 0;
	const struct kallsyms_entry *sym = kallsyms_lookup(addr, &offset);

	if (sym != NULL && sym->name != NULL) {
		if (sym->size != 0) {
			klog_ns_force("panic", "%s[<%016llx>] %s+0x%llx/0x%llx", label,
						  (unsigned long long)addr, sym->name,
						  (unsigned long long)offset,
						  (unsigned long long)sym->size);
		} else {
			klog_ns_force("panic", "%s[<%016llx>] %s+0x%llx", label,
						  (unsigned long long)addr, sym->name,
						  (unsigned long long)offset);
		}
		return;
	}

	klog_ns_force("panic", "%s[<%016llx>]", label, (unsigned long long)addr);
}

static void panic_log_trace(size_t depth, uint64_t addr)
{
	uint64_t offset = 0;
	const struct kallsyms_entry *sym = kallsyms_lookup(addr, &offset);

	if (sym != NULL && sym->name != NULL) {
		if (sym->size != 0) {
			klog_ns_force("panic", "  #%zu [<%016llx>] %s+0x%llx/0x%llx", depth,
						  (unsigned long long)addr, sym->name,
						  (unsigned long long)offset,
						  (unsigned long long)sym->size);
		} else {
			klog_ns_force("panic", "  #%zu [<%016llx>] %s+0x%llx", depth,
						  (unsigned long long)addr, sym->name,
						  (unsigned long long)offset);
		}
		return;
	}

	klog_ns_force("panic", "  #%zu [<%016llx>]", depth,
				  (unsigned long long)addr);
}

static void panic_log_message(const char *fmt, va_list ap)
{
	char msg[KLOG_MSG_MAX];

	kvsnprintf(msg, sizeof(msg), fmt, ap);
	klog_ns_force("panic", "Kernel panic: %s", msg);
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
	klog_ns_force("panic", "Call Trace:");

	if (regs == NULL)
		return;

	panic_log_trace(0, regs->rip);

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

	klog_ns_force("panic",
				  "RAX: %016llx  RBX: %016llx  RCX: %016llx  RDX: %016llx",
				  (unsigned long long)regs->rax, (unsigned long long)regs->rbx,
				  (unsigned long long)regs->rcx, (unsigned long long)regs->rdx);
	klog_ns_force("panic",
				  "RSI: %016llx  RDI: %016llx  RBP: %016llx  RSP: %016llx",
				  (unsigned long long)regs->rsi, (unsigned long long)regs->rdi,
				  (unsigned long long)regs->rbp, (unsigned long long)regs->rsp);
	klog_ns_force("panic",
				  "R8:  %016llx  R9:  %016llx  R10: %016llx  R11: %016llx",
				  (unsigned long long)regs->r8, (unsigned long long)regs->r9,
				  (unsigned long long)regs->r10, (unsigned long long)regs->r11);
	klog_ns_force("panic",
				  "R12: %016llx  R13: %016llx  R14: %016llx  R15: %016llx",
				  (unsigned long long)regs->r12, (unsigned long long)regs->r13,
				  (unsigned long long)regs->r14, (unsigned long long)regs->r15);
	klog_ns_force("panic",
				  "CS:  %016llx  SS:  %016llx  DS:  %016llx  ES:  %016llx",
				  (unsigned long long)regs->cs, (unsigned long long)regs->ss,
				  (unsigned long long)regs->ds, (unsigned long long)regs->es);
	klog_ns_force("panic",
				  "CR0: %016llx  CR2: %016llx  CR3: %016llx  CR4: %016llx",
				  (unsigned long long)regs->cr0, (unsigned long long)regs->cr2,
				  (unsigned long long)regs->cr3, (unsigned long long)regs->cr4);
	klog_ns_force("panic", "RFLAGS: %016llx  ERR: %016llx  VEC: %llu",
				  (unsigned long long)regs->rflags,
				  (unsigned long long)regs->err,
				  (unsigned long long)regs->vector);
	panic_log_addr("RIP: ", regs->rip);
}

static void panic_dump_cpu(void)
{
	struct cpu_info *cpu = cpu_current();

	if (cpu == NULL) {
		klog_ns_force("panic", "CPU: unknown");
		return;
	}

	klog_ns_force("panic", "CPU: cpu%u lapic=%u acpi=%u%s", cpu->index,
				  cpu->lapic_id, cpu->processor_id,
				  cpu->is_bsp ? " (bsp)" : "");
}

void vkpanic(interrupt_frame_t *regs, const char *fmt, va_list ap)
{
	cli();
	spinlock_lock(&panic_lock);

	panic_log_message(fmt, ap);
	panic_dump_cpu();
	panic_dump_regs(regs);
	panic_dump_backtrace(regs);
	klog_ns_force("panic", "---[ end Kernel panic ]---");

	cli();
	for (;;)
		hlt();
}

/*
 * regs != NULL path: a real interrupt frame was passed in, so we just
 * forward the variadic args to vkpanic normally. The naked trampoline
 * below calls this directly after the testq/jnz check.
 */
void kpanic_with_regs(interrupt_frame_t *regs, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vkpanic(regs, fmt, ap);
	va_end(ap);
}

/*
 * kpanic — public entry point.
 *
 * regs != NULL: tail-call kpanic_with_regs, which sets up va_list properly.
 * regs == NULL: capture the full register state onto the stack as an
 *               interrupt_frame_t and call vkpanic directly.
 *
 * Stack at entry (before any pushes):
 *   rsp+0  = retaddr
 *   rdi    = regs (NULL here)
 *   rsi    = fmt
 *   rdx/rcx/r8/r9 = first four variadic args
 *
 * After pushq rax (spill):        rsp+0=rax_spill, rsp+8=retaddr
 * After pushq ss:                 rsp+0=ss, rsp+8=rax_spill, rsp+16=retaddr
 *   caller rsp = retaddr+8 = rsp+24
 * After pushq caller_rsp:         rsp+0=caller_rsp, ..., rsp+24=retaddr
 * After pushfq:                   rsp+0=rflags, ..., rsp+32=retaddr
 * After pushq cs:                 rsp+0=cs, ..., rsp+40=retaddr -> rip
 *
 * rax recovery: 21 pushes * 8 = 168 bytes above rsp at that point.
 */
__attribute__((naked)) void kpanic(maybe_unused interrupt_frame_t *regs,
								   maybe_unused const char *fmt, ...)
{
	__asm__ volatile("testq  %%rdi, %%rdi\n\t"
					 "jnz    1f\n\t"

					 "pushq  %%rax\n\t"

					 "xorq   %%rax, %%rax\n\t"
					 "movw   %%ss, %%ax\n\t"
					 "pushq  %%rax\n\t"

					 "leaq   24(%%rsp), %%rax\n\t"
					 "pushq  %%rax\n\t"

					 "pushfq\n\t"

					 "xorq   %%rax, %%rax\n\t"
					 "movw   %%cs, %%ax\n\t"
					 "pushq  %%rax\n\t"

					 "movq   40(%%rsp), %%rax\n\t"
					 "pushq  %%rax\n\t"

					 "pushq  $0\n\t"
					 "pushq  $0\n\t"

					 "pushq  %%r15\n\t"
					 "pushq  %%r14\n\t"
					 "pushq  %%r13\n\t"
					 "pushq  %%r12\n\t"
					 "pushq  %%r11\n\t"
					 "pushq  %%r10\n\t"
					 "pushq  %%r9\n\t"
					 "pushq  %%r8\n\t"

					 "pushq  %%rsi\n\t"
					 "pushq  %%rdi\n\t"
					 "pushq  %%rbp\n\t"
					 "pushq  %%rdx\n\t"
					 "pushq  %%rcx\n\t"
					 "pushq  %%rbx\n\t"

					 "movq   168(%%rsp), %%rax\n\t"
					 "pushq  %%rax\n\t"

					 "movq   %%cr4, %%rax\n\t"
					 "pushq  %%rax\n\t"
					 "movq   %%cr3, %%rax\n\t"
					 "pushq  %%rax\n\t"
					 "movq   %%cr2, %%rax\n\t"
					 "pushq  %%rax\n\t"
					 "movq   %%cr0, %%rax\n\t"
					 "pushq  %%rax\n\t"

					 "xorq   %%rax, %%rax\n\t"
					 "movw   %%ds, %%ax\n\t"
					 "pushq  %%rax\n\t"
					 "xorq   %%rax, %%rax\n\t"
					 "movw   %%es, %%ax\n\t"
					 "pushq  %%rax\n\t"

					 "movq   %%rsp, %%rdi\n\t"
					 "call   kpanic_with_regs\n\t"

					 "1:\n\t"
					 "jmp    kpanic_with_regs\n\t"
					 :
					 :
					 :);
}
