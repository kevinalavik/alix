#ifndef FS_INITRD_H
#define FS_INITRD_H

#include <stddef.h>

int initrd_unpack(const void *buffer, size_t length);

#endif // FS_INITRD_H
