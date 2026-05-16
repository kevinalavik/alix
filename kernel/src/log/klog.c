#include <log/klog.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include <api/time.h>
#include <dev/uart.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <core/alix.h>

static struct klog_record klog_ring[KLOG_RING_SIZE];

static size_t klog_head;
static size_t klog_count;
static uint64_t klog_next_seq;
static bool klog_ready;

static void klog_buf_putc(char *buf, size_t bufsz, size_t *pos, char c)
{
	if (*pos + 1 < bufsz)
		buf[*pos] = c;

	(*pos)++;
}

static void klog_buf_puts_crlf(char *buf, size_t bufsz, size_t *pos,
							   const char *s)
{
	bool prev_was_cr = false;

	if (s == NULL)
		s = "(null)";

	while (*s != '\0') {
		char c = *s++;

		if (c == '\n' && !prev_was_cr)
			klog_buf_putc(buf, bufsz, pos, '\r');

		klog_buf_putc(buf, bufsz, pos, c);
		prev_was_cr = (c == '\r');
	}
}

static void klog_emit(const struct klog_record *rec)
{
	char line[512];
	size_t pos = 0;

	ksnprintf(
		line, sizeof(line),
		"[%llu.%03llu] %s: ", (unsigned long long)(rec->time_us / 1000000ULL),
		(unsigned long long)((rec->time_us / 1000ULL) % 1000ULL), rec->ns);
	pos = strlen(line);

	klog_buf_puts_crlf(line, sizeof(line), &pos, rec->msg);
	klog_buf_puts_crlf(line, sizeof(line), &pos, "\r\n");

	if (pos >= sizeof(line))
		line[sizeof(line) - 1] = '\0';
	else
		line[pos] = '\0';

	uart_wstr(line);

	if (ft_ctx != NULL)
		flanterm_write(ft_ctx, line, strlen(line));
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
