#ifndef LOG_KLOG_H
#define LOG_KLOG_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define KLOG_RING_SIZE 512
#define KLOG_NS_MAX 32
#define KLOG_MSG_MAX 192

#define KLOG_LEVEL_ALWAYS 0
#define KLOG_LEVEL_INFO 1
#define KLOG_LEVEL_VERBOSE 2
#define KLOG_LEVEL_VVERBOSE 3

#ifndef CONFIG_KLOG_VERBOSITY
#define CONFIG_KLOG_VERBOSITY KLOG_LEVEL_INFO
#endif

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

void klog_write_level(int level, const char *ns, const char *fmt, ...);
void kvlog_write_level(int level, const char *ns, const char *fmt, va_list ap);
void klog_write(const char *ns, const char *fmt, ...);
void kvlog_write(const char *ns, const char *fmt, va_list ap);

#define klogf(fmt, ...) \
	klog_write_level(KLOG_LEVEL_ALWAYS, KLOG_NS, fmt, ##__VA_ARGS__)
#define klog(fmt, ...) \
	klog_write_level(KLOG_LEVEL_INFO, KLOG_NS, fmt, ##__VA_ARGS__)
#define klogv(fmt, ...) \
	klog_write_level(KLOG_LEVEL_VERBOSE, KLOG_NS, fmt, ##__VA_ARGS__)
#define klogvv(fmt, ...) \
	klog_write_level(KLOG_LEVEL_VVERBOSE, KLOG_NS, fmt, ##__VA_ARGS__)

#define klog_ns(ns, fmt, ...) \
	klog_write_level(KLOG_LEVEL_INFO, ns, fmt, ##__VA_ARGS__)
#define klog_ns_v(ns, fmt, ...) \
	klog_write_level(KLOG_LEVEL_VERBOSE, ns, fmt, ##__VA_ARGS__)
#define klog_ns_vv(ns, fmt, ...) \
	klog_write_level(KLOG_LEVEL_VVERBOSE, ns, fmt, ##__VA_ARGS__)
#define klog_ns_force(ns, fmt, ...) \
	klog_write_level(KLOG_LEVEL_ALWAYS, ns, fmt, ##__VA_ARGS__)

#endif // LOG_KLOG_H
