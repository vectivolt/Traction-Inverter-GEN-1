/* hwid.h — SKU identity (FW-01) and the parameter-set / calibration binding (FW-02).
 * HW_ID (harness pin 2, C12 ADC3_P6): card 10 k pull-up to VREF5 against the power board's RHWID,
 * ratiometric with the ADC reference. Windows +/-4 % around 0.90 / 1.60 / 2.50 / 3.44 V; open
 * > 4.6 V, short < 0.2 V => fault, no DRV_EN. */
#ifndef HWID_H
#define HWID_H

#include "ti_params.h"

typedef enum { HWID_OK = 0, HWID_OPEN, HWID_SHORT, HWID_UNKNOWN, HWID_UNSTABLE } hwid_result_t;

hwid_result_t hwid_classify(uint16_t code, ti_sku_t *sku);
/* Every sample must classify to the same SKU (a stable reading before arming). */
hwid_result_t hwid_classify_stable(const uint16_t *codes, uint32_t n, ti_sku_t *sku);
/* FW-02: hardware identity, loaded parameter set and calibration record must agree. */
bool hwid_identity_ok(ti_sku_t hw, ti_sku_t params_sku, ti_sku_t calib_sku);

#endif /* HWID_H */
