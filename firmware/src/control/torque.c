/* torque.c — limits, derating; torque -> current: MTPA and field weakening on the torque hyperbola, solved
 * together with the voltage ellipse, the current circle and the demagnetisation limit (round 23, FW-37). */
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

/* Round 23 (FW-45): with the saturation maps the flux linkages are psi + L_d(|id|) id and L_q(|iq|) iq (motor_sat: the
 * apparent inductance, the scalar exactly for a flat map), so T = 1.5 pp (lambda_d iq - lambda_q id) keeps its form. */
float torque_from_current(float id, float iq, const motor_t *m)
{
    float dd;
    float dq;
    const float ld = m->ld_h * motor_sat(m->ld_map_h, ti_absf(id), m->i_map_a, &dd);
    const float lq = m->lq_h * motor_sat(m->lq_map_h, ti_absf(iq), m->i_map_a, &dq);
    return 1.5f * (float)m->pp * (m->psi_wb + ((ld - lq) * id)) * iq;
}

float torque_v_required(float id, float iq, float omega_e, const motor_t *m)
{
    float dd;
    float dq;
    const float ld = m->ld_h * motor_sat(m->ld_map_h, ti_absf(id), m->i_map_a, &dd);
    const float lq = m->lq_h * motor_sat(m->lq_map_h, ti_absf(iq), m->i_map_a, &dq);
    const float vd = (m->rs_ohm * id) - (omega_e * lq * iq);
    const float vq = (m->rs_ohm * iq) + (omega_e * ((ld * id) + m->psi_wb));
    return sqrtf((vd * vd) + (vq * vq));
}

float torque_v_available(float vdc, const ti_params_t *p)
{
    return (1.0f - p->cal_vdyn_reserve_frac) * p->cal_mod_index_max * ti_maxf(vdc, 0.0f) * (1.0f / TI_SQRT3);
}

/* ---- torque -> current (round 23, FW-37) ----
 * Until round 22 the MTPA pair was moved by field weakening (id more negative) and by the current circle with iq
 * kept: with Lq > Ld the extra -id ADDS reluctance torque at the same iq, and the witness looked at the voltage
 * only — twice the requested torque on a salient motor. Now every point lies on the torque hyperbola
 *   iq = t / (kt psi_e(id)),  psi_e = psi + (Ld - Lq) id > 0,  kt = 1.5 pp
 * of the torque it stands for, with id in [d_lo, 0], d_lo = -min(i_max, id_demag) (and inside the hyperbola's end
 * psi_e = 0 when Ld > Lq). A point fits when |v| <= v_av (the voltage ellipse, the dynamic reserve kept) and
 * id^2 + iq^2 <= i_max^2 (the current circle). On a hyperbola |i|^2 = id^2 + iq^2 and |psi|^2 = (Ld id + psi)^2 +
 * (Lq iq)^2 are both convex in id (1/psi_e^2 of an affine psi_e > 0 is convex), and
 *   |v|^2 = Rs^2 |i|^2 + w^2 |psi|^2 + 2 Rs w t / kt
 * (its cross term 2 Rs w iq psi_e = 2 Rs w t / kt is constant on it) is convex too: each constraint holds on one
 * interval of id, so the searches below are bisections with an interval criterion, never a sampled guess. */
#define TQ_TOL_REL 1.0e-3f   /* the postcondition's torque band: 0.1 % ... */
#define TQ_TOL_ABS 1.0e-3f   /* ... + 1 mN m */
#define FIT_TOL 1.0e-6f      /* the postcondition's slack on the voltage, current and demagnetisation limits */
#define ID_TOL_A 1.0e-2f     /* the id searches stop at a 0.01 A interval (<= 19 halvings over 3000 A) ... */
#define SEARCH_STEPS 40u     /* ... or at this bound */
#define SCALE_TOL_NM 0.05f   /* torque reduction: stop at 0.05 N m (16 halvings of 2000 N m) ... */
#define SCALE_STEPS 24u      /* ... or at this bound */

typedef struct {
    const motor_t *m;
    float w;    /* omega_e */
    float v_av; /* torque_v_available() */
    float i2;   /* i_max^2 */
    float d_lo; /* the lowest id searched */
    float kt;   /* 1.5 pp */
    float dl;   /* Ld - Lq */
    bool sat;   /* round 23 (FW-45): a map is not flat — the contour and its slope with the maps (below) */
    float step;                /* ... the breakpoints' spacing, A, and its inverse */
    float inv_step;
    float ld_pt[MOTOR_MAP_N];  /* ... L_d, L_q at the breakpoints (the maps at the scalars' level), H */
    float lq_pt[MOTOR_MAP_N];
} tq_ctx_t;

/* A vector's torque against the request: the same sign (or zero), never above |t_req| (1 + 0.1 %) + 1 mN m and, when
 * exact, not below |t_req| (1 - 0.1 %) - 1 mN m. NaN fails. */
static bool t_within(float t_got, float t_req, bool exact)
{
    const float a = ti_absf(t_got);
    const float r = ti_absf(t_req);
    const bool sign_ok = (t_got == 0.0f) || (ti_signf(t_got) == ti_signf(t_req));
    const bool upper = a <= ((r * (1.0f + TQ_TOL_REL)) + TQ_TOL_ABS);
    const bool lower = !exact || (a >= ((r * (1.0f - TQ_TOL_REL)) - TQ_TOL_ABS));
    return sign_ok && upper && lower;
}

static bool volt_ok(const tq_ctx_t *c, float d, float q)
{
    return torque_v_required(d, q, c->w, c->m) <= c->v_av;
}

static bool fits(const tq_ctx_t *c, float d, float q)
{
    return (((d * d) + (q * q)) <= c->i2) && (d >= c->d_lo) && (d <= 0.0f) && volt_ok(c, d, q);
}

/* ---- round 23 (FW-45): the torque contour with the saturation maps ----
 * The FW-37 argument above holds for constant Ld, Lq; with the maps the contour is no hyperbola and |i|^2, |v|^2 along it
 * need not be convex. The same searches run on it (bisection on the slope's sign, on the voltage boundary, on the
 * reduction): each keeps its bracket and its step bound, so the solve still terminates and every point it returns is
 * checked by the postcondition; where convexity fails a search can end on a local optimum — a valid, not the least-current
 * (or the largest-torque) point (tests/test_fw45_46.c measures how often). A flat map never takes this path: the code
 * above, bit for bit. */

/* motor_sat() on the context's absolute points (no division): L at |i|, *ld its differential. */
static float pt_l(const tq_ctx_t *c, const float pt[MOTOR_MAP_N], float i_abs, float *ld)
{
    const float x = i_abs * c->inv_step;
    if (!(x < (float)(MOTOR_MAP_N - 1u))) {
        *ld = pt[MOTOR_MAP_N - 1u];
        return pt[MOTOR_MAP_N - 1u];
    }
    const uint32_t k = (uint32_t)x;
    const float s = pt[k + 1u] - pt[k];
    const float l = pt[k] + ((x - (float)k) * s);
    *ld = l + (x * s);
    return l;
}

/* For u = |iq| on the contour of t at d: u (lambda_d + |d| L_q(u)) = |t| / kt, lambda_d = psi + L_d(|d|) d. On each
 * segment of the L_q map a quadratic in u through 0; psi_e > 0 on [d_lo, 0] makes it grow without bound, so the first
 * breakpoint where it reaches |t| / kt brackets the root; the segment's quadratic in its stable form (no cancellation on a
 * flat segment); beyond the table, the last point's line. */
static float hyp_iq_sat(const tq_ctx_t *c, float t, float d)
{
    float dd;
    const float ad = -d;
    const float lam_d = c->m->psi_wb + (pt_l(c, c->ld_pt, ad, &dd) * d);
    const float tau = ti_absf(t) / c->kt;
    if (!(tau > 0.0f)) {
        return 0.0f;
    }
    float lq0 = c->lq_pt[0];
    for (uint32_t k = 0u; k < (MOTOR_MAP_N - 1u); k++) {
        const float u0 = c->step * (float)k;
        const float u1 = c->step * (float)(k + 1u);
        const float lq1 = c->lq_pt[k + 1u];
        if ((u1 * (lam_d + (ad * lq1))) >= tau) {
            const float s = (lq1 - lq0) * c->inv_step;         /* L_q(u) = lq0 + s (u - u0) */
            const float a2 = ad * s;                           /* u^2 */
            const float b1 = lam_d + (ad * (lq0 - (s * u0))); /* u */
            const float disc = ti_maxf((b1 * b1) + (4.0f * a2 * tau), 0.0f);
            return ti_signf(t) * ti_clampf((2.0f * tau) / (b1 + sqrtf(disc)), u0, u1);
        }
        lq0 = lq1;
    }
    return ti_signf(t) * (tau / (lam_d + (ad * lq0)));
}

/* iq on the hyperbola of t: the torque equation solved for iq (psi_e > 0 for every d >= d_lo) */
static float hyp_iq(const tq_ctx_t *c, float t, float d)
{
    if (c->sat) {
        return hyp_iq_sat(c, t, d);
    }
    return t / (c->kt * (c->m->psi_wb + (c->dl * d)));
}

/* The slope in d of (wi |i|^2 + wv |lambda|^2) / 2 along the contour, times its positive denominator: with q' = N / D from
 * dT = 0 (N = lambda_q - L_d' q, D = lambda_d - d L_q', the ' the differential inductances), D (wi (d + q q') + wv (lambda_d
 * L_d' + lambda_q L_q' q')). A non-physical map (D < 0) keeps the sign right; the searches only use the sign. */
static float hyp_slope_sat(const tq_ctx_t *c, float t, float d, float wi, float wv)
{
    float ldd;
    float lqd;
    const float q = hyp_iq_sat(c, t, d);
    const float lam_d = c->m->psi_wb + (pt_l(c, c->ld_pt, -d, &ldd) * d);
    const float lam_q = pt_l(c, c->lq_pt, ti_absf(q), &lqd) * q;
    const float den = lam_d - (d * lqd);
    const float num = lam_q - (ldd * q);
    const float s = (wi * ((d * den) + (q * num))) + (wv * ((lam_d * ldd * den) + (lam_q * lqd * num)));
    return (den < 0.0f) ? -s : s;
}

/* The slope in id of (wi |i|^2 + wv |psi|^2) / 2 along the hyperbola, times psi_e^3 > 0 (the sign is kept, no division):
 * wi (d psi_e^3 - k) + wv (Ld (Ld d + psi) psi_e^3 - Lq^2 k), with k = (t/kt)^2 (Ld - Lq) = -iq iq' psi_e^3. */
static float hyp_slope(const tq_ctx_t *c, float t, float k, float d, float wi, float wv)
{
    if (c->sat) {
        return hyp_slope_sat(c, t, d, wi, wv);
    }
    const motor_t *m = c->m;
    const float pe = m->psi_wb + (c->dl * d);
    const float pe3 = pe * pe * pe;
    return (wi * ((d * pe3) - k)) + (wv * ((m->ld_h * ((m->ld_h * d) + m->psi_wb) * pe3) - (m->lq_h * m->lq_h * k)));
}

/* The minimum on [lo, hi] of wi |i|^2 + wv |psi|^2 along the hyperbola of t: convex, so its slope changes sign once —
 * bisection on that sign. (1, 0): the least current, the MTPA point (Ld >= Lq: id = 0; the magnetising optimum of a
 * reverse-salient motor, id > 0, is not used). (Rs^2, w^2): the least voltage. */
static float argmin_in(const tq_ctx_t *c, float t, float k, float lo, float hi, float wi, float wv)
{
    if (!(hyp_slope(c, t, k, hi, wi, wv) > 0.0f)) {
        return hi; /* still falling at id = 0 */
    }
    if (!(hyp_slope(c, t, k, lo, wi, wv) < 0.0f)) {
        return lo; /* already rising at the lowest id */
    }
    for (uint32_t n = 0u; (n < SEARCH_STEPS) && ((hi - lo) > ID_TOL_A); n++) {
        const float mid = 0.5f * (lo + hi);
        if (hyp_slope(c, t, k, mid, wi, wv) > 0.0f) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return 0.5f * (lo + hi);
}

/* Round 23 (FW-45): wi |i|^2 + wv |lambda|^2 at the contour's point for d, with the maps. */
static float hyp_cost(const tq_ctx_t *c, float t, float d, float wi, float wv)
{
    float dd;
    float dq;
    const float q = hyp_iq(c, t, d);
    const float lam_d = c->m->psi_wb + (pt_l(c, c->ld_pt, -d, &dd) * d);
    const float lam_q = pt_l(c, c->lq_pt, ti_absf(q), &dq) * q;
    return (wi * ((d * d) + (q * q))) + (wv * ((lam_d * lam_d) + (lam_q * lam_q)));
}

/* The minimum on [d_lo, 0] (above). Round 23 (FW-45): with a saturating map the cost along the contour can have more than
 * one local minimum — a saturating L_q below L_d turns the reluctance torque over at high |i_q|, a saturating L_d on an SPM
 * makes one — so the slope's sign is first read at SCAN_N + 1 points: each sign change - to + (and an end the cost falls
 * toward) brackets a local minimum, each is refined by the bisection, the least is taken. Bounded: SCAN_N + 1 slopes,
 * at most SCAN_N / 2 + 1 bisections. A minimum narrower than a scan step can still be missed: the point returned is then a
 * local one — valid, checked like any other. */
#define SCAN_N 4u

static float hyp_argmin(const tq_ctx_t *c, float t, float wi, float wv)
{
    const float tau = t / c->kt;
    const float k = tau * tau * c->dl;
    if (!c->sat) {
        return argmin_in(c, t, k, c->d_lo, 0.0f, wi, wv); /* FW-37, bit for bit */
    }
    float dp = c->d_lo;
    float sp = hyp_slope(c, t, k, dp, wi, wv);
    float best_d = dp;
    float best = (sp < 0.0f) ? INFINITY : hyp_cost(c, t, dp, wi, wv); /* rising at the lowest id: a candidate */
    for (uint32_t j = 1u; j <= SCAN_N; j++) {
        const float dn = (j == SCAN_N) ? 0.0f : (c->d_lo * (1.0f - ((float)j / (float)SCAN_N)));
        const float sn = hyp_slope(c, t, k, dn, wi, wv);
        float cand = NAN;
        if ((sp < 0.0f) && (sn >= 0.0f)) {
            cand = argmin_in(c, t, k, dp, dn, wi, wv);
        } else if ((j == SCAN_N) && (sn < 0.0f)) {
            cand = 0.0f; /* still falling at id = 0 */
        } else {
            /* no minimum in this step */
        }
        if (ti_finite(cand)) {
            const float cj = hyp_cost(c, t, cand, wi, wv);
            if (cj < best) {
                best = cj;
                best_d = cand;
            }
        }
        dp = dn;
        sp = sn;
    }
    return best_d;
}

/* (a) + (b): the least-current point of the hyperbola of t that fits, if any. The MTPA point d_m is the least-current
 * point of the hyperbola: if its current is above i_max, nothing on it fits. If only its voltage fails, the ids whose
 * voltage fits form one interval holding the least-voltage point d_v (none if d_v fails); the current grows
 * monotonically away from d_m, so the least-current point meeting the voltage is that interval's end nearest d_m —
 * a bisection between d_v (fits) and d_m (does not) — and it fits the circle only if the current there does. */
static bool hyp_point(const tq_ctx_t *c, float t, float *d, float *q)
{
    float in = hyp_argmin(c, t, 1.0f, 0.0f);
    float qi = hyp_iq(c, t, in);
    if (((in * in) + (qi * qi)) > c->i2) {
        return false;
    }
    if (!volt_ok(c, in, qi)) {
        float out = in;
        in = hyp_argmin(c, t, c->m->rs_ohm * c->m->rs_ohm, c->w * c->w);
        if (!volt_ok(c, in, hyp_iq(c, t, in))) {
            return false;
        }
        for (uint32_t n = 0u; (n < SEARCH_STEPS) && (ti_absf(out - in) > ID_TOL_A); n++) {
            const float mid = 0.5f * (in + out);
            if (volt_ok(c, mid, hyp_iq(c, t, mid))) {
                in = mid;
            } else {
                out = mid;
            }
        }
        qi = hyp_iq(c, t, in);
        if (!fits(c, in, qi)) {
            return false;
        }
    }
    *d = in;
    *q = qi;
    return true;
}

/* The calibration LUT's point for t, used only when it reproduces t within the TQ_OK band (its torque recomputed from
 * its own id, iq): a table that is not this motor's — or a segment whose linear interpolation strays off the
 * hyperbola by more than that — gives way to the solved MTPA point. */
static bool lut_point(const mtpa_lut_t *lut, float t, const motor_t *m, float *id, float *iq)
{
    const float mag = ti_absf(t);
    if (!lut_ok(lut) || !(mag <= lut->t_nm[lut->n - 1u]) || !(lut->t_nm[0] <= 0.0f)) {
        return false;
    }
    uint32_t k = 1u;
    while ((k < (lut->n - 1u)) && (lut->t_nm[k] < mag)) {
        k++;
    }
    const float f = (mag - lut->t_nm[k - 1u]) / (lut->t_nm[k] - lut->t_nm[k - 1u]);
    *id = lut->id_a[k - 1u] + (f * (lut->id_a[k] - lut->id_a[k - 1u]));
    *iq = ti_signf(t) * (lut->iq_a[k - 1u] + (f * (lut->iq_a[k] - lut->iq_a[k - 1u])));
    return t_within(torque_from_current(*id, *iq, m), t, true);
}

static bool solve(const tq_ctx_t *c, float t, const mtpa_lut_t *lut, float *d, float *q)
{
    float dt = 0.0f;
    float qt = 0.0f;
    if (lut_point(lut, t, c->m, &dt, &qt) && fits(c, dt, qt)) {
        *d = dt;
        *q = qt;
        return true;
    }
    return hyp_point(c, t, d, q);
}

/* (d) From the returned vector alone, whatever path produced it. */
static bool postcondition(tq_res_t r, float t_req, float d, float q, float omega_e, float v_av, float i_max,
                          const motor_t *m)
{
    const bool torque_ok = t_within(torque_from_current(d, q, m), t_req, r == TQ_OK);
    const bool volt_fit = (r == TQ_INFEASIBLE) ? (q == 0.0f)
                                               : (torque_v_required(d, q, omega_e, m) <= (v_av * (1.0f + FIT_TOL)));
    const bool curr_fit = sqrtf((d * d) + (q * q)) <= (i_max * (1.0f + FIT_TOL));
    const bool demag_fit = d >= -(m->id_demag_a * (1.0f + FIT_TOL));
    return torque_ok && volt_fit && curr_fit && demag_fit;
}

static bool map_flat(const float map[MOTOR_MAP_N])
{
    bool flat = true;
    for (uint32_t k = 1u; k < MOTOR_MAP_N; k++) {
        flat = flat && (map[k] == map[0]);
    }
    return flat;
}

/* Bounded: each id search <= SEARCH_STEPS halvings, a solve <= 3 of them, the reduction <= SCALE_STEPS solves (FW-45: on a
 * map a minimum search is SCAN_N + 1 slopes and at most SCAN_N / 2 + 1 of those halvings). */
tq_res_t torque_to_current(float t_nm, float omega_e, float vdc, float i_max_a, const motor_t *m, const mtpa_lut_t *lut,
                           const ti_params_t *p, float *id, float *iq)
{
    *id = 0.0f;
    *iq = 0.0f;
    if (!ti_finite(t_nm) || !ti_finite(omega_e) || !ti_finite(vdc) || !ti_finite(i_max_a)) {
        return TQ_NONFINITE;
    }
    const float i_max = ti_maxf(i_max_a, 0.0f);
    const float d_min = -ti_minf(i_max, m->id_demag_a); /* the demagnetisation clamp and the circle */
    const float dl = m->ld_h - m->lq_h;
    tq_ctx_t c = {.m = m, .w = omega_e, .v_av = torque_v_available(vdc, p), .i2 = i_max * i_max,
                  .kt = 1.5f * (float)m->pp, .dl = dl, .sat = !map_flat(m->ld_map_h) || !map_flat(m->lq_map_h),
                  .step = m->i_map_a / (float)(MOTOR_MAP_N - 1u), .inv_step = (float)(MOTOR_MAP_N - 1u) / m->i_map_a};
    /* round 23 (FW-45): with the maps psi_e = psi + (L_d(|d|) - L_q(|q|)) d >= psi + (max L_d - min L_q) d on d <= 0 */
    float dl_end = dl;
    if (c.sat) {
        float ld_max = 0.0f;
        float lq_min = INFINITY;
        for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
            const bool nd = m->ld_map_h[0] == 0.0f; /* no map: the scalar */
            const bool nq = m->lq_map_h[0] == 0.0f;
            c.ld_pt[k] = nd ? m->ld_h : (m->ld_h * (m->ld_map_h[k] / m->ld_map_h[0]));
            c.lq_pt[k] = nq ? m->lq_h : (m->lq_h * (m->lq_map_h[k] / m->lq_map_h[0]));
            ld_max = ti_maxf(ld_max, c.ld_pt[k]);
            lq_min = ti_minf(lq_min, c.lq_pt[k]);
        }
        dl_end = ld_max - lq_min;
    }
    const float d_end = (dl_end > 0.0f) ? (-0.999f * m->psi_wb / dl_end) : d_min; /* Ld > Lq: stay inside psi_e = 0 */
    c.d_lo = ti_maxf(d_min, d_end);
    float d = 0.0f;
    float q = 0.0f;
    tq_res_t r = TQ_OK;
    if (!solve(&c, t_nm, lut, &d, &q)) {
        /* F23: zero torque — iq = 0 at the least-voltage id inside the demagnetisation and current limits */
        const float w2 = omega_e * omega_e;
        const float d_v = c.sat ? hyp_argmin(&c, 0.0f, m->rs_ohm * m->rs_ohm, w2) /* FW-45: along iq = 0 */
                                : (-(w2 * m->ld_h * m->psi_wb) / ((m->rs_ohm * m->rs_ohm) + (w2 * m->ld_h * m->ld_h)));
        d = ti_clampf(d_v, d_min, 0.0f);
        q = 0.0f;
        if (!(torque_v_required(d, 0.0f, omega_e, m) <= c.v_av)) {
            r = TQ_INFEASIBLE;
        } else {
            /* (c) Reduce the torque: the largest s in (0, 1] whose hyperbola of s t has a point that fits. Monotone in s:
             * the set S that fits (id in [d_min, 0], the circle, the ellipse — v is affine in (id, iq)) is convex and
             * holds the zero-torque point (d, 0) just checked. If P1 in S has torque s1 t, the segment from (d, 0) to P1
             * lies in S; along it iq = lambda iq1 keeps the sign of t while T runs continuously from 0 to s1 t, so every
             * s in (0, s1] has a point of S with torque s t — where T and iq share the sign of t, so psi_e > 0: on the
             * hyperbola hyp_point searches. The start: the least-current zero-torque point. */
            (void)hyp_point(&c, 0.0f, &d, &q);
            float lo = 0.0f;
            float hi = 1.0f;
            for (uint32_t k = 0u; (k < SCALE_STEPS) && (((hi - lo) * ti_absf(t_nm)) > SCALE_TOL_NM); k++) {
                const float s = 0.5f * (lo + hi);
                float ds = 0.0f;
                float qs = 0.0f;
                if (solve(&c, s * t_nm, lut, &ds, &qs)) {
                    lo = s;
                    d = ds;
                    q = qs;
                } else {
                    hi = s;
                }
            }
            r = (t_nm != 0.0f) ? TQ_LIMITED : TQ_OK;
        }
    }
    if (!ti_finite(d) || !ti_finite(q)) {
        return TQ_NONFINITE;
    }
    if (!postcondition(r, t_nm, d, q, omega_e, c.v_av, i_max, m)) {
        return TQ_POSTCOND;
    }
    *id = d;
    *iq = q;
    return r;
}

/* ---- round 23 (FW-46): the torque-ripple feed-forward ---- */
float torque_ripple_at(const int16_t tab[TQ_RIPPLE_N], float mean_cnt, float theta_e)
{
    if (!ti_finite(theta_e)) {
        return 0.0f;
    }
    const bool in = (theta_e >= 0.0f) && (theta_e < TI_2PI); /* the FOC's angle is: no fmodf in the ISR */
    const float x = (in ? theta_e : ti_wrap_2pi(theta_e)) * ((float)TQ_RIPPLE_N / TI_2PI);
    uint32_t k = (uint32_t)x;
    k = (k < TQ_RIPPLE_N) ? k : (TQ_RIPPLE_N - 1u); /* a wrap rounded up to 2 pi */
    const float a = (float)tab[k];
    const float b = (float)tab[(k + 1u) % TQ_RIPPLE_N];
    return 0.01f * ((a + ((x - (float)k) * (b - a))) - mean_cnt); /* in counts first: a constant table is exactly 0 */
}

static bool ff_fits(float d, float q, float w, float v_av, float i_max, const motor_t *m)
{
    return (sqrtf((d * d) + (q * q)) <= i_max) && (torque_v_required(d, q, w, m) <= v_av);
}

/* Bounded: 2 + 2 x 12 checks. */
float torque_ripple_scale(float id, float iq, float ff_lo, float ff_hi, float omega_e, float vdc, float i_max_a,
                          const motor_t *m, const ti_params_t *p)
{
    const float v_av = torque_v_available(vdc, p);
    const float i_max = ti_maxf(i_max_a, 0.0f);
    if (!ff_fits(id, iq, omega_e, v_av, i_max, m)) {
        return 0.0f;
    }
    float lo = 0.0f;
    float hi = 1.0f;
    for (uint32_t n = 0u; n <= 12u; n++) {
        const float k = (n == 0u) ? 1.0f : (0.5f * (lo + hi)); /* all of it first */
        if (ff_fits(id, iq + (k * ff_hi), omega_e, v_av, i_max, m) && ff_fits(id, iq + (k * ff_lo), omega_e, v_av, i_max, m)) {
            lo = k;
            if (n == 0u) {
                break;
            }
        } else {
            hi = k;
        }
    }
    return lo;
}
