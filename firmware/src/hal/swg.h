/* hal/swg.h — sine-wave generator SWG1_0 (ball A8, SGEN_1): the resolver excitation source.
 * The card's MFB filter (|H| 1.85 at 10 kHz, round 12 R2-F04) and ALM2402 buffer follow it;
 * the amplitude register is the firmware knob against the excitation monitor. */
#ifndef HAL_SWG_H
#define HAL_SWG_H

#include "ti_types.h"

/* amplitude_code: SWG IOAMPL field (0.39–2.30 V pp range, S32K39 DS Table 40). */
bool hal_swg_start(uint32_t freq_hz, uint8_t amplitude_code);
void hal_swg_stop(void);
bool hal_swg_error(void); /* STAT PHERR/FERR/SERR */

#endif /* HAL_SWG_H */
