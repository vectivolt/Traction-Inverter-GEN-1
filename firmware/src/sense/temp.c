/* temp.c — FW-13 conversions and plausibility. */
#include "temp.h"

#include <math.h>

#include "ti_math.h"

#define T0_K 298.15f
#define K0 273.15f

float temp_ntc_c(float v_pin, float pullup_ohm, float series_ohm, float r25_ohm, float b_k)
{
    const float v = ti_clampf(v_pin, 1.0e-3f, TI_ADC_VREF_V - 1.0e-3f);
    const float r_total = pullup_ohm * v / (TI_ADC_VREF_V - v);
    const float r = ti_maxf(r_total - series_ohm, 1.0f);
    return 1.0f / ((1.0f / T0_K) + (logf(r / r25_ohm) / b_k)) - K0;
}

float temp_pt1000_c(float v_pin, float pullup_ohm)
{
    /* Callendar–Van Dusen above 0 degC (A = 3.9083e-3, B = -5.775e-7), inverted in closed form */
    const float v = ti_clampf(v_pin, 1.0e-3f, TI_ADC_VREF_V - 1.0e-3f);
    const float r = pullup_ohm * v / (TI_ADC_VREF_V - v);
    const float a = 3.9083e-3f;
    const float b = -5.775e-7f;
    const float disc = (a * a) - (4.0f * b * (1.0f - (r / 1000.0f)));
    return (-a + sqrtf(ti_maxf(disc, 0.0f))) / (2.0f * b);
}

void temp_init(temp_t *t)
{
    *t = (temp_t){0};
}

static float convert(temp_ch_t ch, float v, const temp_mt_cal_t *mt, const ti_params_t *p)
{
    if (ch <= TEMP_TMOD_W) {
        return temp_ntc_c(v, p->ntc_pullup_ohm, p->ntc_series_ohm, p->ntc_r25_ohm, p->ntc_b_k);
    }
    if (ch <= TEMP_NTC_A) {
        return temp_ntc_c(v, p->bntc_pullup_ohm, 0.0f, p->bntc_r25_ohm, p->bntc_b_k);
    }
    if (mt->type == TEMP_MT_NTC) {
        return temp_ntc_c(v, p->mt_pullup_ohm, 0.0f, mt->r25_ohm, mt->b_k);
    }
    return temp_pt1000_c(v, p->mt_pullup_ohm);
}

static void accept(temp_ch_state_t *c, float t, float code, uint32_t now_ms)
{
    c->rate_cnt = 0u;
    c->t_c = t;
    c->ref_code = code;
    c->valid = true;
    c->primed = true;
    c->last_ms = now_ms;
}

static void update_one(temp_ch_state_t *c, temp_ch_t ch, uint16_t code, uint32_t now_ms, const temp_mt_cal_t *mt,
                       const ti_params_t *p)
{
    const float v = ti_code_to_v(code);
    /* PT1000 against 10 k never reaches the rail: open reads VREF5, short reads 0 V either way */
    if (v >= p->cal_ntc_open_v) {
        c->fault = TEMP_OPEN;
    } else if (v <= p->cal_ntc_short_v) {
        c->fault = TEMP_SHORT;
    } else if (c->fault != TEMP_RATE) {
        c->fault = TEMP_OK;
    } else {
        /* a rate fault stays latched for the key cycle */
    }
    if ((c->fault == TEMP_OPEN) || (c->fault == TEMP_SHORT) || (c->fault == TEMP_RATE)) {
        c->valid = false;
        c->acc = 0u; /* an open or shorted sensor's samples never enter a window */
        c->n = 0u;
        c->win_ms = now_ms;
        return;
    }
    if (!c->primed) {
        accept(c, convert(ch, v, mt, p), (float)code, now_ms); /* the first sample: a value at once */
        c->win_ms = now_ms;
        return;
    }
    c->acc += code;
    c->n++;
    if (!ti_elapsed(now_ms, c->win_ms, p->cal_temp_rate_win_ms) || (c->n == 0u)) {
        return; /* the accepted mean stays in force until the window closes */
    }
    const float mean = (float)c->acc / (float)c->n;
    c->acc = 0u;
    c->n = 0u;
    c->win_ms = now_ms;
    const float t = convert(ch, mean * (TI_ADC_VREF_V / (float)TI_ADC_MAX_CODE), mt, p);
    const float dt_s = (float)ti_age(now_ms, c->last_ms) * 1.0e-3f;
    const bool moved = ti_absf(mean - c->ref_code) > (float)p->cal_temp_rate_db_codes;
    if (moved && (dt_s > 0.0f) && ((ti_absf(t - c->t_c) / dt_s) > p->cal_temp_rate_c_s)) {
        c->rate_cnt++;
        c->valid = false; /* this window rejected, the previous mean held */
        if (c->rate_cnt >= 3u) {
            c->fault = TEMP_RATE;
        }
        return;
    }
    accept(c, t, mean, now_ms);
}

void temp_update(temp_t *t, const uint16_t codes[TEMP_COUNT], uint32_t now_ms, const temp_mt_cal_t *mt,
                 const ti_params_t *p)
{
    for (uint32_t i = 0u; i < (uint32_t)TEMP_COUNT; i++) {
        update_one(&t->ch[i], (temp_ch_t)i, codes[i], now_ms, mt, p);
    }
}

float temp_module_max(const temp_t *t, bool *any_valid, bool *all_valid)
{
    float m = -273.0f;
    *any_valid = false;
    *all_valid = true;
    for (uint32_t i = 0u; i <= (uint32_t)TEMP_TMOD_W; i++) {
        if (t->ch[i].valid) {
            *any_valid = true;
            m = ti_maxf(m, t->ch[i].t_c);
        } else {
            *all_valid = false;
        }
    }
    return m;
}
