/* test_hwid.c — FW-01 windows, open/short, FW-02 identity. */
#include "hwid.h"
#include "test.h"

static uint16_t code_r(float r) { return (uint16_t)(4095.0f * r / (r + 10000.0f) + 0.5f); }
static uint16_t code_v(float v) { return (uint16_t)(v * 4095.0f / 5.0f + 0.5f); }

TEST(each_sku_resistor)
{
    ti_sku_t s;
    CHECK(hwid_classify(code_r(2200.0f), &s) == HWID_OK && s == TI_SKU_4XX_IGBT);
    CHECK(hwid_classify(code_r(4700.0f), &s) == HWID_OK && s == TI_SKU_8XX_IGBT);
    CHECK(hwid_classify(code_r(10000.0f), &s) == HWID_OK && s == TI_SKU_8XX_SIC);
    CHECK(hwid_classify(code_r(22000.0f), &s) == HWID_OK && s == TI_SKU_4XX_SIC);
    /* 1 % parts at both ends still inside the +/-4 % window */
    CHECK(hwid_classify(code_r(10000.0f * 1.02f), &s) == HWID_OK && s == TI_SKU_8XX_SIC);
}

TEST(open_short_and_between_windows)
{
    ti_sku_t s;
    CHECK(hwid_classify(code_v(4.8f), &s) == HWID_OPEN && s == TI_SKU_NONE);
    CHECK(hwid_classify(code_v(0.1f), &s) == HWID_SHORT && s == TI_SKU_NONE);
    CHECK(hwid_classify(code_v(2.0f), &s) == HWID_UNKNOWN && s == TI_SKU_NONE);
    CHECK(hwid_classify(code_v(2.5f * 1.045f), &s) == HWID_UNKNOWN); /* just outside +4 % */
    CHECK(hwid_classify(code_v(2.5f * 1.035f), &s) == HWID_OK);
}

TEST(unstable_reading_rejected)
{
    ti_sku_t s;
    const uint16_t c[4] = {code_r(10000.0f), code_r(10000.0f), code_r(4700.0f), code_r(10000.0f)};
    CHECK(hwid_classify_stable(c, 4u, &s) == HWID_UNSTABLE);
    const uint16_t d[4] = {code_r(10000.0f), code_r(10000.0f), code_r(10000.0f), code_r(10000.0f)};
    CHECK(hwid_classify_stable(d, 4u, &s) == HWID_OK && s == TI_SKU_8XX_SIC);
}

TEST(fw02_identity_binding)
{
    CHECK(hwid_identity_ok(TI_SKU_8XX_SIC, TI_SKU_8XX_SIC, TI_SKU_8XX_SIC));
    CHECK(!hwid_identity_ok(TI_SKU_4XX_IGBT, TI_SKU_8XX_SIC, TI_SKU_8XX_SIC)); /* wrong parameter set */
    CHECK(!hwid_identity_ok(TI_SKU_8XX_SIC, TI_SKU_8XX_SIC, TI_SKU_4XX_SIC));  /* wrong calibration */
    CHECK(!hwid_identity_ok(TI_SKU_NONE, TI_SKU_NONE, TI_SKU_NONE));
}

void suite_hwid(void)
{
    RUN(each_sku_resistor);
    RUN(open_short_and_between_windows);
    RUN(unstable_reading_rejected);
    RUN(fw02_identity_binding);
}
