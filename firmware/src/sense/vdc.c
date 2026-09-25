/* vdc.c — FW-07 plausibility, FW-18 HV state. */
#include "vdc.h"

#include <math.h>

#include "ti_math.h"

void vdc_init(vdc_t *s)
{
    *s = (vdc_t){0};
    s->hv = TI_HV_UNKNOWN;
}

void vdc_update(vdc_t *s, const uint16_t code_ch[2], const uint32_t t_ch_us[2], uint16_t code_vofs,
                uint16_t code_v5gd, uint32_t now_us, const vdc_cal_t cal[2], const ti_params_t *p)
{
    s->vofs_v = ti_code_to_v(code_vofs);
    s->vofs_ok = (s->vofs_v >= p->vofs_min_v) && (s->vofs_v <= p->vofs_max_v);
    s->v5gd_v = ti_code_to_v(code_v5gd) / p->v5gd_sns_ratio;
    s->v5gd_ok = (s->v5gd_v >= p->v5gd_min_v) && (s->v5gd_v <= p->v5gd_max_v);
    for (uint32_t i = 0u; i < 2u; i++) {
        const float pin = ti_code_to_v(code_ch[i]);
        s->ch_failsafe[i] = pin < p->vdc_failsafe_v;
        s->ch_stale[i] = ti_stale(now_us, t_ch_us[i], p->cal_vdc_stale_us); /* round 18: signed */
        s->v_ch[i] = cal[i].gain * (pin - s->vofs_v) + cal[i].offset_v;
        s->ch_valid[i] = !s->ch_failsafe[i] && !s->ch_stale[i] && s->vofs_ok && s->v5gd_ok;
    }
    const float big = ti_maxf(ti_absf(s->v_ch[0]), ti_absf(s->v_ch[1]));
    const float lim = ti_maxf(p->vdc_disagree_frac * big, p->cal_vdc_disagree_floor_v);
    s->disagree = s->ch_valid[0] && s->ch_valid[1] && (ti_absf(s->v_ch[0] - s->v_ch[1]) > lim);
    s->valid = s->ch_valid[0] && s->ch_valid[1] && !s->disagree;
    s->vdc = s->valid ? (0.5f * (s->v_ch[0] + s->v_ch[1])) : 0.0f;
    if (!s->valid) {
        s->hv = TI_HV_UNKNOWN;
    } else {
        s->hv = (s->vdc < p->hv_safe_v) ? TI_HV_SAFE : TI_HV_PRESENT;
    }
}

void vdc_bms_check(vdc_t *s, bool contactors_closed, bool pack_fresh, float v_pack, uint32_t now_ms,
                   const ti_params_t *p)
{
    const bool applicable = contactors_closed && pack_fresh && s->valid && (v_pack > 0.0f);
    const bool bad = applicable && (ti_absf(s->vdc - v_pack) > (p->vdc_bms_frac * v_pack));
    if (!bad) {
        s->bms_bad_pending = false;
        s->bms_mismatch = false;
        return;
    }
    if (!s->bms_bad_pending) {
        s->bms_bad_pending = true;
        s->bms_bad_since_ms = now_ms;
    }
    s->bms_mismatch = ti_elapsed(now_ms, s->bms_bad_since_ms, p->cal_vdc_bms_debounce_ms);
}

uint16_t vdc_ov_code(const vdc_cal_t *c, const ti_params_t *p)
{
    const float pin = p->vofs_nom_v + (p->ov_trip_v - c->offset_v) / c->gain;
    return (uint16_t)ti_clampf(ceilf(pin * ((float)TI_ADC_MAX_CODE / TI_ADC_VREF_V)), 1.0f, (float)TI_ADC_MAX_CODE);
}
