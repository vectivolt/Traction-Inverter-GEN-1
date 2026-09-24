/* hal/sdadc.h — resolver sigma-delta channels (GEN3 allocation, ball map rev A.12):
 *   SDADC1 AN0/AN1 = excitation monitor (VREXM_P/N, A12/D12)
 *   SDADC2 AN0/AN1 = SIN (C15/A16)
 *   SDADC3 AN0/AN1 = COS (B13/B14)
 * The three run differentially at a data rate of HAL_SDADC_BLOCK_N samples per excitation
 * period, started from the same trigger as the SWG so sample 0 is carrier phase 0. The platform
 * DMA delivers one block per carrier period. */
#ifndef HAL_SDADC_H
#define HAL_SDADC_H

#include "ti_types.h"

#define HAL_SDADC_BLOCK_N 16u

typedef enum { HAL_SD_EXC = 0, HAL_SD_SIN, HAL_SD_COS, HAL_SD_COUNT } hal_sd_ch_t;

bool hal_sdadc_init(uint32_t carrier_hz);

/* Latest complete block (signed codes, full scale +/-32767 = +/-VREFP). Returns false if no new
 * block since the previous call for this channel. */
bool hal_sdadc_read_block(hal_sd_ch_t ch, int16_t out[HAL_SDADC_BLOCK_N], uint32_t *t_us);

#endif /* HAL_SDADC_H */
