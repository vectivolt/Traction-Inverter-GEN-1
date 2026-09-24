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
#include "s32k396_cfg.h"
#include "ti_types.h"

/* ---------------- instances ---------------- */
#define TI_PWM_INST 1u   /* eFlexPWM_1 (board_pins: PWM_1_A/B[0..2], PWM_1_FAULT[0]/[2]) */
#define TI_LPSPI_INST 3u /* FS26 on LPSPI3 PCS0 */
#define TI_STM_INST 0u   /* free-running 1 MHz time base */
#define TI_SWT_INST 0u
#define TI_CAN_VEH 0u    /* FlexCAN0 */
#define TI_CAN_DIAG 1u   /* FlexCAN1 */

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
#endif

/* SIUL2 MSCR fields used directly (S32K3 RM, SIUL2 chapter). TODO(HW-RM): confirm on S32K39. */
#define TI_MSCR_OBE (1u << 21) /* output buffer enable */
#define TI_MSCR_IBE (1u << 19) /* input buffer enable */
#define TI_MSCR_SSS_MASK 0x7u  /* source signal select (ALT function) */

/* IMCR routing of the FLT pins to eFlexPWM_1 FAULT0/FAULT2 (contract FW-15: "routed through the
 * SIUL2 input mux"). TODO(RTD/HW): IMCR index and SSS value from the S32K39 IOMUX table; if the
 * mux cannot reach FAULT0/FAULT2 from PTC26/PTC25 the contract asks for a card pin swap. */
#define TI_IMCR_PWM1_FAULT0 0u
#define TI_IMCR_PWM1_FAULT2 0u
#define TI_IMCR_SSS_PTC26 0u
#define TI_IMCR_SSS_PTC25 0u

/* Platform-internal entry points (s32k396_io.c), called from s32k396_main.c. */
bool s32k_gpio_init(void);
bool s32k_timer_init(void);
void s32k_sdadc_block_irq(void);

#endif /* S32K396_H */
