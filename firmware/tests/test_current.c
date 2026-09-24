/* test_current.c — FW-05: validity window, open wire, sum plausibility, OC at the crest. */
#include "current.h"
#include "test.h"

static const isns_cal_t CAL_NOM = {.offset_v = 2.5f, .gain_v_per_a = 2.22e-3f, .sign = 1};

static uint16_t code_of_v(float v) { return (uint16_t)(v * 4095.0f / 5.0f + 0.5f); }

static void set3(isns_t *s, float ia, float ib, float ic, const isns_cal_t cal[3], const ti_params_t *p)
{
    const float i[3] = {ia, ib, ic};
    uint16_t c[3];
    for (unsigned k = 0u; k < 3u; k++) {
        c[k] = code_of_v(cal[k].offset_v + (float)cal[k].sign * cal[k].gain_v_per_a * i[k]);
    }
    isns_update(s, c, 1000u, 1000u, cal, p);
}

TEST(conversion_and_validity)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const isns_cal_t cal[3] = {CAL_NOM, CAL_NOM, CAL_NOM};
    isns_t s;
    isns_init(&s);
    set3(&s, 300.0f, -150.0f, -150.0f, cal, p);
    CHECK(s.valid);
    CHECK_NEAR(s.i_a[0], 300.0, 1.0);
    CHECK_NEAR(s.i_a[1], -150.0, 1.0);
}

TEST(open_hall_wire_reads_0v_and_invalidates)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const isns_cal_t cal[3] = {CAL_NOM, CAL_NOM, CAL_NOM};
    isns_t s;
    isns_init(&s);
    const uint16_t c[3] = {0u, code_of_v(2.5f), code_of_v(2.5f)}; /* 100 k pull-down: 0 V */
    isns_update(&s, c, 1000u, 1000u, cal, p);
    CHECK(!s.ch_valid[0] && s.open_wire[0]);
    CHECK(!s.valid);
    CHECK(s.i_a[0] == 0.0f);
    const uint16_t hi[3] = {code_of_v(4.85f), code_of_v(2.5f), code_of_v(2.5f)}; /* above 4.8 V */
    isns_update(&s, hi, 1000u, 1000u, cal, p);
    CHECK(!s.ch_valid[0] && !s.open_wire[0] && !s.valid);
    const uint16_t edge[3] = {code_of_v(0.21f), code_of_v(4.79f), code_of_v(2.5f)};
    isns_update(&s, edge, 1000u, 1000u, cal, p);
    CHECK(s.ch_valid[0] && s.ch_valid[1]);
}

TEST(sum_plausibility_debounced_then_latched)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const isns_cal_t cal[3] = {CAL_NOM, CAL_NOM, CAL_NOM};
    isns_t s;
    isns_init(&s);
    set3(&s, 100.0f, -50.0f, -10.0f, cal, p); /* sum 40 A < 45 A */
    CHECK(s.valid && !s.sum_fault);
    for (unsigned k = 0u; k < (unsigned)p->cal_isum_debounce - 1u; k++) {
        set3(&s, 100.0f, -20.0f, -10.0f, cal, p); /* sum 70 A */
        CHECK(!s.sum_fault);
    }
    set3(&s, 100.0f, -20.0f, -10.0f, cal, p);
    CHECK(s.sum_fault && !s.valid);
    set3(&s, 100.0f, -50.0f, -50.0f, cal, p); /* back to plausible: stays latched */
    CHECK(s.sum_fault && !s.valid);
}

TEST(stale_sample_invalid)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const isns_cal_t cal[3] = {CAL_NOM, CAL_NOM, CAL_NOM};
    isns_t s;
    isns_init(&s);
    const uint16_t c[3] = {code_of_v(2.5f), code_of_v(2.5f), code_of_v(2.5f)};
    isns_update(&s, c, 1000u, 1000u + p->cal_isns_stale_us - 1u, cal, p);
    CHECK(s.valid);
    isns_update(&s, c, 1000u, 1000u + p->cal_isns_stale_us, cal, p);
    CHECK(!s.valid && !s.fresh);
}

static bool hw_trips(const isns_cal_t *c, float i, float trip)
{
    uint16_t lo;
    uint16_t hi;
    isns_oc_codes(c, trip, &lo, &hi);
    const uint16_t code = code_of_v(c->offset_v + (float)c->sign * c->gain_v_per_a * i);
    return (code >= hi) || (code <= lo);
}

TEST(overcurrent_at_the_crest_8xx_both_polarities)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const isns_cal_t cal[3] = {CAL_NOM, CAL_NOM, CAL_NOM};
    isns_t s;
    isns_init(&s);
    set3(&s, 481.0f, -240.5f, -240.5f, cal, p); /* rated crest must NOT trip at 601 A */
    CHECK(!isns_oc(&s, p));
    set3(&s, -481.0f, 240.5f, 240.5f, cal, p);
    CHECK(!isns_oc(&s, p));
    set3(&s, 620.0f, -310.0f, -310.0f, cal, p);
    CHECK(isns_oc(&s, p));
    set3(&s, -620.0f, 310.0f, 310.0f, cal, p);
    CHECK(isns_oc(&s, p));
    /* hardware codes, including a calibrated offset and a reversed sensor */
    const isns_cal_t odd = {.offset_v = 2.46f, .gain_v_per_a = 2.30e-3f, .sign = -1};
    CHECK(!hw_trips(&CAL_NOM, 481.0f, p->i_oc_trip_a) && !hw_trips(&CAL_NOM, -481.0f, p->i_oc_trip_a));
    CHECK(hw_trips(&CAL_NOM, 620.0f, p->i_oc_trip_a) && hw_trips(&CAL_NOM, -620.0f, p->i_oc_trip_a));
    CHECK(!hw_trips(&odd, 481.0f, p->i_oc_trip_a) && !hw_trips(&odd, -481.0f, p->i_oc_trip_a));
    CHECK(hw_trips(&odd, 620.0f, p->i_oc_trip_a) && hw_trips(&odd, -620.0f, p->i_oc_trip_a));
    CHECK(!hw_trips(&CAL_NOM, 598.0f, p->i_oc_trip_a) && hw_trips(&CAL_NOM, 603.0f, p->i_oc_trip_a));
}

TEST(overcurrent_4xx_threshold)
{
    const ti_params_t *p = ti_params_get(TI_SKU_4XX_IGBT);
    CHECK(!hw_trips(&CAL_NOM, 566.0f, p->i_oc_trip_a)); /* 4XX crest */
    CHECK(hw_trips(&CAL_NOM, 720.0f, p->i_oc_trip_a));
    CHECK(hw_trips(&CAL_NOM, -720.0f, p->i_oc_trip_a));
}

TEST(offset_self_test)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const isns_cal_t cal[3] = {CAL_NOM, CAL_NOM, CAL_NOM};
    const float ok[3] = {2.52f, 2.48f, 2.5f};
    const float bad[3] = {2.5f, 2.75f, 2.5f};
    CHECK(isns_offset_ok(ok, cal, p));
    CHECK(!isns_offset_ok(bad, cal, p));
}

void suite_current(void)
{
    RUN(conversion_and_validity);
    RUN(open_hall_wire_reads_0v_and_invalidates);
    RUN(sum_plausibility_debounced_then_latched);
    RUN(stale_sample_invalid);
    RUN(overcurrent_at_the_crest_8xx_both_polarities);
    RUN(overcurrent_4xx_threshold);
    RUN(offset_self_test);
}
