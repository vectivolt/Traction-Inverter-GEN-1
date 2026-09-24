/* test_gains.c — per-SKU gain sets per f_sw under the §2 crossover ceilings. */
#include "gains.h"
#include "test.h"
#include "ti_math.h"

TEST(sic_10k_8k_and_igbt_5k)
{
    const ti_params_t *s8 = ti_params_get(TI_SKU_8XX_SIC);
    const ti_params_t *g8 = ti_params_get(TI_SKU_8XX_IGBT);
    gain_set_t g;
    CHECK(gains_default(s8, 10000u, 0.35e-3f, 0.025f, &g));
    CHECK_NEAR(g.fc_hz, 0.9 * 1200.0, 1e-3);
    CHECK_NEAR(g.kp_v_per_a, 0.35e-3 * 2.0 * 3.14159265 * 1080.0, 1e-4);
    CHECK_NEAR(g.ki_v_per_as, 0.025 * 2.0 * 3.14159265 * 1080.0, 1e-3);
    CHECK_NEAR(g.ts_s, 50e-6, 1e-9);   /* double update: T_sw / 2 */
    CHECK_NEAR(g.delay_s, 75e-6, 1e-9); /* 0.75 T_sw */
    CHECK(gains_default(s8, 8000u, 0.35e-3f, 0.025f, &g));
    CHECK_NEAR(g.fc_hz, 0.9 * 1100.0, 1e-3);
    CHECK(gains_default(g8, 5000u, 0.35e-3f, 0.025f, &g));
    CHECK_NEAR(g.fc_hz, 0.9 * 700.0, 1e-3);
}

TEST(ceiling_and_fsw_enforced)
{
    const ti_params_t *s8 = ti_params_get(TI_SKU_8XX_SIC);
    const ti_params_t *g8 = ti_params_get(TI_SKU_8XX_IGBT);
    gain_set_t g;
    CHECK(gains_compute(s8, 10000u, 1200.0f, 0.35e-3f, 0.025f, &g));
    CHECK(!gains_compute(s8, 8000u, 1200.0f, 0.35e-3f, 0.025f, &g)); /* 1.1 kHz at 8 kHz */
    CHECK(!gains_compute(s8, 5000u, 600.0f, 0.35e-3f, 0.025f, &g));  /* not a SiC switching mode */
    CHECK(!gains_compute(g8, 10000u, 600.0f, 0.35e-3f, 0.025f, &g)); /* not an IGBT switching mode */
    CHECK(!gains_compute(g8, 5000u, 800.0f, 0.35e-3f, 0.025f, &g));
    CHECK(!gains_compute(s8, 10000u, 1000.0f, 0.0f, 0.025f, &g)); /* no inductance */
    CHECK(gains_ceiling_hz(s8, 7000u) == 0.0f);
}

void suite_gains(void)
{
    RUN(sic_10k_8k_and_igbt_5k);
    RUN(ceiling_and_fsw_enforced);
}
