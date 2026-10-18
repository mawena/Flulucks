#ifndef FL_CRC32_H
#define FL_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* CRC32 IEEE 802.3 (polynôme réfléchi 0xEDB88320), comme GPT. */
uint32_t fl_crc32(const void *data, size_t len);

#endif
