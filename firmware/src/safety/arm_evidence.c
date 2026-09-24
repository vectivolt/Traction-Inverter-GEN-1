/* arm_evidence.c — see arm_evidence.h. */
#include "arm_evidence.h"

#include <stddef.h>
#include <string.h>

#include "pwm.h"
#include "ti_crc.h"

void arm_validation_seal(arm_validation_t *r) { r->crc32 = ti_crc32(r, offsetof(arm_validation_t, crc32)); }

void arm_validation_make(arm_validation_t *r, const uint8_t serial[8], ti_sku_t sku, uint32_t fw_id, uint8_t flags,
                         uint32_t ovp_chain_ns)
{
    (void)memset(r, 0, sizeof *r);
    r->magic = ARM_VAL_MAGIC;
    r->layout = ARM_VAL_LAYOUT;
    r->sku = (uint8_t)sku;
    r->flags = (uint8_t)(flags & ARM_EV_VALIDATED);
    (void)memcpy(r->hw_serial, serial, 8u);
    r->fw_id = fw_id;
    r->ovp_chain_ns = ovp_chain_ns;
    arm_validation_seal(r);
}

uint8_t arm_validation_flags(const arm_validation_t *r, bool present, const uint8_t serial[8], ti_sku_t sku,
                             uint32_t fw_id, const ti_params_t *p)
{
    if (!present || (r == NULL)) {
        return 0u;
    }
    const bool intact = (r->magic == ARM_VAL_MAGIC) && (r->layout == ARM_VAL_LAYOUT) &&
                        (r->crc32 == ti_crc32(r, offsetof(arm_validation_t, crc32)));
    const bool ours = (r->sku == (uint8_t)sku) && (r->fw_id == fw_id) && (memcmp(r->hw_serial, serial, 8u) == 0);
    if (!intact || !ours) {
        return 0u;
    }
    uint8_t f = (uint8_t)(r->flags & ARM_EV_VALIDATED);
    const float chain_us = (float)r->ovp_chain_ns * 1.0e-3f;
    if ((r->ovp_chain_ns == 0u) || (chain_us > p->fw06_budget_us)) {
        f = (uint8_t)(f & (uint8_t)~ARM_EV_OVP_ROUTE_VALIDATED); /* flag without a measurement in budget */
    }
    return f;
}

uint8_t arm_evidence_platform(void)
{
    return (uint8_t)((hal_pwm_fault_route_bound() ? ARM_EV_ROUTE_BOUND : 0u) |
                     (hal_pwm_config_matches() ? ARM_EV_CONFIG_MATCHES : 0u) |
                     (hal_pwm_protection_locked() ? ARM_EV_PROTECTION_LOCKED : 0u));
}
