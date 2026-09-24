/* dclink.c — FW-08 DC-link voltage controller. */
#include "dclink.h"

#include "ti_math.h"

void dcl_reset(dcl_t *d) { d->integ = 0.0f; }

float dcl_step(dcl_t *d, float v_ref, float v_dc, float omega_mech_rad_s, float dt_s, const ti_params_t *p)
{
    if (!ti_finite(v_dc) || !ti_finite(v_ref) || !ti_finite(omega_mech_rad_s)) {
        d->integ = 0.0f;
        return 0.0f;
    }
    const float e = v_dc - v_ref; /* link too high => consume energy => motoring */
    const float lim = p->cal_dcl_tmax_nm;
    d->integ = ti_clampf(d->integ + (p->cal_dcl_ki_nm_vs * e * dt_s), -lim, lim);
    const float t = ti_clampf((p->cal_dcl_kp_nm_v * e) + d->integ, -lim, lim);
    return (omega_mech_rad_s >= 0.0f) ? t : -t;
}
