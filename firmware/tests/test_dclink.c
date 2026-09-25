/* test_dclink.c — the FW-08 DC-link trim (round 17, item 26): a regen limiter above the normal-range
 * maximum, never a motoring command, bounded, reference fixed at vdc_max_v. */
#include "dclink.h"
#include "test.h"

TEST(trim_takes_back_regen_only_above_the_range_maximum)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const float vmax = p->vdc_max_v; /* 850 V: the reference, whatever the caller */
    dcl_t d;
    dcl_reset(&d);
    bool untouched = true;
    for (int k = 0; k < 2000; k++) { /* at or below the range maximum: nothing taken, nothing wound up */
        untouched = untouched && (dcl_trim(&d, -150.0f, vmax - 0.5f * (float)(k % 3), 300.0f, 1e-3f, p) == -150.0f);
    }
    CHECK(untouched && d.integ == 0.0f);
    CHECK(dcl_trim(&d, 120.0f, vmax + 20.0f, 300.0f, 1e-3f, p) == 120.0f); /* motoring passes unchanged */
    dcl_reset(&d);
    const float fwd = dcl_trim(&d, -150.0f, vmax + 20.0f, 300.0f, 1e-3f, p); /* forward, braking */
    CHECK(fwd > -150.0f && fwd <= 0.0f);
    dcl_reset(&d);
    const float rev = dcl_trim(&d, 150.0f, vmax + 20.0f, -300.0f, 1e-3f, p); /* reverse, braking */
    CHECK(rev < 150.0f && rev >= 0.0f);
    CHECK_NEAR(fwd, -rev, 1e-4);
    dcl_reset(&d);
    for (int k = 0; k < 10000; k++) { /* far above: bounded authority, and a small request goes to 0, never motoring */
        (void)dcl_trim(&d, -400.0f, 1000.0f, 300.0f, 1e-3f, p);
    }
    CHECK(d.integ <= p->cal_dcl_tmax_nm);
    CHECK_NEAR(dcl_trim(&d, -400.0f, 1000.0f, 300.0f, 1e-3f, p), -400.0f + p->cal_dcl_tmax_nm, 1e-3);
    CHECK(dcl_trim(&d, -20.0f, 1000.0f, 300.0f, 1e-3f, p) == 0.0f);
    for (int k = 0; k < 10000; k++) { /* back in range: the integrator bleeds off, regen returns */
        (void)dcl_trim(&d, -150.0f, vmax - 50.0f, 300.0f, 1e-3f, p);
    }
    CHECK(d.integ == 0.0f && dcl_trim(&d, -150.0f, vmax - 50.0f, 300.0f, 1e-3f, p) == -150.0f);
    CHECK(dcl_trim(&d, -150.0f, NAN, 300.0f, 1e-3f, p) == 0.0f && d.integ == 0.0f); /* unjudged link: no regen */
    CHECK(dcl_trim(&d, 150.0f, NAN, 300.0f, 1e-3f, p) == 150.0f);
    CHECK(dcl_trim(&d, NAN, 870.0f, 300.0f, 1e-3f, p) == 0.0f && dcl_trim(&d, 150.0f, 870.0f, NAN, 1e-3f, p) == 0.0f);
}

void suite_dclink(void) { RUN(trim_takes_back_regen_only_above_the_range_maximum); }
