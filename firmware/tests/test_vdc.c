/* test_vdc.c — FW-07 plausibility and FW-18 HV state. */
#include "test.h"
#include "vdc.h"

static const vdc_cal_t CAL[2] = {{.gain = 455.839f, .offset_v = 0.0f}, {.gain = 455.839f, .offset_v = 0.0f}};

static uint16_t code_v(float v) { return (uint16_t)(v * 4095.0f / 5.0f + 0.5f); }

static void upd(vdc_t *s, float v1, float v2, float vofs, float v5gd, const ti_params_t *p)
{
    const uint16_t c[2] = {code_v(0.5f + v1 / 455.839f), code_v(0.5f + v2 / 455.839f)};
    const uint32_t t[2] = {5000u, 5000u};
    vdc_update(s, c, t, code_v(vofs), code_v(0.5f * v5gd), 5000u, CAL, p);
}

TEST(nominal_pair_valid)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    vdc_t s;
    vdc_init(&s);
    upd(&s, 800.0f, 800.0f, 0.5f, 5.0f, p);
    CHECK(s.valid && s.vofs_ok && s.v5gd_ok);
    CHECK_NEAR(s.vdc, 800.0, 3.0);
    CHECK(s.hv == TI_HV_PRESENT);
    upd(&s, 20.0f, 20.0f, 0.5f, 5.0f, p);
    CHECK(s.valid && (s.hv == TI_HV_SAFE));
}

TEST(disagreement_over_5_percent_invalidates_both)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    vdc_t s;
    vdc_init(&s);
    upd(&s, 800.0f, 750.0f, 0.5f, 5.0f, p); /* 6.25 % */
    CHECK(s.disagree && !s.valid && (s.hv == TI_HV_UNKNOWN));
    upd(&s, 800.0f, 770.0f, 0.5f, 5.0f, p); /* 3.75 % */
    CHECK(!s.disagree && s.valid);
    upd(&s, 5.0f, 15.0f, 0.5f, 5.0f, p); /* low voltage: floor, not 5 % of 15 V */
    CHECK(!s.disagree);
    upd(&s, 5.0f, 40.0f, 0.5f, 5.0f, p);
    CHECK(s.disagree);
}

TEST(vofs_out_of_window_invalidates_both)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    vdc_t s;
    vdc_init(&s);
    upd(&s, 800.0f, 800.0f, 0.46f, 5.0f, p);
    CHECK(!s.vofs_ok && !s.ch_valid[0] && !s.ch_valid[1] && !s.valid && (s.hv == TI_HV_UNKNOWN));
    upd(&s, 800.0f, 800.0f, 0.54f, 5.0f, p);
    CHECK(!s.vofs_ok && !s.valid);
    upd(&s, 800.0f, 800.0f, 0.49f, 5.0f, p);
    CHECK(s.vofs_ok && s.valid);
}

TEST(v5gd_out_of_window_invalidates_both)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    vdc_t s;
    vdc_init(&s);
    upd(&s, 0.0f, 0.0f, 0.5f, 4.6f, p); /* the AMC LV sides unpowered: a false "0 V bus" */
    CHECK(!s.v5gd_ok && !s.valid && (s.hv == TI_HV_UNKNOWN));
    upd(&s, 0.0f, 0.0f, 0.5f, 5.4f, p);
    CHECK(!s.v5gd_ok && !s.valid);
    upd(&s, 0.0f, 0.0f, 0.5f, 4.8f, p);
    CHECK(s.v5gd_ok && s.valid && (s.hv == TI_HV_SAFE));
}

TEST(failsafe_below_0p25v_is_not_a_dead_bus)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    vdc_t s;
    vdc_init(&s);
    const uint16_t c[2] = {code_v(0.1f), code_v(0.5f)};
    const uint32_t t[2] = {5000u, 5000u};
    vdc_update(&s, c, t, code_v(0.5f), code_v(2.5f), 5000u, CAL, p);
    CHECK(s.ch_failsafe[0] && !s.ch_valid[0] && s.ch_valid[1]);
    CHECK(!s.valid && (s.hv == TI_HV_UNKNOWN)); /* never SAFE on a failed witness */
}

TEST(stale_channel_invalid)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    vdc_t s;
    vdc_init(&s);
    const uint16_t c[2] = {code_v(1.5f), code_v(1.5f)};
    const uint32_t t[2] = {5000u, 5000u - p->cal_vdc_stale_us};
    vdc_update(&s, c, t, code_v(0.5f), code_v(2.5f), 5000u, CAL, p);
    CHECK(s.ch_stale[1] && !s.valid);
}

TEST(bms_cross_check_3_percent_with_contactors_closed)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    vdc_t s;
    vdc_init(&s);
    upd(&s, 760.0f, 760.0f, 0.5f, 5.0f, p); /* pack 800 V: 5 % low */
    vdc_bms_check(&s, true, true, 800.0f, 100u, p);
    CHECK(!s.bms_mismatch); /* debounce */
    vdc_bms_check(&s, true, true, 800.0f, 100u + p->cal_vdc_bms_debounce_ms, p);
    CHECK(s.bms_mismatch);
    vdc_bms_check(&s, false, true, 800.0f, 300u, p); /* contactors open: not applicable */
    CHECK(!s.bms_mismatch);
    upd(&s, 790.0f, 790.0f, 0.5f, 5.0f, p);
    vdc_bms_check(&s, true, true, 800.0f, 400u, p);
    vdc_bms_check(&s, true, true, 800.0f, 1400u, p);
    CHECK(!s.bms_mismatch);
}

TEST(ov_compare_code)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const uint16_t hi = vdc_ov_code(&CAL[0], p);
    CHECK(code_v(0.5f + 876.0f / 455.839f) < hi);
    CHECK(code_v(0.5f + 882.0f / 455.839f) >= hi);
    const ti_params_t *q = ti_params_get(TI_SKU_4XX_IGBT);
    const uint16_t h4 = vdc_ov_code(&CAL[0], q);
    CHECK(code_v(0.5f + 526.0f / 455.839f) < h4 && code_v(0.5f + 533.0f / 455.839f) >= h4);
}

/* Round 18 (A16-R01): the target stamps a V_DC conversion when it reads it, after the ISR read its entry time.
 * Channels stamped up to 50 us after the check time are fresh (they read 2^32 us old: V_DC invalid, HV
 * unknown); one cal_vdc_stale_us before it is stale — also across the 32-bit wrap. */
TEST(a_channel_stamped_after_the_check_time_is_fresh)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const uint16_t c[2] = {code_v(0.5f + 800.0f / 455.839f), code_v(0.5f + 800.0f / 455.839f)};
    const uint32_t nows[2] = {5000u, 0xFFFFFFFCu};
    const uint32_t ahead[3] = {1u, 5u, 50u};
    for (unsigned n = 0u; n < 2u; n++) {
        for (unsigned k = 0u; k < 3u; k++) {
            vdc_t s;
            vdc_init(&s);
            const uint32_t t[2] = {nows[n] + ahead[k], nows[n] + (ahead[k] / 2u)};
            vdc_update(&s, c, t, code_v(0.5f), code_v(2.5f), nows[n], CAL, p);
            CHECK(!s.ch_stale[0] && !s.ch_stale[1] && s.valid && (s.hv == TI_HV_PRESENT));
        }
        vdc_t s;
        vdc_init(&s);
        const uint32_t t[2] = {nows[n], nows[n] - p->cal_vdc_stale_us};
        vdc_update(&s, c, t, code_v(0.5f), code_v(2.5f), nows[n], CAL, p);
        CHECK(!s.ch_stale[0] && s.ch_stale[1] && !s.valid);
    }
}

void suite_vdc(void)
{
    RUN(nominal_pair_valid);
    RUN(disagreement_over_5_percent_invalidates_both);
    RUN(vofs_out_of_window_invalidates_both);
    RUN(v5gd_out_of_window_invalidates_both);
    RUN(failsafe_below_0p25v_is_not_a_dead_bus);
    RUN(stale_channel_invalid);
    RUN(bms_cross_check_3_percent_with_contactors_closed);
    RUN(ov_compare_code);
    RUN(a_channel_stamped_after_the_check_time_is_fresh);
}
