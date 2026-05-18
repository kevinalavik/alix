#ifndef SDI_HOST_H
#define SDI_HOST_H

#include <sdi.h>

int sdi_kernel_init(void);
int sdi_load_drivers(const char *directory);
int sdi_kernel_register_loaded_driver(const sdi_driver_desc_t *driver,
									  const char *path);

#endif // SDI_HOST_H
