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

void suite_params(void)
{
    RUN(all_skus_validate);
    RUN(fw05_trip_is_instantaneous_1p25_sqrt2_ipk);
    RUN(section2_table_values);
    RUN(fw01_hwid_nominal_voltages);
    RUN(section6_link_headroom_matches_contract);
    RUN(fw07_fw12_fw11_contract_constants);
    RUN(cal_value_out_of_range_rejected);
}
