/* hwid.c — FW-01 / FW-02. */
#include "hwid.h"

#include "ti_math.h"

hwid_result_t hwid_classify(uint16_t code, ti_sku_t *sku)
{
    const float v = ti_code_to_v(code);
    const ti_params_t *any = ti_params_get(TI_SKU_8XX_SIC);
    *sku = TI_SKU_NONE;
    if (v > any->hwid_open_v) {
        return HWID_OPEN;
    }
    if (v < any->hwid_short_v) {
        return HWID_SHORT;
    }
    for (uint32_t s = (uint32_t)TI_SKU_8XX_SIC; s < (uint32_t)TI_SKU_COUNT; s++) {
        const ti_params_t *p = ti_params_get((ti_sku_t)s);
        const float nom = TI_ADC_VREF_V * p->hwid_ratio_nom;
        if (ti_absf(v - nom) <= (p->hwid_window_frac * nom)) {
            *sku = p->sku;
            return HWID_OK;
        }
    }
    return HWID_UNKNOWN;
}

hwid_result_t hwid_classify_stable(const uint16_t *codes, uint32_t n, ti_sku_t *sku)
{
    ti_sku_t first = TI_SKU_NONE;
    hwid_result_t r0 = HWID_UNKNOWN;
    *sku = TI_SKU_NONE;
    for (uint32_t i = 0u; i < n; i++) {
        ti_sku_t s;
        const hwid_result_t r = hwid_classify(codes[i], &s);
        if (i == 0u) {
            first = s;
            r0 = r;
        } else if ((r != r0) || (s != first)) {
            return HWID_UNSTABLE;
        } else {
            /* consistent */
        }
    }
    *sku = first;
    return (n == 0u) ? HWID_UNKNOWN : r0;
}

bool hwid_identity_ok(ti_sku_t hw, ti_sku_t params_sku, ti_sku_t calib_sku)
{
    return (hw != TI_SKU_NONE) && (hw == params_sku) && (hw == calib_sku);
}
