/* test_torque.c — FW-03 envelope, FW-04 derating, MTPA/FW/demag/circle, NaN, BMS limits. */
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

TEST(mtpa_lut_interpolates)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const motor_t m = motor_screening();
    mtpa_lut_t lut = {.n = 3u, .t_nm = {0.0f, 100.0f, 200.0f}, .id_a = {0.0f, -20.0f, -60.0f}, .iq_a = {0.0f, 110.0f, 210.0f}};
    float id;
    float iq;
    CHECK(torque_to_current(150.0f, 0.0f, 700.0f, 480.0f, &m, &lut, p, &id, &iq));
    CHECK_NEAR(id, -40.0, 1e-3);
    CHECK_NEAR(iq, 160.0, 1e-3);
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
}
