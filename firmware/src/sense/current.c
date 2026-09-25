/* current.c — FW-05 phase current conversion, validity, sum plausibility, OC thresholds. */
#include "current.h"

#include <math.h>

#include "ti_math.h"

void isns_init(isns_t *s)
{
    *s = (isns_t){0};
}

void isns_update(isns_t *s, const uint16_t codes[3], uint32_t t_us, uint32_t now_us, const isns_cal_t cal[3],
                 const ti_params_t *p)
{
    bool all = true;
    for (uint32_t i = 0u; i < 3u; i++) {
        const float v = ti_code_to_v(codes[i]);
        s->v_pin[i] = v;
        s->open_wire[i] = v < p->isns_valid_min_v;
        s->ch_valid[i] = (v >= p->isns_valid_min_v) && (v <= p->isns_valid_max_v) && (cal[i].gain_v_per_a > 0.0f);
        s->i_a[i] = s->ch_valid[i] ? ((float)cal[i].sign * (v - cal[i].offset_v) / cal[i].gain_v_per_a) : 0.0f;
        all = all && s->ch_valid[i];
    }
    s->t_us = t_us;
    s->fresh = !ti_stale(now_us, t_us, p->cal_isns_stale_us); /* round 18: now_us is read after the triplet */
    if (all) {
        s->sum_a = s->i_a[0] + s->i_a[1] + s->i_a[2];
        if (ti_absf(s->sum_a) > p->cal_isum_tol_a) {
            s->sum_cnt = (s->sum_cnt < 255u) ? (uint8_t)(s->sum_cnt + 1u) : 255u;
        } else {
            s->sum_cnt = 0u;
        }
        if (s->sum_cnt >= p->cal_isum_debounce) {
            s->sum_fault = true;
        }
    }
    s->valid = all && s->fresh && !s->sum_fault && !s->stuck_fault;
}

void isns_lost(isns_t *s)
{
    for (uint32_t i = 0u; i < 3u; i++) {
        s->ch_valid[i] = false;
    }
    s->fresh = false;
    s->valid = false;
}

void isns_activity(isns_t *s, const float iref_abc[3], const ti_params_t *p)
{
    for (uint32_t i = 0u; i < 3u; i++) {
        const float e = ti_absf(iref_abc[i]);
        if (e < p->cal_isns_act_min_a) {
            continue; /* this phase is not asked for current now: no evidence either way */
        }
        if (s->ch_valid[i] && (ti_absf(s->i_a[i]) >= (p->cal_isns_act_frac * e))) {
            s->act_cnt[i] = 0u;
        } else {
            s->act_cnt[i] = (s->act_cnt[i] < 255u) ? (uint8_t)(s->act_cnt[i] + 1u) : 255u;
            if (s->act_cnt[i] >= p->cal_isns_act_debounce) {
                s->stuck_fault = true;
                s->valid = false;
            }
        }
    }
}

bool isns_oc(const isns_t *s, const ti_params_t *p)
{
    bool oc = false;
    for (uint32_t i = 0u; i < 3u; i++) {
        oc = oc || (s->ch_valid[i] && (ti_absf(s->i_a[i]) >= p->i_oc_trip_a));
    }
    return oc;
}

static float v_to_codef(float v) { return v * ((float)TI_ADC_MAX_CODE / TI_ADC_VREF_V); }

void isns_oc_codes(const isns_cal_t *c, float i_trip_a, uint16_t *lo_trip, uint16_t *hi_trip)
{
    /* +I and -I map to offset +/- gain*I whatever the sign, so both polarities give this pair.
     * ceil/floor: the compare fires at or beyond the trip current, never before. */
    const float hi = ceilf(v_to_codef(c->offset_v + c->gain_v_per_a * i_trip_a));
    const float lo = floorf(v_to_codef(c->offset_v - c->gain_v_per_a * i_trip_a));
    *hi_trip = (uint16_t)ti_clampf(hi, 1.0f, (float)TI_ADC_MAX_CODE);
    *lo_trip = (uint16_t)ti_clampf(lo, 0.0f, (float)TI_ADC_MAX_CODE - 1.0f);
}

bool isns_offset_ok(const float mean_v[3], const isns_cal_t cal[3], const ti_params_t *p)
{
    bool ok = true;
    for (uint32_t i = 0u; i < 3u; i++) {
        ok = ok && (ti_absf(mean_v[i] - cal[i].offset_v) <= p->cal_isns_offset_tol_v);
    }
    return ok;
}
