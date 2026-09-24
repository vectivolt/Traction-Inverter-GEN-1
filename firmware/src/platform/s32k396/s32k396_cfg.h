/* s32k396_cfg.h — pure register images and index maps of the S32K396 platform layer.
 * No hardware access: s32k396_*.c write these values, tests/test_platform_cfg.c checks them on
 * the host. Bit positions are those of the NXP eFlexPWM / ADC_SAR IP as documented in the
 * S32K39 reference manual. TODO(HW-RM): re-check each field against the RM revision in use.
 *
 * eFlexPWM_1 use (hal/pwm.h): submodules 0..2, A = high side, B = low side, complementary.
 *   FAULT0 = FLT_HS_N (PTC26, active low)  -> A and B of all three submodules
 *   FAULT1 = ADC analog watchdog via TRGMUX/LCU (active high) -> A only (§4c: high sides off)
 *   FAULT2 = FLT_LS_N (PTC25, active low)  -> A and B
 *   all three: fail-safe (FSAFE = 1), manual clear (FAUTO = 0), interrupt enabled (FIE = 1),
 *   combinational path kept (FCTRL2.NOCOMB = 0), no input filter (FFILT = 0: see FFILT_VALUE). */
#ifndef S32K396_CFG_H
#define S32K396_CFG_H

#include <stdbool.h>
#include <stdint.h>

/* ---------------- eFlexPWM field layout (16-bit registers) ---------------- */
#define PWM_NSUB 3u
#define PWM_FAULT_MASK3 0x7u /* FAULT0..2 */
#define PWM_F0 0x1u
#define PWM_F1 0x2u
#define PWM_F2 0x4u

/* FCTRL: FIE[3:0] | FSAFE[7:4] | FAUTO[11:8] | FLVL[15:12] */
static inline uint16_t pwm_fctrl(uint16_t fie, uint16_t fsafe, uint16_t fauto, uint16_t flvl)
{
    return (uint16_t)((fie & 0xFu) | ((fsafe & 0xFu) << 4) | ((fauto & 0xFu) << 8) | ((flvl & 0xFu) << 12));
}
/* FSTS: FFLAG[3:0] (w1c) | FFULL[7:4] | FFPIN[11:8] | FHALF[15:12] */
static inline uint16_t pwm_fsts_fflag(uint16_t fsts) { return (uint16_t)(fsts & 0xFu); }
static inline uint16_t pwm_fsts_ffpin(uint16_t fsts) { return (uint16_t)((fsts >> 8) & 0xFu); }
/* FSTS FFULL/FHALF: 1 = outputs re-enable at a full / half cycle boundary after a manual clear */
static inline uint16_t pwm_fsts_reenable_full(void) { return (uint16_t)(PWM_FAULT_MASK3 << 4); }
/* DISMAP0 (per submodule): DIS0A[3:0] | DIS0B[7:4] | DIS0X[11:8] */
static inline uint16_t pwm_dismap0(uint16_t dis_a, uint16_t dis_b)
{
    return (uint16_t)((dis_a & 0xFu) | ((dis_b & 0xFu) << 4));
}
/* OUTEN: PWMX_EN[3:0] | PWMB_EN[7:4] | PWMA_EN[11:8]; MASK: MASKX[3:0] | MASKB[7:4] | MASKA[11:8] */
static inline uint16_t pwm_ab(uint16_t a, uint16_t b) { return (uint16_t)(((b & 0xFu) << 4) | ((a & 0xFu) << 8)); }
/* DTSRCSEL: SMnSEL45[4n+1:4n] (B source) | SMnSEL23[4n+3:4n+2] (A source); 0 = PWM generator,
 * 2 = SWCOUT. SWCOUT: SMnOUT45 = bit 2n, SMnOUT23 = bit 2n+1. */
#define PWM_SRC_GEN 0u
#define PWM_SRC_SWCOUT 2u
static inline uint16_t pwm_dtsrcsel_all(uint16_t sel23)
{
    uint16_t v = 0u;
    for (uint32_t n = 0u; n < PWM_NSUB; n++) {
        v = (uint16_t)(v | ((sel23 & 0x3u) << ((4u * n) + 2u)));
    }
    return v;
}
/* OCTRL: PWMAFS[5:4], PWMBFS[3:2] fault state: 00 = logic 0 */
#define PWM_OCTRL_FAULT_LOW 0x0000u
/* CTRL2: CLK_SEL[1:0] (2 = master clock from SM0), RELOAD_SEL bit 2 (1 = master reload),
 * FORCE_SEL[5:3] (1 = master FORCE from SM0), FORCE bit 6, FRCEN bit 7 (0: FORCE does not
 * re-initialise the counter), INIT_SEL[9:8] (2 = master sync), INDEP bit 13 (0 = complementary),
 * DBGEN bit 15 (0 = outputs stop in debug). */
#define PWM_CTRL2_FORCE (1u << 6)
static inline uint16_t pwm_ctrl2(uint32_t sub)
{
    return (sub == 0u) ? 0u : (uint16_t)(2u | (1u << 2) | (1u << 3) | (2u << 8)); /* SM1/SM2 slaved to SM0 */
}
/* CTRL: HALF bit 11 and FULL bit 10 (double update: reload at VAL0 and at VAL1) */
#define PWM_CTRL_HALF (1u << 11)
#define PWM_CTRL_FULL (1u << 10)
/* MCTRL: LDOK[3:0], CLDOK[7:4], RUN[11:8] */
#define PWM_MCTRL_LDOK3 0x0007u
#define PWM_MCTRL_RUN3 0x0700u
/* TCTRL: OUT_TRIG_EN[5:0]: bit0 = VAL0 match (counter 0, centre), bit1 = VAL1 match (end) */
#define PWM_TCTRL_TRIG_VAL0_VAL1 0x0003u
/* FFILT = 0: glitch filter bypassed. The contract specifies no filter; the FLT lines come from
 * Schmitt buffers (card §7). A filter would only add latency to FW-15. HIL item: no false trips. */
#define PWM_FFILT_VALUE 0u
/* FCTRL2: NOCOMB[3:0] = 0: the fault input also acts through the direct combinational path */
#define PWM_FCTRL2_VALUE 0u

/* The locked fault image (FW-15 / §4c). */
static inline uint16_t pwm_fault_fctrl(void)
{
    return pwm_fctrl(PWM_FAULT_MASK3, PWM_FAULT_MASK3, 0u, PWM_F1 /* TRGMUX output active high */);
}
static inline uint16_t pwm_fault_dismap0(void)
{
    return pwm_dismap0(PWM_F0 | PWM_F1 | PWM_F2, PWM_F0 | PWM_F2);
}

/* Centre-aligned, signed counter: INIT = -half, VAL1 = half - 1, VAL0 = 0 (half-cycle reload). */
typedef struct {
    int16_t init;
    int16_t val1;
    uint16_t half;
    uint16_t dtcnt;
    bool ok;
} pwm_counts_t;

static inline pwm_counts_t pwm_counts(uint32_t clk_hz, uint32_t fsw_hz, uint32_t dead_ns)
{
    pwm_counts_t c = {0, 0, 0u, 0u, false};
    if ((fsw_hz == 0u) || (clk_hz == 0u)) {
        return c;
    }
    const uint32_t half = clk_hz / (2u * fsw_hz);
    const uint64_t dt = (((uint64_t)dead_ns * clk_hz) + 999999999u) / 1000000000u; /* never shorter */
    if ((half < 2u) || (half > 32767u) || (dt > 0x7FFu)) {
        return c; /* period or dead time not representable: refuse */
    }
    c.half = (uint16_t)half;
    c.init = (int16_t)(-(int32_t)half);
    c.val1 = (int16_t)(half - 1u);
    c.dtcnt = (uint16_t)dt;
    c.ok = true;
    return c;
}

/* High-side (PWM23 = A) edges for a duty 0..1 centred on the counter zero. */
static inline void pwm_edges(float duty, uint16_t half, int16_t *val2, int16_t *val3)
{
    if (!(duty > 0.0f)) { /* 0 and NaN: never set (edges beyond VAL1) */
        *val2 = (int16_t)half;
        *val3 = (int16_t)half;
    } else if (duty >= 1.0f) { /* set at INIT, never cleared */
        *val2 = (int16_t)(-(int32_t)half);
        *val3 = (int16_t)half;
    } else {
        const int32_t w = (int32_t)((duty * (float)half) + 0.5f);
        *val2 = (int16_t)(-w);
        *val3 = (int16_t)w;
    }
}

/* Output images per mode (hal_pwm_mode_t order: OFF, ASC, MOD). ASC: A masked to 0, the dead-time
 * source switched to SWCOUT with OUT23 = 0, so the complementary B = 1 after the dead time. */
typedef struct {
    uint16_t mask;
    uint16_t dtsrcsel;
    uint16_t swcout;
} pwm_mode_img_t;

static inline pwm_mode_img_t pwm_mode_image(uint32_t mode)
{
    pwm_mode_img_t m = {pwm_ab(PWM_FAULT_MASK3, PWM_FAULT_MASK3), 0u, 0u}; /* OFF: everything masked low */
    if (mode == 1u) {
        m.mask = pwm_ab(PWM_FAULT_MASK3, 0u);
        m.dtsrcsel = pwm_dtsrcsel_all(PWM_SRC_SWCOUT);
        m.swcout = 0u;
    } else if (mode == 2u) {
        m.mask = 0u;
    } else {
        /* OFF */
    }
    return m;
}

/* ---------------- ADC_SAR ---------------- */
/* RTD/RM channel index: precision inputs Pn = n (0..7), standard inputs Sn = 32 + n (0..23). */
#define S32K3_ADC_CH(sub, n) ((uint8_t)(((sub) == 'S') ? (32u + (uint32_t)(n)) : (uint32_t)(n)))
/* Analog watchdog: the RM compares strictly (flag when data > THRH or data < THRL). The HAL trips
 * at code >= hi and code <= lo (lo = 0: low compare off). */
static inline uint16_t adc_thrh(uint16_t hi_trip) { return (hi_trip > 0u) ? (uint16_t)(hi_trip - 1u) : 0u; }
static inline uint16_t adc_thrl(uint16_t lo_trip) { return (lo_trip > 0u) ? (uint16_t)(lo_trip + 1u) : 0u; }

/* ---------------- FlexCAN (CAN-FD) ---------------- */
static inline uint8_t can_dlc_to_len(uint8_t dlc)
{
    static const uint8_t LEN[16] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u};
    return LEN[dlc & 0xFu];
}
static inline uint8_t can_len_to_dlc(uint8_t len)
{
    uint8_t dlc = 0u;
    while ((dlc < 15u) && (can_dlc_to_len(dlc) < len)) {
        dlc++;
    }
    return dlc;
}

#endif /* S32K396_CFG_H */
