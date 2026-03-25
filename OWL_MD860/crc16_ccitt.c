#include "crc16_ccitt.h"

#include <stddef.h>
#include <stdint.h>

uint16_t crc16_ccitt_false(const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    uint16_t crc = 0xFFFFu;
    size_t i;

    if (p == NULL) {
        return crc;
    }

    for (i = 0u; i < len; ++i) {
        uint8_t bit;
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (bit = 0u; bit < 8u; ++bit) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}
