#ifndef DEV_PIT_H
#define DEV_PIT_H

#include <stdint.h>

void pit_init(void);

uint64_t pit_calibrate_tsc(void);

#endif // DEV_PIT_H
