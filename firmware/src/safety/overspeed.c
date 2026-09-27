/* overspeed.c — FW-42 bands, hysteresis and debounce (see overspeed.h). */
#include "overspeed.h"

#include "ti_math.h"

void ovs_init(ovs_t *o) { *o = (ovs_t){0}; }

/* The band this speed asks for: a band starts at its threshold and is left a hysteresis below it. */
static ovs_band_t wanted(ovs_band_t now, float n, float n_max, const ti_params_t *p)
{
    const float hyst = p->cal_ovs_hyst_frac * n_max;
    const float trip = p->cal_ovs_trip_frac * n_max;
    const float warn = p->cal_ovs_warn_frac * n_max;
    if ((n >= trip) || ((now == OVS_TRIP) && (n > (trip - hyst)))) {
        return OVS_TRIP;
    }
    if ((n >= warn) || ((now != OVS_NONE) && (n > (warn - hyst)))) {
        return OVS_WARN;
    }
    return OVS_NONE;
}

ovs_band_t ovs_step(ovs_t *o, float speed_rpm, bool measured, float n_max_rpm, const ti_params_t *p)
{
    if (!measured || !ti_finite(speed_rpm) || !(n_max_rpm > 0.0f)) {
        o->n_ms = 0u; /* no evidence either way: the band is held */
        return o->band;
    }
    const ovs_band_t w = wanted(o->band, ti_absf(speed_rpm), n_max_rpm, p);
    if (w == o->band) {
        o->n_ms = 0u;
    } else {
        o->n_ms++;
        if (o->n_ms >= p->cal_ovs_debounce_ms) {
            o->band = w;
            o->n_ms = 0u;
        }
    }
    return o->band;
}
