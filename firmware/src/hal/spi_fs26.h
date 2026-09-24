/* hal/spi_fs26.h — LPSPI3 to the FS26 (SCK M17, SOUT L17, SIN K15, PCS0 G15).
 * One 32-bit frame per transfer, CPOL 0 / CPHA 1 (MOSI latched on the SCLK falling edge per the
 * FS26 DS §15.8), MSB first, CS framed. Blocking, bounded by the platform's timeout. */
#ifndef HAL_SPI_FS26_H
#define HAL_SPI_FS26_H

#include "ti_types.h"

bool hal_fs26_spi_init(void);
bool hal_fs26_xfer(uint32_t tx, uint32_t *rx);

#endif /* HAL_SPI_FS26_H */
