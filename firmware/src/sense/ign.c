/* ign.c — KL15 with hysteresis and debounce. */
#include "ign.h"

float ign_kl15_v(uint16_t code)
{
    const float v = ti_code_to_v(code);
    return (v > 0.01f) ? ((v * 5.7f) + 0.3f) : 0.0f; /* 47k+10k over 10k, DIGN ~0.3 V */
}

void ign_init(ign_t *g, bool on)
{
    *g = (ign_t){0};
    g->on = on;
}

void ign_update(ign_t *g, uint16_t code, uint32_t now_ms, const ti_params_t *p)
{
    const float kl15 = ign_kl15_v(code);
    bool raw = g->on;
    if (kl15 >= p->cal_ign_on_v) {
        raw = true;
    } else if (kl15 <= p->cal_ign_off_v) {
        raw = false;
    } else {
        /* hysteresis band: keep */
    }
    if (raw == g->on) {
        g->pending = false;
        return;
    }
    if (!g->pending || (g->cand != raw)) {
        g->pending = true;
        g->cand = raw;
        g->since_ms = now_ms;
    }
    if (ti_elapsed(now_ms, g->since_ms, p->cal_ign_debounce_ms)) {
        g->on = raw;
        g->pending = false;
    }
}
