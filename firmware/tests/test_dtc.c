/* test_dtc.c — UDS status bits and occurrence counting. */
#include "dtc.h"
#include "test.h"

TEST(status_bits_and_occurrences)
{
    dtc_init();
    CHECK(!dtc_active(DTC_V5GD));
    CHECK((dtc_status(DTC_V5GD) & (DTC_TNCSLC | DTC_TNCTOC)) == (DTC_TNCSLC | DTC_TNCTOC));
    dtc_set(DTC_V5GD, 10u);
    dtc_set(DTC_V5GD, 11u); /* still failed: not a new occurrence */
    CHECK(dtc_active(DTC_V5GD) && dtc_occurrences(DTC_V5GD) == 1u);
    CHECK((dtc_status(DTC_V5GD) & (DTC_TF | DTC_CDTC | DTC_TFSLC | DTC_TFTOC)) == (DTC_TF | DTC_CDTC | DTC_TFSLC | DTC_TFTOC));
    dtc_pass(DTC_V5GD);
    CHECK(!dtc_active(DTC_V5GD) && (dtc_status(DTC_V5GD) & DTC_CDTC) != 0u);
    dtc_set(DTC_V5GD, 20u);
    CHECK(dtc_occurrences(DTC_V5GD) == 2u);
    dtc_new_cycle();
    CHECK((dtc_status(DTC_V5GD) & DTC_TFTOC) == 0u);
    CHECK(dtc_confirmed_count() == 1u && dtc_first_active() == DTC_V5GD);
    CHECK(dtc_code(DTC_V5GD) == (0xD10000u | (uint32_t)DTC_V5GD));
    CHECK(dtc_take_dirty() && !dtc_take_dirty());
    dtc_clear_all();
    CHECK(dtc_confirmed_count() == 0u && !dtc_active(DTC_V5GD));
    dtc_set(DTC_NONE, 1u);
    dtc_set(DTC_COUNT, 1u);
    CHECK(dtc_confirmed_count() == 0u);
}

void suite_dtc(void) { RUN(status_bits_and_occurrences); }
