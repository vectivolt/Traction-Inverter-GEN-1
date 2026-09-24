/* s32k396_pwm.c — hal/pwm.h on eFlexPWM_1 (FW-05, FW-06, FW-15, §4c).
 * Hardware first: the three FAULT inputs are set once here, in fail-safe + manual-clear mode, and
 * write-protected; software only confirms and clears FFLAG in FW-15 step 4. Three separate pieces of
 * arming evidence come from here (F01/F02, round 14), none assumed:
 *   hal_pwm_fault_route_bound() : the board configuration binds PTC26/PTC25 to FAULT0/FAULT2 and the
 *                                 two IMCRs read back so (placeholders cannot build for the target);
 *   hal_pwm_config_matches()    : the lock-down image reads back;
 *   hal_pwm_protection_locked() : the REG_PROT soft-lock bits and the hard lock read back set.
 * RTD: FlexPwm_Ip_Init() applies the generated submodule configuration; the lock-down registers
 * below are then written directly, locked and read back. */
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
static bool s_cfg_ok;

#define PROT_MAX (3u + (3u * PWM_NSUB))

/* The registers REG_PROT must lock (the fault lock-down image), as module offsets. CTRL2 is not one
 * of them: every mode change writes its FORCE bit (force_now), and a soft-locked CTRL2 would stop
 * PWM-ASC entry. Its INDEP bit is read back instead (lock_readback, every 1 ms task through the
 * arming-evidence watch). TODO(RM): if REG_PROT locks per byte and eFlexPWM takes byte writes, lock
 * the upper byte (INDEP, INIT_SEL, DBGEN) and write FORCE as a byte. */
static uint32_t prot_regs(regprot_reg_t r[PROT_MAX])
{
    uint32_t n = 0u;
    r[n++] = (regprot_reg_t){(uint32_t)((uintptr_t)&PWM_R(FCTRL) - TI_PWM_BASE), 2u};
    r[n++] = (regprot_reg_t){(uint32_t)((uintptr_t)&PWM_R(FCTRL2) - TI_PWM_BASE), 2u};
    r[n++] = (regprot_reg_t){(uint32_t)((uintptr_t)&PWM_R(FFILT) - TI_PWM_BASE), 2u};
    for (uint32_t k = 0u; k < PWM_NSUB; k++) {
        r[n++] = (regprot_reg_t){(uint32_t)((uintptr_t)&PWM_SM(k, DISMAP0) - TI_PWM_BASE), 2u};
        r[n++] = (regprot_reg_t){(uint32_t)((uintptr_t)&PWM_SM(k, DISMAP1) - TI_PWM_BASE), 2u};
        r[n++] = (regprot_reg_t){(uint32_t)((uintptr_t)&PWM_SM(k, OCTRL) - TI_PWM_BASE), 2u};
    }
    return n;
}

/* Sets the soft locks of the lock-down registers, then the hard lock (read-only until reset).
 * TODO(RM): confirm REG_PROT covers eFlexPWM_1 on the S32K39; otherwise make these registers
 * read-only for the application domain through XRDC (and make hal_pwm_protection_locked() read
 * that instead). TODO(RM): the two IMCRs through the SIUL2 protection the same way. */
static void prot_lock(void)
{
#ifdef TI_RTD_AVAILABLE
    regprot_reg_t r[PROT_MAX];
    const uint32_t n = prot_regs(r);
    for (uint32_t i = 0u; i < n; i++) {
        REGPROT_U8(TI_PWM_BASE, TI_REGPROT_SLBR_OFS + regprot_slbr_index(r[i].ofs)) =
            regprot_slbr_lock_value(r[i].ofs, r[i].width);
    }
    REGPROT_U32(TI_PWM_BASE, TI_REGPROT_GCR_OFS) |= TI_REGPROT_GCR_HLB;
#endif
}

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
    /* SIUL2 input mux: PTC26 -> FAULT0, PTC25 -> FAULT2 (input buffer on, output buffer off). An
     * unbound board configuration writes no IMCR at all (never IMCR[0] = 0: another input's mux). */
    SIUL2_MSCR(BP_FLT_HS_N_MSCR) = TI_MSCR_IBE;
    SIUL2_MSCR(BP_FLT_LS_N_MSCR) = TI_MSCR_IBE;
#if TI_IMCR_ROUTE_BOUND
    SIUL2_IMCR(TI_IMCR_PWM1_FAULT0) = TI_IMCR_SSS_PTC26;
    SIUL2_IMCR(TI_IMCR_PWM1_FAULT2) = TI_IMCR_SSS_PTC25;
#endif
    prot_lock();
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
    s_cfg_ok = false;
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
    s_cfg_ok = lock_readback();
    return s_cfg_ok;
}

bool hal_pwm_config_matches(void) { return s_cfg_ok && lock_readback(); }

bool hal_pwm_protection_locked(void)
{
    regprot_reg_t r[PROT_MAX];
    const uint32_t n = prot_regs(r);
    uint8_t slbr[PROT_MAX] = {0u};
    uint32_t gcr = 0u; /* nothing read back => not locked */
#ifdef TI_RTD_AVAILABLE
    for (uint32_t i = 0u; i < n; i++) {
        slbr[i] = REGPROT_U8(TI_PWM_BASE, TI_REGPROT_SLBR_OFS + regprot_slbr_index(r[i].ofs));
    }
    gcr = REGPROT_U32(TI_PWM_BASE, TI_REGPROT_GCR_OFS);
#endif
    return regprot_locked(gcr, slbr, r, n);
}

bool hal_pwm_fault_route_bound(void)
{
#if TI_IMCR_ROUTE_BOUND
    return ((SIUL2_IMCR(TI_IMCR_PWM1_FAULT0) & TI_IMCR_SSS_MASK) == TI_IMCR_SSS_PTC26) &&
           ((SIUL2_IMCR(TI_IMCR_PWM1_FAULT2) & TI_IMCR_SSS_MASK) == TI_IMCR_SSS_PTC25);
#else
    return false; /* UNBOUND: s32k396_board_cfg.h is still the TODO(RM) placeholder */
#endif
}

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
