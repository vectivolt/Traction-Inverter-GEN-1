/* test_temp.c — FW-13 conversions, open/short/rate, module max. */
#include "sim.h"
#include "temp.h"
#include "test.h"

static uint16_t code_v(float v) { return (uint16_t)(v * 4095.0f / 5.0f + 0.5f); }

/* Round 24 (F241): the tests before it feed a new conversion every call */
static const ti_acq_t ALL_NEW[TEMP_COUNT] = {TI_ACQ_NEW, TI_ACQ_NEW, TI_ACQ_NEW, TI_ACQ_NEW,
                                             TI_ACQ_NEW, TI_ACQ_NEW, TI_ACQ_NEW};

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
    temp_update(&t, c, ALL_NEW, 100u, &mt, p);
    CHECK(t.ch[TEMP_TMOD_U].valid && t.ch[TEMP_MT1].valid);
    CHECK_NEAR(t.ch[TEMP_MT1].t_c, 25.0, 1.0);
    c[TEMP_TMOD_V] = code_v(4.99f); /* open */
    c[TEMP_TMOD_W] = code_v(0.0f);  /* short */
    temp_update(&t, c, ALL_NEW, 101u, &mt, p);
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
        temp_update(&t, c, ALL_NEW, ms, &mt, p);
    }
    CHECK(!t.ch[TEMP_TMOD_U].valid && (t.ch[TEMP_TMOD_U].fault == TEMP_OK));
    CHECK_NEAR(t.ch[TEMP_TMOD_U].t_c, 25.0, 0.5); /* held */
    for (; ms < (102u + (3u * win)); ms++) {
        temp_update(&t, c, ALL_NEW, ms, &mt, p);
    }
    CHECK(t.ch[TEMP_TMOD_U].fault == TEMP_RATE);
    c[TEMP_TMOD_U] = code_v(2.5f);
    temp_update(&t, c, ALL_NEW, 5000u, &mt, p);
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
        temp_update(&t, c, ALL_NEW, 100u * k, &mt, p);
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
            temp_update(&t, c, ALL_NEW, 1000u + ms, &mt, p);
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
        temp_update(&t, c, ALL_NEW, 1000u + ms, &mt, p);
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
        temp_update(&t, c, ALL_NEW, 1000u + ms, &mt, p);
    }
    uint32_t latched_at = 0u;
    for (uint32_t k = 0u; (k <= 1000u) && (latched_at == 0u); k++, ms++) {
        codes_at(c, 60.0f + (40.0f * (float)k * 1.0e-3f), 0);
        temp_update(&t, c, ALL_NEW, 1000u + ms, &mt, p);
        latched_at = (t.ch[TEMP_TMOD_U].fault == TEMP_RATE) ? k : 0u;
    }
    CHECK(latched_at > 0u && latched_at <= ((4u * win) + 1u) && !t.ch[TEMP_TMOD_U].valid);
    codes_at(c, 60.0f, 0);
    temp_update(&t, c, ALL_NEW, 1000u + ms + (10u * win), &mt, p);
    CHECK(!t.ch[TEMP_TMOD_U].valid && t.ch[TEMP_TMOD_U].fault == TEMP_RATE); /* latched for the key cycle */
    /* a persistent step */
    temp_init(&t);
    for (ms = 0u; ms < 1000u; ms++) {
        codes_at(c, 60.0f, 0);
        temp_update(&t, c, ALL_NEW, 1000u + ms, &mt, p);
    }
    latched_at = 0u;
    for (uint32_t k = 0u; (k <= (4u * win)) && (latched_at == 0u); k++, ms++) {
        codes_at(c, 90.0f, 0);
        temp_update(&t, c, ALL_NEW, 1000u + ms, &mt, p);
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
        temp_update(&t, c, ALL_NEW, 1000u + ms, &mt, p);
        always_valid = always_valid && t.ch[TEMP_TMOD_U].valid;
    }
    CHECK(always_valid && t.ch[TEMP_TMOD_U].fault == TEMP_OK);
    /* open and short: at once */
    c[TEMP_TMOD_V] = code_v(4.99f);
    c[TEMP_TMOD_W] = code_v(0.0f);
    temp_update(&t, c, ALL_NEW, 1000u + ms, &mt, p);
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
        temp_update(&t, c, ALL_NEW, 1000u + ms, &mt, &q);
        always_valid = always_valid && t.ch[TEMP_MT1].valid;
    }
    CHECK(always_valid && t.ch[TEMP_MT1].fault == TEMP_OK);
    CHECK_NEAR(t.ch[TEMP_MT1].t_c, 80.0, 2.0);
}

/* ---------------- round 24 (F241): a sample is a new conversion ---------------- */

static void acq_all(ti_acq_t q[TEMP_COUNT], ti_acq_t v)
{
    for (unsigned i = 0u; i < (unsigned)TEMP_COUNT; i++) {
        q[i] = v;
    }
}

/* HELD takes nothing — the channel's state bit for bit for two windows' time, though the code moves (a register nothing
 * converts into any more); EXPIRED withdraws the channel, empties its open window and keeps its value and fault; resumed
 * conversions are judged afresh: a primed channel is valid again at the next window's accepted mean, not before, one never
 * primed at its first sample. A latched TEMP_RATE stays latched through an expiry and the resumption. */
TEST(a_held_conversion_is_not_taken_again_and_an_expired_one_withdraws_the_channel)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const temp_mt_cal_t mt = {.type = TEMP_MT_PT1000};
    const uint32_t win = p->cal_temp_rate_win_ms;
    temp_t t;
    temp_init(&t);
    const temp_ch_state_t *u = &t.ch[TEMP_TMOD_U];
    uint16_t c[TEMP_COUNT];
    ti_acq_t q[TEMP_COUNT];
    acq_all(q, TI_ACQ_NEW);
    uint32_t ms = 1000u;
    for (; ms < (1000u + win + (win / 2u)); ms++) { /* primed, a window accepted, the next one half full */
        codes_at(c, 60.0f, 0);
        temp_update(&t, c, q, ms, &mt, p);
    }
    const temp_ch_state_t b = *u;
    CHECK(b.valid && (b.n > 0u));
    acq_all(q, TI_ACQ_HELD);
    for (uint32_t k = 0u; k < (2u * win); k++, ms++) {
        codes_at(c, 61.0f, 0);
        temp_update(&t, c, q, ms, &mt, p);
    }
    CHECK((u->t_c == b.t_c) && (u->valid == b.valid) && (u->fault == b.fault) && (u->primed == b.primed) &&
          (u->last_ms == b.last_ms) && (u->ref_code == b.ref_code) && (u->acc == b.acc) && (u->n == b.n) &&
          (u->win_ms == b.win_ms) && (u->rate_cnt == b.rate_cnt));
    acq_all(q, TI_ACQ_EXPIRED);
    temp_update(&t, c, q, ms++, &mt, p);
    CHECK(!u->valid && (u->acc == 0u) && (u->n == 0u) && (u->fault == TEMP_OK) && (u->t_c == b.t_c));
    acq_all(q, TI_ACQ_NEW);
    bool early = false;
    for (uint32_t k = 0u; k < (win - 1u); k++, ms++) {
        temp_update(&t, c, q, ms, &mt, p);
        early = early || u->valid;
    }
    for (uint32_t k = 0u; k < 2u; k++, ms++) {
        temp_update(&t, c, q, ms, &mt, p);
    }
    CHECK(!early && u->valid && (u->fault == TEMP_OK));
    CHECK_NEAR(u->t_c, 61.0, 0.3);
    /* a latched TEMP_RATE */
    temp_init(&t);
    acq_all(q, TI_ACQ_NEW);
    for (ms = 1000u; ms < 2000u; ms++) {
        codes_at(c, 60.0f, 0);
        temp_update(&t, c, q, ms, &mt, p);
    }
    codes_at(c, 90.0f, 0);
    for (uint32_t k = 0u; (k < (4u * win)) && (u->fault != TEMP_RATE); k++, ms++) {
        temp_update(&t, c, q, ms, &mt, p);
    }
    CHECK(u->fault == TEMP_RATE);
    acq_all(q, TI_ACQ_EXPIRED);
    temp_update(&t, c, q, ms++, &mt, p);
    CHECK(!u->valid && (u->fault == TEMP_RATE));
    acq_all(q, TI_ACQ_NEW);
    codes_at(c, 60.0f, 0);
    for (uint32_t k = 0u; k < (2u * win); k++, ms++) {
        temp_update(&t, c, q, ms, &mt, p);
    }
    CHECK(!u->valid && (u->fault == TEMP_RATE));
    /* never primed: withdrawn from the first call, accepted at the first new conversion */
    temp_init(&t);
    acq_all(q, TI_ACQ_EXPIRED);
    codes_at(c, 40.0f, 0);
    temp_update(&t, c, q, 5000u, &mt, p);
    CHECK(!u->valid && !u->primed && (u->fault == TEMP_OK));
    acq_all(q, TI_ACQ_NEW);
    temp_update(&t, c, q, 5001u, &mt, p);
    CHECK(u->valid);
    CHECK_NEAR(u->t_c, 40.0, 0.3);
}

/* ---------------- F243: a latched TEMP_RATE is never replaced by a later sample ---------------- */

/* Every channel primed at 60 degC, then a persistent step to 90 degC: TEMP_RATE latched. */
static uint32_t latch_rate(temp_t *t, const temp_mt_cal_t *mt, const ti_params_t *p)
{
    uint16_t c[TEMP_COUNT];
    uint32_t ms = 1000u;
    for (; ms < 2000u; ms++) {
        codes_at(c, 60.0f, 0);
        temp_update(t, c, ALL_NEW, ms, mt, p);
    }
    codes_at(c, 90.0f, 0);
    for (uint32_t k = 0u; (k < (4u * p->cal_temp_rate_win_ms)) && (t->ch[TEMP_TMOD_U].fault != TEMP_RATE); k++, ms++) {
        temp_update(t, c, ALL_NEW, ms, mt, p);
    }
    return ms;
}

/* The reviewer's probe: TEMP_RATE latched, one open (short) sample put TEMP_OPEN (TEMP_SHORT) in its place, and the samples
 * back in range cleared that — TEMP_OK, the channel valid at the next window's mean (the 200th sample), the module maximum
 * (FW-04's derating) back on it. Now the latch and its invalid verdict stay: the open (short) is recorded in `wire`
 * (DTC_TEMP_OPEN_SHORT, app.c) and cleared by the next in-range sample; three windows at the last accepted mean later the
 * channel is still RATE and invalid, and no module channel is back in the maximum. */
TEST(a_latched_rate_fault_is_never_replaced_by_an_open_or_short_sample)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const temp_mt_cal_t mt = {.type = TEMP_MT_PT1000};
    const float rail[2] = {4.99f, 0.0f};
    const temp_fault_t read[2] = {TEMP_OPEN, TEMP_SHORT};
    for (unsigned k = 0u; k < 2u; k++) {
        temp_t t;
        temp_init(&t);
        const temp_ch_state_t *u = &t.ch[TEMP_TMOD_U];
        uint32_t ms = latch_rate(&t, &mt, p);
        CHECK(!u->valid && (u->fault == TEMP_RATE) && (u->wire == TEMP_OK));
        uint16_t c[TEMP_COUNT];
        codes_at(c, 90.0f, 0);
        c[TEMP_TMOD_U] = code_v(rail[k]);
        temp_update(&t, c, ALL_NEW, ms++, &mt, p);
        CHECK(!u->valid && (u->fault == TEMP_RATE) && (u->wire == read[k]));
        codes_at(c, 60.0f, 0);
        bool held = true;
        for (uint32_t n = 0u; n < (3u * p->cal_temp_rate_win_ms); n++, ms++) {
            temp_update(&t, c, ALL_NEW, ms, &mt, p);
            held = held && !u->valid && (u->fault == TEMP_RATE) && (u->wire == TEMP_OK);
        }
        bool any = true;
        bool all = true;
        (void)temp_module_max(&t, &any, &all);
        CHECK(held && !any && !all);
    }
}

/* Without a latched TEMP_RATE nothing changes: an open (short) sample invalidates the channel at once (`wire` reads the
 * same), and in-range samples bring it back at the next window's accepted mean, TEMP_OK — not before; a channel never
 * primed is accepted at its first in-range sample. */
TEST(open_and_short_without_a_rate_latch_still_act_per_sample)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const temp_mt_cal_t mt = {.type = TEMP_MT_PT1000};
    const uint32_t win = p->cal_temp_rate_win_ms;
    const float rail[2] = {4.99f, 0.0f};
    const temp_fault_t read[2] = {TEMP_OPEN, TEMP_SHORT};
    for (unsigned k = 0u; k < 2u; k++) {
        temp_t t;
        temp_init(&t);
        const temp_ch_state_t *u = &t.ch[TEMP_TMOD_U];
        uint16_t c[TEMP_COUNT];
        uint32_t ms = 1000u;
        for (; ms < 2000u; ms++) {
            codes_at(c, 60.0f, 0);
            temp_update(&t, c, ALL_NEW, ms, &mt, p);
        }
        CHECK(u->valid && (u->fault == TEMP_OK) && (u->wire == TEMP_OK));
        c[TEMP_TMOD_U] = code_v(rail[k]);
        temp_update(&t, c, ALL_NEW, ms++, &mt, p);
        CHECK(!u->valid && (u->fault == read[k]));
        CHECK(u->wire == read[k]); /* F243: the sample's open/short in wire too */
        codes_at(c, 60.0f, 0);
        bool early = false;
        for (uint32_t n = 0u; n < (win - 1u); n++, ms++) {
            temp_update(&t, c, ALL_NEW, ms, &mt, p);
            early = early || u->valid;
        }
        for (uint32_t n = 0u; n < 2u; n++, ms++) {
            temp_update(&t, c, ALL_NEW, ms, &mt, p);
        }
        CHECK(!early && u->valid && (u->fault == TEMP_OK) && (u->wire == TEMP_OK));
        CHECK_NEAR(u->t_c, 60.0, 0.3);
        temp_init(&t); /* never primed */
        codes_at(c, 60.0f, 0);
        c[TEMP_TMOD_U] = code_v(rail[k]);
        temp_update(&t, c, ALL_NEW, 5000u, &mt, p);
        CHECK(!u->valid && !u->primed && (u->fault == read[k]));
        CHECK(u->wire == read[k]);
        codes_at(c, 40.0f, 0);
        temp_update(&t, c, ALL_NEW, 5001u, &mt, p);
        CHECK(u->valid && (u->fault == TEMP_OK) && (u->wire == TEMP_OK));
        CHECK_NEAR(u->t_c, 40.0, 0.3);
    }
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
    RUN(a_held_conversion_is_not_taken_again_and_an_expired_one_withdraws_the_channel);
    RUN(a_latched_rate_fault_is_never_replaced_by_an_open_or_short_sample);
    RUN(open_and_short_without_a_rate_latch_still_act_per_sample);
}
