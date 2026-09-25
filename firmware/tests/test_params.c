/* test_params.c — the generated parameter sets against the contract tables and formulas. */
#include "test.h"
#include "ti_math.h"
#include "ti_params.h"

TEST(all_skus_validate)
{
    for (int s = TI_SKU_8XX_SIC; s < TI_SKU_COUNT; s++) {
        const ti_params_t *p = ti_params_get((ti_sku_t)s);
        CHECK(p != NULL);
        CHECK(p->sku == (ti_sku_t)s);
        CHECK(ti_params_validate(p) == 0u);
    }
    CHECK(ti_params_get(TI_SKU_NONE) == NULL);
}

TEST(fw05_trip_is_instantaneous_1p25_sqrt2_ipk)
{
    const ti_params_t *s8 = ti_params_get(TI_SKU_8XX_SIC);
    const ti_params_t *i4 = ti_params_get(TI_SKU_4XX_IGBT);
    CHECK_NEAR(s8->i_oc_trip_a, 601.0, 0.5); /* ±601 A 8XX */
    CHECK_NEAR(i4->i_oc_trip_a, 707.1, 0.5); /* ±707 A 4XX */
    CHECK_NEAR(s8->i_crest_a, 480.8, 0.2);   /* rated crest must sit below the trip */
    CHECK(s8->i_crest_a < s8->i_oc_trip_a);
    CHECK_NEAR(s8->i_oc_trip_a, 1.25 * sqrt(2.0) * 340.0, 0.1);
}

TEST(section2_table_values)
{
    const ti_params_t *s8 = ti_params_get(TI_SKU_8XX_SIC);
    const ti_params_t *g8 = ti_params_get(TI_SKU_8XX_IGBT);
    const ti_params_t *g4 = ti_params_get(TI_SKU_4XX_IGBT);
    const ti_params_t *s4 = ti_params_get(TI_SKU_4XX_SIC);
    CHECK(s8->ov_trip_v == 880.0f && g8->ov_trip_v == 880.0f && g4->ov_trip_v == 530.0f && s4->ov_trip_v == 530.0f);
    CHECK(s8->vdc_min_v == 500.0f && s8->vdc_max_v == 850.0f && g4->vdc_min_v == 250.0f && g4->vdc_max_v == 500.0f);
    CHECK(s8->fsw_hz[0] == 10000u && s8->fsw_hz[1] == 8000u && s8->n_fsw == 2u);
    CHECK(g8->fsw_hz[0] == 5000u && g8->n_fsw == 1u && g4->fsw_hz[0] == 5000u);
    CHECK(s8->fc_ceiling_hz[0] == 1200.0f && s8->fc_ceiling_hz[1] == 1100.0f && g8->fc_ceiling_hz[0] == 700.0f);
    CHECK(s8->dead_time_ns == 1000u && g8->dead_time_ns == 2500u && g4->dead_time_ns == 2500u && s4->dead_time_ns == 1000u);
    CHECK(s8->p_peak_w == 220000.0f && s8->p_cont_w == 120000.0f && g4->p_peak_w == 150000.0f && g4->p_cont_w == 90000.0f);
    CHECK(s8->ntc_b_k == 3375.0f && g4->ntc_b_k == 3375.0f);
    CHECK_NEAR(s8->s6_delay_tsw, 0.75, 1e-6);
}

TEST(fw01_hwid_nominal_voltages)
{
    CHECK_NEAR(5.0 * ti_params_get(TI_SKU_4XX_IGBT)->hwid_ratio_nom, 0.90, 0.01);
    CHECK_NEAR(5.0 * ti_params_get(TI_SKU_8XX_IGBT)->hwid_ratio_nom, 1.60, 0.01);
    CHECK_NEAR(5.0 * ti_params_get(TI_SKU_8XX_SIC)->hwid_ratio_nom, 2.50, 0.01);
    CHECK_NEAR(5.0 * ti_params_get(TI_SKU_4XX_SIC)->hwid_ratio_nom, 3.44, 0.01);
}

TEST(section6_link_headroom_matches_contract)
{
    const ti_params_t *s8 = ti_params_get(TI_SKU_8XX_SIC);
    const ti_params_t *g4 = ti_params_get(TI_SKU_4XX_IGBT);
    CHECK_NEAR(s8->c_min_f * 1e6, 291.0, 0.5);
    CHECK_NEAR(g4->c_min_f * 1e6, 723.0, 0.5);
    /* "DC-link energy 32.8 J 8XX / 28.6 J 4XX to U_N" */
    CHECK_NEAR(0.5 * s8->c_min_f * (1000.0 * 1000.0 - 880.0 * 880.0), 32.8, 0.1);
    CHECK_NEAR(0.5 * g4->c_min_f * (600.0 * 600.0 - 530.0 * 530.0), 28.6, 0.1);
    /* FW-16: <= 12 V true => <= 26 mJ 8XX / <= 64 mJ 4XX at C_max */
    CHECK(0.5 * s8->c_max_f * 144.0 <= 0.026);
    CHECK(0.5 * g4->c_max_f * 144.0 <= 0.064);
}

TEST(fw07_fw12_fw11_contract_constants)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    CHECK(p->vofs_min_v == 0.475f && p->vofs_max_v == 0.525f);
    CHECK(p->v5gd_min_v == 4.75f && p->v5gd_max_v == 5.25f);
    CHECK(p->vdc_disagree_frac == 0.05f && p->vdc_bms_frac == 0.03f && p->vdc_failsafe_v == 0.25f);
    CHECK(p->fs26_wd_err_limit == 2u && p->fs26_wdw_period_ms <= 3u);
    CHECK(p->fs26_fs1b_tdelay_ms == 0u && p->fs26_fs1b_tdur_ms == 100u);
    CHECK(p->fs26_backup_fs0b && !p->fs26_backup_fs1b);
    CHECK(p->can_stale_ms == 20u && p->hvil_reaction_ms == 100u);
    CHECK(p->fw15_low_us == 1500u && p->desat_retry_min_ms == 1000u);
    CHECK(p->qdis_on_max_ms == 5000u && p->qdis_max_per_window == 3u && p->qdis_window_ms == 300000u);
    CHECK_NEAR(p->fw06_analog_us + (1e6 / p->fw06_sample_hz) + p->fw06_conv_us + p->fw06_action_us, p->fw06_budget_us, 1e-3);
}

TEST(cal_value_out_of_range_rejected)
{
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    CHECK(ti_params_validate(&p) == 0u);
    p.cal_isum_tol_a = 500.0f; /* outside [15, 90] */
    CHECK(ti_params_validate(&p) >= 1u);
    p = *ti_params_get(TI_SKU_8XX_SIC);
    p.i_oc_trip_a = 400.0f; /* below the crest */
    CHECK(ti_params_validate(&p) >= 1u);
    p = *ti_params_get(TI_SKU_8XX_SIC);
    p.fs26_wd_err_limit = 6u;
    CHECK(ti_params_validate(&p) >= 1u);
}

/* A12-R05: the DESAT hold is a range-checked CAL: default 60 us, never below the 53 us upper bound of
 * the hardware latch path; the round-14 CAL rows carry their defaults. */
TEST(round14_cal_defaults_and_ranges)
{
    for (int k = TI_SKU_8XX_SIC; k < TI_SKU_COUNT; k++) {
        const ti_params_t *q = ti_params_get((ti_sku_t)k);
        CHECK(q->cal_desat_en_hold_us == 60u && q->cal_vdyn_reserve_frac == 0.05f);
        CHECK(q->cal_isns_act_min_a == 20.0f && q->cal_isns_act_frac == 0.2f && q->cal_isns_act_debounce == 20u);
    }
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    p.cal_desat_en_hold_us = 53u; /* at the latch path's own upper bound: no margin */
    CHECK(ti_params_validate(&p) >= 1u);
    p.cal_desat_en_hold_us = 300u; /* beyond the range (the FW-15 1.5 ms low must follow it) */
    CHECK(ti_params_validate(&p) >= 1u);
    p = *ti_params_get(TI_SKU_8XX_SIC);
    p.cal_vdyn_reserve_frac = 0.3f;
    CHECK(ti_params_validate(&p) >= 1u);
}

/* Round 16: the CAL rows' defaults and ranges. The hold (A14-R01) is at least two carrier periods plus
 * jitter (a normal empty read never faults) and at most the observer's re-acquisition gap. */
TEST(round16_cal_defaults_and_ranges)
{
    for (int k = TI_SKU_8XX_SIC; k < TI_SKU_COUNT; k++) {
        const ti_params_t *q = ti_params_get((ti_sku_t)k);
        CHECK(q->cal_rslv_hold_us == 500u && q->cal_rslv_exc_target_vpp == 7.2f && q->cal_swg_code_init == 8u);
        CHECK_NEAR(q->cal_rslv_wind_per_mon, 70.0 / 72.6, 1e-6);
    }
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    p.cal_rslv_hold_us = 200u; /* one missed frame at a 10 kHz current loop would fault */
    CHECK(ti_params_validate(&p) >= 1u);
    p.cal_rslv_hold_us = 900u; /* past the observer's 8-block re-acquisition gap */
    CHECK(ti_params_validate(&p) >= 1u);
    p = *ti_params_get(TI_SKU_8XX_SIC);
    p.cal_swg_code_init = 15u; /* the register maximum: the untrimmed max corner slew-limits */
    CHECK(ti_params_validate(&p) >= 1u);
    /* the hold's derivation: extrapolating e_w t + alpha t^2 / 2 with e_w = alpha / (e wn) reaches the
     * observer's own lag alpha / wn^2 at t = 1.09 / wn, whatever alpha; 500 us stays inside it */
    const double wn = 2.0 * 3.14159265358979 * p.cal_rslv_bw_hz;
    const double a = p.cal_rslv_accel_max_rad_s2;
    const double t = p.cal_rslv_hold_us * 1e-6;
    CHECK_NEAR(1.0934 / wn, 580e-6, 5e-6);
    CHECK((a / (exp(1.0) * wn)) * t + 0.5 * a * t * t < a / (wn * wn));
    CHECK_NEAR(((a / (exp(1.0) * wn)) * t + 0.5 * a * t * t) * 180.0 / 3.14159265358979, 0.255, 0.01); /* deg el */
}

/* A14-N01: the planes and the trim headroom. From the design (13 k / 28 k / 10 k, 1.5 nF, 220 pF MFB;
 * RSX 2.2 ohm, PTC 1.3 ohm cold / 5 ohm post-trip per line, 70 ohm primary): the 7.2 V pp monitor setpoint
 * is 7.64 V pp at the amplifier (1.91 V pk per output, under the 2.07 V pk -40 degC slew ceiling), needs
 * 1.843 V pp from the SWG (2.2 % under the 1.884 V pp low corner: no saturation there), and gives the
 * winding 6.94 V pp cold, 6.3 V pp post-trip (under the 6.5 V pp floor: FW-10 flags it). The validation
 * refuses a set whose setpoint the low corner cannot reach, that would slew-limit, or that starves the
 * cold winding. */
TEST(exciter_planes_and_trim_headroom)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    CHECK_NEAR(p->exc_amp_per_mon, 77.0 / 72.6, 1e-5);
    CHECK_NEAR(p->exc_gain / 2.0, 2.072, 0.001);
    CHECK_NEAR(p->exc_slew_max_vpp, 4.0 * 0.13e6 / (2.0 * 3.14159265358979 * 10e3), 1e-4);
    const double amp = p->cal_rslv_exc_target_vpp * p->exc_amp_per_mon;
    CHECK_NEAR(amp, 7.636, 0.001);
    CHECK_NEAR(amp / 4.0, 1.909, 0.001); /* V pk per output */
    CHECK_NEAR(amp / p->exc_gain, 1.843, 0.001);
    CHECK_NEAR(p->swg_maxapp_min_vpp / (amp / p->exc_gain), 1.022, 0.001); /* headroom at the low corner */
    CHECK_NEAR(p->cal_rslv_exc_target_vpp * p->cal_rslv_wind_per_mon, 6.942, 0.001);
    CHECK_NEAR(p->cal_rslv_exc_target_vpp * 70.0 / 80.0, 6.3, 1e-6);
    CHECK(p->cal_rslv_exc_target_vpp * 70.0 / 80.0 < p->rslv_floor_vpp);
    CHECK(ti_params_validate(p) == 0u);
    ti_params_t q = *p;
    q.cal_rslv_exc_target_vpp = 7.4f; /* 1.894 V pp from the SWG: beyond the low corner */
    CHECK(ti_params_validate(&q) == 1u);
    q.cal_rslv_exc_target_vpp = 8.0f; /* the round-15 figure: beyond the corner AND the slew ceiling */
    CHECK(ti_params_validate(&q) == 2u);
    q.cal_rslv_exc_target_vpp = 6.7f; /* 6.46 V pp at the cold winding */
    CHECK(ti_params_validate(&q) == 1u);
    q = *p;
    q.cal_rslv_wind_per_mon = 0.875f; /* crediting a tripped PTC's 5 ohm */
    CHECK(ti_params_validate(&q) == 1u);
}

void suite_params(void)
{
    RUN(all_skus_validate);
    RUN(fw05_trip_is_instantaneous_1p25_sqrt2_ipk);
    RUN(section2_table_values);
    RUN(fw01_hwid_nominal_voltages);
    RUN(section6_link_headroom_matches_contract);
    RUN(fw07_fw12_fw11_contract_constants);
    RUN(cal_value_out_of_range_rejected);
    RUN(round14_cal_defaults_and_ranges);
    RUN(round16_cal_defaults_and_ranges);
    RUN(exciter_planes_and_trim_headroom);
}
