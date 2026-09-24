/* test_hvil.c — FW-09 signature decode and the <= 100 ms reaction. */
#include "hvil.h"
#include "test.h"

static uint16_t code_v(float v) { return (uint16_t)(v * 4095.0f / 5.0f + 0.5f); }

TEST(signatures)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    CHECK(hvil_classify(3.0f, true, p) == HVIL_CLOSED);
    CHECK(hvil_classify(2.0f, false, p) == HVIL_CLOSED);
    CHECK(hvil_classify(2.5f, true, p) == HVIL_OPEN);
    CHECK(hvil_classify(2.5f, false, p) == HVIL_OPEN);
    CHECK(hvil_classify(0.0f, true, p) == HVIL_SHORT_GND);
    CHECK(hvil_classify(5.0f, false, p) == HVIL_SHORT_SUPPLY);
    CHECK(hvil_classify(3.0f, false, p) == HVIL_IMPLAUSIBLE); /* high signature with the drive low */
    CHECK(hvil_classify(2.0f, true, p) == HVIL_IMPLAUSIBLE);
}

/* loop model: closed 3.0/2.0 V following the drive, open 2.5 V */
static float loop_v(bool closed, bool drive) { return closed ? (drive ? 3.0f : 2.0f) : 2.5f; }

TEST(open_detected_within_100ms)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    hvil_t h;
    hvil_init(&h);
    bool drive = hvil_step(&h, 0u, 0u, p);
    for (uint32_t t = 1u; t <= 200u; t++) {
        drive = hvil_step(&h, code_v(loop_v(true, drive)), t, p);
    }
    CHECK(h.status == HVIL_CLOSED);
    uint32_t t_open = 0u;
    for (uint32_t t = 201u; t <= 400u; t++) {
        drive = hvil_step(&h, code_v(loop_v(false, drive)), t, p);
        if ((h.status == HVIL_OPEN) && (t_open == 0u)) {
            t_open = t;
        }
    }
    CHECK(t_open != 0u);
    CHECK((t_open - 200u) <= p->hvil_reaction_ms);
}

TEST(short_to_ground_detected)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    hvil_t h;
    hvil_init(&h);
    (void)hvil_step(&h, 0u, 0u, p);
    for (uint32_t t = 1u; t <= 100u; t++) {
        (void)hvil_step(&h, code_v(0.0f), t, p);
    }
    CHECK(h.status == HVIL_SHORT_GND);
}

void suite_hvil(void)
{
    RUN(signatures);
    RUN(open_detected_within_100ms);
    RUN(short_to_ground_detected);
}
