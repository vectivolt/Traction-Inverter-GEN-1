/* s32k396.h — platform-internal definitions for the S32K396 build.
 *
 * Two build modes:
 *   TI_RTD_AVAILABLE defined (make target): the NXP S32K3 RTD IP drivers and the S32K39 device
 *     headers are included and every TODO(RTD) body calls them;
 *   undefined (make target-check): the RTD calls are compiled out and each HAL function returns
 *     its safe default (outputs off, inputs "fault", init false), and the few registers written
 *     directly land in a RAM shadow. An image built this way never arms: hal_pwm_init() fails.
 *
 * TODO(RTD) marks every item the integrator binds against the installed RTD release: the
 * generated configuration symbol names (S32 Config Tools) and the device-header register names.
 * TODO(HW) marks a value that must be measured or confirmed on the target. */
#ifndef S32K396_H
#define S32K396_H

#include <stdint.h>

#include "board_pins.h"
#include "s32k396_board_cfg.h"
#include "s32k396_cfg.h"
#include "ti_types.h"

/* ---------------- instances ---------------- */
#define TI_PWM_INST 1u   /* eFlexPWM_1 (board_pins: PWM_1_A/B[0..2], PWM_1_FAULT[0]/[2]) */
#define TI_LPSPI_INST 3u /* FS26 on LPSPI3 PCS0 */
#define TI_STM_INST 0u   /* free-running 1 MHz time base */
#define TI_SWT_INST 0u
#define TI_CAN_VEH 0u    /* FlexCAN0 */
#define TI_CAN_DIAG 1u   /* FlexCAN1 */

/* ---------------- SAR ADC inputs (hal/adc.h hal_adc_sig_t order) ----------------
 * Instance, subtype and channel all come from board_pins.h (generated from the ball map): nothing
 * here is written per pin, so a relocated ball cannot leave a stale 'P'/'S' behind (round 15,
 * A13-R04). The group picks the conversion schedule (s32k396_cfg.h). */
#define TI_ADC_ROW(sig, grp) {BP_##sig##_INST, BP_##sig##_SUB, BP_##sig##_CHAN, (grp)}
#define TI_ADC_MAP_INIT                                                                                     \
    {TI_ADC_ROW(ISNS_U, TI_ADC_G_PHASE),  TI_ADC_ROW(ISNS_V, TI_ADC_G_PHASE),    TI_ADC_ROW(ISNS_W, TI_ADC_G_PHASE), \
     TI_ADC_ROW(VDC1_SE, TI_ADC_G_VDC),   TI_ADC_ROW(VDC2_SE, TI_ADC_G_VDC),     TI_ADC_ROW(VOFS, TI_ADC_G_SLOW),    \
     TI_ADC_ROW(V5GD_SNS, TI_ADC_G_SLOW), TI_ADC_ROW(HW_ID, TI_ADC_G_SLOW),      TI_ADC_ROW(IGN_SNS, TI_ADC_G_SLOW), \
     TI_ADC_ROW(INTRLOK_N, TI_ADC_G_SLOW), TI_ADC_ROW(TMOD_U, TI_ADC_G_SLOW),    TI_ADC_ROW(TMOD_V, TI_ADC_G_SLOW),  \
     TI_ADC_ROW(TMOD_W, TI_ADC_G_SLOW),   TI_ADC_ROW(NTC_H, TI_ADC_G_SLOW),      TI_ADC_ROW(NTC_A, TI_ADC_G_SLOW),   \
     TI_ADC_ROW(MT1_SIG, TI_ADC_G_SLOW),  TI_ADC_ROW(MT2_SIG, TI_ADC_G_SLOW),    TI_ADC_ROW(SBC_AMUX, TI_ADC_G_SLOW)}

/* ---------------- clocks (the Clock_Ip configuration fixes these) ---------------- */
#ifndef TI_PWM_CLK_HZ
#define TI_PWM_CLK_HZ 160000000u /* TODO(RTD): eFlexPWM_1 functional clock of the chosen Clock_Ip config */
#endif
#define TI_STM_HZ 1000000u      /* STM prescaled to 1 MHz: hal_time_us() is the raw counter */

/* ---------------- interrupt priorities (NVIC, 4 bits, 0 = highest) ---------------- */
#define TI_PRIO_PWM_FAULT 0u   /* eFlexPWM_1 fault: FW-15 step 1, FW-06 ASC request */
#define TI_PRIO_CURRENT 2u     /* BCTU conversion complete: app_isr_current */
#define TI_PRIO_TASK 4u        /* STM ch0 1 ms: app_task_1ms */
#define TI_PRIO_CAN 5u         /* FlexCAN RX into the software ring */
#define TI_PRIO_SDADC 1u       /* eDMA major loop of the SIN channel: block time stamp only */
/* hal_crit_enter() sets PRIMASK: nv_queue() is called from the fault ISR too, so its copy must
 * exclude every level. Bound: one record copy (<= NV_PAYLOAD_MAX bytes), counted in docs/timing.md
 * against the FW-06 budget. */

/* ---------------- directly written registers ---------------- */
#ifdef TI_RTD_AVAILABLE
#include "S32K39_FLEXPWM.h" /* TODO(RTD): device-header file and symbol names of the installed release */
#include "S32K39_SIUL2.h"
#include "S32K39_ADC.h"
#include "S32K39_STM.h"
#define TI_PWM (IP_FLEXPWM_1)                  /* TODO(RTD) */
#define PWM_R(reg) (TI_PWM->reg)               /* module registers: OUTEN, MASK, SWCOUT, ... */
#define PWM_SM(n, reg) (TI_PWM->SUB[(n)].reg)  /* TODO(RTD): SUB[] / SM[] per the header */
#define SIUL2_MSCR(i) (IP_SIUL2->MSCR[(i)])
#define SIUL2_IMCR(i) (IP_SIUL2->IMCR[(i)])
#define STM_CNT() (IP_STM_0->CNT)
/* REG_PROT areas (s32k396_cfg.h layout): module base + offset. TODO(RM): base symbols. */
#define TI_PWM_BASE ((uintptr_t)IP_FLEXPWM_1)
#define TI_SIUL2_BASE ((uintptr_t)IP_SIUL2)
#define REGPROT_U8(base, ofs) (*(volatile uint8_t *)((base) + (ofs)))
#define REGPROT_U32(base, ofs) (*(volatile uint32_t *)((base) + (ofs)))
#else
typedef struct {
    volatile uint16_t INIT, CTRL2, CTRL, VAL0, VAL1, VAL2, VAL3, VAL4, VAL5, OCTRL, TCTRL, DISMAP0, DISMAP1, DTCNT0,
        DTCNT1;
} ti_pwm_sub_t;
typedef struct {
    ti_pwm_sub_t SUB[4];
    volatile uint16_t OUTEN, MASK, SWCOUT, DTSRCSEL, MCTRL, MCTRL2, FCTRL, FSTS, FFILT, FTST, FCTRL2;
} ti_pwm_regs_t;
typedef struct {
    ti_pwm_regs_t pwm;
    volatile uint32_t mscr[256];
    volatile uint32_t imcr[512];
    volatile uint32_t stm_cnt;
} ti_shadow_t;
extern ti_shadow_t g_ti_shadow; /* host syntax-check build only */
#define PWM_R(reg) (g_ti_shadow.pwm.reg)
#define PWM_SM(n, reg) (g_ti_shadow.pwm.SUB[(n)].reg)
#define SIUL2_MSCR(i) (g_ti_shadow.mscr[(i)])
#define SIUL2_IMCR(i) (g_ti_shadow.imcr[(i)])
#define STM_CNT() (g_ti_shadow.stm_cnt)
#define TI_PWM_BASE ((uintptr_t)&g_ti_shadow.pwm) /* offsets only: the shadow has no REG_PROT area */
#endif
#define TI_IMCR_SSS_MASK 0xFu /* TODO(RM): IMCR SSS field width */

/* SIUL2 MSCR fields used directly (S32K3 RM, SIUL2 chapter). TODO(HW-RM): confirm on S32K39. */
#define TI_MSCR_OBE (1u << 21) /* output buffer enable */
#define TI_MSCR_IBE (1u << 19) /* input buffer enable */
#define TI_MSCR_SSS_MASK 0x7u  /* source signal select (ALT function) */

/* IMCR routing of the FLT pins to eFlexPWM_1 FAULT0/FAULT2 (contract FW-15: "routed through the
 * SIUL2 input mux"): the values come from s32k396_board_cfg.h (TODO(RM)). A placeholder can never
 * reach a target image: the header #errors, and filled values must be non-zero with two different
 * IMCR indices. Other builds see the route as UNBOUND (hal_pwm_fault_route_bound() false). */
#define TI_IMCR_ROUTE_BOUND                                                                              \
    ((TI_IMCR_PWM1_FAULT0 != TI_IMCR_UNBOUND) && (TI_IMCR_PWM1_FAULT2 != TI_IMCR_UNBOUND) &&             \
     (TI_IMCR_SSS_PTC26 != TI_IMCR_UNBOUND) && (TI_IMCR_SSS_PTC25 != TI_IMCR_UNBOUND) &&                 \
     (TI_IMCR_PWM1_FAULT0 != TI_IMCR_PWM1_FAULT2))
#if defined(TI_RTD_AVAILABLE)
_Static_assert(TI_IMCR_ROUTE_BOUND, "s32k396_board_cfg.h: IMCR values must be non-zero, the two indices distinct");
#endif

/* Platform-internal entry points (s32k396_io.c), called from s32k396_main.c. */
bool s32k_gpio_init(void);
bool s32k_timer_init(void);
void s32k_sdadc_block_irq(void);

#endif /* S32K396_H */
