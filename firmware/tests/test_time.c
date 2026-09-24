/* test_time.c — A12-R06: one time domain. The 32-bit microsecond counter wraps every 4294.967296 s;
 * the 64-bit extension and the millisecond domain derived from it must be continuous across that
 * wrap, across repeated wraps, and read the same from any context. */
#include "can_cmd.h"
#include "dtc.h"
#include "sim.h"
#include "test.h"
#include "timer.h"

#define WRAP_US 4294967296ull

TEST(us64_extension_survives_repeated_wraps)
{
    ti_time64_t e = {0};
    uint64_t prev = 0u;
    uint64_t truth = 0u;
    bool mono = true;
    bool exact = true;
    for (uint32_t k = 0u; k < 40u; k++) { /* 40 steps of 17.9 min: ten wraps */
        truth += WRAP_US / 4u + 12345u;
        const uint64_t t = ti_time64_extend(&e, (uint32_t)truth);
        mono = mono && (t > prev);
        exact = exact && (t == truth);
        prev = t;
    }
    CHECK(mono && exact && (e.hi >= 10u));
    /* the millisecond domain wraps at 2^32 ms like any uint32: ti_elapsed stays exact */
    const uint64_t a = (WRAP_US * 1000u) - 3000u;          /* 3 ms before the 2^32 ms wrap */
    CHECK(ti_elapsed(ti_ms_from_us64(a + 8000u), ti_ms_from_us64(a), 8u));
    CHECK(!ti_elapsed(ti_ms_from_us64(a + 8000u), ti_ms_from_us64(a), 9u));
}

TEST(sim_clock_across_the_microsecond_wrap)
{
    sim_reset_at_us(WRAP_US - 5000u); /* 5 ms before the 32-bit microsecond counter wraps */
    const uint32_t ms0 = hal_time_ms();
    const uint64_t us0 = hal_time_us64();
    sim_advance_us(8000u); /* across it */
    CHECK(hal_time_us() < 5000u);                     /* the raw counter did wrap */
    CHECK(hal_time_us64() == us0 + 8000u);            /* the extension did not */
    CHECK(ti_age(hal_time_ms(), ms0) == 8u);          /* an 8 ms old stamp reads 8 ms */
    uint64_t prev = hal_time_us64();
    bool mono = true;
    for (uint32_t k = 0u; k < 12u; k++) { /* repeated wraps, read at least once per wrap */
        sim_advance_us(1200000000u); /* 20 min */
        const uint64_t t = hal_time_us64();
        mono = mono && (t == prev + 1200000000u);
        prev = t;
    }
    CHECK(mono);
}

/* FW-11 freshness with the stamps the firmware now uses, straddling the wrap. */
TEST(can_freshness_across_the_microsecond_wrap)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    sim_reset_at_us(WRAP_US - 4000u);
    can_cmd_t c;
    can_cmd_init(&c);
    hal_can_frame_t f;
    can_encode_vcu_cmd(&f, 1u, TI_GEAR_D, true, false, 50.0f, TI_CONT_CLOSED, false, false, false, 60.0f);
    CHECK(can_cmd_rx(&c, &f, hal_time_ms(), p));
    can_encode_vcu_bms(&f, 1u, 750.0f, 50e3f, 200e3f);
    CHECK(can_cmd_rx(&c, &f, hal_time_ms(), p));
    sim_advance_us(8000u); /* 8 ms later, after the wrap */
    CHECK(can_cmd_fresh(&c, hal_time_ms(), p) && can_bms_fresh(&c, hal_time_ms(), p));
    sim_advance_us(13000u); /* 21 ms after the frame: stale, as FW-11 requires */
    CHECK(!can_cmd_fresh(&c, hal_time_ms(), p) && can_bms_fresh(&c, hal_time_ms(), p));
}

/* DTC time stamps taken across the wrap keep their order and distance. */
TEST(dtc_time_stamps_across_the_microsecond_wrap)
{
    sim_reset_at_us(WRAP_US - 7000u);
    dtc_init();
    dtc_set(DTC_CAN_TIMEOUT, hal_time_ms());
    sim_advance_us(12000u);
    dtc_set(DTC_CAN_TIMEOUT, hal_time_ms());
    uint32_t first = 0u;
    uint32_t last = 0u;
    CHECK(dtc_times(DTC_CAN_TIMEOUT, &first, &last) && (ti_age(last, first) == 12u));
    dtc_init();
    dtc_set(DTC_V5GD, 0u); /* 0 ms is a valid stamp (the 2^32 ms wrap): still the first occurrence */
    dtc_set(DTC_V5GD, 5u);
    CHECK(dtc_times(DTC_V5GD, &first, &last) && first == 0u && last == 5u);
}

void suite_time(void)
{
    RUN(us64_extension_survives_repeated_wraps);
    RUN(sim_clock_across_the_microsecond_wrap);
    RUN(can_freshness_across_the_microsecond_wrap);
    RUN(dtc_time_stamps_across_the_microsecond_wrap);
}
