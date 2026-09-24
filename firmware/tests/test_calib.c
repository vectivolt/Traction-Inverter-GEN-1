/* test_calib.c — FW-20: version, CRC, ranges, SKU / serial / motor binding. */
#include <string.h>

#include "arm_evidence.h"
#include "calib.h"
#include "test.h"

static const uint8_t SN[8] = {'T', 'I', '-', '0', '0', '0', '0', '1'};

static void good(calib_t *c, const ti_params_t *p)
{
    calib_nominal(c, p, SN);
    c->motor_id = 42u;
    calib_seal(c);
}

TEST(nominal_needs_a_motor)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    calib_t c;
    calib_nominal(&c, p, SN);
    CHECK(calib_check(&c, p, SN) == CAL_ERR_MOTOR_ID); /* no torque until commissioning binds a motor */
    good(&c, p);
    CHECK(calib_check(&c, p, SN) == 0u);
}

TEST(each_failure_detected)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    calib_t c;
    good(&c, p);
    c.isns[1].offset_v = 3.0f; /* range, but not resealed: CRC too */
    CHECK((calib_check(&c, p, SN) & (CAL_ERR_CRC | CAL_ERR_RANGE)) == (CAL_ERR_CRC | CAL_ERR_RANGE));
    calib_seal(&c);
    CHECK(calib_check(&c, p, SN) == CAL_ERR_RANGE);
    good(&c, p);
    c.layout_version = 99u;
    calib_seal(&c);
    CHECK(calib_check(&c, p, SN) == CAL_ERR_VERSION);
    good(&c, p);
    CHECK(calib_check(&c, ti_params_get(TI_SKU_4XX_SIC), SN) & CAL_ERR_SKU);
    const uint8_t other[8] = {'T', 'I', '-', '0', '0', '0', '0', '2'};
    CHECK(calib_check(&c, p, other) == CAL_ERR_SERIAL);
    good(&c, p);
    c.fsw_hz = 5000u; /* not a SiC switching mode */
    calib_seal(&c);
    CHECK(calib_check(&c, p, SN) == CAL_ERR_FSW);
    good(&c, p);
    c.rslv.motor_pp = 5u; /* motor pp 4 in the motor block: inconsistent */
    calib_seal(&c);
    CHECK(calib_check(&c, p, SN) == CAL_ERR_RANGE);
    good(&c, p);
    c.isns[0].sign = 0;
    calib_seal(&c);
    CHECK(calib_check(&c, p, SN) == CAL_ERR_RANGE);
    good(&c, p);
    ((uint8_t *)&c)[40] ^= 0x10u; /* a flipped bit anywhere */
    CHECK(calib_check(&c, p, SN) & CAL_ERR_CRC);
    CHECK(calib_check(NULL, p, SN) == CAL_ERR_MISSING);
}

/* F06: the EOL/HIL validation record proves its two items only for this image, card and SKU, intact,
 * with the FW-06 chain measured inside its budget. */
TEST(validation_record_binds_image_card_and_crc)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const uint32_t fw = 0x0A0C000Eu;
    arm_validation_t v;
    arm_validation_make(&v, SN, p->sku, fw, ARM_EV_VALIDATED, 14200u);
    CHECK(arm_validation_flags(&v, true, SN, p->sku, fw, p) == ARM_EV_VALIDATED);
    CHECK(arm_validation_flags(&v, false, SN, p->sku, fw, p) == 0u); /* no record */
    CHECK(arm_validation_flags(&v, true, SN, p->sku, fw + 1u, p) == 0u);
    CHECK(arm_validation_flags(&v, true, SN, TI_SKU_4XX_SIC, fw, p) == 0u);
    const uint8_t other[8] = {'T', 'I', '-', '0', '0', '0', '0', '2'};
    CHECK(arm_validation_flags(&v, true, other, p->sku, fw, p) == 0u);
    arm_validation_t c = v;
    c.flags = ARM_EV_ALL; /* not resealed */
    CHECK(arm_validation_flags(&c, true, SN, p->sku, fw, p) == 0u);
    arm_validation_make(&c, SN, p->sku, fw, ARM_EV_VALIDATED, 15700u); /* 15.7 us > 15.6 us */
    CHECK(arm_validation_flags(&c, true, SN, p->sku, fw, p) == ARM_EV_FAULT_ROUTE_VALIDATED);
    arm_validation_make(&c, SN, p->sku, fw, ARM_EV_VALIDATED, 0u); /* flag without a measurement */
    CHECK(arm_validation_flags(&c, true, SN, p->sku, fw, p) == ARM_EV_FAULT_ROUTE_VALIDATED);
    arm_validation_make(&c, SN, p->sku, fw, ARM_EV_ALL, 14200u); /* platform bits cannot be stored */
    CHECK(arm_validation_flags(&c, true, SN, p->sku, fw, p) == ARM_EV_VALIDATED);
}

void suite_calib(void)
{
    RUN(nominal_needs_a_motor);
    RUN(each_failure_detected);
    RUN(validation_record_binds_image_card_and_crc);
}
