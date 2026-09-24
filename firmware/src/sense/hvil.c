/* hvil.c — FW-09 signature decode with debounce. */
#include "hvil.h"

#include "ti_math.h"

void hvil_init(hvil_t *h)
{
    *h = (hvil_t){0};
}

hvil_status_t hvil_classify(float v, bool drive, const ti_params_t *p)
{
    const float closed = drive ? p->hvil_closed_hi_v : p->hvil_closed_lo_v;
    if (ti_absf(v - closed) <= p->cal_hvil_tol_v) {
        return HVIL_CLOSED;
    }
    if (ti_absf(v - p->hvil_open_v) <= p->cal_hvil_tol_v) {
        return HVIL_OPEN;
    }
    if (v < 1.0f) {
        return HVIL_SHORT_GND;
    }
    if (v > 4.0f) {
        return HVIL_SHORT_SUPPLY;
    }
    return HVIL_IMPLAUSIBLE;
}

bool hvil_step(hvil_t *h, uint16_t code, uint32_t now_ms, const ti_params_t *p)
{
    if (!h->started) {
        h->started = true;
        h->last_ms = now_ms;
        h->drive = true;
        return h->drive;
    }
    if (!ti_elapsed(now_ms, h->last_ms, p->cal_hvil_period_ms)) {
        return h->drive;
    }
    h->last_ms = now_ms;
    const hvil_status_t c = hvil_classify(ti_code_to_v(code), h->drive, p);
    if (c == h->cand) {
        h->cnt = (h->cnt < 255u) ? (uint8_t)(h->cnt + 1u) : 255u;
    } else {
        h->cand = c;
        h->cnt = 1u;
    }
    if (h->cnt >= p->cal_hvil_debounce) {
        h->status = c;
    }
    h->drive = !h->drive;
    return h->drive;
}
