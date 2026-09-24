/* gains.h — current-loop gain set per SKU and f_sw (§2, round 7 RR08 / round 12 R2-F24).
 * The S6 screen closes 45 deg PM at <= 1.2 kHz (10 kHz), <= 1.1 kHz (8 kHz), <= 0.7 kHz (5 kHz)
 * assuming DOUBLE-UPDATE PWM (sample-to-actuation 0.75 T_sw) and the 0.35 mH / 25 mOhm screening
 * motor. PI by pole cancellation: Kp = L*wc, Ki = R*wc; loop period T_sw/2. A request above the
 * ceiling, or at an f_sw the SKU does not list, is refused (no gain set => no torque). */
#ifndef GAINS_H
#define GAINS_H

#include "ti_params.h"

typedef struct {
    uint32_t fsw_hz;
    float fc_hz;
    float kp_v_per_a;
    float ki_v_per_as;
    float ts_s;    /* current-loop period = T_sw / 2 */
    float delay_s; /* sample-to-actuation */
} gain_set_t;

float gains_ceiling_hz(const ti_params_t *p, uint32_t fsw_hz);
bool gains_compute(const ti_params_t *p, uint32_t fsw_hz, float fc_hz, float l_h, float r_ohm, gain_set_t *g);
/* The default: cal_fc_fraction of the ceiling. */
bool gains_default(const ti_params_t *p, uint32_t fsw_hz, float l_h, float r_ohm, gain_set_t *g);

#endif /* GAINS_H */
