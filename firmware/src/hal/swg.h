/* hal/swg.h — sine-wave generator SWG1_0 (ball A8, SGEN_1): the resolver excitation source.
 * The card's MFB filter (|H(10 kHz)| 2.07 since rev A.15, x 2 by the ALM2402 bridge) follows it; the
 * amplitude register is the firmware knob that holds the excitation monitor at its setpoint (FW-10).
 * Round 16 (A14-N01): the code starts low (cal_swg_code_init) and ramps up under the trim, never at the
 * register maximum — the untrimmed MAXAPP max corner would slew-limit the amplifier. */
#ifndef HAL_SWG_H
#define HAL_SWG_H

#include "ti_types.h"

#define HAL_SWG_CODE_MAX 15u /* IOAMPL: 4 bits; code 15 = MAXAPP (1.884 / 2.093 / 2.302 V pp, DS Table 40) */

/* amplitude_code: SWG IOAMPL field (0.39–2.30 V pp range, S32K39 DS Table 40). */
bool hal_swg_start(uint32_t freq_hz, uint8_t amplitude_code);
void hal_swg_stop(void);
bool hal_swg_error(void); /* STAT PHERR/FERR/SERR */

#endif /* HAL_SWG_H */
