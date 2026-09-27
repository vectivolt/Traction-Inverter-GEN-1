/* test_torque.c — FW-03 envelope, FW-04 derating, MTPA/FW/demag/circle, NaN, BMS limits; round 23 (FW-37): torque
 * never above the request, the least current, the reduction, the LUT check and the postcondition. */
#include "test.h"
#include "torque.h"

TEST(fw03_envelope_matches_contract_table)
{
    const ti_params_t *s8 = ti_params_get(TI_SKU_8XX_SIC);
    const ti_params_t *g4 = ti_params_get(TI_SKU_4XX_IGBT);
    CHECK_NEAR(torque_p_max_w(654.0f, 340.0f, 220e3f, s8), 220e3, 300.0); /* full peak from 654 V */
    CHECK_NEAR(torque_p_max_w(500.0f, 340.0f, 220e3f, s8), 168e3, 1000.0);
    CHECK_NEAR(torque_p_max_w(656.0f, 185.0f, 120e3f, s8), 120e3, 300.0);
    CHECK_NEAR(torque_p_max_w(500.0f, 185.0f, 120e3f, s8), 91e3, 1000.0);
    CHECK_NEAR(torque_p_max_w(379.0f, 400.0f, 150e3f, g4), 150e3, 300.0);
    CHECK_NEAR(torque_p_max_w(250.0f, 400.0f, 150e3f, g4), 99e3, 1000.0);
    CHECK_NEAR(torque_p_max_w(250.0f, 250.0f, 90e3f, g4), 62e3, 1000.0);
}

TEST(mtpa_spm_and_ipm)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const motor_t spm = motor_screening();
    float id;
    float iq;
    CHECK(torque_to_current(180.0f, 0.0f, 700.0f, 480.0f, &spm, NULL, p, &id, &iq));
    CHECK_NEAR(id, 0.0, 1e-3);
    CHECK_NEAR(iq, 180.0 / (1.5 * 4.0 * 0.15), 0.1);
    motor_t ipm = spm;
    ipm.ld_h = 0.2e-3f;
    ipm.lq_h = 0.5e-3f;
    CHECK(torque_to_current(200.0f, 0.0f, 700.0f, 480.0f, &ipm, NULL, p, &id, &iq));
    CHECK(id < 0.0f);
    const float t = 1.5f * 4.0f * (ipm.psi_wb * iq + (ipm.ld_h - ipm.lq_h) * id * iq);
    CHECK_NEAR(t, 200.0, 1.0);
    CHECK_NEAR(id, torque_mtpa_id(iq, &ipm), 0.5);
    CHECK(torque_to_current(-200.0f, 0.0f, 700.0f, 480.0f, &ipm, NULL, p, &id, &iq) && iq < 0.0f);
}

/* Round 23: the table must be this motor's — the screening motor makes 0.9 N m/A, so iq = T / 0.9 (the round-22
 * table, iq 110 / 210 A, gave 144 N m for 150: it is the refused case of the test below). */
TEST(mtpa_lut_interpolates)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const motor_t m = motor_screening();
    mtpa_lut_t lut = {.n = 3u, .t_nm = {0.0f, 100.0f, 200.0f}, .id_a = {0.0f, -20.0f, -60.0f},
                      .iq_a = {0.0f, 100.0f / 0.9f, 200.0f / 0.9f}};
    float id;
    float iq;
    CHECK(torque_to_current(150.0f, 0.0f, 700.0f, 480.0f, &m, &lut, p, &id, &iq) == TQ_OK);
    CHECK_NEAR(id, -40.0, 1e-3);
    CHECK_NEAR(iq, 150.0 / 0.9, 1e-3);
    lut.t_nm[2] = 50.0f; /* non-monotonic table falls back to the closed form */
    CHECK(torque_to_current(150.0f, 0.0f, 700.0f, 480.0f, &m, &lut, p, &id, &iq));
    CHECK_NEAR(id, 0.0, 1e-3);
}

TEST(field_weakening_holds_voltage_ellipse_and_demag_clamp)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    motor_t m = motor_screening();
    float id;
    float iq;
    const float vdc = 600.0f;
    const float w = motor_omega_e(9000.0f, &m); /* beyond base speed at 600 V */
    CHECK(torque_to_current(60.0f, w, vdc, 480.0f, &m, NULL, p, &id, &iq));
    const float v_lim = p->cal_mod_index_max * vdc / TI_SQRT3;
    const float lhs = (m.ld_h * id + m.psi_wb) * (m.ld_h * id + m.psi_wb) + (m.lq_h * iq) * (m.lq_h * iq);
    CHECK(lhs <= (v_lim / w) * (v_lim / w) * 1.001f);
    CHECK(id < 0.0f);
    m.id_demag_a = 100.0f;
    CHECK(torque_to_current(60.0f, w, vdc, 480.0f, &m, NULL, p, &id, &iq) == TQ_INFEASIBLE); /* F23 */
    CHECK(id >= -100.0f - 1e-3f); /* never below the demagnetisation limit */
}

TEST(current_circle)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const motor_t m = motor_screening();
    float id;
    float iq;
    CHECK(torque_to_current(2000.0f, 0.0f, 700.0f, 480.0f, &m, NULL, p, &id, &iq));
    CHECK(id * id + iq * iq <= 480.0f * 480.0f * 1.0001f);
    CHECK_NEAR(iq, 480.0, 0.1);
}

TEST(nan_torque_command_zeroed)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const motor_t m = motor_screening();
    float id = 5.0f;
    float iq = 5.0f;
    CHECK(!torque_to_current(NAN, 0.0f, 700.0f, 480.0f, &m, NULL, p, &id, &iq));
    CHECK(id == 0.0f && iq == 0.0f);
    CHECK(!torque_to_current(10.0f, INFINITY, 700.0f, 480.0f, &m, NULL, p, &id, &iq));
    torque_lim_t l;
    torque_lim_init(&l, p);
    torque_limits(&l, 700.0f, 100.0f, 50e3f, 200e3f, true, p);
    CHECK(torque_clamp(&l, NAN, 100.0f) == 0.0f);
}

TEST(derate_hysteresis)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    torque_lim_t l;
    torque_lim_init(&l, p);
    torque_derate_update(&l, 80.0f, true, 50.0f, true, 100.0f, 1e-3f, p);
    CHECK(!l.derate_active && l.derate == 1.0f);
    torque_derate_update(&l, 100.0f, true, 50.0f, true, 100.0f, 1e-3f, p);
    CHECK(l.derate_active && l.derate < 1.0f);
    torque_derate_update(&l, 88.0f, true, 50.0f, true, 100.0f, 1e-3f, p); /* below start, inside hysteresis */
    CHECK(l.derate_active);
    torque_derate_update(&l, 84.0f, true, 50.0f, true, 100.0f, 1e-3f, p);
    CHECK(!l.derate_active && l.derate == 1.0f);
    torque_derate_update(&l, 120.0f, true, 50.0f, true, 100.0f, 1e-3f, p);
    CHECK(l.derate == 0.0f && l.i_limit_rms_a == 0.0f);
    torque_derate_update(&l, 25.0f, false, 50.0f, true, 100.0f, 1e-3f, p); /* NTC invalid: never up */
    CHECK(l.derate <= p->i_cont_rms_a / p->i_pk_rms_a);
}

TEST(peak_budget_30s_and_full_recovery)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    torque_lim_t l;
    torque_lim_init(&l, p);
    for (int k = 0; k < 29900; k++) {
        torque_derate_update(&l, 60.0f, true, 50.0f, true, 300.0f, 1e-3f, p);
    }
    CHECK(!l.peak_exhausted && l.i_limit_rms_a > p->i_cont_rms_a);
    for (int k = 0; k < 200; k++) {
        torque_derate_update(&l, 60.0f, true, 50.0f, true, 300.0f, 1e-3f, p);
    }
    CHECK(l.peak_exhausted);
    CHECK_NEAR(l.i_limit_rms_a, p->i_cont_rms_a, 1e-3);
    for (int k = 0; k < 170000; k++) { /* 170 s at continuous: not yet recovered */
        torque_derate_update(&l, 60.0f, true, 50.0f, true, 150.0f, 1e-3f, p);
    }
    CHECK(l.peak_exhausted);
    for (int k = 0; k < 11000; k++) {
        torque_derate_update(&l, 60.0f, true, 50.0f, true, 150.0f, 1e-3f, p);
    }
    CHECK(!l.peak_exhausted);
}

TEST(coolant_above_assumption_removes_peak)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    torque_lim_t l;
    torque_lim_init(&l, p);
    torque_derate_update(&l, 60.0f, true, 85.0f, true, 100.0f, 1e-3f, p);
    CHECK_NEAR(l.i_limit_rms_a, p->i_cont_rms_a, 1e-3);
    torque_derate_update(&l, 60.0f, true, 0.0f, false, 100.0f, 1e-3f, p); /* unknown coolant: no peak */
    CHECK_NEAR(l.i_limit_rms_a, p->i_cont_rms_a, 1e-3);
    torque_derate_update(&l, 60.0f, true, 60.0f, true, 100.0f, 1e-3f, p);
    CHECK_NEAR(l.i_limit_rms_a, p->i_pk_rms_a, 1e-3);
}

TEST(bms_limits_and_timeout_zero_regen)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    torque_lim_t l;
    torque_lim_init(&l, p);
    const float w = 300.0f; /* rad/s mech */
    torque_limits(&l, 750.0f, w, 30e3f, 200e3f, true, p);
    CHECK_NEAR(l.t_lim_regen_nm, 100.0, 0.1); /* 30 kW / 300 rad/s */
    CHECK(torque_clamp(&l, -300.0f, w) >= -100.1f);
    torque_limits(&l, 750.0f, w, 30e3f, 200e3f, false, p); /* BMS stale */
    CHECK(l.t_lim_regen_nm == 0.0f);
    CHECK(torque_clamp(&l, -300.0f, w) == 0.0f);
    CHECK(torque_clamp(&l, 300.0f, w) > 0.0f); /* motoring bounded by the inverter itself */
    torque_limits(&l, 750.0f, w, 0.0f, 200e3f, true, p); /* charge limit 0: zero regen */
    CHECK(torque_clamp(&l, -50.0f, w) == 0.0f);
}

/* |v| of a dq pair in double, independent of the firmware's own helper */
static double v_need(double id, double iq, double w, const motor_t *m)
{
    const double vd = m->rs_ohm * id - w * m->lq_h * iq;
    const double vq = m->rs_ohm * iq + w * (m->ld_h * id + m->psi_wb);
    return sqrt(vd * vd + vq * vq);
}

/* F23: a motor-map sweep (low bus, high speed, hot magnets, a salient motor, a restrictive
 * demagnetisation limit). Every result the firmware may use passes the voltage witness and the
 * limits; every TQ_INFEASIBLE is really infeasible at iq = 0 anywhere inside the limits and comes
 * back as iq = 0. */
TEST(witness_motor_map_sweep_never_returns_an_infeasible_pair)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const float vdc[3] = {250.0f, 500.0f, 750.0f};
    const float rpm[6] = {0.0f, 2000.0f, 6000.0f, 9000.0f, 12000.0f, 16000.0f};
    const float tq[5] = {-300.0f, -100.0f, 0.0f, 100.0f, 300.0f};
    unsigned n_ok = 0u;
    unsigned n_lim = 0u;
    unsigned n_inf = 0u;
    unsigned bad = 0u;
    for (unsigned mv = 0u; mv < 4u; mv++) {
        motor_t m = motor_screening();
        if (mv == 1u) {
            m.psi_wb = 0.12f; /* hot magnets */
        } else if (mv == 2u) {
            m.ld_h = 0.2e-3f; /* salient (IPM) */
            m.lq_h = 0.5e-3f;
        } else if (mv == 3u) {
            m.id_demag_a = 100.0f; /* restrictive demagnetisation limit */
        } else {
            /* the screening motor */
        }
        for (unsigned a = 0u; a < 3u; a++) {
            for (unsigned b = 0u; b < 6u; b++) {
                for (unsigned c = 0u; c < 5u; c++) {
                    const float w = motor_omega_e(rpm[b], &m);
                    float id = 0.0f;
                    float iq = 0.0f;
                    const tq_res_t r = torque_to_current(tq[c], w, vdc[a], 480.0f, &m, NULL, p, &id, &iq);
                    const double v_av = torque_v_available(vdc[a], p);
                    if ((r == TQ_OK) || (r == TQ_LIMITED)) {
                        bad += (v_need(id, iq, w, &m) <= v_av * 1.0001) ? 0u : 1u;
                        bad += ((id * id + iq * iq) <= 480.0 * 480.0 * 1.0001) ? 0u : 1u;
                        bad += (id >= -m.id_demag_a - 1e-3f) ? 0u : 1u;
                        n_ok += (r == TQ_OK) ? 1u : 0u;
                        n_lim += (r == TQ_LIMITED) ? 1u : 0u;
                    } else if (r == TQ_INFEASIBLE) {
                        n_inf++;
                        bad += (iq == 0.0f) ? 0u : 1u;
                        const double d_min = fmax(-m.id_demag_a, -480.0);
                        for (int k = 0; k <= 400; k++) { /* brute force over every allowed id */
                            const double d = d_min + (480.0 - d_min) * k / 400.0;
                            bad += (v_need(d, 0.0, w, &m) > v_av) ? 0u : 1u;
                        }
                    } else {
                        bad++;
                    }
                }
            }
        }
    }
    CHECK(bad == 0u);
    CHECK(n_ok > 0u && n_lim > 0u && n_inf > 0u); /* the sweep reaches all three outcomes */
    /* the review case: 9000 rpm at 600 V with the demagnetisation limit at 100 A */
    motor_t m = motor_screening();
    m.id_demag_a = 100.0f;
    float id = 0.0f;
    float iq = 0.0f;
    const float w = motor_omega_e(9000.0f, &m);
    CHECK(torque_to_current(60.0f, w, 600.0f, 480.0f, &m, NULL, p, &id, &iq) == TQ_INFEASIBLE);
    CHECK(iq == 0.0f && id >= -100.0f - 1e-3f && v_need(id, iq, w, &m) > torque_v_available(600.0f, p));
}

/* ======================= round 23 (FW-37) ======================= */

/* The review's salient motor: Ld 0.2 mH, Lq 0.8 mH, 25 mOhm, 0.1 Wb, 4 pole pairs (inside the calibration ranges),
 * the screening motor's 450 A demagnetisation limit. */
static motor_t motor_salient(void)
{
    motor_t m = motor_screening();
    m.ld_h = 0.2e-3f;
    m.lq_h = 0.8e-3f;
    m.rs_ohm = 0.025f;
    m.psi_wb = 0.1f;
    m.pp = 4u;
    return m;
}

/* the torque of a dq pair in double, independent of the firmware's own helper */
static double t_of(double id, double iq, const motor_t *m)
{
    return 1.5 * m->pp * (m->psi_wb + (m->ld_h - m->lq_h) * id) * iq;
}

#define ORACLE_N 20000

/* The largest torque of sign sg (+1 / -1) that any pair inside the voltage ellipse, the current circle and the
 * demagnetisation limit gives: a fine scan of id with, at each id, the feasible iq interval in closed form (the
 * circle's, intersected with the roots of the quadratic |v|^2 = v_av^2 in iq) — a different method from the
 * firmware's, which walks torque hyperbolas. -INFINITY if nothing fits. */
static double t_max_oracle(double sg, double w, double v_av, double i_max, const motor_t *m)
{
    const double d_lo = -fmin(i_max, m->id_demag_a);
    double best = -INFINITY;
    for (int k = 0; k <= ORACLE_N; k++) {
        const double d = d_lo * k / ORACLE_N;
        const double pe = m->psi_wb + (m->ld_h - m->lq_h) * d;
        const double qc2 = i_max * i_max - d * d;
        const double a = w * w * m->lq_h * m->lq_h + m->rs_ohm * m->rs_ohm;
        const double b = m->rs_ohm * w * pe;
        const double c = m->rs_ohm * m->rs_ohm * d * d + w * w * (m->ld_h * d + m->psi_wb) * (m->ld_h * d + m->psi_wb) -
                         v_av * v_av;
        const double disc = b * b - a * c;
        if ((pe <= 0.0) || (qc2 < 0.0) || (disc < 0.0)) {
            continue;
        }
        const double lo = fmax((-b - sqrt(disc)) / a, -sqrt(qc2));
        const double hi = fmin((-b + sqrt(disc)) / a, sqrt(qc2));
        if (lo <= hi) {
            best = fmax(best, sg * 1.5 * m->pp * pe * ((sg > 0.0) ? hi : lo));
        }
    }
    return best;
}

/* The least current of the pairs on the hyperbola of torque t that fit: a fine scan of id. INFINITY if none. */
static double i_least_oracle(double t, double w, double v_av, double i_max, const motor_t *m)
{
    const double d_lo = -fmin(i_max, m->id_demag_a);
    double best = INFINITY;
    for (int k = 0; k <= 10 * ORACLE_N; k++) {
        const double d = d_lo * k / (10 * ORACLE_N);
        const double q = t / (1.5 * m->pp * (m->psi_wb + (m->ld_h - m->lq_h) * d));
        const double mag = sqrt(d * d + q * q);
        if ((mag <= i_max) && (v_need(d, q, w, m) <= v_av)) {
            best = fmin(best, mag);
        }
    }
    return best;
}

/* One result against the references: the limits; never more torque than asked, never the other sign; TQ_OK = the
 * request within 0.1 % at the least current that fits (+0.2 %), wherever the oracle finds it feasible (0.1 % margin);
 * TQ_LIMITED = less, within 0.1 N m of the most that fits. Returns the checks that failed. */
static unsigned check_result(tq_res_t r, float t_req, float w, float vdc, float i_max, const motor_t *m, float id,
                             float iq)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const double v_av = torque_v_available(vdc, p);
    const double t = t_of(id, iq, m);
    const double t_max = t_max_oracle((t_req >= 0.0f) ? 1.0 : -1.0, w, v_av, i_max, m);
    unsigned bad = 0u;
    bad += ((r == TQ_OK) || (r == TQ_LIMITED)) ? 0u : 1u;
    bad += ((fabs(t) <= fabs(t_req) * 1.001 + 1e-3) && (t * t_req >= 0.0)) ? 0u : 1u;
    bad += (v_need(id, iq, w, m) <= v_av * 1.00001) ? 0u : 1u;
    bad += ((id * id + iq * iq) <= (double)i_max * i_max * 1.00001) ? 0u : 1u;
    bad += (id >= -m->id_demag_a - 1e-3) ? 0u : 1u;
    if (t_max >= fabs(t_req) * 1.001) {
        bad += ((r == TQ_OK) && (fabs(t - t_req) <= 1e-3 * fabs(t_req) + 1e-3)) ? 0u : 1u;
        bad += (sqrt(id * id + iq * iq) <= i_least_oracle(t_req, w, v_av, i_max, m) * 1.002) ? 0u : 1u;
    } else if (t_max >= 0.0) {
        bad += ((r == TQ_LIMITED) && (fabs(t) < fabs(t_req)) && (fabs(t) >= t_max - 0.1)) ? 0u : 1u;
    } else {
        bad++; /* the cases here are all feasible at zero torque */
    }
    if (bad != 0u) {
        printf("    ^ T %+.1f N m, w %+.0f rad/s, %.0f V, %.0f A: r %d, id %.2f, iq %.2f -> %+.3f N m (most that fits %.3f)\n",
               (double)t_req, (double)w, (double)vdc, (double)i_max, (int)r, (double)id, (double)iq, t, t_max);
    }
    return bad;
}

/* The external review's reproduction (defect 1): the salient motor in field weakening, all four quadrants at 400 V /
 * 2000 rad/s el / 250 A rms and 700 V / 3500 rad/s el / 340 A rms, +-100 N m. Round 22 moved the MTPA id to the field-
 * weakening id (-305 A) with iq kept, and the reluctance torque of that id doubled the torque:
 *   400 V, +w: +100 -> +195.8 N m at 230.6 A rms (TQ_LIMITED), -100 -> -205.1 N m (TQ_OK); -w: the mirror image;
 *   700 V, +w: +100 -> +199.8 N m (TQ_LIMITED),                 -100 -> -205.1 N m (TQ_OK); -w: the mirror image.
 * Now: exactly the request (TQ_OK, all eight are feasible) at the least current that fits: 112-118 A rms. */
TEST(field_weakening_never_delivers_more_torque_than_requested)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const motor_t m = motor_salient();
    const float vdc[2] = {400.0f, 700.0f};
    const float w_e[2] = {2000.0f, 3500.0f};
    const float i_rms[2] = {250.0f, 340.0f};
    for (unsigned a = 0u; a < 2u; a++) {
        for (unsigned c = 0u; c < 4u; c++) {
            const float w = ((c & 1u) != 0u) ? -w_e[a] : w_e[a];
            const float t_req = ((c & 2u) != 0u) ? -100.0f : 100.0f;
            const float i_max = TI_SQRT2 * i_rms[a];
            float id = 0.0f;
            float iq = 0.0f;
            const tq_res_t r = torque_to_current(t_req, w, vdc[a], i_max, &m, NULL, p, &id, &iq);
            CHECK(check_result(r, t_req, w, vdc[a], i_max, &m, id, iq) == 0u);
            CHECK(r == TQ_OK && fabs(t_of(id, iq, &m) - t_req) <= 0.1); /* all eight are feasible */
        }
    }
}

/* Defect 2: the MTPA fallback was an 8-step fixed point on iq with no convergence criterion. At 200 N m it oscillated
 * to 267.4 A on a strongly salient motor (Ld 0.2 mH, Lq 1.0 mH, 0.05 Wb, 4 pole pairs) where the least current is
 * 246.3 A (+8.6 %, +17.9 % I^2) — and 227.87 A for 227.83 A on the review's motor. Now a bisection with an interval
 * criterion: within 0.2 % of the optimum bracketed here by a fine scan of the hyperbola, the torque exact, the MTPA
 * condition of the closed form met; both torque signs, standstill. */
TEST(mtpa_is_the_least_current_point_of_the_torque_hyperbola)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    for (unsigned k = 0u; k < 2u; k++) {
        motor_t m = motor_salient();
        if (k == 1u) {
            m.lq_h = 1.0e-3f;
            m.psi_wb = 0.05f;
        }
        for (unsigned s = 0u; s < 2u; s++) {
            const float t_req = (s == 0u) ? 200.0f : -200.0f;
            float id = 0.0f;
            float iq = 0.0f;
            CHECK(torque_to_current(t_req, 0.0f, 700.0f, TI_SQRT2 * 340.0f, &m, NULL, p, &id, &iq) == TQ_OK);
            double best = INFINITY;
            for (int j = 0; j <= 450000; j++) { /* the hyperbola, 1 mA steps down to the demagnetisation limit */
                const double d = -1e-3 * j;
                const double q = t_req / (1.5 * m.pp * (m.psi_wb + (m.ld_h - m.lq_h) * d));
                best = fmin(best, sqrt(d * d + q * q));
            }
            const double mag = sqrt((double)id * id + (double)iq * iq);
            CHECK(mag <= best * 1.002 && mag >= best * (1.0 - 1e-6));
            CHECK(fabs(t_of(id, iq, &m) - t_req) <= 0.2);
            CHECK_NEAR(id, torque_mtpa_id(iq, &m), 0.5);
            if (mag > best * 1.002) {
                printf("    ^ motor %u, %+.0f N m: |i| %.2f A, least %.2f A\n", k, (double)t_req, mag, best);
            }
        }
    }
}

/* Monotone in speed: the review's motor with a 150 A demagnetisation limit at 400 V / 250 A rms, +-150 N m, both
 * directions of rotation, 0 to 6000 rad/s el in 50 rad/s steps. The torque delivered never rises with |w| (0.05 N m:
 * the reduction's resolution), the status runs TQ_OK -> TQ_LIMITED -> TQ_INFEASIBLE and never back, all three occur,
 * and every TQ_OK / TQ_LIMITED passes the references above. */
TEST(delivered_torque_falls_with_speed_through_ok_limited_infeasible)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    motor_t m = motor_salient();
    m.id_demag_a = 150.0f;
    const float i_max = TI_SQRT2 * 250.0f;
    for (unsigned c = 0u; c < 4u; c++) {
        const float dir = ((c & 1u) != 0u) ? -1.0f : 1.0f;
        const float t_req = ((c & 2u) != 0u) ? -150.0f : 150.0f;
        float t_prev = INFINITY;
        int stage = 0; /* 0 OK, 1 LIMITED, 2 INFEASIBLE */
        unsigned seen = 0u;
        unsigned bad = 0u;
        for (unsigned k = 0u; k <= 120u; k++) {
            const float w = dir * 50.0f * (float)k;
            float id = 0.0f;
            float iq = 0.0f;
            const tq_res_t r = torque_to_current(t_req, w, 400.0f, i_max, &m, NULL, p, &id, &iq);
            const int st = (r == TQ_OK) ? 0 : ((r == TQ_LIMITED) ? 1 : ((r == TQ_INFEASIBLE) ? 2 : 3));
            const float t = ti_absf(torque_from_current(id, iq, &m));
            bad += (st >= stage && st != 3) ? 0u : 1u;
            bad += (t <= t_prev + 0.05f) ? 0u : 1u;
            bad += (st == 2) ? ((iq == 0.0f) ? 0u : 1u) : check_result(r, t_req, w, 400.0f, i_max, &m, id, iq);
            stage = (st < 3) ? st : stage;
            seen |= 1u << (unsigned)st;
            t_prev = t;
        }
        CHECK(bad == 0u && seen == 7u);
        if ((bad != 0u) || (seen != 7u)) {
            printf("    ^ %+.0f N m, direction %+.0f: %u failed, outcomes seen 0x%x\n", (double)t_req, (double)dir, bad, seen);
        }
    }
}

/* The sign of the request is kept in all four quadrants, at MTPA and in field weakening; zero torque is exactly zero
 * (iq = 0): at standstill id = 0, in field weakening the least |id| that holds the voltage, beyond the demagnetisation
 * limit TQ_INFEASIBLE; a request of 1 mN m keeps its sign or is zero. */
TEST(torque_sign_and_zero_torque)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    motor_t m = motor_salient();
    float id = 1.0f;
    float iq = 1.0f;
    CHECK(torque_to_current(0.0f, 0.0f, 700.0f, 480.0f, &m, NULL, p, &id, &iq) == TQ_OK && id == 0.0f && iq == 0.0f);
    const float w_fw = 4000.0f; /* back-EMF 400 V: above the 208 V a 400 V link allows */
    CHECK(torque_to_current(0.0f, w_fw, 400.0f, 480.0f, &m, NULL, p, &id, &iq) == TQ_OK && iq == 0.0f && id < 0.0f);
    const double v_av = torque_v_available(400.0f, p);
    CHECK(v_need(id, 0.0, w_fw, &m) <= v_av * 1.00001 && v_need(id + 0.05, 0.0, w_fw, &m) > v_av); /* the least |id| */
    m.id_demag_a = 100.0f;
    CHECK(torque_to_current(0.0f, w_fw, 400.0f, 480.0f, &m, NULL, p, &id, &iq) == TQ_INFEASIBLE && iq == 0.0f &&
          id >= -100.0f - 1e-3f);
    m = motor_salient();
    const float tq[4] = {50.0f, -50.0f, 1.0e-3f, -1.0e-3f};
    const float ws[5] = {0.0f, 2500.0f, -2500.0f, 4000.0f, -4000.0f};
    unsigned bad = 0u;
    for (unsigned a = 0u; a < 4u; a++) {
        for (unsigned b = 0u; b < 5u; b++) {
            const tq_res_t r = torque_to_current(tq[a], ws[b], 400.0f, 480.0f, &m, NULL, p, &id, &iq);
            const double t = t_of(id, iq, &m);
            bad += ((r == TQ_OK) || (r == TQ_LIMITED)) ? 0u : 1u;
            bad += ((t == 0.0) || ((t > 0.0) == (tq[a] > 0.0f))) ? 0u : 1u;
            bad += ((iq == 0.0f) || ((iq > 0.0f) == (tq[a] > 0.0f))) ? 0u : 1u;
            bad += (fabs(t) <= fabs(tq[a]) * 1.001 + 1e-3) ? 0u : 1u;
        }
    }
    CHECK(bad == 0u);
}

/* A calibration LUT is used only where its point reproduces the torque (0.1 %). The consistent table of the test above
 * is used as it is; with its iq 10 % high (+10 % torque — round 22 used it: 165 N m for 150) or the round-22 fixture
 * (iq 110 / 210 A: 144 N m for 150) it is refused: the solved MTPA point, the torque exact; a NaN entry the same. On
 * the salient motor a table of its own MTPA points (the optimum of a fine scan) is used at them, as it is. */
TEST(mtpa_lut_is_refused_where_it_does_not_reproduce_the_torque)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const motor_t spm = motor_screening();
    mtpa_lut_t lut = {.n = 3u, .t_nm = {0.0f, 100.0f, 200.0f}, .id_a = {0.0f, -20.0f, -60.0f},
                      .iq_a = {0.0f, 1.1f * 100.0f / 0.9f, 1.1f * 200.0f / 0.9f}};
    float id = 0.0f;
    float iq = 0.0f;
    for (unsigned k = 0u; k < 3u; k++) {
        if (k == 1u) {
            lut.iq_a[1] = 110.0f; /* the round-22 fixture */
            lut.iq_a[2] = 210.0f;
        } else if (k == 2u) {
            lut.iq_a[1] = 100.0f / 0.9f;
            lut.iq_a[2] = 200.0f / 0.9f;
            lut.id_a[2] = NAN;
        } else {
            /* 10 % high */
        }
        CHECK(torque_to_current(150.0f, 0.0f, 700.0f, 480.0f, &spm, &lut, p, &id, &iq) == TQ_OK);
        CHECK(id == 0.0f && fabs(t_of(id, iq, &spm) - 150.0) <= 0.15); /* the SPM's MTPA: id = 0 */
    }
    const motor_t m = motor_salient();
    mtpa_lut_t own = {.n = 5u};
    for (unsigned k = 0u; k < 5u; k++) {
        const double t = 100.0 * k;
        double best = INFINITY;
        double d_best = 0.0;
        for (int j = 0; j <= 450000; j++) {
            const double d = -1e-3 * j;
            const double q = t / (1.5 * m.pp * (m.psi_wb + (m.ld_h - m.lq_h) * d));
            if (sqrt(d * d + q * q) < best) {
                best = sqrt(d * d + q * q);
                d_best = d;
            }
        }
        own.t_nm[k] = (float)t;
        own.id_a[k] = (float)d_best;
        own.iq_a[k] = (float)(t / (1.5 * m.pp * (m.psi_wb + (m.ld_h - m.lq_h) * d_best)));
    }
    for (unsigned k = 1u; k < 5u; k++) {
        CHECK(torque_to_current(own.t_nm[k], 0.0f, 700.0f, 480.0f, &m, &own, p, &id, &iq) == TQ_OK);
        CHECK(id == own.id_a[k] && iq == own.iq_a[k]);
        CHECK(torque_to_current(-own.t_nm[k], 0.0f, 700.0f, 480.0f, &m, &own, p, &id, &iq) == TQ_OK);
        CHECK(id == own.id_a[k] && iq == -own.iq_a[k]);
    }
}

/* Every non-finite input is TQ_NONFINITE with the outputs zeroed; so is a non-finite result (a NaN flux in the motor
 * record), which never reaches the postcondition as a number. */
TEST(non_finite_inputs_and_results_zero_the_outputs)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const motor_t m = motor_salient();
    const float bad[3] = {NAN, INFINITY, -INFINITY};
    unsigned fails = 0u;
    for (unsigned k = 0u; k < 3u; k++) {
        for (unsigned a = 0u; a < 4u; a++) {
            float in[4] = {100.0f, 2000.0f, 400.0f, 350.0f};
            in[a] = bad[k];
            float id = 5.0f;
            float iq = 5.0f;
            const tq_res_t r = torque_to_current(in[0], in[1], in[2], in[3], &m, NULL, p, &id, &iq);
            fails += ((r == TQ_NONFINITE) && (id == 0.0f) && (iq == 0.0f)) ? 0u : 1u;
        }
    }
    CHECK(fails == 0u);
    motor_t n = m;
    n.psi_wb = NAN;
    float id = 5.0f;
    float iq = 5.0f;
    CHECK(torque_to_current(100.0f, 2000.0f, 400.0f, 350.0f, &n, NULL, p, &id, &iq) == TQ_NONFINITE && id == 0.0f &&
          iq == 0.0f);
}

/* The postcondition runs on every result, whatever path produced it. A corrupt motor record that the calibration check
 * would refuse — a negative demagnetisation limit, i.e. "id >= +50 A" — leaves the solver only a magnetising id: that
 * vector breaks the demagnetisation rule, so it is TQ_POSTCOND with the outputs zeroed, never a reference. */
TEST(a_vector_failing_its_postcondition_is_never_returned)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    motor_t m = motor_salient();
    m.id_demag_a = -50.0f;
    float id = 5.0f;
    float iq = 5.0f;
    CHECK(torque_to_current(50.0f, 0.0f, 700.0f, 480.0f, &m, NULL, p, &id, &iq) == TQ_POSTCOND);
    CHECK(id == 0.0f && iq == 0.0f);
    CHECK(torque_to_current(0.0f, 1000.0f, 700.0f, 480.0f, &m, NULL, p, &id, &iq) == TQ_POSTCOND);
    CHECK(id == 0.0f && iq == 0.0f);
}

void suite_torque(void)
{
    RUN(fw03_envelope_matches_contract_table);
    RUN(mtpa_spm_and_ipm);
    RUN(mtpa_lut_interpolates);
    RUN(field_weakening_holds_voltage_ellipse_and_demag_clamp);
    RUN(current_circle);
    RUN(nan_torque_command_zeroed);
    RUN(derate_hysteresis);
    RUN(peak_budget_30s_and_full_recovery);
    RUN(coolant_above_assumption_removes_peak);
    RUN(bms_limits_and_timeout_zero_regen);
    RUN(witness_motor_map_sweep_never_returns_an_infeasible_pair);
    RUN(field_weakening_never_delivers_more_torque_than_requested);
    RUN(mtpa_is_the_least_current_point_of_the_torque_hyperbola);
    RUN(delivered_torque_falls_with_speed_through_ok_limited_infeasible);
    RUN(torque_sign_and_zero_torque);
    RUN(mtpa_lut_is_refused_where_it_does_not_reproduce_the_torque);
    RUN(non_finite_inputs_and_results_zero_the_outputs);
    RUN(a_vector_failing_its_postcondition_is_never_returned);
}
