/* ti_crc.h — CRC-8 (poly 0x1D, MSB-first), CRC-32 (IEEE 802.3, reflected) and CRC-32C (Castagnoli, reflected). */
#ifndef TI_CRC_H
#define TI_CRC_H

#include <stddef.h>
#include <stdint.h>

/* CRC-8, polynomial x^8+x^4+x^3+x^2+1 (0x1D), no reflection.
 *  - FS26 SPI (DS Rev.3 §15.8.1): init 0xFF, xorout 0x00 over frame bits 31..8.
 *  - CAN E2E (SAE J1850): init 0xFF, xorout 0xFF. */
uint8_t ti_crc8_1d(const uint8_t *data, size_t len, uint8_t init, uint8_t xorout);

/* CRC-32/IEEE (poly 0x04C11DB7 reflected = 0xEDB88320, init 0xFFFFFFFF, xorout 0xFFFFFFFF). */
uint32_t ti_crc32(const void *data, size_t len);

/* CRC-32C (poly 0x1EDC6F41 reflected = 0x82F63B78, init and xorout 0xFFFFFFFF; check "123456789" = 0xE3069283).
 * Round 23: the NVM slot check (nvm/nvlog.c) — a polynomial the records' own CRC-32 does not share. */
uint32_t ti_crc32c(const void *data, size_t len);

#endif /* TI_CRC_H */
