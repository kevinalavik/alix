#include <sys/klog.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <api/time.h>
#include <lib/kprintf.h>
#include <lib/string.h>

static struct klog_record klog_ring[KLOG_RING_SIZE];

static size_t klog_head;
static size_t klog_count;
static uint64_t klog_next_seq;
static bool klog_ready;

static void klog_emit(const struct klog_record *rec)
{
	kprintf("[%llu.%03llu] %s: %s\n",
			(unsigned long long)(rec->time_us / 1000000ULL),
			(unsigned long long)((rec->time_us / 1000ULL) % 1000ULL), rec->ns,
			rec->msg);
}

static void klog_ring_push(uint64_t time_us, const char *ns, const char *msg)
{
	size_t slot = klog_head;

	klog_ring[slot].seq = klog_next_seq++;
	klog_ring[slot].time_us = time_us;

	strlcpy(klog_ring[slot].ns, ns, sizeof(klog_ring[slot].ns));
	strlcpy(klog_ring[slot].msg, msg, sizeof(klog_ring[slot].msg));

	klog_head = (klog_head + 1) % KLOG_RING_SIZE;

	if (klog_count < KLOG_RING_SIZE)
		klog_count++;
}

void klog_init(void)
{
	klog_head = 0;
	klog_count = 0;
	klog_next_seq = 0;
	klog_ready = true;
}

void kvlog_write(const char *ns, const char *fmt, va_list ap)
{
	struct klog_record rec;

	if (ns == NULL || ns[0] == '\0')
		ns = "kernel";

	rec.seq = klog_next_seq;
	rec.time_us = time_uptime_us();

	strlcpy(rec.ns, ns, sizeof(rec.ns));
	kvsnprintf(rec.msg, sizeof(rec.msg), fmt, ap);

	if (klog_ready)
		klog_ring_push(rec.time_us, rec.ns, rec.msg);

	klog_emit(&rec);
}

void klog_write(const char *ns, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	kvlog_write(ns, fmt, ap);
	va_end(ap);
}