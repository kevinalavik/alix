#ifndef SYS_TIME_H
#define SYS_TIME_H

#include <stdbool.h>
#include <stdint.h>

struct time_source;

typedef uint64_t (*time_read_us_fn)(void);
typedef uint64_t (*time_read_hz_fn)(void);

struct time_source {
	const char *name;

	time_read_us_fn read_us;
	time_read_hz_fn read_hz;

	bool stable;
};

void time_init(void);
void time_register_source(const struct time_source *source);

uint64_t time_uptime_us(void);
uint64_t time_source_hz(void);

const char *time_source_name(void);
bool time_source_is_stable(void);

#endif // SYS_TIME_H
