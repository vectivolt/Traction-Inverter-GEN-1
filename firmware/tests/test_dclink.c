/* test_dclink.c — FW-08 DC-link voltage controller sign and bounds. */
#include "dclink.h"
#include "test.h"

TEST(sign_and_bounds)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    dcl_t d;
    dcl_reset(&d);
    CHECK(dcl_step(&d, 850.0f, 870.0f, 300.0f, 1e-3f, p) > 0.0f); /* link high, forward: motor */
    dcl_reset(&d);
    CHECK(dcl_step(&d, 850.0f, 870.0f, -300.0f, 1e-3f, p) < 0.0f);
    dcl_reset(&d);
    CHECK(dcl_step(&d, 850.0f, 800.0f, 300.0f, 1e-3f, p) < 0.0f); /* link low: generate */
    dcl_reset(&d);
    for (int k = 0; k < 10000; k++) {
        (void)dcl_step(&d, 850.0f, 1000.0f, 300.0f, 1e-3f, p);
    }
    CHECK(dcl_step(&d, 850.0f, 1000.0f, 300.0f, 1e-3f, p) <= p->cal_dcl_tmax_nm);
    CHECK(dcl_step(&d, 850.0f, NAN, 300.0f, 1e-3f, p) == 0.0f);
}

void suite_dclink(void) { RUN(sign_and_bounds); }
