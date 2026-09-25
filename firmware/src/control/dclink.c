/* dclink.c — FW-08 DC-link trim: a regen limiter above the normal-range maximum. */
#include "dclink.h"

#include "ti_math.h"

void dcl_reset(dcl_t *d) { d->integ = 0.0f; }

float dcl_trim(dcl_t *d, float t_nm, float v_dc, float omega_mech_rad_s, float dt_s, const ti_params_t *p)
{
    if (!ti_finite(t_nm) || !ti_finite(omega_mech_rad_s)) {
        d->integ = 0.0f;
        return 0.0f;
    }
    const bool regen = (t_nm * omega_mech_rad_s) < 0.0f;
    if (!ti_finite(v_dc)) {
        d->integ = 0.0f;
        return regen ? 0.0f : t_nm; /* a link it cannot judge gets no regen */
    }
    const float e = v_dc - p->vdc_max_v; /* > 0: the link is above the normal range */
    const float lim = p->cal_dcl_tmax_nm;
    d->integ = ti_clampf(d->integ + (p->cal_dcl_ki_nm_vs * e * dt_s), 0.0f, lim);
    const float cut = ti_clampf((p->cal_dcl_kp_nm_v * e) + d->integ, 0.0f, lim);
    if (!regen) {
        return t_nm;
    }
    return (t_nm > 0.0f) ? ti_maxf(t_nm - cut, 0.0f) : ti_minf(t_nm + cut, 0.0f);
}
