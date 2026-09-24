/* foc.c — current loop (runs in the PWM-synchronised ISR, twice per PWM period). */
#include "foc.h"

void foc_reset(foc_t *f)
{
    const bool guard = f->guard_trip;
    *f = (foc_t){0};
    f->guard_trip = guard;
    for (uint32_t i = 0u; i < 3u; i++) {
        f->duty[i] = 0.5f;
    }
}

void foc_clarke(const float iabc[3], float *ia, float *ib)
{
    *ia = ((2.0f * iabc[0]) - iabc[1] - iabc[2]) * (1.0f / 3.0f);
    *ib = (iabc[1] - iabc[2]) * (1.0f / TI_SQRT3);
}

void foc_park(float a, float b, float th, float *d, float *q)
{
    const float c = cosf(th);
    const float s = sinf(th);
    *d = (a * c) + (b * s);
    *q = (-a * s) + (b * c);
}

void foc_ipark(float d, float q, float th, float *a, float *b)
{
    const float c = cosf(th);
    const float s = sinf(th);
    *a = (d * c) - (q * s);
    *b = (d * s) + (q * c);
}

void foc_svpwm(float va, float vb, float vdc, float duty[3])
{
    const float v[3] = {va, (-0.5f * va) + (0.5f * TI_SQRT3 * vb), (-0.5f * va) - (0.5f * TI_SQRT3 * vb)};
    const float vmax = ti_maxf(v[0], ti_maxf(v[1], v[2]));
    const float vmin = ti_minf(v[0], ti_minf(v[1], v[2]));
    const float off = 0.5f * (vmax + vmin);
    for (uint32_t i = 0u; i < 3u; i++) {
        duty[i] = 0.5f + ((v[i] - off) / vdc);
    }
}

void foc_dt_comp(float duty[3], const float iabc[3], float dt_frac, float band_a)
{
    for (uint32_t i = 0u; i < 3u; i++) {
        duty[i] += ti_clampf(iabc[i] / band_a, -1.0f, 1.0f) * dt_frac;
    }
}

static bool all_finite(const float *x, uint32_t n)
{
    bool ok = true;
    for (uint32_t i = 0u; i < n; i++) {
        ok = ok && ti_finite(x[i]);
    }
    return ok;
}

/* conditional integration: an axis integrates unless its output is limited AND the error would
 * push it further into the limit (a back-calculation that rewrites the integrator whenever the
 * proportional term alone exceeds the limit leaves a long negative tail) */
static float integrate(float xi, float e, float v, float v_u, float ki_ts, float lim)
{
    const bool limited = (v != v_u);
    if (!limited || ((e * v_u) < 0.0f)) {
        xi += ki_ts * e;
    }
    return ti_clampf(xi, -lim, lim);
}

static void pi_axes(foc_t *f, float ed, float eq, float ffd, float ffq, const gain_set_t *g)
{
    const float vd_u = (g->kp_v_per_a * ed) + f->xi_d + ffd;
    const float vq_u = (g->kp_v_per_a * eq) + f->xi_q + ffq;
    f->vd = ti_clampf(vd_u, -f->vmax, f->vmax);
    const float vq_lim = sqrtf(ti_maxf((f->vmax * f->vmax) - (f->vd * f->vd), 0.0f));
    f->vq = ti_clampf(vq_u, -vq_lim, vq_lim);
    f->sat = (f->vd != vd_u) || (f->vq != vq_u);
    const float ki_ts = g->ki_v_per_as * g->ts_s;
    f->xi_d = integrate(f->xi_d, ed, f->vd, vd_u, ki_ts, f->vmax);
    f->xi_q = integrate(f->xi_q, eq, f->vq, vq_u, ki_ts, f->vmax);
}

bool foc_step(foc_t *f, const float iabc[3], float theta_e, float omega_e, float vdc, const motor_t *m,
              const gain_set_t *g, const ti_params_t *p)
{
    const float in[4] = {theta_e, omega_e, f->id_ref, f->iq_ref};
    if (!all_finite(iabc, 3u) || !all_finite(in, 4u) || !ti_finite(vdc) || (vdc <= 1.0f)) {
        f->guard_trip = true;
        return false;
    }
    float ia;
    float ib;
    foc_clarke(iabc, &ia, &ib);
    foc_park(ia, ib, theta_e, &f->id, &f->iq);
    f->vmax = p->cal_mod_index_max * vdc * (1.0f / TI_SQRT3);
    const float ffd = -omega_e * m->lq_h * f->iq;
    const float ffq = omega_e * ((m->ld_h * f->id) + m->psi_wb);
    pi_axes(f, f->id_ref - f->id, f->iq_ref - f->iq, ffd, ffq, g);
    float va;
    float vb;
    foc_ipark(f->vd, f->vq, theta_e + (omega_e * g->delay_s), &va, &vb);
    float d[3];
    foc_svpwm(va, vb, vdc, d);
    foc_dt_comp(d, iabc, (float)p->dead_time_ns * 1.0e-9f * (float)g->fsw_hz, p->cal_dtcomp_band_a);
    for (uint32_t i = 0u; i < 3u; i++) {
        d[i] = ti_clampf(d[i], 0.0f, 1.0f);
    }
    const float st[2] = {f->xi_d, f->xi_q};
    if (!all_finite(d, 3u) || !all_finite(st, 2u)) {
        f->guard_trip = true;
        return false;
    }
    for (uint32_t i = 0u; i < 3u; i++) {
        f->duty[i] = d[i];
    }
    return true;
}

float foc_omega_model(const foc_t *f, const motor_t *m, bool *valid)
{
    const float flux = (m->ld_h * f->id) + m->psi_wb;
    *valid = !f->sat && (ti_absf(flux) > (0.2f * m->psi_wb));
    return *valid ? ((f->vq - (m->rs_ohm * f->iq)) / flux) : 0.0f;
}
