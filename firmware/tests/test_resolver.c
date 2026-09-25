/* test_resolver.c — FW-10: demodulation with the -24 deg filter compensation, observer, wrap,
 * amplitude / excitation / tracking / rate plausibility. Blocks are synthesised here. */
#include "resolver.h"
#include "swg.h"
#include "test.h"
#include "ti_math.h"

#define N HAL_SDADC_BLOCK_N

static rslv_cal_t cal_nom(void)
{
    const rslv_cal_t c = {.ratio_nom = 0.8f, .exc_code_per_vpp = 2500.0f, .sin_gain = 1.0f, .cos_gain = 1.0f,
                          .motor_pp = 4u, .resolver_pp = 1u};
    return c;
}

/* one carrier block at resolver angle th: monitor 20000 codes, sin/cos 16000*amp lagging lag_deg */
static void block(float th, float amp, float exc, float lag_deg, int16_t e[N], int16_t s[N], int16_t c[N])
{
    const float lag = lag_deg * TI_PI / 180.0f;
    for (unsigned k = 0u; k < N; k++) {
        const float ph = TI_2PI * (float)k / (float)N;
        e[k] = (int16_t)lrintf(exc * sinf(ph));
        s[k] = (int16_t)lrintf(16000.0f * amp * sinf(th) * sinf(ph - lag));
        c[k] = (int16_t)lrintf(16000.0f * amp * cosf(th) * sinf(ph - lag));
    }
}

static void feed(rslv_t *r, const rslv_cal_t *c, const ti_params_t *p, float th0, float w, unsigned n, float amp,
                 float exc)
{
    int16_t e[N];
    int16_t s[N];
    int16_t k[N];
    const uint32_t t0 = r->have_first ? (r->t_ref_us + 100u) : 0u; /* time runs on across calls */
    r->exc_ready = true; /* the excitation is at its setpoint: the trim's job, tested on its own below */
    for (unsigned i = 0u; i < n; i++) {
        block(th0 + (w * 1e-4f * (float)i), amp, exc, 24.0f, e, s, k);
        rslv_update(r, e, s, k, 1e-4f, t0 + (100u * i), c, p);
    }
}

TEST(standstill_angles)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const rslv_cal_t c = cal_nom();
    const float angles[5] = {0.0f, 1.0f, 2.5f, 3.9f, 5.8f};
    for (unsigned a = 0u; a < 5u; a++) {
        rslv_t r;
        rslv_init(&r);
        feed(&r, &c, p, angles[a], 0.0f, 40u, 1.0f, 20000.0f);
        CHECK(r.valid);
        CHECK_NEAR(ti_wrap_pi(r.theta - angles[a]), 0.0, 0.01);
        CHECK_NEAR(r.amp, 1.0, 0.01);
    }
}

TEST(filter_phase_compensation_matters)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    rslv_cal_t c = cal_nom();
    rslv_t r;
    rslv_init(&r);
    feed(&r, &c, p, 1.0f, 0.0f, 10u, 1.0f, 20000.0f);
    CHECK_NEAR(r.amp, 1.0, 0.01); /* 24 deg compensated */
    c.phase_trim_deg = -24.0f;    /* compensation cancelled: the projection loses cos(24 deg) */
    rslv_init(&r);
    feed(&r, &c, p, 1.0f, 0.0f, 10u, 1.0f, 20000.0f);
    CHECK_NEAR(r.amp, cosf(24.0f * TI_PI / 180.0f), 0.01);
}

TEST(tracks_constant_speed_and_wraps)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const rslv_cal_t c = cal_nom();
    rslv_t r;
    rslv_init(&r);
    const float w = 3000.0f / TI_RPM_PER_RAD_S; /* 3000 rpm, resolver pp 1 */
    feed(&r, &c, p, 0.2f, w, 400u, 1.0f, 20000.0f);   /* 40 ms: > 1.9 turns, several wraps */
    CHECK(r.valid && !r.trk_fault && !r.acc_fault);
    CHECK_NEAR(rslv_speed_rpm(&r, &c), 3000.0, 5.0);
    const float truth = ti_wrap_2pi(0.2f + w * 1e-4f * 399.0f); /* theta refers to the last block */
    CHECK_NEAR(ti_wrap_pi(r.theta - truth), 0.0, 0.02);
    CHECK(r.theta >= 0.0f && r.theta < TI_2PI);
    /* motor electrical angle = pp * mechanical - zero */
    rslv_cal_t z = c;
    z.zero_rad = 0.5f;
    CHECK_NEAR(ti_wrap_pi(rslv_theta_e(&r, &z) - ti_wrap_2pi(4.0f * r.theta - 0.5f)), 0.0, 1e-4);
    CHECK_NEAR(rslv_omega_e(&r, &c), 4.0 * r.omega, 1e-3);
}

TEST(amplitude_window_low_invalidates)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const rslv_cal_t c = cal_nom();
    rslv_t r;
    rslv_init(&r);
    feed(&r, &c, p, 1.0f, 0.0f, 40u, 1.0f, 20000.0f);
    CHECK(r.valid);
    feed(&r, &c, p, 1.0f, 0.0f, 5u, 0.6f, 20000.0f); /* 0.6 < 0.75 window */
    CHECK(r.amp_fault && !r.valid);
    feed(&r, &c, p, 1.0f, 0.0f, 5u, 1.0f, 20000.0f);
    CHECK(!r.valid); /* latched */
}

TEST(excitation_monitor_window)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const rslv_cal_t c = cal_nom();
    rslv_t r;
    rslv_init(&r);
    feed(&r, &c, p, 1.0f, 0.0f, 20u, 1.0f, 12000.0f); /* monitor 0.6 of EOL; ratiometric envelopes stay 1 */
    CHECK(r.exc_fault && !r.valid);
}

TEST(glitch_trips_tracking)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const rslv_cal_t c = cal_nom();
    rslv_t r;
    rslv_init(&r);
    feed(&r, &c, p, 1.0f, 0.0f, 40u, 1.0f, 20000.0f);
    CHECK(r.valid);
    feed(&r, &c, p, 1.6f, 0.0f, 10u, 1.0f, 20000.0f); /* 34 deg jump */
    CHECK(r.trk_fault && !r.valid);
}

TEST(acquires_at_speed_after_a_reset)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const rslv_cal_t c = cal_nom();
    rslv_t r;
    rslv_init(&r);
    const float w = 12000.0f / TI_RPM_PER_RAD_S; /* MCU reset while driving at 12 000 rpm */
    feed(&r, &c, p, 2.0f, w, 30u, 1.0f, 20000.0f);
    CHECK(r.valid && !r.trk_fault && !r.acc_fault);
    CHECK_NEAR(rslv_speed_rpm(&r, &c), 12000.0, 20.0);
    /* extrapolation between blocks: half a carrier later the angle has moved w * 50 us */
    ti_params_t q = *p;
    const float a0 = rslv_theta_e_at(&r, &c, r.t_ref_us, &q);
    const float a1 = rslv_theta_e_at(&r, &c, r.t_ref_us + 50u, &q);
    CHECK_NEAR(ti_wrap_pi(a1 - a0), 4.0 * w * 50e-6, 1e-3);
    q.cal_rslv_latency_us = 50.0f;
    CHECK_NEAR(ti_wrap_pi(rslv_theta_e_at(&r, &c, r.t_ref_us + 50u, &q) - a0), 0.0, 1e-3);
}

TEST(missed_blocks_bridged_long_gap_reacquires)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const rslv_cal_t c = cal_nom();
    rslv_t r;
    rslv_init(&r);
    r.exc_ready = true;
    const float w = 12000.0f / TI_RPM_PER_RAD_S;
    int16_t e[N];
    int16_t s[N];
    int16_t k[N];
    bool ok = true;
    for (unsigned i = 0u; i < 200u; i++) { /* every third block missed by the ISR */
        if ((i > 40u) && ((i % 3u) == 0u)) {
            continue;
        }
        block(w * 1e-4f * (float)i, 1.0f, 20000.0f, 24.0f, e, s, k);
        rslv_update(&r, e, s, k, 1e-4f, 100u * i, &c, p);
        ok = ok && ((i < 30u) || r.valid);
    }
    CHECK(ok && !r.acc_fault && !r.trk_fault);
    CHECK_NEAR(rslv_speed_rpm(&r, &c), 12000.0, 20.0);
    const unsigned t1 = 200u + 9u; /* a 0.9 ms gap: re-acquire, invalid until locked again */
    block(w * 1e-4f * (float)t1, 1.0f, 20000.0f, 24.0f, e, s, k);
    rslv_update(&r, e, s, k, 1e-4f, 100u * t1, &c, p);
    CHECK(!r.valid && !r.primed && !r.acc_fault);
    for (unsigned i = t1 + 1u; i < (t1 + 30u); i++) {
        block(w * 1e-4f * (float)i, 1.0f, 20000.0f, 24.0f, e, s, k);
        rslv_update(&r, e, s, k, 1e-4f, 100u * i, &c, p);
    }
    CHECK(r.valid && !r.acc_fault && !r.trk_fault);
}

TEST(rate_vs_current_model)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const rslv_cal_t c = cal_nom();
    rslv_t r;
    rslv_init(&r);
    const float w = 2000.0f / TI_RPM_PER_RAD_S;
    feed(&r, &c, p, 0.0f, w, 200u, 1.0f, 20000.0f);
    const float we = rslv_omega_e(&r, &c);
    for (unsigned i = 0u; i < 50u; i++) {
        rslv_rate_check(&r, we * 1.02f, true, &c, p); /* agrees within tolerance */
    }
    CHECK(!r.rate_fault && r.valid);
    for (unsigned i = 0u; i < 29u; i++) {
        rslv_rate_check(&r, we * 0.5f, true, &c, p);
    }
    CHECK(!r.rate_fault); /* debounce */
    rslv_rate_check(&r, we * 0.5f, true, &c, p);
    CHECK(r.rate_fault && !r.valid);
}

TEST(swg_trim_direction)
{
    rslv_t r;
    rslv_init(&r);
    r.exc_ratio = 0.9f;
    CHECK(rslv_swg_trim(10u, &r) == 11u);
    r.exc_ratio = 1.1f;
    CHECK(rslv_swg_trim(10u, &r) == 9u);
    r.exc_ratio = 1.0f;
    CHECK(rslv_swg_trim(10u, &r) == 10u);
    r.exc_ratio = 0.5f;
    CHECK(rslv_swg_trim(15u, &r) == 15u);
}

/* ======================= round 16 ======================= */

/* Frames at t0, t0 + 100, ... (block starts), n of them; returns the last block's start. The windings
 * follow the excitation (amp = the resolver output relative to its EOL ratio). */
static uint32_t frames(rslv_t *r, const rslv_cal_t *c, const ti_params_t *p, uint32_t t0, float th0, float w,
                       unsigned n, float amp, float exc)
{
    int16_t e[N];
    int16_t s[N];
    int16_t k[N];
    for (unsigned i = 0u; i < n; i++) {
        block(th0 + (w * 1e-4f * (float)i), amp * exc / 20000.0f, exc, 24.0f, e, s, k);
        rslv_update(r, e, s, k, 1e-4f, t0 + (100u * i), c, p);
    }
    return t0 + (100u * (n - 1u));
}

/* A14-R01, the reviewer's reproduction: with no new frame nothing expired the angle — valid stayed 1
 * a whole second later. Now the age check alone (no frame) withdraws it at exactly cal_rslv_hold_us
 * after the newest frame's start, at standstill and while turning, also when the microsecond counter
 * wraps in between; the frames that return are acquired afresh (priming + SETTLE_BLOCKS), never
 * resumed from the extrapolated state. */
TEST(validity_expires_without_new_frames_and_reacquires_from_scratch)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const rslv_cal_t c = cal_nom();
    const float speeds[2] = {0.0f, 3000.0f / TI_RPM_PER_RAD_S};
    const uint32_t epochs[2] = {1000u, 0xFFFFFE00u}; /* the second straddles the 32-bit microsecond wrap */
    for (unsigned sp = 0u; sp < 2u; sp++) {
        for (unsigned ep = 0u; ep < 2u; ep++) {
            rslv_t r;
            rslv_init(&r);
            r.exc_ready = true;
            const uint32_t t_last = frames(&r, &c, p, epochs[ep], 0.4f, speeds[sp], 40u, 1.0f, 18000.0f);
            CHECK(r.valid && !r.stale);
            rslv_age(&r, t_last + p->cal_rslv_hold_us - 1u, p); /* a missing frame inside the hold */
            CHECK(r.valid && !r.stale);
            rslv_age(&r, t_last + p->cal_rslv_hold_us, p);
            CHECK(!r.valid && r.stale && !r.locked && !r.primed);
            rslv_age(&r, t_last + 1000000u, p); /* and a second later still */
            CHECK(!r.valid && r.stale);
            /* frames return: nothing valid before priming (2) + SETTLE_BLOCKS (20) have passed */
            const uint32_t t1 = t_last + 2000u;
            bool early = false;
            for (unsigned i = 0u; i < 22u; i++) {
                (void)frames(&r, &c, p, t1 + (100u * i), 0.4f + (speeds[sp] * 0.2f), speeds[sp], 1u, 1.0f, 18000.0f);
                rslv_age(&r, t1 + (100u * i) + 50u, p);
                early = early || r.valid;
            }
            CHECK(!early);
            (void)frames(&r, &c, p, t1 + 2200u, 0.4f + (speeds[sp] * 0.2f), speeds[sp], 3u, 1.0f, 18000.0f);
            rslv_age(&r, t1 + 2450u, p);
            CHECK(r.valid && !r.stale && !r.trk_fault && !r.acc_fault);
        }
    }
}

/* A14-N01: the monitor taps the protected node, before the PTC. A PTC still at 5 ohm an hour after a
 * trip takes the winding from 6.94 to 6.3 V pp while the monitor stays at the 7.2 V pp setpoint; only the
 * resolver's own output shows it (0.875 / 0.964 of its EOL ratio). FW-10 judges the winding against the
 * 6.5 V pp floor and flags it — once the trim has brought the excitation up, not during its ramp. */
TEST(winding_plane_flags_what_the_monitor_cannot_see)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const rslv_cal_t c = cal_nom();
    const float trip = (70.0f / 80.0f) / (70.0f / 72.6f); /* resolver output vs EOL, PTC 5 ohm per line */
    rslv_t r;
    rslv_init(&r);
    (void)frames(&r, &c, p, 1000u, 1.0f, 0.0f, 40u, 1.0f, 18000.0f); /* monitor 7.2 V pp: the setpoint */
    CHECK(!r.valid && !r.exc_ready && !r.exc_fault);                   /* locked, but the trim has not run */
    CHECK(rslv_swg_trim(12u, &r) == 12u && r.exc_ready && !r.swg_sat); /* in the band: ready */
    (void)frames(&r, &c, p, 5000u, 1.0f, 0.0f, 3u, 1.0f, 18000.0f);
    CHECK(r.valid);
    CHECK_NEAR(r.mon_vpp, 7.2, 0.01);
    CHECK_NEAR(r.wind_vpp, 7.2 * 70.0 / 72.6, 0.02); /* 6.94 V pp, cold */
    (void)frames(&r, &c, p, 6000u, 1.0f, 0.0f, (unsigned)p->cal_rslv_debounce - 1u, trip, 18000.0f);
    CHECK_NEAR(r.mon_vpp, 7.2, 0.01);                /* the monitor does not see it */
    CHECK_NEAR(r.wind_vpp, 7.2 * 70.0 / 80.0, 0.02); /* 6.3 V pp at the winding */
    CHECK(!r.exc_fault);                             /* debounced */
    (void)frames(&r, &c, p, 7000u, 1.0f, 0.0f, 1u, trip, 18000.0f);
    CHECK(r.exc_fault && !r.valid && !r.amp_fault); /* the ratiometric window (0.75) would not have flagged it */
    /* the same frames before the trim is ready: not valid, but not a fault either (it is still ramping) */
    rslv_t q;
    rslv_init(&q);
    (void)frames(&q, &c, p, 1000u, 1.0f, 0.0f, 40u, trip, 18000.0f);
    CHECK(!q.valid && !q.exc_fault);
}

/* A14-N01: the trim ramps one code at a time; ready once in the +-5 % band, or once it can go no further
 * (then the FW-10 checks judge); saturated = at the top code and still below the band. */
TEST(swg_trim_ramps_readies_and_saturates)
{
    rslv_t r;
    rslv_init(&r);
    r.exc_ratio = 0.7f;
    CHECK(rslv_swg_trim(8u, &r) == 9u && !r.exc_ready && !r.swg_sat);
    r.exc_ratio = 0.96f;
    CHECK(rslv_swg_trim(12u, &r) == 12u && r.exc_ready && !r.swg_sat);
    rslv_init(&r);
    r.exc_ratio = 0.9f;
    CHECK(rslv_swg_trim(HAL_SWG_CODE_MAX, &r) == HAL_SWG_CODE_MAX && r.swg_sat && r.exc_ready);
    r.exc_ratio = 1.0f;
    CHECK(rslv_swg_trim(HAL_SWG_CODE_MAX, &r) == HAL_SWG_CODE_MAX && !r.swg_sat);
    rslv_init(&r);
    r.exc_ratio = 1.3f;
    CHECK(rslv_swg_trim(0u, &r) == 0u && r.exc_ready && !r.swg_sat);
}

void suite_resolver(void)
{
    RUN(standstill_angles);
    RUN(filter_phase_compensation_matters);
    RUN(tracks_constant_speed_and_wraps);
    RUN(amplitude_window_low_invalidates);
    RUN(excitation_monitor_window);
    RUN(glitch_trips_tracking);
    RUN(acquires_at_speed_after_a_reset);
    RUN(missed_blocks_bridged_long_gap_reacquires);
    RUN(rate_vs_current_model);
    RUN(swg_trim_direction);
    RUN(validity_expires_without_new_frames_and_reacquires_from_scratch);
    RUN(winding_plane_flags_what_the_monitor_cannot_see);
    RUN(swg_trim_ramps_readies_and_saturates);
}
