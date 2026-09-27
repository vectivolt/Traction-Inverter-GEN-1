/* test_temp.c — FW-13 conversions, open/short/rate, module max. */
#include "sim.h"
#include "temp.h"
#include "test.h"

static uint16_t code_v(float v) { return (uint16_t)(v * 4095.0f / 5.0f + 0.5f); }

static float ntc_mod_v(float t)
{
    const float r = 5000.0f * expf(3375.0f * (1.0f / (t + 273.15f) - 1.0f / 298.15f)) + 100.0f;
    return 5.0f * r / (r + 5100.0f);
}

TEST(module_ntc_curve_with_series_resistor)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    CHECK_NEAR(temp_ntc_c(ntc_mod_v(25.0f), 5100.0f, 100.0f, 5000.0f, 3375.0f), 25.0, 0.2);
    CHECK_NEAR(temp_ntc_c(ntc_mod_v(100.0f), 5100.0f, 100.0f, 5000.0f, 3375.0f), 100.0, 0.5);
    CHECK_NEAR(temp_ntc_c(ntc_mod_v(125.0f), p->ntc_pullup_ohm, p->ntc_series_ohm, p->ntc_r25_ohm, p->ntc_b_k), 125.0, 1.0);
    /* ignoring the 100 R would read ~12 K cold at 125 degC: the series term matters */
    CHECK(temp_ntc_c(ntc_mod_v(125.0f), 5100.0f, 0.0f, 5000.0f, 3375.0f) < 120.0f);
}

TEST(pt1000_and_board_ntc)
{
    const float r = 1000.0f * (1.0f + 3.9083e-3f * 80.0f - 5.775e-7f * 6400.0f);
    CHECK_NEAR(temp_pt1000_c(5.0f * r / (r + 10000.0f), 10000.0f), 80.0, 0.3);
    CHECK_NEAR(temp_ntc_c(2.5f, 10000.0f, 0.0f, 10000.0f, 3435.0f), 25.0, 0.1);
}

TEST(open_short_rate_plausibility)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const temp_mt_cal_t mt = {.type = TEMP_MT_PT1000};
    temp_t t;
    temp_init(&t);
    uint16_t c[TEMP_COUNT];
    for (unsigned i = 0u; i < (unsigned)TEMP_COUNT; i++) {
        c[i] = code_v(2.5f);
    }
    c[TEMP_MT1] = code_v(0.4946f);
    c[TEMP_MT2] = code_v(0.4946f);
    temp_update(&t, c, 100u, &mt, p);
    CHECK(t.ch[TEMP_TMOD_U].valid && t.ch[TEMP_MT1].valid);
    CHECK_NEAR(t.ch[TEMP_MT1].t_c, 25.0, 1.0);
    c[TEMP_TMOD_V] = code_v(4.99f); /* open */
    c[TEMP_TMOD_W] = code_v(0.0f);  /* short */
    temp_update(&t, c, 101u, &mt, p);
    CHECK(!t.ch[TEMP_TMOD_V].valid && (t.ch[TEMP_TMOD_V].fault == TEMP_OPEN));
    CHECK(!t.ch[TEMP_TMOD_W].valid && (t.ch[TEMP_TMOD_W].fault == TEMP_SHORT));
    bool any;
    bool all;
    const float m = temp_module_max(&t, &any, &all);
    CHECK(any && !all);
    CHECK_NEAR(m, 25.0, 0.5);
    /* 25 -> 80 degC at once: the next window's mean is rejected, the value held; three windows in a row latch a RATE
     * fault (round 23: the rate is judged per cal_temp_rate_win_ms window, no longer per 1 ms sample) */
    c[TEMP_TMOD_U] = code_v(ntc_mod_v(80.0f));
    const uint32_t win = p->cal_temp_rate_win_ms;
    uint32_t ms = 102u;
    for (; ms < (102u + win); ms++) {
        temp_update(&t, c, ms, &mt, p);
    }
    CHECK(!t.ch[TEMP_TMOD_U].valid && (t.ch[TEMP_TMOD_U].fault == TEMP_OK));
    CHECK_NEAR(t.ch[TEMP_TMOD_U].t_c, 25.0, 0.5); /* held */
    for (; ms < (102u + (3u * win)); ms++) {
        temp_update(&t, c, ms, &mt, p);
    }
    CHECK(t.ch[TEMP_TMOD_U].fault == TEMP_RATE);
    c[TEMP_TMOD_U] = code_v(2.5f);
    temp_update(&t, c, 5000u, &mt, p);
    CHECK(!t.ch[TEMP_TMOD_U].valid); /* latched for the key cycle */
}

TEST(slow_heating_accepted)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const temp_mt_cal_t mt = {.type = TEMP_MT_PT1000};
    temp_t t;
    temp_init(&t);
    uint16_t c[TEMP_COUNT];
    for (unsigned k = 0u; k <= 60u; k++) { /* 25 -> 85 degC over 6 s: 10 K/s */
        for (unsigned i = 0u; i < (unsigned)TEMP_COUNT; i++) {
            c[i] = code_v(ntc_mod_v(25.0f + (float)k));
        }
        temp_update(&t, c, 100u * k, &mt, p);
    }
    CHECK(t.ch[TEMP_TMOD_U].valid && (t.ch[TEMP_TMOD_U].fault == TEMP_OK));
    CHECK_NEAR(t.ch[TEMP_TMOD_U].t_c, 85.0, 0.5);
}

/* ---------------- round 23 (item 1): the rate check over a window with a deadband ---------------- */

/* All seven channels at the module NTC's code for t_c plus `dither` codes (the harness's module curve on every
 * channel: the rate logic is the same for each). */
static void codes_at(uint16_t c[TEMP_COUNT], float t_c, int dither)
{
    const int k = (int)code_v(ntc_mod_v(t_c)) + dither;
    for (unsigned i = 0u; i < (unsigned)TEMP_COUNT; i++) {
        c[i] = (uint16_t)k;
    }
}

/* Every 1 ms sample, as the task runs it: one ADC code (0.04 degC at 50 degC, 0.13 at 100, 0.5 at 125) between two
 * samples read 40-500 degC/s against cal_temp_rate_c_s over 1 ms. A code toggling every sample for 5 s at 50, 100 and
 * 125 degC, and a real 5 degC/s rise from 70 to 110 degC (one code every 20-60 ms: three rejections in a row latched
 * TEMP_RATE above ~70 degC), must keep every channel valid, unlatched and within a code of the truth. */
TEST(one_code_steps_and_a_slow_rise_never_trip_the_rate_check)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const temp_mt_cal_t mt = {.type = TEMP_MT_PT1000};
    const float at[3] = {50.0f, 100.0f, 125.0f};
    for (unsigned a = 0u; a < 3u; a++) {
        temp_t t;
        temp_init(&t);
        uint16_t c[TEMP_COUNT];
        bool always_valid = true;
        for (uint32_t ms = 0u; ms < 5000u; ms++) {
            codes_at(c, at[a], ((ms & 1u) != 0u) ? 1 : 0);
            temp_update(&t, c, 1000u + ms, &mt, p);
            always_valid = always_valid && t.ch[TEMP_TMOD_U].valid;
        }
        CHECK(always_valid && t.ch[TEMP_TMOD_U].fault == TEMP_OK);
        CHECK_NEAR(t.ch[TEMP_TMOD_U].t_c, at[a], 0.6);
    }
    temp_t t;
    temp_init(&t);
    uint16_t c[TEMP_COUNT];
    bool always_valid = true;
    for (uint32_t ms = 0u; ms <= 8000u; ms++) {
        codes_at(c, 70.0f + (5.0f * (float)ms * 1.0e-3f), 0);
        temp_update(&t, c, 1000u + ms, &mt, p);
        always_valid = always_valid && t.ch[TEMP_TMOD_U].valid;
    }
    CHECK(always_valid && t.ch[TEMP_TMOD_U].fault == TEMP_OK);
    CHECK_NEAR(t.ch[TEMP_TMOD_U].t_c, 110.0, 1.5); /* the window's mean lags the ramp by half a window */
}

/* A real rise beyond the plausibility bound still latches: 40 degC/s (60 -> 100 degC in 1 s) within four windows (the
 * first may hold only the ramp's start), for the key cycle; a persistent step (60 -> 90 degC) within three; a single
 * wild sample among 200 does not (the window mean moves 0.15 degC); an open and a short trip at the very sample. */
TEST(a_fast_ramp_a_step_open_and_short_still_trip)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const temp_mt_cal_t mt = {.type = TEMP_MT_PT1000};
    const uint32_t win = p->cal_temp_rate_win_ms;
    temp_t t;
    temp_init(&t);
    uint16_t c[TEMP_COUNT];
    uint32_t ms = 0u;
    for (; ms < 1000u; ms++) {
        codes_at(c, 60.0f, 0);
        temp_update(&t, c, 1000u + ms, &mt, p);
    }
    uint32_t latched_at = 0u;
    for (uint32_t k = 0u; (k <= 1000u) && (latched_at == 0u); k++, ms++) {
        codes_at(c, 60.0f + (40.0f * (float)k * 1.0e-3f), 0);
        temp_update(&t, c, 1000u + ms, &mt, p);
        latched_at = (t.ch[TEMP_TMOD_U].fault == TEMP_RATE) ? k : 0u;
    }
    CHECK(latched_at > 0u && latched_at <= ((4u * win) + 1u) && !t.ch[TEMP_TMOD_U].valid);
    codes_at(c, 60.0f, 0);
    temp_update(&t, c, 1000u + ms + (10u * win), &mt, p);
    CHECK(!t.ch[TEMP_TMOD_U].valid && t.ch[TEMP_TMOD_U].fault == TEMP_RATE); /* latched for the key cycle */
    /* a persistent step */
    temp_init(&t);
    for (ms = 0u; ms < 1000u; ms++) {
        codes_at(c, 60.0f, 0);
        temp_update(&t, c, 1000u + ms, &mt, p);
    }
    latched_at = 0u;
    for (uint32_t k = 0u; (k <= (4u * win)) && (latched_at == 0u); k++, ms++) {
        codes_at(c, 90.0f, 0);
        temp_update(&t, c, 1000u + ms, &mt, p);
        latched_at = (t.ch[TEMP_TMOD_U].fault == TEMP_RATE) ? k : 0u;
    }
    CHECK(latched_at > 0u && latched_at <= ((3u * win) + 1u));
    /* one wild sample */
    temp_init(&t);
    bool always_valid = true;
    for (ms = 0u; ms < 3000u; ms++) {
        codes_at(c, 60.0f, 0);
        if (ms == 1500u) {
            codes_at(c, 90.0f, 0);
        }
        temp_update(&t, c, 1000u + ms, &mt, p);
        always_valid = always_valid && t.ch[TEMP_TMOD_U].valid;
    }
    CHECK(always_valid && t.ch[TEMP_TMOD_U].fault == TEMP_OK);
    /* open and short: at once */
    c[TEMP_TMOD_V] = code_v(4.99f);
    c[TEMP_TMOD_W] = code_v(0.0f);
    temp_update(&t, c, 1000u + ms, &mt, p);
    CHECK(!t.ch[TEMP_TMOD_V].valid && t.ch[TEMP_TMOD_V].fault == TEMP_OPEN);
    CHECK(!t.ch[TEMP_TMOD_W].valid && t.ch[TEMP_TMOD_W].fault == TEMP_SHORT);
}

/* The deadband: at the shortest window (100 ms) a slow swing of the reading by 4 codes (a +/-2-code square wave, one
 * level per window — interference the window's averaging cannot remove) reads 30 degC/s on a PT1000 at 80 degC
 * (0.76 degC per code); inside cal_temp_rate_db_codes it never counts. */
TEST(the_deadband_holds_a_slow_swing_of_a_few_codes)
{
    ti_params_t q = *ti_params_get(TI_SKU_8XX_SIC);
    q.cal_temp_rate_win_ms = 100u;
    const temp_mt_cal_t mt = {.type = TEMP_MT_PT1000};
    const float r = 1000.0f * (1.0f + (3.9083e-3f * 80.0f) - (5.775e-7f * 6400.0f));
    const uint16_t base = code_v(5.0f * r / (r + 10000.0f));
    temp_t t;
    temp_init(&t);
    uint16_t c[TEMP_COUNT];
    bool always_valid = true;
    for (uint32_t ms = 0u; ms < 3000u; ms++) {
        for (unsigned i = 0u; i < (unsigned)TEMP_COUNT; i++) {
            c[i] = (uint16_t)(base + ((((ms / 100u) & 1u) != 0u) ? 2 : -2));
        }
        temp_update(&t, c, 1000u + ms, &mt, &q);
        always_valid = always_valid && t.ch[TEMP_MT1].valid;
    }
    CHECK(always_valid && t.ch[TEMP_MT1].fault == TEMP_OK);
    CHECK_NEAR(t.ch[TEMP_MT1].t_c, 80.0, 2.0);
}

void suite_temp(void)
{
    RUN(module_ntc_curve_with_series_resistor);
    RUN(pt1000_and_board_ntc);
    RUN(open_short_rate_plausibility);
    RUN(slow_heating_accepted);
    RUN(one_code_steps_and_a_slow_rise_never_trip_the_rate_check);
    RUN(a_fast_ramp_a_step_open_and_short_still_trip);
    RUN(the_deadband_holds_a_slow_swing_of_a_few_codes);
}
