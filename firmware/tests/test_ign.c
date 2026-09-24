/* test_ign.c — KL15 hysteresis and debounce. */
#include "ign.h"
#include "test.h"

static uint16_t code_kl15(float v) { return (uint16_t)(((v - 0.3f) * 10.0f / 57.0f) * 4095.0f / 5.0f + 0.5f); }

TEST(hysteresis_and_debounce)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    ign_t g;
    ign_init(&g, false);
    CHECK_NEAR(ign_kl15_v(code_kl15(13.5f)), 13.5, 0.1);
    ign_update(&g, code_kl15(12.0f), 0u, p);
    CHECK(!g.on);
    ign_update(&g, code_kl15(12.0f), p->cal_ign_debounce_ms, p);
    CHECK(g.on);
    ign_update(&g, code_kl15(5.0f), 100u, p); /* inside the hysteresis band: stays on */
    ign_update(&g, code_kl15(5.0f), 200u, p);
    CHECK(g.on);
    ign_update(&g, code_kl15(1.0f), 300u, p);
    ign_update(&g, code_kl15(12.0f), 305u, p); /* a bounce resets the debounce */
    ign_update(&g, code_kl15(1.0f), 310u, p);
    ign_update(&g, code_kl15(1.0f), 310u + p->cal_ign_debounce_ms - 1u, p);
    CHECK(g.on);
    ign_update(&g, code_kl15(1.0f), 310u + p->cal_ign_debounce_ms, p);
    CHECK(!g.on);
}

void suite_ign(void) { RUN(hysteresis_and_debounce); }
