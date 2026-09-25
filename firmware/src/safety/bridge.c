/* bridge.c — actuator sequences (§4c, FW-06a, §7 / FW-15) and the DESAT hold (A12-R05). */
#include "bridge.h"

#include "gpio.h"
#include "pwm.h"
#include "timer.h"
#include "ti_math.h"

#define FLT_HS_BIT 0x1u
#define FLT_LS_BIT 0x2u

void br_init(bridge_t *b, const ti_params_t *p)
{
    *b = (bridge_t){0};
    b->hold_us = p->cal_desat_en_hold_us;
    /* §9 step 1: every enable low; ASC_CLR's latch high (no clear) before anything else */
    hal_gpio_write(HAL_DO_ASC_CLR_N, true);
    hal_gpio_write(HAL_DO_MCU_GATE_EN, false);
    hal_gpio_write(HAL_DO_ASC_REQ, false);
    hal_gpio_write(HAL_DO_FLT_CLR, false);
    hal_gpio_write(HAL_DO_QDIS, false);
    hal_pwm_force_off();
    b->mode = BR_DISARMED;
}

/* The FLT lines first, then the time: a line newly seen low starts the hold no earlier than the
 * read that saw it, so the hold always ends >= cal_desat_en_hold_us after the FLT edge. */
static void observe(bridge_t *b)
{
    const uint8_t low = (uint8_t)((hal_gpio_read(HAL_DI_FLT_HS_N) ? 0u : FLT_HS_BIT) |
                                  (hal_gpio_read(HAL_DI_FLT_LS_N) ? 0u : FLT_LS_BIT));
    const uint32_t now = hal_time_us();
    if ((low & (uint8_t)~b->flt_seen) != 0u) {
        b->hold_active = true;
        b->t_hold_us = now;
    }
    b->flt_seen = low;
    if (b->hold_active && ti_elapsed(now, b->t_hold_us, b->hold_us)) {
        b->hold_active = false;
    }
}

/* The only software path that lowers MCU_GATE_EN. Inside a hold the drop is left pending (the
 * fault latch takes DRV_EN low 22–53 us after the FLT; EN follows once the hold has run). The
 * critical section keeps the look, the decision and the write together against the fault ISR. */
static void en_low(bridge_t *b)
{
    hal_crit_enter();
    observe(b);
    if (b->hold_active && hal_gpio_out_state(HAL_DO_MCU_GATE_EN)) {
        b->en_drop_pending = true;
    } else {
        hal_gpio_write(HAL_DO_MCU_GATE_EN, false);
        b->en_drop_pending = false;
    }
    hal_crit_exit();
}

void br_service(bridge_t *b)
{
    hal_crit_enter();
    observe(b);
    if (b->en_drop_pending && !b->hold_active) {
        hal_pwm_force_off();
        hal_gpio_write(HAL_DO_MCU_GATE_EN, false);
        b->en_drop_pending = false;
        b->mode = BR_DISARMED;
    }
    hal_crit_exit();
}

bool br_en_drop_pending(const bridge_t *b) { return b->en_drop_pending; }

void br_spo(bridge_t *b, bool en_low_req)
{
    hal_pwm_force_off(); /* never delayed: the FAULT0/2 input has already done it in hardware */
    if (en_low_req) {
        en_low(b);
    }
    b->mode = (en_low_req || b->en_drop_pending) ? BR_DISARMED : BR_IDLE;
}

bool br_arm_idle(bridge_t *b)
{
    if ((b->mode == BR_ASC) || b->en_drop_pending) {
        return false; /* leave ASC only through br_exit_asc(); never arm inside a DESAT hold */
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
    if (b->asc_cleared_recently) {
        /* FW-06a step 3 (round 17): no high-side pulse before the ASC pins have released (cal_asc_release_ns: the
         * verifier's 1.07 us + margin) and the low sides have then turned off (the SKU dead time, the design's
         * complementary turn-off allowance), counted from the clear's falling edge; one count more because the
         * microsecond counter floors both readings */
        const uint32_t need_us = ((p->cal_asc_release_ns + p->dead_time_ns + 999u) / 1000u) + 1u;
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

/* The latch clears asynchronously on the falling edge: the returned stamp is taken right after it. */
static uint32_t asc_clear_pulse(void)
{
    hal_gpio_write(HAL_DO_ASC_CLR_N, false);
    const uint32_t t_us = hal_time_us();
    hal_delay_us(1u);
    hal_gpio_write(HAL_DO_ASC_CLR_N, true);
    return t_us;
}

void br_asc_clear_pulse(void) { (void)asc_clear_pulse(); }

void br_flt_clear_pulse(void)
{
    hal_gpio_write(HAL_DO_FLT_CLR, true);
    hal_delay_us(1u);
    hal_gpio_write(HAL_DO_FLT_CLR, false); /* falling edge fires the one-shot */
}

void br_enter_pwm_asc(bridge_t *b, uint32_t t_hs_off_us, const ti_params_t *p)
{
    if (b->en_drop_pending) {
        return; /* an SPO inside a DESAT hold stands: no PWM-ASC until it is carried out */
    }
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
    b->t_asc_clear_us = asc_clear_pulse(); /* 1) while PWM-ASC still holds the low sides */
    b->asc_cleared_recently = true;
    hal_pwm_force_off(); /* 2) next state: SPO here; modulation resumes through br_modulate() */
    b->mode = BR_IDLE;
    return true;
}

void br_rec_start(bridge_t *b, uint32_t t_fault_us)
{
    hal_pwm_force_off();
    en_low(b); /* inside the DESAT hold this only records the drop (br_service carries it out) */
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
        br_service(b); /* a drop still pending happens before the reset edge, never after it */
        if (!b->en_drop_pending && ti_elapsed(now_us, b->rec_t_fault_us, p->fw15_low_us)) {
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
            /* a hard short re-tripped at the reset edge, >= cal_oneshot_wait_us - 46 us ago: its
             * soft turn-off is over, and the hold still applies if a line is newly low */
            en_low(b);
            b->rec = BR_REC_FAIL;
        }
        break;
    }
    default:
        break;
    }
    return b->rec;
}
