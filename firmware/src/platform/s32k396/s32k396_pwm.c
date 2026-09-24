/* s32k396_pwm.c — hal/pwm.h on eFlexPWM_1 (FW-05, FW-06, FW-15, §4c).
 * Hardware first: the three FAULT inputs are set once here, in fail-safe + manual-clear mode, and
 * write-protected; software only confirms (hal_pwm_config_locked) and clears FFLAG in FW-15 step 4.
 * RTD: FlexPwm_Ip_Init() applies the generated submodule configuration; the lock-down registers
 * below are then written directly and read back. */
#include <math.h>

#include "pwm.h"
#include "s32k396.h"

#ifdef TI_RTD_AVAILABLE
#include "FlexPwm_Ip.h" /* TODO(RTD): eFlexPWM IP driver of the S32K39 RTD */
extern const FlexPwm_Ip_UserCfgType FlexPwm_Ip_UserCfg_1; /* TODO(RTD): Config Tools symbol */
#endif

#define MASK_UPDATE_NOW ((uint16_t)(0x7u << 12)) /* MASK.UPDATE_MASK[2:0]: apply at once */

static pwm_counts_t s_cnt;
static hal_pwm_mode_t s_mode = HAL_PWM_OFF;
static bool s_locked;

static void force_now(void)
{
    /* SM1/SM2 take FORCE_SEL = master (SM0): one write updates all three pairs together */
    PWM_SM(0u, CTRL2) = (uint16_t)(PWM_SM(0u, CTRL2) | PWM_CTRL2_FORCE);
}

static void apply_mode(hal_pwm_mode_t m)
{
    const pwm_mode_img_t img = pwm_mode_image((uint32_t)m);
    if (m == HAL_PWM_OFF) {
        PWM_R(MASK) = (uint16_t)(img.mask | MASK_UPDATE_NOW); /* immediate, no FORCE needed */
    }
    PWM_R(DTSRCSEL) = img.dtsrcsel;
    PWM_R(SWCOUT) = img.swcout;
    PWM_R(MASK) = img.mask;
    force_now();
    s_mode = m;
}

static void lock_faults(void)
{
    PWM_R(FFILT) = PWM_FFILT_VALUE;
    PWM_R(FCTRL2) = PWM_FCTRL2_VALUE;
    PWM_R(FCTRL) = pwm_fault_fctrl();
    PWM_R(FSTS) = (uint16_t)(pwm_fsts_reenable_full() | PWM_FAULT_MASK3); /* w1c stale flags */
    for (uint32_t n = 0u; n < PWM_NSUB; n++) {
        PWM_SM(n, DISMAP0) = pwm_fault_dismap0();
        PWM_SM(n, DISMAP1) = 0u;
        PWM_SM(n, OCTRL) = PWM_OCTRL_FAULT_LOW;
    }
    /* SIUL2 input mux: PTC26 -> FAULT0, PTC25 -> FAULT2 (input buffer on, output buffer off) */
    SIUL2_MSCR(BP_FLT_HS_N_MSCR) = TI_MSCR_IBE;
    SIUL2_MSCR(BP_FLT_LS_N_MSCR) = TI_MSCR_IBE;
    SIUL2_IMCR(TI_IMCR_PWM1_FAULT0) = TI_IMCR_SSS_PTC26;
    SIUL2_IMCR(TI_IMCR_PWM1_FAULT2) = TI_IMCR_SSS_PTC25;
    /* Write protection. TODO(RTD/HW-RM): set the REG_PROT soft-lock bits (SLBRn) of eFlexPWM_1 for
     * FCTRL, FCTRL2, FFILT, DISMAP0/1, OCTRL and CTRL2 of SM0..2, and of the two IMCRs, then the
     * REG_PROT hard lock (GCR.HLB) so they stay read-only until reset. If REG_PROT does not cover
     * these instances on the S32K39, make them read-only for the application domain through XRDC.
     * lock_readback() confirms the image either way (hal_pwm_config_locked). */
}

static bool lock_readback(void)
{
    bool ok = (PWM_R(FCTRL) == pwm_fault_fctrl()) && (PWM_R(FCTRL2) == PWM_FCTRL2_VALUE) &&
              (PWM_R(FFILT) == PWM_FFILT_VALUE);
    for (uint32_t n = 0u; n < PWM_NSUB; n++) {
        ok = ok && (PWM_SM(n, DISMAP0) == pwm_fault_dismap0()) && (PWM_SM(n, OCTRL) == PWM_OCTRL_FAULT_LOW) &&
             (PWM_SM(n, CTRL2) & (1u << 13)) == 0u; /* INDEP stays 0 */
    }
    return ok;
}

bool hal_pwm_init(uint32_t fsw_hz, uint32_t dead_time_ns)
{
    s_locked = false;
    s_cnt = pwm_counts(TI_PWM_CLK_HZ, fsw_hz, dead_time_ns);
    if (!s_cnt.ok) {
        return false;
    }
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): FlexPwm_Ip_Init(TI_PWM_INST, &FlexPwm_Ip_UserCfg_1) — complementary pairs,
     * centre-aligned signed counter, FULL + HALF reload, SM1/SM2 clocked, reloaded and forced from
     * SM0, TCTRL triggers on VAL0/VAL1 to the BCTU (via TRGMUX). */
    if (FlexPwm_Ip_Init(TI_PWM_INST, &FlexPwm_Ip_UserCfg_1) != FLEXPWM_IP_STATUS_SUCCESS) {
        return false;
    }
#endif
    PWM_R(MCTRL) = 0u; /* stopped while the counts are written */
    for (uint32_t n = 0u; n < PWM_NSUB; n++) {
        PWM_SM(n, INIT) = (uint16_t)s_cnt.init;
        PWM_SM(n, VAL0) = 0u;
        PWM_SM(n, VAL1) = (uint16_t)s_cnt.val1;
        PWM_SM(n, VAL2) = s_cnt.half; /* 0 % until the first duty write */
        PWM_SM(n, VAL3) = s_cnt.half;
        PWM_SM(n, DTCNT0) = s_cnt.dtcnt;
        PWM_SM(n, DTCNT1) = s_cnt.dtcnt;
        PWM_SM(n, CTRL) = (uint16_t)(PWM_CTRL_HALF | PWM_CTRL_FULL);
        PWM_SM(n, CTRL2) = pwm_ctrl2(n); /* INDEP 0: complementary */
        PWM_SM(n, TCTRL) = (n == 0u) ? (uint16_t)PWM_TCTRL_TRIG_VAL0_VAL1 : 0u;
    }
    lock_faults();
    apply_mode(HAL_PWM_OFF);
    PWM_R(OUTEN) = pwm_ab(PWM_FAULT_MASK3, PWM_FAULT_MASK3);
    PWM_R(MCTRL) = (uint16_t)(PWM_MCTRL_RUN3 | PWM_MCTRL_LDOK3);
    s_locked = lock_readback();
    return s_locked;
}

bool hal_pwm_config_locked(void) { return s_locked && lock_readback(); }

void hal_pwm_force_off(void) { apply_mode(HAL_PWM_OFF); }

void hal_pwm_set_asc(void) { apply_mode(HAL_PWM_ASC); }

void hal_pwm_set_duty(const float duty[3])
{
    int16_t v2[3];
    int16_t v3[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        if (!isfinite(duty[i]) || (duty[i] < 0.0f) || (duty[i] > 1.0f)) {
            return; /* the whole write is refused; the previous edges stay */
        }
        pwm_edges(duty[i], s_cnt.half, &v2[i], &v3[i]);
    }
    for (uint32_t i = 0u; i < 3u; i++) {
        PWM_SM(i, VAL2) = (uint16_t)v2[i];
        PWM_SM(i, VAL3) = (uint16_t)v3[i];
    }
    PWM_R(MCTRL) = (uint16_t)(PWM_R(MCTRL) | PWM_MCTRL_LDOK3); /* loaded at the next half/full reload */
    if (s_mode != HAL_PWM_MOD) {
        apply_mode(HAL_PWM_MOD);
    }
}

hal_pwm_mode_t hal_pwm_mode(void) { return s_mode; }

/* hal/pwm.h bit order: FLT_HS = FAULT0, ADC_WD = FAULT1, FLT_LS = FAULT2 (same bit positions). */
uint8_t hal_pwm_fault_flags(void) { return (uint8_t)(pwm_fsts_fflag(PWM_R(FSTS)) & HAL_PWM_FAULT_ALL); }

uint8_t hal_pwm_fault_inputs(void)
{
    /* FFPIN shows the raw pin level; FAULT0/2 are active low, FAULT1 active high */
    const uint8_t pin = (uint8_t)pwm_fsts_ffpin(PWM_R(FSTS));
    return (uint8_t)((((uint8_t)~pin) & (HAL_PWM_FAULT_FLT_HS | HAL_PWM_FAULT_FLT_LS)) | (pin & HAL_PWM_FAULT_ADC_WD));
}

uint8_t hal_pwm_fault_clear(uint8_t mask)
{
    const uint8_t clearable = (uint8_t)(mask & (uint8_t)~hal_pwm_fault_inputs() & HAL_PWM_FAULT_ALL);
    PWM_R(FSTS) = (uint16_t)(pwm_fsts_reenable_full() | clearable); /* w1c FFLAG */
    return hal_pwm_fault_flags();
}
