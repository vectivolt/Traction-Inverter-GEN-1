/* torque.c — limits, derating, MTPA / field weakening, current circle. */
#include "torque.h"

float torque_p_max_w(float vdc, float i_rms_a, float p_rated_w, const ti_params_t *p)
{
    const float p_avail = sqrtf(1.5f) * p->fw03_mod_reserve * ti_maxf(vdc, 0.0f) * i_rms_a * p->fw03_pf;
    return ti_minf(p_rated_w, p_avail);
}

void torque_lim_init(torque_lim_t *l, const ti_params_t *p)
{
    *l = (torque_lim_t){0};
    l->derate = 1.0f;
    l->coolant_factor = 1.0f;
    l->i_limit_rms_a = p->i_pk_rms_a;
}

static float ramp_factor(float t, float start, float end)
{
    return ti_clampf((end - t) / (end - start), 0.0f, 1.0f);
}

void torque_derate_update(torque_lim_t *l, float t_mod_c, bool t_mod_valid, float t_cool_c, bool t_cool_valid,
                          float i_rms_now_a, float dt_s, const ti_params_t *p)
{
    /* module NTC: an invalid reading derates to the continuous rating, never up */
    if (t_mod_valid) {
        const float t_eff = l->derate_active ? (t_mod_c + p->cal_derate_hyst_c) : t_mod_c;
        l->derate = ramp_factor(t_eff, p->cal_tmod_derate_start_c, p->cal_tmod_derate_end_c);
    } else {
        l->derate = ti_minf(l->derate, p->i_cont_rms_a / p->i_pk_rms_a);
    }
    l->derate_active = l->derate < 1.0f;
    l->coolant_factor = t_cool_valid ? ramp_factor(t_cool_c, p->cal_coolant_derate_start_c, p->cal_coolant_derate_end_c)
                                     : 0.0f;
    /* 30 s peak budget; after exhaustion the peak returns only once fully recovered (FW-04) */
    if (i_rms_now_a > p->i_cont_rms_a) {
        l->peak_used_s += dt_s;
    } else {
        l->peak_used_s -= dt_s * (p->peak_time_s / p->cal_peak_recovery_s);
    }
    l->peak_used_s = ti_clampf(l->peak_used_s, 0.0f, p->peak_time_s);
    if (l->peak_used_s >= p->peak_time_s) {
        l->peak_exhausted = true;
    } else if (l->peak_used_s <= 0.0f) {
        l->peak_exhausted = false;
    } else {
        /* hold */
    }
    const float peak_extra = l->peak_exhausted ? 0.0f : ((p->i_pk_rms_a - p->i_cont_rms_a) * l->coolant_factor);
    l->i_limit_rms_a = (p->i_cont_rms_a + peak_extra) * ti_maxf(l->derate, 0.0f);
}

void torque_limits(torque_lim_t *l, float vdc, float omega_mech_rad_s, float p_chg_w, float p_dis_w, bool bms_fresh,
                   const ti_params_t *p)
{
    const float w = ti_maxf(ti_absf(omega_mech_rad_s), 1.0f);
    const float p_rated = (l->i_limit_rms_a > p->i_cont_rms_a) ? p->p_peak_w : p->p_cont_w;
    const float p_inv = torque_p_max_w(vdc, l->i_limit_rms_a, p_rated, p);
    const float p_motor = bms_fresh ? ti_minf(p_inv, ti_maxf(p_dis_w, 0.0f)) : p_inv;
    const float p_regen = bms_fresh ? ti_minf(p_inv, ti_maxf(p_chg_w, 0.0f)) : 0.0f; /* FW-11 */
    const float t_cap = p->cal_torque_max_nm * l->derate;
    l->t_lim_motor_nm = ti_minf(t_cap, p_motor / w);
    l->t_lim_regen_nm = ti_minf(t_cap, p_regen / w);
}

float torque_clamp(const torque_lim_t *l, float t_req_nm, float omega_mech_rad_s)
{
    if (!ti_finite(t_req_nm)) {
        return 0.0f;
    }
    const bool regen = (t_req_nm * omega_mech_rad_s) < 0.0f;
    const float lim = regen ? l->t_lim_regen_nm : l->t_lim_motor_nm;
    return ti_clampf(t_req_nm, -lim, lim);
}

float torque_mtpa_id(float iq, const motor_t *m)
{
    const float dl = m->lq_h - m->ld_h;
    if (ti_absf(dl) < 1.0e-7f) {
        return 0.0f;
    }
    return (m->psi_wb - sqrtf((m->psi_wb * m->psi_wb) + (4.0f * dl * dl * iq * iq))) / (2.0f * dl);
}

static bool lut_ok(const mtpa_lut_t *lut)
{
    if ((lut == NULL) || (lut->n < 2u) || (lut->n > MTPA_LUT_MAX)) {
        return false;
    }
    for (uint32_t i = 1u; i < lut->n; i++) {
        if (!(lut->t_nm[i] > lut->t_nm[i - 1u])) {
            return false;
        }
    }
    return true;
}

static void mtpa(float t, const motor_t *m, const mtpa_lut_t *lut, float *id, float *iq)
{
    const float mag = ti_absf(t);
    if (lut_ok(lut) && (mag <= lut->t_nm[lut->n - 1u]) && (lut->t_nm[0] <= 0.0f)) {
        uint32_t k = 1u;
        while ((k < (lut->n - 1u)) && (lut->t_nm[k] < mag)) {
            k++;
        }
        const float f = (mag - lut->t_nm[k - 1u]) / (lut->t_nm[k] - lut->t_nm[k - 1u]);
        *id = lut->id_a[k - 1u] + (f * (lut->id_a[k] - lut->id_a[k - 1u]));
        *iq = ti_signf(t) * (lut->iq_a[k - 1u] + (f * (lut->iq_a[k] - lut->iq_a[k - 1u])));
        return;
    }
    const float kt = 1.5f * (float)m->pp;
    float q = t / (kt * m->psi_wb);
    float d = 0.0f;
    for (uint32_t it = 0u; it < 8u; it++) { /* fixed-point on iq; converges in a few steps */
        d = torque_mtpa_id(q, m);
        q = t / (kt * (m->psi_wb + ((m->ld_h - m->lq_h) * d)));
    }
    *id = d;
    *iq = q;
}

float torque_v_required(float id, float iq, float omega_e, const motor_t *m)
{
    const float vd = (m->rs_ohm * id) - (omega_e * m->lq_h * iq);
    const float vq = (m->rs_ohm * iq) + (omega_e * ((m->ld_h * id) + m->psi_wb));
    return sqrtf((vd * vd) + (vq * vq));
}

float torque_v_available(float vdc, const ti_params_t *p)
{
    return (1.0f - p->cal_vdyn_reserve_frac) * p->cal_mod_index_max * ti_maxf(vdc, 0.0f) * (1.0f / TI_SQRT3);
}

#define WITNESS_STEPS 24u /* bisection: 2^-24 of the interval */

/* F23: the clamps above can leave a finite pair that the link cannot drive. |v|^2 is a convex
 * quadratic along each search line below, so the feasible part of a line is one interval and a
 * bisection from a feasible end finds its boundary; the result is checked once more at the end. */
static tq_res_t witness(float *d, float *q, float omega_e, float v_av, float i_max_a, const motor_t *m)
{
    if (torque_v_required(*d, *q, omega_e, m) <= v_av) {
        return TQ_OK;
    }
    if (torque_v_required(*d, 0.0f, omega_e, m) <= v_av) {
        /* 1) keep id, reduce |iq|: scale 0 is feasible, scale 1 is not */
        float lo = 0.0f;
        float hi = 1.0f;
        for (uint32_t k = 0u; k < WITNESS_STEPS; k++) {
            const float mid = 0.5f * (lo + hi);
            if (torque_v_required(*d, mid * *q, omega_e, m) <= v_av) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        *q *= lo;
    } else {
        /* 2) iq = 0, id toward the demagnetisation limit: d_best is the least-voltage id inside the
         * demagnetisation and current limits; if even it fails, nothing with iq = 0 is feasible */
        *q = 0.0f;
        const float d_min = ti_maxf(-m->id_demag_a, -i_max_a);
        const float w2 = omega_e * omega_e;
        const float d_v = -(w2 * m->ld_h * m->psi_wb) / ((m->rs_ohm * m->rs_ohm) + (w2 * m->ld_h * m->ld_h));
        const float d_best = ti_clampf(d_v, d_min, ti_maxf(i_max_a, d_min));
        if (!(torque_v_required(d_best, 0.0f, omega_e, m) <= v_av)) {
            *d = d_best;
            return TQ_INFEASIBLE;
        }
        float lo = d_best; /* feasible */
        float hi = *d;     /* not feasible: the boundary nearest the requested id */
        for (uint32_t k = 0u; k < WITNESS_STEPS; k++) {
            const float mid = 0.5f * (lo + hi);
            if (torque_v_required(mid, 0.0f, omega_e, m) <= v_av) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        *d = lo;
    }
    return (torque_v_required(*d, *q, omega_e, m) <= v_av) ? TQ_LIMITED : TQ_INFEASIBLE;
}

tq_res_t torque_to_current(float t_nm, float omega_e, float vdc, float i_max_a, const motor_t *m, const mtpa_lut_t *lut,
                           const ti_params_t *p, float *id, float *iq)
{
    *id = 0.0f;
    *iq = 0.0f;
    if (!ti_finite(t_nm) || !ti_finite(omega_e) || !ti_finite(vdc) || !ti_finite(i_max_a)) {
        return TQ_NONFINITE;
    }
    float d;
    float q;
    mtpa(t_nm, m, lut, &d, &q);
    /* field weakening: (Ld id + psi)^2 + (Lq iq)^2 <= (V/w)^2, on the voltage the witness allows */
    const float w = ti_absf(omega_e);
    const float v_av = torque_v_available(vdc, p);
    if (w > 1.0f) {
        const float v_lim = v_av;
        const float r2 = (v_lim / w) * (v_lim / w);
        const float lq_iq2 = (m->lq_h * q) * (m->lq_h * q);
        if (r2 > lq_iq2) {
            const float d_fw = (-m->psi_wb + sqrtf(r2 - lq_iq2)) / m->ld_h;
            d = ti_minf(d, d_fw);
        } else {
            q = ti_signf(q) * (v_lim / w) / m->lq_h;
            d = -m->psi_wb / m->ld_h;
        }
    }
    d = ti_maxf(d, -m->id_demag_a);         /* demagnetisation clamp */
    d = ti_clampf(d, -i_max_a, i_max_a);    /* the circle, d first (field weakening priority) */
    const float q_max = sqrtf(ti_maxf((i_max_a * i_max_a) - (d * d), 0.0f));
    q = ti_clampf(q, -q_max, q_max);
    if (!ti_finite(d) || !ti_finite(q)) {
        return TQ_NONFINITE;
    }
    const tq_res_t r = witness(&d, &q, omega_e, v_av, i_max_a, m);
    if (!ti_finite(d) || !ti_finite(q)) {
        return TQ_NONFINITE;
    }
    *id = d;
    *iq = q;
    return r;
}
