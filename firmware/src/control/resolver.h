/* resolver.h — resolver-to-digital in software (FW-10). SWG1 excites at 10 kHz; SDADC1 samples the
 * excitation monitor, SDADC2/3 the SIN/COS windings, one coherent frame of HAL_SDADC_BLOCK_N samples
 * per channel per carrier period (hal/sdadc.h). Per frame:
 *   1. correlate each channel with the carrier (in-phase/quadrature, exact over whole periods);
 *   2. the monitor gives the reference amplitude and phase; the SIN/COS carriers lag it by the
 *      card's input filter (-24 deg at 10 kHz, round 13) plus the resolver's own phase — the
 *      reference is rotated by cal_rslv_phase_comp_deg (+ the unit's EOL phase) before projecting;
 *   3. envelopes are normalised by the monitor amplitude (ratiometric) and the EOL gains/offsets;
 *   4. amplitude window on sqrt(sin^2 + cos^2), excitation window at two planes (round 16, below),
 *      type-II tracking observer, tracking-error and acceleration plausibility; a separate check
 *      compares the angle rate with the current model's back-EMF speed.
 * Round 16:
 *   - A14-R01: validity expires. rslv_age() runs on every control tick, new frame or not: the angle
 *     may be extrapolated from the newest coherent frame for at most cal_rslv_hold_us, then it is
 *     withdrawn (stale) and the observer re-acquires from scratch when frames return (priming, then
 *     SETTLE_BLOCKS). The speed is not held here: the application holds the last valid speed only
 *     for the bounded §6 column decision (cal_speed_hold_ms), never as angle feedback.
 *   - A14-N01: amplitude planes. The monitor taps the protected node (after RSX, before the PTC and
 *     the harness). mon_vpp is that plane; the SWG trim holds it at cal_rslv_exc_target_vpp, ramping
 *     up from cal_swg_code_init. The winding gets mon_vpp * cal_rslv_wind_per_mon (the cold allowance)
 *     times what the resolver's own ratiometric output says changed since EOL (amp): a PTC still at
 *     5 ohm after a trip shows there (0.875 / 0.964), never at the monitor. The resolver's floor
 *     (rslv_floor_vpp, 6.5 V pp) is checked AT THE WINDING. The excitation checks start once the trim
 *     has brought the excitation up (exc_ready); until then the resolver is not valid (not a fault).
 * Any fault => invalid: no angle-dependent torque (the §6 row "resolver invalid"). */
#ifndef RESOLVER_H
#define RESOLVER_H

#include "sdadc.h"
#include "ti_params.h"

typedef struct {
    float ratio_nom;        /* |envelope| / monitor amplitude at EOL */
    float exc_code_per_vpp; /* monitor carrier amplitude (codes) per V pp at the monitor plane (EOL) */
    float sin_gain;
    float cos_gain;
    float sin_offset;       /* normalised units */
    float cos_offset;
    float phase_trim_deg;   /* EOL phase added to cal_rslv_phase_comp_deg */
    float zero_rad;         /* electrical zero (motor electrical) */
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
    float exc_ratio; /* monitor plane / cal_rslv_exc_target_vpp */
    float mon_vpp;   /* excitation at the monitor plane (protected node), V pp */
    float wind_vpp;  /* excitation at the winding (estimate), V pp */
    float err;       /* normalised tracking error (~ sin of the angle error) */
    uint8_t n_amp, n_exc, n_trk, n_acc, n_rate;
    uint16_t n_blocks; /* since priming; tracking/acceleration count once locked */
    bool amp_fault, exc_fault, trk_fault, acc_fault, rate_fault;
    bool have_first;   /* priming takes two consecutive blocks: angle and speed */
    bool primed;
    bool locked;
    bool valid;
    bool exc_ready;    /* round 16: the trim has brought the excitation to its setpoint (or can go no further) */
    bool swg_sat;      /* round 16: the trim is at its top code and the monitor still below the band */
    bool stale;        /* round 16: no coherent frame within cal_rslv_hold_us (cleared once valid again) */
    bool have_frame;
    uint32_t t_frame_us; /* start of the newest coherent frame */
    uint32_t t_ref_us;   /* start of the last block the observer consumed */
    float t_mid_us;      /* theta refers to t_ref_us + t_mid_us (the block's mean sampling instant) */
} rslv_t;

void rslv_init(rslv_t *r);
void rslv_update(rslv_t *r, const int16_t exc[HAL_SDADC_BLOCK_N], const int16_t sn[HAL_SDADC_BLOCK_N],
                 const int16_t cs[HAL_SDADC_BLOCK_N], float ts_s, uint32_t t_us, const rslv_cal_t *c,
                 const ti_params_t *p);
/* A14-R01: every control tick. Withdraws the angle once the newest frame is cal_rslv_hold_us old. Round 18
 * (A16-R01): now_us is read after the frame read, and the age is signed (ti_stale) — the task's older time
 * against a frame the current-loop ISR published meanwhile is not an expiry. */
void rslv_age(rslv_t *r, uint32_t now_us, const ti_params_t *p);
float rslv_theta_e(const rslv_t *r, const rslv_cal_t *c);  /* motor electrical, [0, 2pi), at the last block */
/* Motor electrical angle extrapolated to now_us (the current loop runs twice per carrier period). The chain
 * latency cal_rslv_latency_us is a positive delay: the block's angle is the rotor's that long before its
 * mid-block reference, so the extrapolation adds it (round 18, A16-R03: it was subtracted). */
float rslv_theta_e_at(const rslv_t *r, const rslv_cal_t *c, uint32_t now_us, const ti_params_t *p);
float rslv_omega_e(const rslv_t *r, const rslv_cal_t *c);  /* motor electrical rad/s */
float rslv_speed_rpm(const rslv_t *r, const rslv_cal_t *c); /* mechanical */
/* FW-10 angle rate vs the current model (called at 1 kHz while modulating). */
void rslv_rate_check(rslv_t *r, float omega_e_model, bool model_valid, const rslv_cal_t *c, const ti_params_t *p);
/* SWG amplitude trim toward the monitor-plane setpoint (R2-F04, round 16): the next code; sets
 * exc_ready and swg_sat. */
uint8_t rslv_swg_trim(uint8_t code, rslv_t *r);

#endif /* RESOLVER_H */
