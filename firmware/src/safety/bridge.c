/* bridge.c — actuator sequences (§4c, FW-06a, §7 / FW-15). */
#include "bridge.h"

#include "gpio.h"
#include "pwm.h"
#include "timer.h"
#include "ti_math.h"

void br_init(bridge_t *b)
{
    *b = (bridge_t){0};
    /* §9 step 1: every enable low; ASC_CLR's latch high (no clear) before anything else */
    hal_gpio_write(HAL_DO_ASC_CLR_N, true);
    hal_gpio_write(HAL_DO_MCU_GATE_EN, false);
    hal_gpio_write(HAL_DO_ASC_REQ, false);
    hal_gpio_write(HAL_DO_FLT_CLR, false);
    hal_gpio_write(HAL_DO_QDIS, false);
    hal_pwm_force_off();
    b->mode = BR_DISARMED;
}

void br_spo(bridge_t *b, bool en_low)
{
    hal_pwm_force_off();
    if (en_low) {
        hal_gpio_write(HAL_DO_MCU_GATE_EN, false);
        b->mode = BR_DISARMED;
    } else {
        b->mode = BR_IDLE;
    }
}

bool br_arm_idle(bridge_t *b)
{
    if (b->mode == BR_ASC) {
        return false; /* leave ASC only through br_exit_asc() */
    }
    hal_pwm_force_off();
    hal_gpio_write(HAL_DO_MCU_GATE_EN, true);
    b->mode = BR_IDLE;
    return true;
}

bool br_modulate(bridge_t *b, const float duty[3], const ti_params_t *p)
{
    if ((b->mode != BR_IDLE) && (b->mode != BR_MOD)) {
        return false;
    }
    for (uint32_t i = 0u; i < 3u; i++) {
        if (!ti_finite(duty[i]) || (duty[i] < 0.0f) || (duty[i] > 1.0f)) {
            hal_pwm_force_off(); /* the guard: nothing non-finite reaches the registers */
            b->mode = BR_IDLE;
            return false;
        }
    }
    if (b->asc_cleared_recently) { /* FW-06a step 3 */
        const uint32_t need_us = (p->asc_exit_hs_delay_ns + 999u) / 1000u;
        const uint32_t since = ti_age(hal_time_us(), b->t_asc_clear_us);
        if (since < need_us) {
            hal_delay_us(need_us - since);
        }
        b->asc_cleared_recently = false;
    }
    hal_pwm_set_duty(duty);
    b->mode = BR_MOD;
    return true;
}

void br_asc_clear_pulse(void)
{
    hal_gpio_write(HAL_DO_ASC_CLR_N, false);
    hal_delay_us(1u);
    hal_gpio_write(HAL_DO_ASC_CLR_N, true);
}

void br_flt_clear_pulse(void)
{
    hal_gpio_write(HAL_DO_FLT_CLR, true);
    hal_delay_us(1u);
    hal_gpio_write(HAL_DO_FLT_CLR, false); /* falling edge fires the one-shot */
}

void br_enter_pwm_asc(bridge_t *b, uint32_t t_hs_off_us, const ti_params_t *p)
{
    /* 1) high sides off (the eFlexPWM fault did it in hardware on the FW-06 path) */
    if (hal_pwm_mode() == HAL_PWM_MOD) {
        hal_pwm_force_off();
        t_hs_off_us = hal_time_us();
    }
    /* 2) latch: the rising edge is the ASC request; the line stays high while in ASC */
    if (hal_gpio_out_state(HAL_DO_ASC_REQ)) {
        hal_gpio_write(HAL_DO_ASC_REQ, false);
    }
    hal_gpio_write(HAL_DO_ASC_REQ, true);
    /* 3) PWM-ASC no sooner than the dead time after 1) */
    const uint32_t dt_us = (p->dead_time_ns + 999u) / 1000u;
    const uint32_t since = ti_age(hal_time_us(), t_hs_off_us);
    if (since < dt_us) {
        hal_delay_us(dt_us - since);
    }
    hal_pwm_set_asc();
    hal_gpio_write(HAL_DO_MCU_GATE_EN, true);
    b->mode = BR_ASC;
    b->n_asc_entries++;
}

bool br_exit_asc(bridge_t *b, bool allowed)
{
    if ((b->mode != BR_ASC) || !allowed) {
        return false; /* never in a hurry, never without the caller's proof */
    }
    hal_gpio_write(HAL_DO_ASC_REQ, false);
    br_asc_clear_pulse(); /* 1) while PWM-ASC still holds the low sides */
    b->t_asc_clear_us = hal_time_us();
    b->asc_cleared_recently = true;
    hal_pwm_force_off(); /* 2) next state: SPO here; modulation resumes through br_modulate() */
    b->mode = BR_IDLE;
    return true;
}

void br_rec_start(bridge_t *b, uint32_t t_fault_us)
{
    hal_pwm_force_off();
    hal_gpio_write(HAL_DO_MCU_GATE_EN, false);
    hal_gpio_write(HAL_DO_ASC_REQ, false);
    br_asc_clear_pulse(); /* always: a set latch would bring ASC back at the reset edge */
    b->rec = BR_REC_WAIT_LOW;
    b->rec_t_fault_us = t_fault_us;
    b->mode = BR_DISARMED;
}

br_rec_t br_rec_step(bridge_t *b, uint32_t now_us, const ti_params_t *p)
{
    switch (b->rec) {
    case BR_REC_WAIT_LOW:
        hal_pwm_force_off();
        if (ti_elapsed(now_us, b->rec_t_fault_us, p->fw15_low_us)) {
            hal_gpio_write(HAL_DO_MCU_GATE_EN, true);
            br_flt_clear_pulse();
            b->rec_t_pulse_us = hal_time_us();
            b->rec = BR_REC_WAIT_EN;
        }
        break;
    case BR_REC_WAIT_EN: {
        /* past the one-shot and the DRV_EN RC: a FLT that did not release has re-set the latch */
        const uint32_t since = ti_age(hal_time_us(), b->rec_t_pulse_us);
        if (since < p->cal_oneshot_wait_us) {
            hal_delay_us(p->cal_oneshot_wait_us - since);
        }
        const bool ok = hal_gpio_read(HAL_DI_FLT_HS_N) && hal_gpio_read(HAL_DI_FLT_LS_N) &&
                        hal_gpio_read(HAL_DI_RDY_HS) && hal_gpio_read(HAL_DI_RDY_LS) &&
                        !hal_gpio_read(HAL_DI_ASC_CMD_RB) && hal_gpio_read(HAL_DI_DRV_EN_RB);
        if (ok) {
            (void)hal_pwm_fault_clear(HAL_PWM_FAULT_FLT_HS | HAL_PWM_FAULT_FLT_LS);
            b->mode = BR_IDLE; /* EN high, PWM low: the caller decides the next state */
            b->rec = BR_REC_DONE;
        } else {
            hal_gpio_write(HAL_DO_MCU_GATE_EN, false);
            b->rec = BR_REC_FAIL;
        }
        break;
    }
    default:
        break;
    }
    return b->rec;
}
