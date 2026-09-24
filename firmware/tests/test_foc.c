/* test_foc.c — transforms, SVPWM, the current loop closed on an RL plant (screening motor),
 * anti-windup, dead-time compensation and the non-finite guard. */
#include "foc.h"
#include "test.h"

TEST(clarke_park_amplitude_invariant)
{
    const float th = 0.7f;
    const float i[3] = {100.0f * cosf(th), 100.0f * cosf(th - 2.0944f), 100.0f * cosf(th + 2.0944f)};
    float a;
    float b;
    float d;
    float q;
    foc_clarke(i, &a, &b);
    CHECK_NEAR(sqrt((double)(a * a + b * b)), 100.0, 0.01);
    foc_park(a, b, th, &d, &q);
    CHECK_NEAR(d, 100.0, 0.01);
    CHECK_NEAR(q, 0.0, 0.01);
    float a2;
    float b2;
    foc_ipark(d, q, th, &a2, &b2);
    CHECK_NEAR(a2, a, 1e-3);
    CHECK_NEAR(b2, b, 1e-3);
}

TEST(svpwm_range_and_zero_vector)
{
    float d[3];
    foc_svpwm(0.0f, 0.0f, 800.0f, d);
    CHECK_NEAR(d[0], 0.5, 1e-6);
    CHECK_NEAR(d[1], 0.5, 1e-6);
    const float vmax = 800.0f / TI_SQRT3; /* linear limit */
    for (int k = 0; k < 12; k++) {
        const float th = 0.5236f * (float)k;
        foc_svpwm(vmax * cosf(th), vmax * sinf(th), 800.0f, d);
        for (int j = 0; j < 3; j++) {
            CHECK(d[j] >= -1e-4f && d[j] <= 1.0001f);
        }
    }
}

/* dq RL plant of the screening motor, standstill (w = 0), exact over one loop period */
static void plant(float *id, float *iq, float vd, float vq, float ts, const motor_t *m)
{
    const float a = expf(-m->rs_ohm * ts / m->ld_h);
    *id = *id * a + (1.0f - a) * vd / m->rs_ohm;
    *iq = *iq * a + (1.0f - a) * vq / m->rs_ohm;
}

static void to_abc(float id, float iq, float th, float i[3])
{
    float a;
    float b;
    foc_ipark(id, iq, th, &a, &b);
    i[0] = a;
    i[1] = -0.5f * a + 0.866025f * b;
    i[2] = -0.5f * a - 0.866025f * b;
}

TEST(current_loop_closes_on_screening_motor)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const motor_t m = motor_screening();
    gain_set_t g;
    CHECK(gains_default(p, 10000u, m.ld_h, m.rs_ohm, &g));
    foc_t f;
    foc_reset(&f);
    f.iq_ref = 200.0f;
    float id = 0.0f;
    float iq = 0.0f;
    float i[3];
    float peak = 0.0f;
    for (int k = 0; k < 200; k++) { /* 10 ms at 20 kHz */
        to_abc(id, iq, 0.3f, i);
        CHECK(foc_step(&f, i, 0.3f, 0.0f, 700.0f, &m, &g, p));
        plant(&id, &iq, f.vd, f.vq, g.ts_s, &m);
        peak = ti_maxf(peak, iq);
    }
    CHECK_NEAR(iq, 200.0, 2.0);
    CHECK_NEAR(id, 0.0, 2.0);
    CHECK(peak < 240.0f); /* <= 20 % overshoot with the delay */
}

TEST(anti_windup_recovers)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const motor_t m = motor_screening();
    gain_set_t g;
    CHECK(gains_default(p, 10000u, m.ld_h, m.rs_ohm, &g));
    foc_t f;
    foc_reset(&f);
    f.iq_ref = 400.0f;
    float id = 0.0f;
    float iq = 0.0f;
    float i[3];
    for (int k = 0; k < 400; k++) { /* 10 V link: saturated the whole time */
        to_abc(id, iq, 0.0f, i);
        (void)foc_step(&f, i, 0.0f, 0.0f, 10.0f, &m, &g, p);
        plant(&id, &iq, f.vd, f.vq, g.ts_s, &m);
    }
    CHECK(f.sat);
    CHECK(fabsf(f.xi_q) <= 2.0f * f.vmax + 1e-3f); /* bounded integrator */
    f.iq_ref = 50.0f;
    for (int k = 0; k < 200; k++) {
        to_abc(id, iq, 0.0f, i);
        (void)foc_step(&f, i, 0.0f, 0.0f, 700.0f, &m, &g, p);
        plant(&id, &iq, f.vd, f.vq, g.ts_s, &m);
    }
    CHECK_NEAR(iq, 50.0, 3.0); /* back on target within 10 ms */
}

TEST(nonfinite_guard_blocks_pwm_write)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const motor_t m = motor_screening();
    gain_set_t g;
    (void)gains_default(p, 10000u, m.ld_h, m.rs_ohm, &g);
    foc_t f;
    foc_reset(&f);
    const float i[3] = {0.0f, 0.0f, 0.0f};
    f.iq_ref = NAN; /* NaN torque -> NaN current reference */
    CHECK(!foc_step(&f, i, 0.0f, 0.0f, 700.0f, &m, &g, p));
    CHECK(f.guard_trip);
    CHECK(f.duty[0] == 0.5f && f.duty[1] == 0.5f && f.duty[2] == 0.5f); /* untouched */
    foc_reset(&f);
    CHECK(f.guard_trip); /* a reset does not hide the event */
    f.iq_ref = 10.0f;
    CHECK(!foc_step(&f, i, INFINITY, 0.0f, 700.0f, &m, &g, p));
    const float bad[3] = {NAN, 0.0f, 0.0f};
    CHECK(!foc_step(&f, bad, 0.0f, 0.0f, 700.0f, &m, &g, p));
    CHECK(!foc_step(&f, i, 0.0f, 0.0f, 0.0f, &m, &g, p)); /* no link voltage */
}

TEST(dead_time_compensation_sign)
{
    float d[3] = {0.5f, 0.5f, 0.5f};
    const float i[3] = {100.0f, -100.0f, 1.0f};
    foc_dt_comp(d, i, 0.01f, 5.0f);
    CHECK_NEAR(d[0], 0.51, 1e-6);
    CHECK_NEAR(d[1], 0.49, 1e-6);
    CHECK_NEAR(d[2], 0.502, 1e-6); /* linear band near zero current */
}

TEST(back_emf_speed_model)
{
    const motor_t m = motor_screening();
    foc_t f;
    foc_reset(&f);
    f.id = 0.0f;
    f.iq = 100.0f;
    f.vq = (m.rs_ohm * 100.0f) + (500.0f * m.psi_wb);
    bool v = false;
    CHECK_NEAR(foc_omega_model(&f, &m, &v), 500.0, 0.5);
    CHECK(v);
    f.sat = true;
    (void)foc_omega_model(&f, &m, &v);
    CHECK(!v);
}

void suite_foc(void)
{
    RUN(clarke_park_amplitude_invariant);
    RUN(svpwm_range_and_zero_vector);
    RUN(current_loop_closes_on_screening_motor);
    RUN(anti_windup_recovers);
    RUN(nonfinite_guard_blocks_pwm_write);
    RUN(dead_time_compensation_sign);
    RUN(back_emf_speed_model);
}
