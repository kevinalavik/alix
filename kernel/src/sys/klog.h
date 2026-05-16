#ifndef SYS_KLOG_H
#define SYS_KLOG_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define KLOG_RING_SIZE 512
#define KLOG_NS_MAX 32
#define KLOG_MSG_MAX 192

#ifndef KLOG_NS
#define KLOG_NS "unknown"
#endif

struct klog_record {
	uint64_t seq;
	uint64_t time_us;
	char ns[KLOG_NS_MAX];
	char msg[KLOG_MSG_MAX];
};

void klog_init(void);

void klog_write(const char *ns, const char *fmt, ...);
void kvlog_write(const char *ns, const char *fmt, va_list ap);

#define klog(fmt, ...) klog_write(KLOG_NS, fmt, ##__VA_ARGS__)
#define klog_ns(ns, fmt, ...) klog_write(ns, fmt, ##__VA_ARGS__)

#endif // SYS_KLOG_H