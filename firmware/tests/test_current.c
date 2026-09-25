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

/* F24: a channel stuck at its zero-current level. KCL: at zero current there is nothing to see; below
 * cal_isum_tol_a the sum stays in tolerance; with all three stuck the sum is 0. The activity check
 * catches it where the reference asks that phase for current, and never flags a phase the reference
 * leaves near zero (standstill at an angle where one phase carries nothing). */
TEST(activity_catches_a_stuck_channel_where_current_is_asked)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const isns_cal_t cal[3] = {CAL_NOM, CAL_NOM, CAL_NOM};
    isns_t s;
    isns_init(&s);
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    for (unsigned k = 0u; k < 200u; k++) { /* zero command, W stuck at 0 A: consistent, no verdict */
        set3(&s, 0.0f, 0.0f, 0.0f, cal, p);
        isns_activity(&s, zero, p);
    }
    CHECK(s.valid && !s.stuck_fault);
    const float ref[3] = {20.0f, 20.0f, -40.0f}; /* 40 A asked of W: inside the 45 A KCL tolerance */
    for (unsigned k = 0u; k < (unsigned)p->cal_isns_act_debounce - 1u; k++) {
        set3(&s, 20.0f, 20.0f, 0.0f, cal, p); /* W stuck: the sum reads 40 A < 45 A */
        isns_activity(&s, ref, p);
    }
    CHECK(!s.sum_fault && !s.stuck_fault && s.valid); /* not yet: debounced */
    set3(&s, 20.0f, 20.0f, 0.0f, cal, p);
    isns_activity(&s, ref, p);
    CHECK(s.stuck_fault && !s.valid && !s.sum_fault);
    set3(&s, 20.0f, 20.0f, -40.0f, cal, p); /* latched */
    CHECK(!s.valid);
    isns_init(&s); /* a phase with a small reference proves nothing either way */
    const float edge[3] = {0.0f, 45.0f, -45.0f};
    for (unsigned k = 0u; k < 200u; k++) {
        set3(&s, 0.0f, 45.0f, -45.0f, cal, p);
        isns_activity(&s, edge, p);
    }
    CHECK(s.valid && !s.stuck_fault);
    for (unsigned k = 0u; k < 200u; k++) { /* all three stuck: the sum is 0, activity is not */
        set3(&s, 0.0f, 0.0f, 0.0f, cal, p);
        isns_activity(&s, edge, p);
    }
    CHECK(s.stuck_fault && !s.sum_fault);
}

/* A14-R03: a sample whose triplet did not arrive complete is lost — invalid, not fresh, no channel valid
 * (so neither the OC backstop nor the activity check reads the previous values) — and it is not taken
 * for an open wire; the time stamp stays the last complete triplet's; the next complete one recovers. */
TEST(lost_sample_is_invalid_and_keeps_its_last_stamp)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const isns_cal_t cal[3] = {CAL_NOM, CAL_NOM, CAL_NOM};
    isns_t s;
    isns_init(&s);
    const uint16_t hi[3] = {code_of_v(2.5f + 2.22e-3f * 650.0f), code_of_v(2.5f), code_of_v(2.5f)};
    isns_update(&s, hi, 5000u, 5000u, cal, p);
    CHECK(isns_oc(&s, p)); /* 650 A in this sample: the software backstop sees it */
    isns_lost(&s);
    CHECK(!isns_oc(&s, p)); /* the next sample is lost: the old 650 A is not evaluated again as if new */
    const uint16_t c[3] = {code_of_v(2.5f + 2.22e-3f * 100.0f), code_of_v(2.5f - 2.22e-3f * 50.0f),
                           code_of_v(2.5f - 2.22e-3f * 50.0f)};
    isns_init(&s);
    isns_update(&s, c, 5000u, 5000u, cal, p);
    CHECK(s.valid && s.fresh);
    isns_lost(&s);
    CHECK(!s.valid && !s.fresh && !s.ch_valid[0] && !s.ch_valid[1] && !s.ch_valid[2]);
    CHECK(!s.open_wire[0] && !s.open_wire[1] && !s.open_wire[2] && s.t_us == 5000u);
    isns_update(&s, c, 5050u, 5050u, cal, p);
    CHECK(s.valid && s.fresh && s.t_us == 5050u);
}

/* Round 18 (A16-R01): the target stamps the triplet when it reads it — after the ISR read its entry time. A
 * triplet whose stamp is 1, 5 or 50 us AFTER the check time is fresh (it was 2^32 us old, the currents
 * invalid, the control lost); one cal_isns_stale_us before it is still stale, also across the 32-bit wrap. */
TEST(a_triplet_stamped_after_the_check_time_is_fresh)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const isns_cal_t cal[3] = {CAL_NOM, CAL_NOM, CAL_NOM};
    const uint16_t c[3] = {code_of_v(2.5f + 2.22e-3f * 100.0f), code_of_v(2.5f - 2.22e-3f * 50.0f),
                           code_of_v(2.5f - 2.22e-3f * 50.0f)};
    const uint32_t nows[2] = {5000u, 0xFFFFFFFEu};
    const uint32_t ahead[3] = {1u, 5u, 50u};
    for (unsigned n = 0u; n < 2u; n++) {
        for (unsigned k = 0u; k < 3u; k++) {
            isns_t s;
            isns_init(&s);
            isns_update(&s, c, nows[n] + ahead[k], nows[n], cal, p);
            CHECK(s.fresh && s.valid);
        }
        isns_t s;
        isns_init(&s);
        isns_update(&s, c, nows[n] - p->cal_isns_stale_us, nows[n], cal, p);
        CHECK(!s.fresh && !s.valid);
    }
}

void suite_current(void)
{
    RUN(lost_sample_is_invalid_and_keeps_its_last_stamp);
    RUN(conversion_and_validity);
    RUN(open_hall_wire_reads_0v_and_invalidates);
    RUN(sum_plausibility_debounced_then_latched);
    RUN(stale_sample_invalid);
    RUN(overcurrent_at_the_crest_8xx_both_polarities);
    RUN(overcurrent_4xx_threshold);
    RUN(offset_self_test);
    RUN(activity_catches_a_stuck_channel_where_current_is_asked);
    RUN(a_triplet_stamped_after_the_check_time_is_fresh);
}
