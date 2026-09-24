/* hal/pwm.h — eFlexPWM_1, submodules 0..2: A = high side (U/V/W), B = low side, complementary
 * with hardware dead time. Fault inputs (locked at init, fail-safe + manual clear, write-protected):
 *   FAULT0 = FLT_HS_N pin  -> all six outputs  (FW-15)
 *   FAULT1 = ADC analog watchdog via TRGMUX (phase OC FW-05 + V_DC OV FW-06) -> high sides only (§4c)
 *   FAULT2 = FLT_LS_N pin  -> all six outputs  (FW-15)
 * Duty = high-side on fraction of the period, 0..1. */
#ifndef HAL_PWM_H
#define HAL_PWM_H

#include "ti_types.h"

#define HAL_PWM_FAULT_FLT_HS 0x1u
#define HAL_PWM_FAULT_ADC_WD 0x2u
#define HAL_PWM_FAULT_FLT_LS 0x4u
#define HAL_PWM_FAULT_ALL 0x7u

typedef enum {
    HAL_PWM_OFF = 0, /* all six forced low by software (mask + force) */
    HAL_PWM_ASC,     /* PWM-ASC: low sides 100 % on, high sides off (§4c step 3) */
    HAL_PWM_MOD      /* complementary modulation */
} hal_pwm_mode_t;

/* Configure period (centre-aligned, double update: reload at half and full cycle), dead time,
 * the fault map above, the FLT pad routing (only when bound) and the write protection. Outputs
 * start OFF. Returns hal_pwm_config_matches(). */
bool hal_pwm_init(uint32_t fsw_hz, uint32_t dead_time_ns);

/* Three separate pieces of arming evidence (safety/arm_evidence.h); none is ever assumed:
 *  - route bound: the board configuration gives the SIUL2 IMCR routing of FLT_HS_N/FLT_LS_N to
 *    FAULT0/FAULT2 (distinct, non-zero values) and those IMCRs read back so. Placeholders are a
 *    build error on the target; the host build is UNBOUND (false) unless a test binds it;
 *  - config matches: the fault lock-down image reads back (FCTRL, FCTRL2, FFILT, DISMAP0/1, OCTRL,
 *    CTRL2 of SM0..2). A matching image is NOT a locked one;
 *  - protection locked: the REG_PROT soft-lock bits of those registers (CTRL2 excepted: its FORCE
 *    bit is written at every mode change, so its INDEP bit is only read back) and the hard lock
 *    read back set. False whenever that cannot be read, never true by default. */
bool hal_pwm_fault_route_bound(void);
bool hal_pwm_config_matches(void);
bool hal_pwm_protection_locked(void);

void hal_pwm_force_off(void);
void hal_pwm_set_asc(void);
void hal_pwm_set_duty(const float duty[3]);
hal_pwm_mode_t hal_pwm_mode(void);

/* Fault flags (FFLAG, latched until cleared) and the live fault input levels. */
uint8_t hal_pwm_fault_flags(void);
uint8_t hal_pwm_fault_inputs(void);

/* Manual clear: clears only flags whose input is inactive; returns the flags still set. */
uint8_t hal_pwm_fault_clear(uint8_t mask);

#endif /* HAL_PWM_H */
