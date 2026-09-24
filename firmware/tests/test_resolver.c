/* test_resolver.c — FW-10: demodulation with the -24 deg filter compensation, observer, wrap,
 * amplitude / excitation / tracking / rate plausibility. Blocks are synthesised here. */
#include "resolver.h"
#include "test.h"
#include "ti_math.h"

#define N HAL_SDADC_BLOCK_N

static rslv_cal_t cal_nom(void)
{
    const rslv_cal_t c = {.ratio_nom = 0.8f, .exc_nom_code = 20000.0f, .sin_gain = 1.0f, .cos_gain = 1.0f,
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
}
