#include <sys/time.h>

#include <stddef.h>

static const struct time_source *active_time_source;

void time_init(void)
{
	active_time_source = NULL;
}

void time_register_source(const struct time_source *source)
{
	if (source == NULL)
		return;

	if (source->read_us == NULL)
		return;

	active_time_source = source;
}

uint64_t time_uptime_us(void)
{
	if (active_time_source == NULL)
		return 0;

	return active_time_source->read_us();
}

uint64_t time_source_hz(void)
{
	if (active_time_source == NULL)
		return 0;

	if (active_time_source->read_hz == NULL)
		return 0;

	return active_time_source->read_hz();
}

const char *time_source_name(void)
{
	if (active_time_source == NULL)
		return "none";

	if (active_time_source->name == NULL)
		return "unknown";

	return active_time_source->name;
}

bool time_source_is_stable(void)
{
	if (active_time_source == NULL)
		return false;

	return active_time_source->stable;
}
