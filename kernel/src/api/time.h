#ifndef API_TIME_H
#define API_TIME_H

#include <stdint.h>
#include <stdbool.h>

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

#endif // API_TIME_H