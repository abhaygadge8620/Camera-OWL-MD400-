#ifndef CRC16_CCITT_H
#define CRC16_CCITT_H

#include <stddef.h>
#include <stdint.h>

uint16_t crc16_ccitt_false(const void* data, size_t len);

#endif
