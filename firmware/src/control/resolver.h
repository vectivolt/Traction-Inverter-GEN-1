/* resolver.h — resolver-to-digital in software (FW-10). SWG1 excites at 10 kHz; SDADC1 samples the
 * excitation monitor, SDADC2/3 the SIN/COS windings, one block of HAL_SDADC_BLOCK_N samples per
 * carrier period. Per block:
 *   1. correlate each channel with the carrier (in-phase/quadrature, exact over whole periods);
 *   2. the monitor gives the reference amplitude and phase; the SIN/COS carriers lag it by the
 *      card's input filter (-24 deg at 10 kHz, round 13) plus the resolver's own phase — the
 *      reference is rotated by cal_rslv_phase_comp_deg (+ the unit's EOL phase) before projecting;
 *   3. envelopes are normalised by the monitor amplitude (ratiometric) and the EOL gains/offsets;
 *   4. amplitude window on sqrt(sin^2 + cos^2), excitation-monitor window, type-II tracking
 *      observer, tracking-error and acceleration plausibility; a separate check compares the
 *      angle rate with the current model's back-EMF speed.
 * Any fault => invalid: no angle-dependent torque (the §6 row "resolver invalid"). */
#ifndef RESOLVER_H
#define RESOLVER_H

#include "sdadc.h"
#include "ti_params.h"

typedef struct {
    float ratio_nom;      /* |envelope| / monitor amplitude at EOL */
    float exc_nom_code;   /* monitor amplitude at EOL (codes) */
    float sin_gain;
    float cos_gain;
    float sin_offset;     /* normalised units */
    float cos_offset;
    float phase_trim_deg; /* EOL phase added to cal_rslv_phase_comp_deg */
    float zero_rad;       /* electrical zero (motor electrical) */
    uint8_t motor_pp;
    uint8_t resolver_pp;
} rslv_cal_t;

typedef struct {
    float theta;   /* observer angle, resolver electrical */
    float omega;   /* observer speed, resolver electrical rad/s */
    float omega_prev;
    float sin_n;
    float cos_n;
    float amp;
    float exc_ratio;
    float err;     /* normalised tracking error (~ sin of the angle error) */
    uint8_t n_amp, n_exc, n_trk, n_acc, n_rate;
    uint16_t n_blocks; /* since priming; tracking/acceleration count once locked */
    bool amp_fault, exc_fault, trk_fault, acc_fault, rate_fault;
    bool have_first;   /* priming takes two consecutive blocks: angle and speed */
    bool primed;
    bool locked;
    bool valid;
    uint32_t t_ref_us; /* start of the last block consumed */
    float t_mid_us;    /* theta refers to t_ref_us + t_mid_us (the block's mean sampling instant) */
} rslv_t;

void rslv_init(rslv_t *r);
void rslv_update(rslv_t *r, const int16_t exc[HAL_SDADC_BLOCK_N], const int16_t sn[HAL_SDADC_BLOCK_N],
                 const int16_t cs[HAL_SDADC_BLOCK_N], float ts_s, uint32_t t_us, const rslv_cal_t *c,
                 const ti_params_t *p);
float rslv_theta_e(const rslv_t *r, const rslv_cal_t *c);  /* motor electrical, [0, 2pi), at the last block */
/* Motor electrical angle extrapolated to now_us (the current loop runs twice per carrier period)
 * minus the chain latency cal_rslv_latency_us. */
float rslv_theta_e_at(const rslv_t *r, const rslv_cal_t *c, uint32_t now_us, const ti_params_t *p);
float rslv_omega_e(const rslv_t *r, const rslv_cal_t *c);  /* motor electrical rad/s */
float rslv_speed_rpm(const rslv_t *r, const rslv_cal_t *c); /* mechanical */
/* FW-10 angle rate vs the current model (called at 1 kHz while modulating). */
void rslv_rate_check(rslv_t *r, float omega_e_model, bool model_valid, const rslv_cal_t *c, const ti_params_t *p);
/* SWG amplitude trim toward the EOL monitor amplitude (the firmware knob, R2-F04). */
uint8_t rslv_swg_trim(uint8_t code, const rslv_t *r);

#endif /* RESOLVER_H */
