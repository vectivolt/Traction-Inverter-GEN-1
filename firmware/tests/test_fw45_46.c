/* test_fw45_46.c — round 23, FW-45 (saturation-dependent L_d/L_q in the torque solve, the current loop and FW-39's
 * commissioning) and FW-46 (the torque-ripple feed-forward), contract §10l / §10m. */
#include <string.h>

#include "calib.h"
#include "commission.h"
#include "dtc.h"
#include "foc.h"
#include "harness.h"
#include "nvlog.h"
#include "sim_pmsm.h"
#include "test.h"
#include "torque.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846 /* not ISO C: newlib and glibc hide it under -std=c11 */
#endif

/* ======================= a random reference (the FW-37 oracle's case distribution) ======================= */
static uint32_t s_rng;

static double urand(double a, double b)
{
    s_rng = (s_rng * 1664525u) + 1013904223u;
    return a + ((b - a) * ((double)(s_rng >> 8) / 16777216.0));
}

static const ti_sku_t SKUS[4] = {TI_SKU_8XX_SIC, TI_SKU_8XX_IGBT, TI_SKU_4XX_IGBT, TI_SKU_4XX_SIC};

/* A motor inside the FW-20 class ranges, as the FW-37 reference drew them: SPM, salient, strongly salient, reverse
 * salient; case i. No map (the scalars). */
static motor_t rnd_motor(uint32_t i)
{
    motor_t m = motor_screening();
    const uint32_t kind = i % 6u;
    m.ld_h = (float)urand(0.05e-3, 1.0e-3);
    m.lq_h = (kind == 0u) ? m.ld_h
           : ((kind == 4u) ? (float)(m.ld_h * urand(0.3, 0.95))
                           : ((kind == 5u) ? (float)(m.ld_h * urand(5.0, 10.0)) : (float)(m.ld_h * urand(1.05, 5.0))));
    m.lq_h = fminf(m.lq_h, 5e-3f);
    m.psi_wb = (float)((kind == 5u) ? urand(0.02, 0.06) : urand(0.02, 0.3));
    m.rs_ohm = (float)urand(0.002, 0.2);
    m.pp = (uint8_t)(2u + (i % 7u));
    m.id_demag_a = (float)urand(10.0, 1500.0);
    return m;
}

typedef struct {
    float t, w, vdc, imax;
} tq_case_t;

static tq_case_t rnd_case(uint32_t i)
{
    tq_case_t c = {.t = (float)(urand(-1.0, 1.0) * urand(0.0, 1.0) * 1500.0), .w = (float)(urand(-1.0, 1.0) * urand(0.0, 1.0) * 20000.0),
                   .vdc = (float)urand(0.0, 900.0), .imax = (float)urand(0.0, 800.0)};
    c.imax = ((i % 23u) == 0u) ? 0.0f : c.imax;
    c.vdc = ((i % 29u) == 0u) ? 0.0f : c.vdc;
    c.t = ((i % 17u) == 0u) ? 0.0f : c.t;
    return c;
}

/* A physical saturation map at the scalar l0 (point 0 = l0): the secant of a flux that bends over — L0 Is atan(i / Is)
 * (Is from 0.35 to 4 x the full scale) or a knee (linear to i0, then that) — or, one axis in six, flat. */
static void rnd_map(float map[MOTOR_MAP_N], float l0, float i_map)
{
    const double r = urand(0.0, 1.0);
    const double is = i_map * urand(0.35, 4.0);
    const double i0 = (r < 0.45) ? 0.0 : (i_map * urand(0.0, 0.6));
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
        const double i = i_map * (double)k / (double)(MOTOR_MAP_N - 1u);
        const double flux = (i <= i0) ? i : (i0 + (is * atan((i - i0) / is)));
        map[k] = (r > 0.84) ? l0 : (float)((k == 0u) ? l0 : (l0 * flux / i));
    }
}

/* ======================= an independent model in double (the maps as FW-45 defines them) ======================= */
static double shape(const float map[MOTOR_MAP_N], double i, double i_map)
{
    if (map[0] == 0.0f) {
        return 1.0;
    }
    const double x = fabs(i) * (double)(MOTOR_MAP_N - 1u) / i_map;
    if (x >= (double)(MOTOR_MAP_N - 1u)) {
        return (double)map[MOTOR_MAP_N - 1u] / (double)map[0];
    }
    const int k = (int)x;
    return ((double)map[k] + ((x - k) * ((double)map[k + 1] - (double)map[k]))) / (double)map[0];
}

static double lam_d(const motor_t *m, double d) { return m->psi_wb + (m->ld_h * shape(m->ld_map_h, d, m->i_map_a) * d); }
static double lam_q(const motor_t *m, double q) { return m->lq_h * shape(m->lq_map_h, q, m->i_map_a) * q; }
static double t_sat(const motor_t *m, double d, double q) { return 1.5 * m->pp * ((lam_d(m, d) * q) - (lam_q(m, q) * d)); }

static double v_sat(const motor_t *m, double d, double q, double w)
{
    const double vd = (m->rs_ohm * d) - (w * lam_q(m, q));
    const double vq = (m->rs_ohm * q) + (w * lam_d(m, d));
    return sqrt((vd * vd) + (vq * vq));
}

/* |q| on the contour of |t| at d (the torque rises with |q| for a physical map): bisection */
static double contour_q(const motor_t *m, double t, double d)
{
    const double at = fabs(t);
    double hi = 1.0;
    while ((t_sat(m, d, hi) < at) && (hi < 1e6)) {
        hi *= 2.0;
    }
    double lo = 0.0;
    for (int n = 0; n < 60; n++) {
        const double mid = 0.5 * (lo + hi);
        if (t_sat(m, d, mid) < at) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return ((t < 0.0) ? -1.0 : 1.0) * 0.5 * (lo + hi);
}

#define ND 200

/* the least current of the contour of t that fits (INFINITY: none), a fine scan of d; margin: the limits shrunk by it */
static double o_least(const motor_t *m, double t, double w, double v_av, double imax, double margin)
{
    const double d_lo = -fmin(imax, m->id_demag_a);
    double best = INFINITY;
    for (int k = 0; k <= ND; k++) {
        const double d = d_lo * k / ND;
        const double q = contour_q(m, t, d);
        const double mag = sqrt((d * d) + (q * q));
        if ((lam_d(m, d) + fabs(d) * m->lq_h * shape(m->lq_map_h, q, m->i_map_a) > 0.0) &&
            (mag <= imax * (1.0 - margin)) && (v_sat(m, d, q, w) <= v_av * (1.0 - margin))) {
            best = fmin(best, mag);
        }
    }
    return best;
}

/* the largest torque of sign sg that fits (-INFINITY: nothing): at each d the largest |q| inside the circle and the
 * ellipse (a coarse downward scan, then a bisection of the voltage boundary) — the torque rises with |q| */
static double o_tmax(const motor_t *m, double sg, double w, double v_av, double imax)
{
    const double d_lo = -fmin(imax, m->id_demag_a);
    double best = -INFINITY;
    for (int k = 0; k <= ND; k++) {
        const double d = d_lo * k / ND;
        const double qc2 = (imax * imax) - (d * d);
        if (qc2 < 0.0) {
            continue;
        }
        const double qc = sqrt(qc2);
        double q_ok = -1.0;
        double q_bad = qc;
        if (v_sat(m, d, sg * qc, w) <= v_av) {
            q_ok = qc;
        } else {
            for (int j = 31; j >= 0; j--) {
                const double q = qc * j / 32.0;
                if (v_sat(m, d, sg * q, w) <= v_av) {
                    q_ok = q;
                    break;
                }
                q_bad = q;
            }
            for (int n = 0; (n < 30) && (q_ok >= 0.0); n++) {
                const double mid = 0.5 * (q_ok + q_bad);
                if (v_sat(m, d, sg * mid, w) <= v_av) {
                    q_ok = mid;
                } else {
                    q_bad = mid;
                }
            }
        }
        if (q_ok >= 0.0) {
            best = fmax(best, sg * t_sat(m, d, sg * q_ok));
        }
    }
    return best;
}

static bool zero_ok(const motor_t *m, double w, double v_av, double imax)
{
    const double d_lo = -fmin(imax, m->id_demag_a);
    for (int k = 0; k <= ND; k++) {
        if (v_sat(m, d_lo * k / ND, 0.0, w) <= v_av) {
            return true;
        }
    }
    return false;
}

/* ======================= FW-45: the solve ======================= */

/* A flat map is the scalar solve bit for bit: 20 000 cases of the FW-37 reference's distribution, the four SKUs, all four
 * quadrants — each motor with no map, with a flat map at its scalar, and with a flat map at another level (a map is its
 * shape at the scalar's level): the same result, i_d, i_q, torque and voltage, bit for bit. */
TEST(a_flat_map_is_the_scalar_solve_bit_for_bit)
{
    s_rng = 20260927u;
    unsigned diff = 0u;
    unsigned seen[5] = {0u, 0u, 0u, 0u, 0u};
    for (uint32_t i = 0u; i < 20000u; i++) {
        const ti_params_t *p = ti_params_get(SKUS[i % 4u]);
        const motor_t m = rnd_motor(i);
        const tq_case_t c = rnd_case(i);
        motor_t f = m;
        const float lvl = ((i % 3u) == 0u) ? 1.7f : 1.0f;
        for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
            f.ld_map_h[k] = lvl * m.ld_h;
            f.lq_map_h[k] = lvl * m.lq_h;
        }
        f.i_map_a = p->i_crest_a;
        float id0 = 0.0f;
        float iq0 = 0.0f;
        float id1 = 0.0f;
        float iq1 = 0.0f;
        const tq_res_t r0 = torque_to_current(c.t, c.w, c.vdc, c.imax, &m, NULL, p, &id0, &iq0);
        const tq_res_t r1 = torque_to_current(c.t, c.w, c.vdc, c.imax, &f, NULL, p, &id1, &iq1);
        const float t0 = torque_from_current(id0, iq0, &m);
        const float t1 = torque_from_current(id1, iq1, &f);
        const float v0 = torque_v_required(id0, iq0, c.w, &m);
        const float v1 = torque_v_required(id1, iq1, c.w, &f);
        diff += ((r0 == r1) && (memcmp(&id0, &id1, sizeof id0) == 0) && (memcmp(&iq0, &iq1, sizeof iq0) == 0) &&
                 (memcmp(&t0, &t1, sizeof t0) == 0) && (memcmp(&v0, &v1, sizeof v0) == 0)) ? 0u : 1u;
        seen[(r0 <= TQ_POSTCOND) ? r0 : 0] += 1u;
    }
    CHECK(diff == 0u);
    CHECK(seen[TQ_OK] > 4000u && seen[TQ_LIMITED] > 4000u && seen[TQ_INFEASIBLE] > 4000u); /* every outcome exercised */
    printf("    flat map vs scalar: 20000 cases, %u differ (OK %u, LIMITED %u, INFEASIBLE %u, NONFINITE %u, POSTCOND %u)\n",
           diff, seen[TQ_OK], seen[TQ_LIMITED], seen[TQ_INFEASIBLE], seen[TQ_NONFINITE], seen[TQ_POSTCOND]);
}

/* The solve on saturating maps: 20 000 cases (the distribution above, the four SKUs, four quadrants), each axis a random
 * physical map (one in six flat). Every result is checked against the model in double: no TQ_POSTCOND or TQ_NONFINITE;
 * TQ_OK / TQ_LIMITED inside the ellipse, the circle and the demagnetisation limit, never more torque than asked, the
 * request's sign; TQ_OK within 0.1 % of it; TQ_LIMITED only where the request does not fit (the largest torque that fits,
 * from a scan of both currents, below it + 0.01 %), and then within 0.1 N m of that largest torque;
 * TQ_INFEASIBLE only where no i_d holds the voltage at i_q = 0. The least current of TQ_OK (every fourth case) and the
 * reduction's shortfall are measured: a point that is valid but not the optimum is counted and reported, not failed.
 * Before FW-45 (the scalar solve, the maps ignored) the same machines get the vector of the unsaturated model: counted. */
TEST(the_saturated_solve_meets_the_references_on_random_saturating_maps)
{
    s_rng = 45u;
    unsigned bad = 0u;
    unsigned n[5] = {0u, 0u, 0u, 0u, 0u};
    unsigned missed = 0u;
    unsigned sub_cur = 0u;
    unsigned sub_lim = 0u;
    unsigned old_off = 0u;
    unsigned old_ok = 0u;
    double worst_cur = 0.0;
    double worst_lim = 0.0;
    double worst_old = 0.0;
    for (uint32_t i = 0u; i < 20000u; i++) {
        const ti_params_t *p = ti_params_get(SKUS[i % 4u]);
        motor_t m = rnd_motor(i);
        m.i_map_a = p->i_crest_a;
        rnd_map(m.ld_map_h, m.ld_h, m.i_map_a);
        rnd_map(m.lq_map_h, m.lq_h, m.i_map_a);
        const tq_case_t c = rnd_case(i);
        float id = 0.0f;
        float iq = 0.0f;
        const tq_res_t r = torque_to_current(c.t, c.w, c.vdc, c.imax, &m, NULL, p, &id, &iq);
        n[(r <= TQ_POSTCOND) ? r : 0] += 1u;
        const double v_av = torque_v_available(c.vdc, p);
        const double t = c.t;
        const double tg = t_sat(&m, id, iq);
        unsigned b = 0u;
        if ((r == TQ_OK) || (r == TQ_LIMITED)) {
            b += (v_sat(&m, id, iq, c.w) <= v_av * 1.00001) ? 0u : 1u;
            b += (sqrt(((double)id * id) + ((double)iq * iq)) <= c.imax * 1.00001) ? 0u : 1u;
            b += (id >= -m.id_demag_a - 1e-3) ? 0u : 1u;
            b += ((fabs(tg) <= fabs(t) * 1.001 + 1e-3) && (tg * t >= 0.0)) ? 0u : 1u;
        }
        if (r == TQ_OK) {
            b += (fabs(tg - t) <= 1e-3 * fabs(t) + 1e-3) ? 0u : 1u;
            if ((i % 4u) == 0u) {
                const double lc = o_least(&m, t, c.w, v_av, c.imax, 0.0);
                const double mag = sqrt(((double)id * id) + ((double)iq * iq));
                const double ex = (mag / lc) - 1.0;
                sub_cur += ((mag - lc) > fmax(2e-3 * lc, 0.01)) ? 1u : 0u; /* 0.2 %, or the search's 0.01 A */
                worst_cur = fmax(worst_cur, ((mag - lc) > 0.01) ? ex : 0.0);
            }
        } else if (r == TQ_LIMITED) {
            const double tm = o_tmax(&m, (t >= 0.0) ? 1.0 : -1.0, c.w, v_av, c.imax);
            missed += (tm >= (fabs(t) * (1.0 + 1e-4)) + 1e-3) ? 1u : 0u;
            const double sh = fmin(tm, fabs(t)) - fabs(tg);
            sub_lim += (sh > 0.1) ? 1u : 0u;
            worst_lim = fmax(worst_lim, sh);
        } else if (r == TQ_INFEASIBLE) {
            b += ((iq == 0.0f) && !zero_ok(&m, c.w, v_av, c.imax)) ? 0u : 1u;
        } else {
            b++; /* TQ_POSTCOND / TQ_NONFINITE */
        }
        if (b != 0u) {
            printf("    ^ case %u: r %d, T %+.2f, w %+.0f, %.0f V, %.0f A: id %.3f iq %.3f -> %+.4f N m\n", (unsigned)i, (int)r,
                   t, (double)c.w, (double)c.vdc, (double)c.imax, (double)id, (double)iq, tg);
        }
        bad += b;
        /* the scalar solve (FW-37 without FW-45) on the same machine */
        motor_t f = m;
        (void)memset(f.ld_map_h, 0, sizeof f.ld_map_h);
        (void)memset(f.lq_map_h, 0, sizeof f.lq_map_h);
        float idf = 0.0f;
        float iqf = 0.0f;
        if (torque_to_current(c.t, c.w, c.vdc, c.imax, &f, NULL, p, &idf, &iqf) == TQ_OK) {
            old_ok++;
            const double e = fabs(t_sat(&m, idf, iqf) - t) - (1e-3 * fabs(t) + 1e-3);
            old_off += (e > 0.0) ? 1u : 0u;
            worst_old = fmax(worst_old, e);
        }
    }
    CHECK(bad == 0u);
    CHECK(missed == 0u);
    CHECK(n[TQ_OK] > 4000u && n[TQ_LIMITED] > 4000u && n[TQ_INFEASIBLE] > 4000u);
    CHECK(old_off > 1000u); /* the scalar solve misses the torque of a saturated machine */
    printf("    saturated maps: 20000 cases — OK %u, LIMITED %u, INFEASIBLE %u, NONFINITE %u, POSTCOND %u; %u failed checks;\n"
           "      LIMITED where the request fits: %u; valid but not the least current (> 0.2 %%, every 4th OK): %u, worst %+.4f;\n"
           "      LIMITED > 0.1 N m below the most that fits: %u, worst %.4f N m; the scalar solve outside 0.1 %% in %u of its %u OK,"
           " worst %.2f N m\n",
           n[TQ_OK], n[TQ_LIMITED], n[TQ_INFEASIBLE], n[TQ_NONFINITE], n[TQ_POSTCOND], bad, missed, sub_cur, worst_cur,
           sub_lim, worst_lim, old_off, old_ok, worst_old);
}

/* The psi_e guard from the maps' extremes (max L_d - min L_q): on weak magnets (0.01-0.06 Wb) with a salient q axis that
 * saturates below L_d (I_sat 0.35-1.5 x the full scale) a d current deep enough makes psi + (L_d - L_q(|q|)) d negative at
 * a high q current: there the torque contour does not exist, and a guard from the scalars (L_q > L_d: none) lets the
 * searches into it — 204 TQ_POSTCOND in 200 000 such cases (the §6 "control lost" row, driving). With the guard: 20 000 of
 * them, every result valid (as in the test above), none refused by its own postcondition. */
TEST(the_psi_e_guard_keeps_the_contour_defined_on_weak_magnets)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    s_rng = 7u;
    unsigned bad = 0u;
    unsigned n = 0u;
    while (n < 20000u) {
        motor_t m = motor_screening();
        m.psi_wb = (float)urand(0.01, 0.06);
        m.ld_h = (float)urand(0.1e-3, 1e-3);
        m.lq_h = (float)(m.ld_h * urand(1.05, 3.0));
        m.rs_ohm = (float)urand(0.005, 0.05);
        m.id_demag_a = (float)urand(200.0, 1500.0);
        m.i_map_a = p->i_crest_a;
        const double is = m.i_map_a * urand(0.35, 1.5);
        for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
            const double x = (m.i_map_a * k / 5.0) / is;
            m.ld_map_h[k] = m.ld_h;
            m.lq_map_h[k] = (k == 0u) ? m.lq_h : (float)(m.lq_h * atan(x) / x);
        }
        const tq_case_t c = {.t = (float)(urand(-1.0, 1.0) * 600.0), .w = (float)(urand(-1.0, 1.0) * 3000.0),
                             .vdc = (float)urand(200.0, 850.0), .imax = (float)urand(100.0, 480.0)};
        if (m.lq_map_h[5] < 0.3f * m.lq_map_h[0]) {
            continue;
        }
        n++;
        float id = 0.0f;
        float iq = 0.0f;
        const tq_res_t r = torque_to_current(c.t, c.w, c.vdc, c.imax, &m, NULL, p, &id, &iq);
        const double v_av = torque_v_available(c.vdc, p);
        const double tg = t_sat(&m, id, iq);
        unsigned b = ((r == TQ_OK) || (r == TQ_LIMITED) || (r == TQ_INFEASIBLE)) ? 0u : 1u;
        if ((r == TQ_OK) || (r == TQ_LIMITED)) {
            b += (v_sat(&m, id, iq, c.w) <= v_av * 1.00001) ? 0u : 1u;
            b += (sqrt(((double)id * id) + ((double)iq * iq)) <= c.imax * 1.00001) ? 0u : 1u;
            b += ((fabs(tg) <= fabs(c.t) * 1.001 + 1e-3) && (tg * c.t >= 0.0)) ? 0u : 1u;
            b += ((r == TQ_LIMITED) || (fabs(tg - c.t) <= 1e-3 * fabs(c.t) + 1e-3)) ? 0u : 1u;
        }
        bad += b;
    }
    CHECK(bad == 0u);
}

/* ======================= FW-45: the record ======================= */

/* Layout 4: a layout-3 record is refused (no torque). The maps: each point in the 20 uH - 5 mH class range, non-increasing
 * beyond the 2 % measurement tolerance (a 1 % rise passes, 3 % is refused),
 * the last >= 0.3 x the first, the full scale the SKU's current limit; the ripple table within +/- 30 A: each violation
 * alone is CAL_ERR_RANGE; the nominal record carries flat maps at the scalars and passes. */
TEST(the_record_is_layout_4_and_a_bad_map_is_refused)
{
    static const uint8_t SN[8] = {'T', 'I', '-', '0', '0', '0', '0', '1'};
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    calib_t c;
    calib_nominal(&c, p, SN);
    c.motor_id = 42u;
    calib_seal(&c);
    CHECK(CALIB_LAYOUT_VERSION == 4u && calib_check(&c, p, SN) == 0u);
    CHECK(c.motor.ld_map_h[5] == c.motor.ld_h && c.motor.lq_map_h[0] == c.motor.lq_h && c.motor.i_map_a == p->i_crest_a);
    const calib_t good = c;
    c.layout_version = 3u;
    calib_seal(&c);
    CHECK(calib_check(&c, p, SN) == CAL_ERR_VERSION);
    for (uint32_t k = 0u; k < 8u; k++) {
        c = good;
        switch (k) {
        case 0u: c.motor.lq_map_h[3] = 1.03f * c.motor.lq_map_h[2]; break;      /* rises with the current beyond the tolerance */
        case 1u: c.motor.ld_map_h[5] = 19e-6f; break;                          /* below the class range */
        case 2u: c.motor.lq_map_h[0] = 5.1e-3f; break;                         /* above it */
        case 3u: c.motor.ld_map_h[5] = 0.29f * c.motor.ld_map_h[0]; break;     /* the last < 0.3 x the first */
        case 4u: c.motor.i_map_a = 0.5f * p->i_crest_a; break;                 /* another full scale */
        case 5u: c.ripple_ff[7] = 3001; break;                                 /* 30.01 A */
        case 6u: c.motor.lq_map_h[4] = NAN; break;
        default: c.ripple_ff[35] = -3001; break;
        }
        calib_seal(&c);
        CHECK(calib_check(&c, p, SN) == CAL_ERR_RANGE);
    }
    c = good; /* the routine's scatter on a machine that does not saturate: a 1 % rise passes (the tool's map commit) */
    c.motor.lq_map_h[3] = 1.01f * c.motor.lq_map_h[2];
    c.motor.ld_map_h[1] = 1.019f * c.motor.ld_map_h[0];
    calib_seal(&c);
    CHECK(calib_check(&c, p, SN) == 0u);
    c = good; /* a saturating map inside the rules */
    const float prof[6] = {1.0f, 0.98f, 0.93f, 0.86f, 0.79f, 0.30f};
    for (uint32_t k = 0u; k < 6u; k++) {
        c.motor.lq_map_h[k] = prof[k] * c.motor.lq_h;
    }
    c.ripple_ff[0] = 3000;
    calib_seal(&c);
    CHECK(calib_check(&c, p, SN) == 0u);
}

/* ======================= FW-45 on the host plant ======================= */
/* The FW-39 plant (tests/test_commission.c): L_d 0.30 / L_q 0.55 mH, 21 mOhm, 0.128 Wb, 4 pole pairs — its electrical zero
 * where the record has it — here saturating: the d axis mildly (I_sat 600 A), the q axis strongly (320 A: at 80 % of the
 * 8XX limit, 385 A, L_q is 0.66 of itself apparent, 0.41 differential). */
#define P_RS 0.021f
#define P_LD 0.30e-3f
#define P_LQ 0.55e-3f
#define P_PSI 0.128f
#define P_ISD 600.0f
#define P_ISQ 320.0f

static sim_pmsm_cfg_t plant_cfg(void)
{
    const sim_pmsm_cfg_t c = {.rs_ohm = P_RS, .ld_h = P_LD, .lq_h = P_LQ, .psi_wb = P_PSI, .pp = 4u, .rpp = 1u,
                              .zero_rad = 0.0f, .i_knee_a = 3.0f, .j_kgm2 = 0.05f, .noise_a = 0.55f,
                              .isat_d_a = P_ISD, .isat_q_a = P_ISQ};
    return c;
}

static void plant(void) { sim_pmsm_step(H.link_v); }

static bool test_key(const uint8_t seed[UDS_SA_LEN], uint8_t key[UDS_SA_LEN])
{
    for (uint32_t i = 0u; i < UDS_SA_LEN; i++) {
        key[i] = (uint8_t)(seed[(i + 1u) % UDS_SA_LEN] ^ (0xA5u + i));
    }
    return true;
}

static float s_ff_max_a;       /* FW-46: the build's cal_ripple_ff_max_a for the next boot (the default 0: off) */
static const int16_t *s_tab;   /* ... and a ripple table for its record (NULL: none) */

/* A power-up with the plant and a record of the plant's unsaturated data; map: the plant's apparent inductance at the six
 * breakpoints (else the flat maps calib_nominal writes); then armed through the normal path. */
static bool boot_on(ti_sku_t sku, sim_pmsm_cfg_t c, bool map)
{
    sim_reset();
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
    dtc_init();
    h_setup(sku);
    h_p.cal_ripple_ff_max_a = s_ff_max_a;
    c.t_dead_s = 1.1f * (float)h_p.dead_time_ns * 1e-9f;
    c.fsw_hz = (float)h_p.fsw_hz[0];
    sim_pmsm_init(&c, 0.9f);
    H.plant = plant;
    H.veh_speed_valid = true;
    motor_t *m = &h_cal.motor;
    m->ld_h = P_LD;
    m->lq_h = P_LQ;
    m->rs_ohm = P_RS;
    m->psi_wb = P_PSI;
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
        const float i = m->i_map_a * (float)k / (float)(MOTOR_MAP_N - 1u);
        m->ld_map_h[k] = map ? sim_pmsm_l_app_h(false, i) : P_LD;
        m->lq_map_h[k] = map ? sim_pmsm_l_app_h(true, i) : P_LQ;
    }
    if (s_tab != NULL) {
        (void)memcpy(h_cal.ripple_ff, s_tab, sizeof h_cal.ripple_ff);
    }
    calib_seal(&h_cal);
    h_boot();
    g_app.uds.key_fn = test_key;
    return (g_app.cal_err == 0u) && h_to_armed();
}

static void dyno_ramp(float rpm, uint32_t ms)
{
    const float r0 = sim_pmsm_rpm();
    for (uint32_t k = 1u; k <= ms; k++) {
        sim_pmsm_rotor(SIM_ROTOR_DYNO, r0 + ((rpm - r0) * (float)k / (float)ms));
        h_run_ms(1u);
    }
}

/* the plant's torque at (id, iq), in double, from its own flux law */
static double plant_t(double id, double iq)
{
    const double ld = P_LD * P_ISD * atan(id / P_ISD);
    const double lq = P_LQ * P_ISQ * atan(iq / P_ISQ);
    return 1.5 * 4.0 * (((P_PSI + ld) * iq) - (lq * id));
}

/* the plant's most torque at the current i (its MTPA), a scan of the current angle */
static double plant_t_at(double i)
{
    double best = 0.0;
    for (int k = 0; k <= 9000; k++) {
        const double b = (M_PI / 2.0) * k / 9000.0;
        best = fmax(best, plant_t(-i * sin(b), i * cos(b)));
    }
    return best;
}

/* RUN at t_nm on the dyno at rpm: the mean shaft torque and current magnitude over 100 ms after the slew and 200 ms */
static bool run_torque(float t_nm, float rpm, double *t_mean, double *i_mean)
{
    dyno_ramp(rpm, 300u);
    h_run_ms(50u);
    H.enable = true;
    H.torque_nm = t_nm;
    if (!h_run_until(SM_RUN, 200u)) {
        return false;
    }
    h_run_ms((uint32_t)(1000.0f * t_nm / h_p.cal_torque_slew_nm_s) + 200u);
    double ts = 0.0;
    double is = 0.0;
    for (uint32_t k = 0u; k < 100u; k++) {
        h_run_ms(1u);
        float id;
        float iq;
        sim_pmsm_idq(&id, &iq);
        ts += sim_pmsm_torque_nm();
        is += sqrt(((double)id * id) + ((double)iq * iq));
    }
    *t_mean = ts / 100.0;
    *i_mean = is / 100.0;
    return (g_app.sm.st == SM_RUN) && !dtc_active(DTC_TORQUE_POSTCOND);
}

/* The torque the plant delivers at 80 % of the 8XX current limit (385 A; its MTPA torque there, T80): with flat maps (the
 * scalar model — FW-37 as it was) the solve believes in the unsaturated reluctance torque and asks too little current;
 * with the plant's map at the six breakpoints it delivers T80 within 2 %. 1000 rpm on the dyno, 750 V. */
TEST(the_map_delivers_the_torque_of_a_saturating_machine_at_80_percent_current)
{
    const double i80 = 0.8 * ti_params_get(TI_SKU_8XX_SIC)->i_crest_a;
    const double t80 = plant_t_at(i80);
    double err[2] = {0.0, 0.0};
    double cur[2] = {0.0, 0.0};
    for (uint32_t k = 0u; k < 2u; k++) {
        CHECK(boot_on(TI_SKU_8XX_SIC, plant_cfg(), k == 1u));
        double t = 0.0;
        CHECK(run_torque((float)t80, 1000.0f, &t, &cur[k]));
        err[k] = (t - t80) / t80;
    }
    CHECK(fabs(err[1]) < 0.02);  /* the map */
    CHECK(err[0] < -0.05);       /* the flat map: short */
    printf("    at 80 %% of I_max (%.1f A, T80 %.1f N m, 1000 rpm): flat map %+.2f %% at %.1f A, the plant's map %+.2f %% at %.1f A\n",
           i80, t80, 100.0 * err[0], cur[0], 100.0 * err[1], cur[1]);
}

/* Gain scheduling: the q axis of a saturating machine (the screening motor, L_q 0.35 mH unsaturated, I_sat 300 A: at
 * 390 A its differential inductance is 0.37 of itself) at standstill, the target's actuation (the voltage computed at a
 * sample acts from the next one for a period: 1.5 periods on average), 8XX SiC at 10 kHz (the loop at 20 kHz, 1080 Hz
 * crossover by gains.c). Settled at 380 A, a 20 A step. A fixed gain (the flat map) sees the crossover rise by 1 / 0.37
 * to ~2.9 kHz — the phase margin falls from ~61 deg to ~12 deg (90 deg - 360 f_c x 75 us) and the step rings; the
 * scheduled gain (the plant's map at the six breakpoints) keeps ~1.1 kHz. */
TEST(gain_scheduling_keeps_the_current_loops_margin_on_a_saturating_axis)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const double is = 300.0;
    double over[2] = {0.0, 0.0};
    double ring[2] = {0.0, 0.0};
    for (uint32_t sched = 0u; sched < 2u; sched++) {
        motor_t m = motor_screening();
        m.i_map_a = p->i_crest_a;
        for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
            const double x = (m.i_map_a * k / 5.0) / is;
            m.ld_map_h[k] = m.ld_h;
            m.lq_map_h[k] = ((sched == 0u) || (k == 0u)) ? m.lq_h : (float)(m.lq_h * atan(x) / x);
        }
        gain_set_t g;
        CHECK(gains_default(p, 10000u, m.ld_h, m.rs_ohm, &g));
        foc_t f = {0}; /* foc_reset keeps guard_trip: initialised, not stack garbage */
        foc_reset(&f);
        double id = 0.0;
        double iq = 0.0;
        double vd = 0.0; /* the voltage acting now: the one computed at the previous sample */
        double vq = 0.0;
        double peak = 0.0;
        double lo_after = 1e9;
        bool stepped = true;
        for (uint32_t n = 0u; n < 1200u; n++) { /* 40 ms at 380 A, then the step: 20 ms */
            f.iq_ref = (n < 800u) ? 380.0f : 400.0f;
            const float i3[3] = {(float)id, (float)(-0.5 * id + 0.8660254 * iq), (float)(-0.5 * id - 0.8660254 * iq)};
            stepped = foc_step(&f, i3, 0.0f, 0.0f, 750.0f, &m, &g, p) && stepped;
            for (uint32_t j = 0u; j < 10u; j++) { /* the plant: 10 sub-steps of the period */
                const double h = g.ts_s / 10.0;
                iq += h * (vq - m.rs_ohm * iq) / (m.lq_h / (1.0 + (iq / is) * (iq / is)));
                id += h * (vd - m.rs_ohm * id) / m.ld_h;
            }
            vd = f.vd;
            vq = f.vq;
            if (n >= 800u) {
                peak = fmax(peak, iq);
                lo_after = (peak > 400.0) ? fmin(lo_after, iq) : lo_after; /* the first undershoot after the peak */
            }
        }
        over[sched] = (peak - 400.0) / 20.0;
        ring[sched] = (400.0 - lo_after) / 20.0;
        CHECK(stepped && fabs(iq - 400.0) < 1.0); /* both settle: at 12 deg the loop still converges */
    }
    CHECK(over[0] > 0.4 && over[1] < 0.15);
    CHECK(ring[1] < 0.05 && ring[0] > 0.2);
    /* the loop's back-EMF speed model (FW-10's rate check) takes lambda_d from the same map: at i_d = -300 A on an L_d
     * falling to 0.8 of itself, w from v_q = R i_q + w lambda_d(i_d) is exact — the scalar L_d reads 25 % high, the whole
     * tolerance of FW-10's rate check (20 % + 50 rad/s el): a false resolver-rate fault in deep field weakening */
    motor_t m = motor_screening();
    m.i_map_a = p->i_crest_a;
    const float prof[6] = {1.0f, 0.99f, 0.95f, 0.9f, 0.85f, 0.8f};
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
        m.ld_map_h[k] = prof[k] * m.ld_h;
        m.lq_map_h[k] = m.lq_h;
    }
    foc_t f = {0};
    foc_reset(&f);
    f.id = -300.0f;
    f.iq = 100.0f;
    float dd;
    const float lam_d = m.psi_wb + ((m.ld_h * motor_sat(m.ld_map_h, 300.0f, m.i_map_a, &dd)) * -300.0f);
    f.vq = (m.rs_ohm * 100.0f) + (1000.0f * lam_d);
    bool v = false;
    CHECK(fabsf(foc_omega_model(&f, &m, &v) - 1000.0f) < 0.5f && v);
    /* the speed voltages from the maps' flux: at the reference (no error, no integral) the loop's output is the decoupling,
     * v_d = -w L_q(|i_q|) i_q, v_q = w (psi + L_d(|i_d|) i_d); and the scheduled gain never below 0.3 of the fixed one —
     * on a map whose differential inductance goes negative (a 0.3 last point after a steep step: L + i dL/di < 0) */
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
        m.lq_map_h[k] = ((k < 4u) ? 1.0f : 0.3f) * m.lq_h;
    }
    gain_set_t g;
    CHECK(gains_default(p, 10000u, m.ld_h, m.rs_ohm, &g));
    const float cur[2] = {-200.0f, 340.0f}; /* i_q on the steep segment (breakpoints 3 to 4) */
    const float i3[3] = {cur[0], (-0.5f * cur[0]) + (0.8660254f * cur[1]), (-0.5f * cur[0]) - (0.8660254f * cur[1])};
    foc_reset(&f);
    f.id_ref = cur[0];
    f.iq_ref = cur[1];
    CHECK(foc_step(&f, i3, 0.0f, 300.0f, 750.0f, &m, &g, p));
    float dq;
    const float lq_app = m.lq_h * motor_sat(m.lq_map_h, 340.0f, m.i_map_a, &dq);
    const float ld_app = m.ld_h * motor_sat(m.ld_map_h, 200.0f, m.i_map_a, &dd);
    CHECK(dq < 0.0f && fabsf(f.vd - (-300.0f * lq_app * 340.0f)) < 0.05f &&
          fabsf(f.vq - (300.0f * (m.psi_wb + (ld_app * -200.0f)))) < 0.05f);
    foc_reset(&f);
    f.id_ref = cur[0];
    f.iq_ref = cur[1] + 1.0f; /* 1 A of q error at standstill: v_q = the scheduled k_p */
    CHECK(foc_step(&f, i3, 0.0f, 0.0f, 750.0f, &m, &g, p));
    CHECK(fabsf(f.vq - (0.3f * g.kp_v_per_a)) < 1e-4f);
    printf("    a 20 A step at 380 A on L_q,diff = 0.37 L_q: fixed gain %.0f %% overshoot, %.0f %% undershoot after it; scheduled"
           " %.0f %%, %.0f %%\n", 100.0 * over[0], 100.0 * ring[0], 100.0 * over[1], 100.0 * ring[1]);
}

/* ======================= FW-45: FW-39's routine at a bias index ======================= */
/* One single-frame request (classic up to 7 bytes, else the CAN-FD escape form); the response the tick sends. */
static bool uds_req(const uint8_t *req, uint8_t n, uint8_t rsp[8])
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = (n <= 7u) ? 8u : 12u};
    (void)memset(f.data, 0xAA, sizeof f.data);
    if (n <= 7u) {
        f.data[0] = n;
        (void)memcpy(&f.data[1], req, n);
    } else {
        f.data[0] = 0u;
        f.data[1] = n;
        (void)memcpy(&f.data[2], req, n);
    }
    hal_can_frame_t r;
    while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
    }
    sim_can_inject(HAL_CAN_DIAG, &f);
    h_run_ms(1u);
    if (!sim_can_pop_tx(HAL_CAN_DIAG, &r) || (r.id != UDS_ID_RSP)) {
        return false;
    }
    (void)memcpy(rsp, r.data, 8u);
    return true;
}

static bool unlock(void)
{
    uint8_t r[8];
    const uint8_t sq[2] = {0x27u, 0x01u};
    if (!uds_req(sq, 2u, r) || (r[1] != 0x67u)) {
        return false;
    }
    if ((r[3] | r[4] | r[5] | r[6]) == 0u) {
        return true;
    }
    uint8_t kq[2u + UDS_SA_LEN] = {0x27u, 0x02u};
    (void)test_key(&r[3], &kq[2]);
    return uds_req(kq, (uint8_t)sizeof kq, r) && (r[1] == 0x67u) && (r[2] == 0x02u);
}

/* 0 = positive response, else the NRC (0xFF: no response, 0xFE: another positive response) */
static uint8_t rc(uint8_t sub, uint16_t rid, const uint8_t *opt, uint8_t n_opt, uint8_t out[3])
{
    uint8_t q[12] = {0x31u, sub, (uint8_t)(rid >> 8), (uint8_t)(rid & 0xFFu)};
    (void)memcpy(&q[4], opt, n_opt);
    uint8_t r[8];
    if (!uds_req(q, (uint8_t)(4u + n_opt), r)) {
        return 0xFFu;
    }
    if ((r[1] == 0x7Fu) && (r[2] == 0x31u)) {
        return r[3];
    }
    if ((r[1] != 0x71u) || (r[2] != sub) || (r[3] != q[2]) || (r[4] != q[3])) {
        return 0xFEu;
    }
    if (out != NULL) {
        out[0] = r[5];
        out[1] = r[6];
        out[2] = r[7];
    }
    return 0u;
}

static bool result(uint8_t idx, uint8_t out[3]) { return (rc(0x03u, UDS_RID_MC_RUN, &idx, 1u, out) == 0u) && (out[0] == idx); }

static float be_l(const uint8_t o[3]) { return (float)(((uint32_t)o[1] << 8) | o[2]) * MC_UNIT_L; }

/* The tool: the heartbeat every 50 ms until the routine has ended. */
static bool run_to_end(uint32_t max_ms)
{
    for (uint32_t t = 0u; t < max_ms; t += 50u) {
        h_run_ms(49u);
        uint8_t o[3];
        if (!result(0u, o)) {
            return false;
        }
        if (o[1] != (uint8_t)MC_RUNNING) {
            return true;
        }
    }
    return false;
}

/* the L_d / L_q routine at bias index k (0xFF: the unbiased one): NRC or 0, run to its end */
static uint8_t ldq_at(uint8_t k)
{
    const uint8_t opt[4] = {(uint8_t)MC_RT_LDQ, (uint8_t)(MC_ATTEST_LOCKED >> 8), (uint8_t)(MC_ATTEST_LOCKED & 0xFFu), k};
    if (!unlock()) {
        return 0xFDu;
    }
    const uint8_t code = rc(0x01u, UDS_RID_MC_RUN, opt, (k == MC_BIAS_NONE) ? 3u : 4u, NULL);
    return (code != 0u) ? code : (run_to_end(3000u) ? 0u : 0xFCu);
}

/* the plant's differential inductance at |i| */
static double plant_l_diff(double l0, double is, double i) { return l0 / (1.0 + ((i / is) * (i / is))); }

/* The map sweep on the saturating plant (8XX SiC, 20 kHz loop): FW-39's L_d/L_q routine at the six bias indices — each
 * point measured at its breakpoint's current on the true axes (the d run at i_d = -b, the q run at i_q = +b), judged
 * against the record's (flat) map: beyond the band, confirmed by a second run. Read over UDS (0x40+k, 0x50+k, 0x02, the
 * flags' bias bit): the differential inductance at each bias within 0.2 % of the plant's. Committed (RID 0xF021) into a
 * whole map per axis — the apparent inductance from the differential by trapezoids; point 0 is the differential at the
 * routine's 50 A minimum bias, FW-39's own convention for the scalar (on this plant, I_sat,q 320 A, 2.5 % below L(0), the
 * points above inheriting part of it): within 3 % of the plant's apparent inductance. The next key cycle arms with it and
 * delivers the plant's torque at 80 % of the current limit within 2 %. */
TEST(the_routine_at_six_biases_measures_and_commits_the_map)
{
    CHECK(boot_on(TI_SKU_8XX_SIC, plant_cfg(), false));
    const float full = g_app.cal.motor.i_map_a;
    double worst[2] = {0.0, 0.0};
    unsigned runs = 0u;
    for (uint8_t k = 0u; k < MOTOR_MAP_N; k++) {
        for (uint32_t r = 0u; (r < 3u) && !(g_mc.mp[0][k].staged && g_mc.mp[1][k].staged); r++) {
            CHECK(ldq_at(k) == 0u && g_mc.st == MC_DONE);
            runs++;
        }
        uint8_t o[3];
        CHECK(result((uint8_t)(0x10u + MC_Q_LD), o) && o[1] == (uint8_t)MC_V_VALID && (o[2] & 0x0Au) == 0x0Au &&
              (o[2] >> 4) == k);
        for (uint32_t ax = 0u; ax < 2u; ax++) {
            const double b = fmax(fmin(full * k / 5.0, ((ax == 0u) ? fmin(full, g_app.cal.motor.id_demag_a) : full) - 20.0), 50.0);
            const double want = plant_l_diff((ax == 0u) ? P_LD : P_LQ, (ax == 0u) ? P_ISD : P_ISQ, b);
            CHECK(result((uint8_t)(((ax == 0u) ? 0x40u : 0x50u) + k), o));
            const double e = be_l(o) / want - 1.0;
            worst[ax] = fmax(worst[ax], fabs(e));
            CHECK(fabs(e) < 0.03);
        }
        CHECK(g_mc.mp[0][k].staged && g_mc.mp[1][k].staged);
    }
    uint8_t o[3];
    CHECK(result(2u, o) && o[1] == 0x3Fu && o[2] == 0x3Fu && result(1u, o) && (o[2] & 0x20u) != 0u);
    CHECK(!g_mc.q[MC_Q_LD].staged && !g_mc.q[MC_Q_LQ].staged); /* the scalars are not a biased run's */
    CHECK(unlock() && rc(0x01u, UDS_RID_MC_COMMIT, NULL, 0u, NULL) == 0u && dtc_active(DTC_MC_CAL_WRITTEN));
    CHECK(result(2u, o) && o[1] == 0u && o[2] == 0u && g_app.cal.motor.lq_map_h[5] == P_LQ); /* this key cycle: unchanged */
    h_run_ms(20u);
    calib_t rd;
    CHECK(nv_read(NV_REC_CALIB, &rd, (uint16_t)sizeof rd) && calib_check(&rd, &h_p, h_serial()) == 0u);
    double werr[2] = {0.0, 0.0};
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
        const float i = full * (float)k / 5.0f;
        werr[0] = fmax(werr[0], fabs((double)rd.motor.ld_map_h[k] / sim_pmsm_l_app_h(false, i) - 1.0));
        werr[1] = fmax(werr[1], fabs((double)rd.motor.lq_map_h[k] / sim_pmsm_l_app_h(true, i) - 1.0));
    }
    CHECK(werr[0] < 0.03 && werr[1] < 0.03); /* point 0 is the differential at the routine's 50 A (FW-39's own bias) */
    CHECK(rd.motor.ld_h == rd.motor.ld_map_h[0] && rd.motor.lq_h == rd.motor.lq_map_h[0] && rd.mtpa.n == 0u);
    /* the next key cycle: the record from NVM; the torque at 80 % of the current limit */
    sim_reset();
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    sim_pmsm_cfg_t c = plant_cfg();
    c.t_dead_s = 1.1f * (float)h_p.dead_time_ns * 1e-9f;
    c.fsw_hz = (float)h_p.fsw_hz[0];
    sim_pmsm_init(&c, 0.9f);
    H.plant = plant;
    H.veh_speed_valid = true;
    app_init(&g_app, &h_p, NULL, h_serial());
    sim_set_fault_isr(app_fault_isr_entry);
    CHECK(g_app.cal_err == 0u && memcmp(&g_app.cal, &rd, sizeof rd) == 0 && h_to_armed());
    const double t80 = plant_t_at(0.8 * h_p.i_crest_a);
    double t = 0.0;
    double i = 0.0;
    CHECK(run_torque((float)t80, 1000.0f, &t, &i));
    CHECK(fabs(t / t80 - 1.0) < 0.02);
    printf("    map sweep: %u runs; the differential inductance at each bias within %.2f %% (d), %.2f %% (q) of the plant's;"
           " the committed map's apparent inductance within %.2f %% (d), %.2f %% (q); T80 %+.2f %% at %.1f A\n",
           runs, 100.0 * worst[0], 100.0 * worst[1], 100.0 * werr[0], 100.0 * werr[1], 100.0 * (t / t80 - 1.0), i);
}

static double s_sd;
static double s_sq;
static unsigned s_ns;

static void plant_avg(void) /* the plant, and its currents in its own frame summed at each current-loop trigger */
{
    float id;
    float iq;
    sim_pmsm_idq(&id, &iq);
    s_sd += id;
    s_sq += iq;
    s_ns++;
    sim_pmsm_step(H.link_v);
}

/* The bias is on the TRUE axes: the plant's electrical zero 20 deg el off the record's (FW-39's P_ZERO), the dyno routine
 * gives this key cycle's zero first; then, in the d run at bias index 3 (288.5 A), the plant's own rotor frame carries
 * i_d = -b and i_q = 0 on average over the HF periods (the record's frame alone would put 34 % of b on q). */
TEST(the_bias_runs_on_the_true_axes)
{
    sim_pmsm_cfg_t c = plant_cfg();
    c.zero_rad = 0.35f;
    c.isat_d_a = 0.0f; /* the linear machine: the axes, not the inductance, are under test */
    c.isat_q_a = 0.0f;
    CHECK(boot_on(TI_SKU_8XX_SIC, c, false));
    dyno_ramp(300.0f, 150u);
    h_run_ms(100u);
    const uint8_t psi[3] = {(uint8_t)MC_RT_PSI_ZERO, (uint8_t)(MC_ATTEST_DYNO_FWD >> 8), (uint8_t)(MC_ATTEST_DYNO_FWD & 0xFFu)};
    CHECK(unlock() && rc(0x01u, UDS_RID_MC_RUN, psi, 3u, NULL) == 0u && run_to_end(3000u) && g_mc.eps_valid);
    CHECK(fabsf(g_mc.eps_rad - 0.35f) < 0.01f);
    dyno_ramp(0.0f, 150u);
    sim_pmsm_rotor(SIM_ROTOR_LOCKED, 0.0f);
    h_run_ms(50u);
    const uint8_t opt[4] = {(uint8_t)MC_RT_LDQ, (uint8_t)(MC_ATTEST_LOCKED >> 8), (uint8_t)(MC_ATTEST_LOCKED & 0xFFu), 3u};
    CHECK(unlock() && rc(0x01u, UDS_RID_MC_RUN, opt, 4u, NULL) == 0u);
    uint8_t o[3];
    h_run_ms(60u); /* past the 40 ms settle */
    CHECK(result(0u, o) && o[1] == (uint8_t)MC_RUNNING);
    s_sd = 0.0;
    s_sq = 0.0;
    s_ns = 0u;
    H.plant = plant_avg; /* 20 ms: ten HF periods, every current-loop sample in the rotor's own frame */
    h_run_ms(20u);
    H.plant = plant;
    const double sd = s_sd;
    const double sq = s_sq;
    const double b = g_app.cal.motor.i_map_a * 3.0 / 5.0;
    CHECK(s_ns == 400u && fabs(sd / 400.0 + b) < 0.02 * b && fabs(sq / 400.0) < 0.02 * b);
    CHECK(rc(0x02u, UDS_RID_MC_RUN, NULL, 0u, NULL) == 0u);
}

/* The bias byte: only for the L_d/L_q routine and only 0..5 (NRC 0x31, the unlock kept), one byte at most (0x13). A map is
 * committed whole: points of one index staged, the commit is refused and writes nothing. The unbiased routine is FW-39's
 * as before — the scalars staged, the map untouched — and a scalar committed alone keeps its axis' map as the shape at
 * the new level (point 0 = the old scalar: the map's shape is unchanged, the torque model takes the new level). */
TEST(the_bias_byte_is_checked_and_a_map_is_committed_whole)
{
    CHECK(boot_on(TI_SKU_8XX_SIC, plant_cfg(), false) && unlock());
    const uint8_t bad_k[4] = {(uint8_t)MC_RT_LDQ, 0x4Cu, 0x4Bu, 6u};
    const uint8_t bad_rt[4] = {(uint8_t)MC_RT_RS, 0x4Cu, 0x4Bu, 0u};
    const uint8_t too_long[5] = {(uint8_t)MC_RT_LDQ, 0x4Cu, 0x4Bu, 0u, 0u};
    CHECK(rc(0x01u, UDS_RID_MC_RUN, bad_k, 4u, NULL) == UDS_NRC_OUT_OF_RANGE);
    CHECK(rc(0x01u, UDS_RID_MC_RUN, bad_rt, 4u, NULL) == UDS_NRC_OUT_OF_RANGE);
    CHECK(rc(0x01u, UDS_RID_MC_RUN, too_long, 5u, NULL) == UDS_NRC_LENGTH);
    CHECK(g_mc.st == MC_IDLE && g_app.uds.unlocked);
    CHECK(ldq_at(2u) == 0u && ldq_at(2u) == 0u && g_mc.mp[0][2].staged && g_mc.mp[1][2].staged);
    const uint8_t rs[3] = {(uint8_t)MC_RT_RS, 0x4Cu, 0x4Bu}; /* R_s staged beside: the commit is refused all the same */
    CHECK(unlock() && rc(0x01u, UDS_RID_MC_RUN, rs, 3u, NULL) == 0u && run_to_end(3000u) && g_mc.q[MC_Q_RS].staged);
    CHECK(unlock() && rc(0x01u, UDS_RID_MC_COMMIT, NULL, 0u, NULL) == UDS_NRC_CONDITIONS);
    h_run_ms(20u);
    calib_t rd;
    CHECK(!nv_read(NV_REC_CALIB, &rd, (uint16_t)sizeof rd) && g_mc.mp[1][2].staged && g_mc.q[MC_Q_RS].staged);
    /* the unbiased routine: the scalars (inside their band: staged at once), the flags without the bias bit */
    CHECK(boot_on(TI_SKU_8XX_SIC, plant_cfg(), false));
    CHECK(ldq_at(MC_BIAS_NONE) == 0u && g_mc.q[MC_Q_LD].staged && g_mc.q[MC_Q_LQ].staged && g_mc.bias_k == MC_BIAS_NONE);
    uint8_t o[3];
    CHECK(result((uint8_t)(0x10u + MC_Q_LQ), o) && o[1] == (uint8_t)MC_V_VALID && (o[2] & 0xF8u) == 0u);
    CHECK(unlock() && rc(0x01u, UDS_RID_MC_COMMIT, NULL, 0u, NULL) == 0u);
    h_run_ms(20u);
    CHECK(nv_read(NV_REC_CALIB, &rd, (uint16_t)sizeof rd) && calib_check(&rd, &h_p, h_serial()) == 0u);
    CHECK(rd.motor.lq_h == g_mc.q[MC_Q_LQ].staged_value && rd.motor.lq_map_h[0] == P_LQ && rd.motor.lq_map_h[5] == P_LQ);
    CHECK(torque_from_current(-100.0f, 300.0f, &rd.motor) == 1.5f * 4.0f * (P_PSI + ((rd.motor.ld_h - rd.motor.lq_h) * -100.0f)) * 300.0f);
}

/* ======================= FW-46: the torque-ripple feed-forward ======================= */
/* A request as a tester sends it on the diagnostic bus: up to 7 bytes a classic single frame, up to 62 the CAN-FD escape
 * single frame, else a first frame and consecutive frames under the ECU's flow control; the single-frame response
 * (8 bytes) the 1 ms task sends. */
static bool iso_write(const uint8_t *req, uint32_t n, uint8_t rsp[8])
{
    if (n <= 7u) {
        return uds_req(req, (uint8_t)n, rsp);
    }
    if (n <= 62u) {
        hal_can_frame_t e = {.id = UDS_ID_REQ, .len = 64u};
        hal_can_frame_t r;
        while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
        }
        (void)memset(e.data, 0xAA, sizeof e.data);
        e.data[0] = 0u;
        e.data[1] = (uint8_t)n;
        (void)memcpy(&e.data[2], req, n);
        sim_can_inject(HAL_CAN_DIAG, &e);
        h_run_ms(1u);
        if (!sim_can_pop_tx(HAL_CAN_DIAG, &r) || (r.id != UDS_ID_RSP)) {
            return false;
        }
        (void)memcpy(rsp, r.data, 8u);
        return true;
    }
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 64u};
    hal_can_frame_t r;
    while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
    }
    (void)memset(f.data, 0xAA, sizeof f.data);
    f.data[0] = (uint8_t)(0x10u | (n >> 8));
    f.data[1] = (uint8_t)n;
    (void)memcpy(&f.data[2], req, 62u);
    sim_can_inject(HAL_CAN_DIAG, &f);
    uint32_t off = 62u;
    uint8_t sn = 1u;
    for (uint32_t t = 0u; t < 50u; t++) {
        h_run_ms(1u);
        while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
            if (r.id != UDS_ID_RSP) {
                continue;
            }
            if (r.data[0] == 0x30u) {
                for (uint32_t k = 0u; (off < n) && ((r.data[1] == 0u) || (k < r.data[1])); k++) {
                    const uint32_t take = ((n - off) < 63u) ? (n - off) : 63u;
                    (void)memset(f.data, 0xAA, sizeof f.data);
                    f.data[0] = (uint8_t)(0x20u | sn);
                    (void)memcpy(&f.data[1], &req[off], take);
                    sim_can_inject(HAL_CAN_DIAG, &f);
                    off += take;
                    sn = (uint8_t)((sn + 1u) & 0x0Fu);
                }
            } else {
                (void)memcpy(rsp, r.data, 8u);
                return (r.data[0] >= 1u) && (r.data[0] <= 7u);
            }
        }
    }
    return false;
}

/* A single-frame request and its whole response (a single frame, or a first frame reassembled under BS 0, STmin 0). */
static uint32_t iso_read(const uint8_t *req, uint8_t n, uint8_t *out)
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u};
    hal_can_frame_t r;
    while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
    }
    (void)memset(f.data, 0xAA, sizeof f.data);
    f.data[0] = n;
    (void)memcpy(&f.data[1], req, n);
    sim_can_inject(HAL_CAN_DIAG, &f);
    uint32_t len = 0u;
    uint32_t got = 0u;
    uint8_t sn = 1u;
    for (uint32_t t = 0u; t < 50u; t++) {
        h_run_ms(1u);
        while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
            if (r.id != UDS_ID_RSP) {
                continue;
            }
            const uint32_t pci = (uint32_t)r.data[0] >> 4;
            if ((pci == 0u) && (len == 0u)) {
                const bool esc = (r.data[0] == 0u);
                len = esc ? r.data[1] : r.data[0];
                (void)memcpy(out, &r.data[esc ? 2u : 1u], len);
                return len;
            }
            if (pci == 1u) {
                len = (((uint32_t)r.data[0] & 0x0Fu) << 8) | r.data[1];
                (void)memcpy(out, &r.data[2], 62u);
                got = 62u;
                hal_can_frame_t fc = {.id = UDS_ID_REQ, .len = 8u};
                (void)memset(fc.data, 0xAA, sizeof fc.data);
                fc.data[0] = 0x30u;
                fc.data[1] = 0u;
                fc.data[2] = 0u;
                sim_can_inject(HAL_CAN_DIAG, &fc);
            } else if ((pci == 2u) && (len > 0u) && (r.data[0] == (0x20u | sn))) {
                const uint32_t take = ((len - got) < 63u) ? (len - got) : 63u;
                (void)memcpy(&out[got], &r.data[1], take);
                got += take;
                sn = (uint8_t)((sn + 1u) & 0x0Fu);
                if (got >= len) {
                    return len;
                }
            } else {
                return 0u;
            }
        }
    }
    return 0u;
}

/* The plant with cogging (no saturation): 4 N m at the 6th, 1.5 N m at the 12th (phase 0.7 rad) — 3.0 N m rms. */
static sim_pmsm_cfg_t cog_cfg(void)
{
    sim_pmsm_cfg_t c = plant_cfg();
    c.isat_d_a = 0.0f;
    c.isat_q_a = 0.0f;
    c.cog6_nm = 4.0f;
    c.cog12_nm = 1.5f;
    c.cog12_ph_rad = 0.7f;
    return c;
}

/* the shaft torque sampled every 1 ms for ms: its rms about its mean (the return), its mean, the amplitudes of its 6th and
 * 12th electrical harmonics (h[0], h[1]); *k_max: the largest feed-forward scale the task set meanwhile */
static double ripple_rms(uint32_t ms, double *mean, double h[2], float *k_max)
{
    double s = 0.0;
    double s2 = 0.0;
    double c[4] = {0.0, 0.0, 0.0, 0.0};
    *k_max = 0.0f;
    for (uint32_t k = 0u; k < ms; k++) {
        h_run_ms(1u);
        const double t = sim_pmsm_torque_nm();
        const double th = sim_pmsm_theta_e();
        s += t;
        s2 += t * t;
        c[0] += t * sin(6.0 * th);
        c[1] += t * cos(6.0 * th);
        c[2] += t * sin(12.0 * th);
        c[3] += t * cos(12.0 * th);
        *k_max = fmaxf(*k_max, g_app.rip_k);
    }
    *mean = s / ms;
    h[0] = 2.0 * sqrt((c[0] * c[0]) + (c[1] * c[1])) / ms;
    h[1] = 2.0 * sqrt((c[2] * c[2]) + (c[3] * c[3])) / ms;
    return sqrt(fmax((s2 / ms) - (*mean * *mean), 0.0));
}

/* The tool's table from a dyno measurement: the rotor at 100 rpm, the bridge idle (no current: the shaft torque is the
 * cogging), the torque and the angle every 1 ms for three electrical periods, the 6th and 12th harmonics fitted (least
 * squares), i_q = -T / (1.5 pp psi) at 10 deg el steps in 0.01 A. */
static void measure_table(int16_t tab[TQ_RIPPLE_N])
{
    double a[4][5] = {{0.0}};
    for (uint32_t k = 0u; k < 450u; k++) {
        h_run_ms(1u);
        const double th = sim_pmsm_theta_e();
        const double b[4] = {sin(6.0 * th), cos(6.0 * th), sin(12.0 * th), cos(12.0 * th)};
        const double t = sim_pmsm_torque_nm();
        for (uint32_t i = 0u; i < 4u; i++) {
            for (uint32_t j = 0u; j < 4u; j++) {
                a[i][j] += b[i] * b[j];
            }
            a[i][4] += b[i] * t;
        }
    }
    for (uint32_t i = 0u; i < 4u; i++) { /* Gauss-Jordan on the 4 x 4 normal equations */
        const double piv = a[i][i];
        for (uint32_t j = 0u; j < 5u; j++) {
            a[i][j] /= piv;
        }
        for (uint32_t r = 0u; r < 4u; r++) {
            const double f = (r == i) ? 0.0 : a[r][i];
            for (uint32_t j = 0u; j < 5u; j++) {
                a[r][j] -= f * a[i][j];
            }
        }
    }
    for (uint32_t j = 0u; j < TQ_RIPPLE_N; j++) {
        const double th = (2.0 * M_PI) * j / TQ_RIPPLE_N;
        const double t = (a[0][4] * sin(6.0 * th)) + (a[1][4] * cos(6.0 * th)) + (a[2][4] * sin(12.0 * th)) +
                         (a[3][4] * cos(12.0 * th));
        tab[j] = (int16_t)lrint(-t / (1.5 * 4.0 * P_PSI) / 0.01);
    }
}

static void wr_req(uint8_t q[3u + (2u * TQ_RIPPLE_N)], const int16_t tab[TQ_RIPPLE_N])
{
    q[0] = 0x2Eu;
    q[1] = 0xFDu;
    q[2] = 0x46u;
    for (uint32_t k = 0u; k < TQ_RIPPLE_N; k++) {
        q[3u + (2u * k)] = (uint8_t)((uint16_t)tab[k] >> 8);
        q[4u + (2u * k)] = (uint8_t)((uint16_t)tab[k] & 0xFFu);
    }
}

/* 0 = 6E FD 46, else the NRC (0xFF: no or another answer) */
static uint8_t wr_table(const uint8_t *q, uint32_t n)
{
    uint8_t r[8];
    if (!iso_write(q, n, r)) {
        return 0xFFu;
    }
    if ((r[1] == 0x7Fu) && (r[2] == 0x2Eu)) {
        return r[3];
    }
    return ((r[0] == 3u) && (r[1] == 0x6Eu) && (r[2] == 0xFDu) && (r[3] == 0x46u)) ? 0u : 0xFFu;
}

/* The loop end to end on the plant with cogging (8XX SiC): the tool's dyno measurement makes the table, 2E FD 46 writes it
 * (segmented ISO-TP), 22 FD 46 reads it back, RID 0xF021 commits it with FW-39's interlocks, the key cycle reads it from
 * NVM. At 100 rpm and 30 N m (6 f_e = 40 Hz) the shaft's torque ripple falls by more than 80 %; at 600 rpm (240 Hz, above
 * cal_ripple_ff_fmax_hz) the table is never applied and the ripple is the one without it. What remains at 100 rpm is mostly
 * the table's own resolution: linear interpolation of 36 points keeps sinc^2 of a harmonic — 0.91 of the 6th (six points
 * a period), 0.68 of the 12th (three) — against the point samples the tool writes. */
TEST(the_ripple_feed_forward_cancels_the_cogging_at_100_rpm)
{
    s_ff_max_a = 10.0f;
    CHECK(boot_on(TI_SKU_8XX_SIC, cog_cfg(), false));
    dyno_ramp(100.0f, 300u);
    h_run_ms(100u);
    int16_t tab[TQ_RIPPLE_N];
    measure_table(tab);
    double mean = 0.0;
    double h_off[2];
    double h_on[2];
    double h600[2];
    float kx = 0.0f;
    H.enable = true;
    H.torque_nm = 30.0f;
    CHECK(h_run_until(SM_RUN, 200u));
    h_run_ms(100u);
    const double off100 = ripple_rms(450u, &mean, h_off, &kx);
    CHECK(kx == 0.0f && fabs(mean - 30.0) < 1.0); /* no table: nothing applied */
    dyno_ramp(600.0f, 300u);
    h_run_ms(100u);
    const double off600 = ripple_rms(450u, &mean, h600, &kx);
    dyno_ramp(100.0f, 300u);
    H.enable = false;
    H.torque_nm = 0.0f;
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 500u));
    h_run_ms(20u);
    uint8_t q[3u + (2u * TQ_RIPPLE_N)];
    wr_req(q, tab);
    CHECK(unlock() && wr_table(q, sizeof q) == 0u && g_mc.rip_staged && !g_app.uds.unlocked);
    uint8_t rb[80];
    const uint8_t rq[3] = {0x22u, 0xFDu, 0x46u};
    CHECK(iso_read(rq, 3u, rb) == 75u && rb[0] == 0x62u && rb[1] == 0xFDu && rb[2] == 0x46u && memcmp(&rb[3], &q[3], 72u) == 0);
    CHECK(unlock() && rc(0x01u, UDS_RID_MC_COMMIT, NULL, 0u, NULL) == 0u);
    h_run_ms(20u);
    calib_t rd;
    CHECK(nv_read(NV_REC_CALIB, &rd, (uint16_t)sizeof rd) && calib_check(&rd, &h_p, h_serial()) == 0u &&
          memcmp(rd.ripple_ff, tab, sizeof tab) == 0);
    /* the key cycle */
    sim_reset();
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    h_p.cal_ripple_ff_max_a = s_ff_max_a;
    sim_pmsm_cfg_t c = cog_cfg();
    c.t_dead_s = 1.1f * (float)h_p.dead_time_ns * 1e-9f;
    c.fsw_hz = (float)h_p.fsw_hz[0];
    sim_pmsm_init(&c, 0.9f);
    H.plant = plant;
    H.veh_speed_valid = true;
    app_init(&g_app, &h_p, NULL, h_serial());
    sim_set_fault_isr(app_fault_isr_entry);
    CHECK(g_app.cal_err == 0u && memcmp(g_app.cal.ripple_ff, tab, sizeof tab) == 0 && h_to_armed());
    dyno_ramp(100.0f, 300u);
    H.enable = true;
    H.torque_nm = 30.0f;
    CHECK(h_run_until(SM_RUN, 200u));
    h_run_ms(100u);
    const double on100 = ripple_rms(450u, &mean, h_on, &kx);
    CHECK(kx == 1.0f && fabs(mean - 30.0) < 1.0);
    CHECK(on100 < 0.2 * off100);
    dyno_ramp(600.0f, 300u);
    h_run_ms(100u);
    const double on600 = ripple_rms(450u, &mean, h600, &kx);
    CHECK(kx == 0.0f && fabs(on600 / off600 - 1.0) < 0.05);
    printf("    torque ripple (rms): 100 rpm %.3f -> %.3f N m (%.1f %% less; 6th %.2f -> %.2f, 12th %.2f -> %.2f N m); 600 rpm"
           " %.3f -> %.3f N m (table not applied)\n", off100, on100, 100.0 * (1.0 - on100 / off100), h_off[0], h_on[0],
           h_off[1], h_on[1], off600, on600);
    s_ff_max_a = 0.0f;
}

/* 2E FD 46 under FW-39's interlocks: the SecurityAccess unlock (0x33 without; one write per unlock), no routine running
 * and not in torque (0x22), exactly 36 values (0x13: a single frame never holds them; a segmented 76 bytes neither), each
 * within cal_ripple_ff_max_a (0x31 — with the default 0 only a zero table). Refused writes stage nothing; 22 FD 46 reads
 * the active record's table (zero) until one is staged. */
TEST(the_ripple_table_write_is_interlocked_and_range_checked)
{
    s_ff_max_a = 0.0f;
    CHECK(boot_on(TI_SKU_8XX_SIC, cog_cfg(), false));
    int16_t tab[TQ_RIPPLE_N];
    for (uint32_t k = 0u; k < TQ_RIPPLE_N; k++) {
        tab[k] = (int16_t)((k % 2u) ? 150 : -150); /* +/- 1.5 A */
    }
    uint8_t q[3u + (2u * TQ_RIPPLE_N) + 1u];
    wr_req(q, tab);
    CHECK(wr_table(q, 75u) == UDS_NRC_SECURITY_DENIED);
    CHECK(unlock() && wr_table(q, 75u) == UDS_NRC_OUT_OF_RANGE && g_app.uds.unlocked); /* the default CAL: 0 A */
    int16_t zero[TQ_RIPPLE_N];
    (void)memset(zero, 0, sizeof zero);
    uint8_t z[75];
    wr_req(z, zero);
    CHECK(wr_table(z, 75u) == 0u && g_mc.rip_staged && !g_app.uds.unlocked); /* a zero table: staged, the unlock used */
    s_ff_max_a = 1.49f; /* 149 counts */
    CHECK(boot_on(TI_SKU_8XX_SIC, cog_cfg(), false) && unlock());
    uint8_t rb[80];
    const uint8_t rq[3] = {0x22u, 0xFDu, 0x46u};
    CHECK(iso_read(rq, 3u, rb) == 75u && memcmp(&rb[3], &z[3], 72u) == 0); /* the record's (zero) table */
    CHECK(wr_table(q, 75u) == UDS_NRC_OUT_OF_RANGE); /* 1.50 A > 1.49 A */
    CHECK(wr_table(q, 76u) == UDS_NRC_LENGTH && wr_table(q, 7u) == UDS_NRC_LENGTH && wr_table(q, 40u) == UDS_NRC_LENGTH);
    tab[0] = 149;
    tab[1] = -149;
    for (uint32_t k = 2u; k < TQ_RIPPLE_N; k++) {
        tab[k] = 0;
    }
    wr_req(q, tab);
    const uint8_t opt[3] = {(uint8_t)MC_RT_RS, 0x4Cu, 0x4Bu};
    CHECK(rc(0x01u, UDS_RID_MC_RUN, opt, 3u, NULL) == 0u && g_mc.st == MC_RUNNING && unlock());
    CHECK(wr_table(q, 75u) == UDS_NRC_CONDITIONS); /* a routine running */
    CHECK(rc(0x02u, UDS_RID_MC_RUN, NULL, 0u, NULL) == 0u);
    H.enable = true;
    H.torque_nm = 20.0f;
    CHECK(h_run_until(SM_RUN, 200u) && g_app.uds.unlocked);
    CHECK(wr_table(q, 75u) == UDS_NRC_CONDITIONS); /* in torque */
    H.enable = false;
    H.torque_nm = 0.0f;
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 500u));
    h_run_ms(10u);
    CHECK(!g_mc.rip_staged && wr_table(q, 75u) == 0u && g_mc.rip_staged);
    CHECK(iso_read(rq, 3u, rb) == 75u && memcmp(&rb[3], &q[3], 72u) == 0);
    s_ff_max_a = 0.0f;
}

/* The feed-forward is scaled down, never the solved vector: torque_ripple_scale keeps (id, iq + k ff) inside the circle
 * and the ellipse at both extremes — all of it with room, a fraction near the circle or the voltage limit, none on them.
 * In the application at the current limit (a hot coolant: 185 A rms, 261.6 A peak) with a +/- 4.3 A table: a request
 * beyond it is TQ_LIMITED on the circle (to the reduction's 0.05 N m) and gets almost nothing; one just inside gets a
 * fraction; the current reference of every current-loop ISR stays inside the circle. */
static float s_imax_chk;
static unsigned s_outside;

static void plant_chk(void)
{
    const float i = sqrtf((g_app.foc.id_ref * g_app.foc.id_ref) + (g_app.foc.iq_ref * g_app.foc.iq_ref));
    s_outside += (i > s_imax_chk * 1.000001f) ? 1u : 0u;
    sim_pmsm_step(H.link_v);
}

TEST(the_feed_forward_never_leaves_the_solved_margin)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    motor_t m = motor_screening();
    CHECK(torque_ripple_scale(0.0f, 100.0f, -5.0f, 5.0f, 0.0f, 750.0f, 300.0f, &m, p) == 1.0f);
    const float k1 = torque_ripple_scale(0.0f, 297.0f, -5.0f, 5.0f, 0.0f, 750.0f, 300.0f, &m, p);
    CHECK(k1 > 0.55f && k1 <= 0.6f && (297.0f + (k1 * 5.0f)) <= 300.0f); /* 3 of the 5 A fit */
    CHECK(torque_ripple_scale(0.0f, 300.0f, -5.0f, 5.0f, 0.0f, 750.0f, 300.0f, &m, p) == 0.0f);
    const float k3 = torque_ripple_scale(0.0f, -297.0f, -5.0f, 4.0f, 0.0f, 750.0f, 300.0f, &m, p); /* regen: the low end */
    CHECK(k3 > 0.55f && k3 <= 0.6f && (-297.0f - (k3 * 5.0f)) >= -300.0f);
    const float w = 3500.0f; /* the voltage: at 400 V the base at 99.5 % of the ellipse leaves a part of the 5 A */
    float id = 0.0f;
    float iq = 0.0f;
    CHECK(torque_to_current(60.0f, w, 400.0f, 480.0f, &m, NULL, p, &id, &iq) == TQ_OK);
    const float k2 = torque_ripple_scale(id, iq, -5.0f, 5.0f, w, 400.0f, 480.0f, &m, p);
    CHECK(k2 < 1.0f && torque_v_required(id, iq + (k2 * 5.0f), w, &m) <= torque_v_available(400.0f, p) &&
          torque_v_required(id, iq - (k2 * 5.0f), w, &m) <= torque_v_available(400.0f, p));
    /* the application */
    static int16_t tab[TQ_RIPPLE_N];
    for (uint32_t k = 0u; k < TQ_RIPPLE_N; k++) {
        tab[k] = (int16_t)lrint(500.0 * sin(6.0 * 2.0 * M_PI * k / TQ_RIPPLE_N)); /* the 6th: +/- 4.33 A at the points */
    }
    s_ff_max_a = 10.0f;
    s_tab = tab;
    CHECK(boot_on(TI_SKU_8XX_SIC, cog_cfg(), false) && g_app.rip_hi > 4.3f && g_app.rip_lo < -4.3f);
    s_tab = NULL;
    H.plant = plant_chk;
    H.coolant_c = 85.0f; /* above the 80 degC end of the peak allowance: the continuous current only */
    dyno_ramp(100.0f, 300u);
    s_imax_chk = TI_SQRT2 * h_p.i_cont_rms_a;
    s_outside = 0u;
    H.enable = true;
    H.torque_nm = 300.0f; /* beyond the circle */
    CHECK(h_run_until(SM_RUN, 200u));
    h_run_ms(300u);
    float id2 = 0.0f;
    float iq2 = 0.0f;
    const tq_res_t r = torque_to_current(g_app.t_cmd_nm, motor_omega_e(g_app.speed_rpm, &g_app.cal.motor), g_app.vdc.vdc,
                                         s_imax_chk, &g_app.cal.motor, NULL, g_app.p, &id2, &iq2);
    const float k_lim = g_app.rip_k;
    CHECK(r == TQ_LIMITED && k_lim < 0.05f && s_outside == 0u); /* on the circle (to the reduction's 0.05 N m): ~nothing */
    const float t_lim = torque_from_current(id2, iq2, &g_app.cal.motor);
    H.torque_nm = 0.985f * t_lim; /* just inside: a part of the feed-forward */
    h_run_ms(300u);
    const float k_part = g_app.rip_k;
    CHECK(k_part > 0.0f && k_part < 1.0f && s_outside == 0u);
    /* the FW-08 zero-current row at the current-loop rate: zero, with or without the feed-forward */
    g_app.zero_now = true;
    h_isr_now();
    CHECK(g_app.foc.id_ref == 0.0f && g_app.foc.iq_ref == 0.0f);
    g_app.zero_now = false;
    /* the build's clamp: the same table under a 2 A CAL is applied within +/- 2 A; a table that is only a mean (a constant)
     * is no ripple — nothing to apply */
    s_ff_max_a = 2.0f;
    s_tab = tab;
    CHECK(boot_on(TI_SKU_8XX_SIC, cog_cfg(), false) && g_app.rip_hi == 2.0f && g_app.rip_lo == -2.0f);
    static int16_t dc[TQ_RIPPLE_N];
    for (uint32_t k = 0u; k < TQ_RIPPLE_N; k++) {
        dc[k] = 150;
    }
    s_tab = dc;
    CHECK(boot_on(TI_SKU_8XX_SIC, cog_cfg(), false) && g_app.rip_hi == 0.0f && g_app.rip_lo == 0.0f);
    dyno_ramp(100.0f, 300u);
    H.enable = true;
    H.torque_nm = 30.0f;
    CHECK(h_run_until(SM_RUN, 200u));
    h_run_ms(100u);
    CHECK(g_app.rip_k == 0.0f && g_app.foc.iq_ref == g_app.iq_ref);
    s_tab = NULL;
    printf("    feed-forward at the current limit (261.6 A): LIMITED %.1f N m -> k %.3f; at 98.5 %% of it k %.3f; %u ISRs outside\n",
           (double)t_lim, (double)k_lim, (double)k_part, s_outside);
    H.coolant_c = 50.0f;
    s_ff_max_a = 0.0f;
}

/* The default: cal_ripple_ff_max_a = 0 — a table in the record changes nothing, bit for bit: the same run (100 rpm, 30 N m,
 * the plant with cogging) with and without the table gives the same shaft torque at every millisecond. */
TEST(with_the_default_cal_a_table_changes_nothing)
{
    static double trace[2][400];
    static int16_t tab[TQ_RIPPLE_N];
    for (uint32_t k = 0u; k < TQ_RIPPLE_N; k++) {
        tab[k] = (int16_t)(100 * (int32_t)(k % 7u));
    }
    bool off = true;
    for (uint32_t run = 0u; run < 2u; run++) {
        s_ff_max_a = 0.0f;
        s_tab = (run == 1u) ? tab : NULL; /* the same power-up with a table in the record */
        CHECK(boot_on(TI_SKU_8XX_SIC, cog_cfg(), false));
        CHECK((run == 0u) || (memcmp(g_app.cal.ripple_ff, tab, sizeof tab) == 0));
        dyno_ramp(100.0f, 300u);
        H.enable = true;
        H.torque_nm = 30.0f;
        CHECK(h_run_until(SM_RUN, 200u));
        for (uint32_t k = 0u; k < 400u; k++) {
            h_run_ms(1u);
            trace[run][k] = sim_pmsm_torque_nm();
            off = off && (g_app.rip_k == 0.0f);
        }
    }
    s_tab = NULL;
    CHECK(off && memcmp(trace[0], trace[1], sizeof trace[0]) == 0);
}

void suite_fw45_46(void)
{
    RUN(a_flat_map_is_the_scalar_solve_bit_for_bit);
    RUN(the_saturated_solve_meets_the_references_on_random_saturating_maps);
    RUN(the_psi_e_guard_keeps_the_contour_defined_on_weak_magnets);
    RUN(the_record_is_layout_4_and_a_bad_map_is_refused);
    RUN(the_map_delivers_the_torque_of_a_saturating_machine_at_80_percent_current);
    RUN(gain_scheduling_keeps_the_current_loops_margin_on_a_saturating_axis);
    RUN(the_routine_at_six_biases_measures_and_commits_the_map);
    RUN(the_bias_byte_is_checked_and_a_map_is_committed_whole);
    RUN(the_bias_runs_on_the_true_axes);
    RUN(the_ripple_feed_forward_cancels_the_cogging_at_100_rpm);
    RUN(the_ripple_table_write_is_interlocked_and_range_checked);
    RUN(the_feed_forward_never_leaves_the_solved_margin);
    RUN(with_the_default_cal_a_table_changes_nothing);
}
