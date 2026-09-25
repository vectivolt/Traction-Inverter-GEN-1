/* arm_evidence.h — the arming-evidence record (F01/F02/F06, round 14). FAIL CLOSED: gate enable is
 * refused — the state machine never leaves the inhibited state, FW-16 never energises a gate —
 * unless every item is present:
 *   ROUTE_BOUND            the board configuration binds FLT_HS_N/FLT_LS_N to eFlexPWM_1
 *                          FAULT0/FAULT2 and the IMCRs read back so (hal_pwm_fault_route_bound);
 *   CONFIG_MATCHES         the fault lock-down image reads back, CTRL2.INDEP included
 *                          (hal_pwm_config_matches);
 *   PROTECTION_LOCKED      the REG_PROT lock bits read back set (hal_pwm_protection_locked);
 *   (the platform items are read back again every 1 ms task: a loss while armed is "control lost")
 *   FAULT_ROUTE_VALIDATED  EOL/HIL: driving each FLT pad low reached the PWM fault input;
 *   OVP_ROUTE_VALIDATED    EOL/HIL: ADC watchdog -> PWM fault -> ASC_REQ measured within the FW-06
 *                          budget (15.6 us).
 * The last two come from a validation record in NVM (NV_REC_VALIDATION), sealed with a CRC-32 and
 * bound to the firmware identity, the SKU and the hardware serial: a record written for another
 * image or another card, or a corrupted one, validates nothing. The CAN status reports the missing
 * items (can_cmd.h). TODO(EOL): the EOL/HIL rig that injects FLT at the pads, measures the FW-06 chain and
 * writes this record for each TI_FW_ID is not in this repository (arm_validation_make/_seal are its side). */
#ifndef ARM_EVIDENCE_H
#define ARM_EVIDENCE_H

#include "ti_params.h"

#define ARM_EV_ROUTE_BOUND 0x01u
#define ARM_EV_CONFIG_MATCHES 0x02u
#define ARM_EV_PROTECTION_LOCKED 0x04u
#define ARM_EV_FAULT_ROUTE_VALIDATED 0x08u
#define ARM_EV_OVP_ROUTE_VALIDATED 0x10u
#define ARM_EV_ALL 0x1Fu
#define ARM_EV_VALIDATED (ARM_EV_FAULT_ROUTE_VALIDATED | ARM_EV_OVP_ROUTE_VALIDATED)

#define ARM_VAL_MAGIC 0x45564944u /* "EVID" */
#define ARM_VAL_LAYOUT 1u

typedef struct {
    uint32_t magic;
    uint16_t layout;
    uint8_t sku;
    uint8_t flags;          /* ARM_EV_FAULT_ROUTE_VALIDATED | ARM_EV_OVP_ROUTE_VALIDATED */
    uint8_t hw_serial[8];   /* the card (the FW-20 serial: device UID) */
    uint32_t fw_id;         /* the image the rig validated */
    uint32_t ovp_chain_ns;  /* the measured FW-06 chain: trip crossing -> ASC_REQ edge */
    uint32_t crc32;         /* CRC-32 over every byte above */
} arm_validation_t;

/* EOL/HIL rig side (and tests): fills and seals a record. */
void arm_validation_make(arm_validation_t *r, const uint8_t serial[8], ti_sku_t sku, uint32_t fw_id, uint8_t flags,
                         uint32_t ovp_chain_ns);
void arm_validation_seal(arm_validation_t *r);
/* The validated items the record proves for this image and card: 0 if absent, corrupt, foreign,
 * or (OVP) the measured chain is missing or outside the FW-06 budget. */
uint8_t arm_validation_flags(const arm_validation_t *r, bool present, const uint8_t serial[8], ti_sku_t sku,
                             uint32_t fw_id, const ti_params_t *p);
/* The three platform items as read now. */
uint8_t arm_evidence_platform(void);

#endif /* ARM_EVIDENCE_H */
