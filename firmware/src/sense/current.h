/* current.h — phase current sensing (FW-05). Three LEM HC5FW 900-S through unity buffers into
 * ADC3_P1 / ADC4_P5 / ADC0_S19, converted simultaneously twice per PWM period.
 *  - per-channel validity window 0.2–4.8 V at the pin (an open wire or unpowered sensor reads
 *    0 V through the card's 100 k pull-down R<ph>B0);
 *  - sum(Ia+Ib+Ic) plausibility every sample;
 *  - over-current at 1.25*sqrt(2)*I_pk,rms instantaneous, both polarities (601 A 8XX / 707 A 4XX):
 *    hardware = ADC analog watchdog codes from each channel's calibrated gain, offset and sign;
 *    software = the same threshold checked in the ISR as a backstop;
 *  - latent stuck channel (F24): KCL cannot see a channel stuck at zero at zero current, a stuck
 *    channel whose current stays inside cal_isum_tol_a, or all three stuck (they still sum to 0).
 *    While modulating, a phase whose reference reaches cal_isns_act_min_a must read at least
 *    cal_isns_act_frac of it; cal_isns_act_debounce such applicable samples in a row (per channel;
 *    a sample where that phase is asked for less neither counts nor resets) latch stuck_fault.
 *    Equal gain errors on all three channels stay unobservable in closed loop with three sensors:
 *    the coverage table is in docs/traceability.md.
 *  - round 16 (A14-R03): a sample whose triplet did not arrive complete (hal_adc_read_phase false) is
 *    LOST: isns_lost() makes the measurement invalid and not fresh and clears ch_valid, so nothing of
 *    the previous sample feeds FOC, the OC backstop or the activity check; the DTC is ISNS_STALE (not
 *    an open wire). t_us keeps the last complete triplet's time.
 *  - round 18 (A16-R01): isns_update's now_us is a time read AFTER hal_adc_read_phase() — the target stamps
 *    the triplet when it reads it, later than the ISR entry — and the age is signed (ti_stale): a stamp
 *    that postdates the check by an ISR's execution is fresh, never 2^32 us old. */
#ifndef CURRENT_H
#define CURRENT_H

#include "ti_params.h"

typedef struct {
    float offset_v;     /* output at 0 A */
    float gain_v_per_a; /* magnitude, > 0 */
    int8_t sign;        /* +1 / -1 mounting orientation */
} isns_cal_t;

typedef struct {
    float i_a[3];
    float v_pin[3];
    bool ch_valid[3];
    bool open_wire[3];
    bool sum_fault; /* latched until isns_init() */
    uint8_t sum_cnt;
    bool stuck_fault; /* F24, latched until isns_init() */
    uint8_t act_cnt[3];
    float sum_a;
    bool fresh;
    bool valid; /* three channels valid, fresh, sum plausible */
    uint32_t t_us;
} isns_t;

void isns_init(isns_t *s);
void isns_update(isns_t *s, const uint16_t codes[3], uint32_t t_us, uint32_t now_us, const isns_cal_t cal[3],
                 const ti_params_t *p);
void isns_lost(isns_t *s); /* no complete triplet this sample (round 16) */
bool isns_oc(const isns_t *s, const ti_params_t *p);
/* F24: iref_abc = the phase currents the loop is tracking now (A); call once per modulated sample. */
void isns_activity(isns_t *s, const float iref_abc[3], const ti_params_t *p);
void isns_oc_codes(const isns_cal_t *c, float i_trip_a, uint16_t *lo_trip, uint16_t *hi_trip);
/* Standstill zero-current check against the EOL offset (§9 step 3). mean_v = averaged pin volts. */
bool isns_offset_ok(const float mean_v[3], const isns_cal_t cal[3], const ti_params_t *p);

#endif /* CURRENT_H */
