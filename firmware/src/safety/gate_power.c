/* gate_power.c — FW-14. */
#include "gate_power.h"

#include "dtc.h"
#include "gpio.h"
#include "pwm.h"

void gp_init(gp_t *g)
{
    *g = (gp_t){0};
    hal_gpio_write(HAL_DO_EN_FLYBK_HS, false);
    hal_gpio_write(HAL_DO_EN_FLYBK_LS, false);
}

bool gp_rdy_both(void) { return hal_gpio_read(HAL_DI_RDY_HS) && hal_gpio_read(HAL_DI_RDY_LS); }

bool gp_request_on(gp_t *g, fs26_t *fs, bool fs1b_released, uint32_t now_ms)
{
    if (!fs1b_released) {
        return false; /* gate power never up while FS1B presets the ASC latch at boot */
    }
    hal_gpio_write(HAL_DO_EN_FLYBK_HS, true);
    hal_gpio_write(HAL_DO_EN_FLYBK_LS, true);
    (void)fs26_set_gpio1(fs, true);
    if (g->st == GP_OFF) {
        g->st = GP_STARTING;
        g->t_start_ms = now_ms;
    }
    return true;
}

void gp_off(gp_t *g, fs26_t *fs)
{
    hal_gpio_write(HAL_DO_EN_FLYBK_HS, false);
    hal_gpio_write(HAL_DO_EN_FLYBK_LS, false);
    (void)fs26_set_gpio1(fs, false);
    g->st = GP_OFF;
}

gp_state_t gp_step(gp_t *g, uint32_t now_ms, const ti_params_t *p)
{
    const bool rdy = gp_rdy_both();
    switch (g->st) {
    case GP_STARTING:
        if (rdy) {
            g->st = GP_READY;
        } else if (ti_elapsed(now_ms, g->t_start_ms, p->cal_rdy_timeout_ms)) {
            g->st = GP_LOST;
            g->timeout_dtc = true;
            dtc_set(DTC_GATE_POWER, now_ms);
        } else {
            /* rails rising (S1: one burst, 73–240 ms) */
        }
        break;
    case GP_READY:
        if (!rdy) {
            g->st = GP_LOST;
            hal_pwm_force_off(); /* no PWM without RDY */
            dtc_set(DTC_GATE_POWER, now_ms);
        }
        break;
    case GP_LOST:
        if (rdy) {
            g->st = GP_READY; /* undervoltage recovers with RDY (no DESAT counting) */
        } else {
            hal_pwm_force_off();
        }
        break;
    default:
        break;
    }
    return g->st;
}
