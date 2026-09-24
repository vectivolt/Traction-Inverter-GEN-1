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
    /* 25 -> 80 degC in 1 ms: rejected; three in a row latch a RATE fault */
    c[TEMP_TMOD_U] = code_v(ntc_mod_v(80.0f));
    temp_update(&t, c, 102u, &mt, p);
    CHECK(!t.ch[TEMP_TMOD_U].valid && (t.ch[TEMP_TMOD_U].fault == TEMP_OK));
    CHECK_NEAR(t.ch[TEMP_TMOD_U].t_c, 25.0, 0.5); /* held */
    temp_update(&t, c, 103u, &mt, p);
    temp_update(&t, c, 104u, &mt, p);
    CHECK(t.ch[TEMP_TMOD_U].fault == TEMP_RATE);
    c[TEMP_TMOD_U] = code_v(2.5f);
    temp_update(&t, c, 2000u, &mt, p);
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

void suite_temp(void)
{
    RUN(module_ntc_curve_with_series_resistor);
    RUN(pt1000_and_board_ntc);
    RUN(open_short_rate_plausibility);
    RUN(slow_heating_accepted);
}
