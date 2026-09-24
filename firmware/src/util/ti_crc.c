/* ti_crc.c — bitwise CRCs (no tables: the FS26 frame is 3 bytes, NVM records are rare). */
#include "ti_crc.h"

uint8_t ti_crc8_1d(const uint8_t *data, size_t len, uint8_t init, uint8_t xorout)
{
    uint8_t crc = init;
    for (size_t i = 0u; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0u; b < 8u; b++) {
            crc = ((crc & 0x80u) != 0u) ? (uint8_t)((uint8_t)(crc << 1) ^ 0x1Du) : (uint8_t)(crc << 1);
        }
    }
    return (uint8_t)(crc ^ xorout);
}

uint32_t ti_crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < len; i++) {
        crc ^= p[i];
        for (uint8_t b = 0u; b < 8u; b++) {
            crc = ((crc & 1u) != 0u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}
