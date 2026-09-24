/* gate_selftest.c — FW-16. */
#include "gate_selftest.h"

#include "bridge.h"
#include "gpio.h"
#include "pwm.h"
#include "timer.h"
#include "ti_math.h"

#define ST_RELEASE_TIMEOUT_MS 100u
#define ST_GATE_EN_US 5u

void st_init(st_t *t)
{
    *t = (st_t){0};
}

bool st_conditions(const st_cond_t *c)
{
    return c->rdy_both && c->vdc_both_valid && c->contactors_open && c->resolver_valid &&
           (ti_absf(c->speed_rpm) < c->n_ss_rpm) && c->pwm_low && c->flt_high;
}

st_energy_t st_energy(const st_cond_t *c, const ti_params_t *p)
{
    if (!c->vdc_both_valid) {
        return ST_ENERGY_REFUSE; /* an invalid witness is not a low voltage */
    }
    const float v = ti_maxf(c->v_ch[0], c->v_ch[1]);
    if (v < p->fw16_both_below_v) {
        return ST_ENERGY_OK;
    }
    return (v < p->fw16_topup_below_v) ? ST_ENERGY_TOPUP : ST_ENERGY_REFUSE;
}

/* Poll both read-backs until they match or max_us passes. */
static bool expect(bool drv_en, bool asc_cmd, uint32_t max_us)
{
    for (uint32_t i = 0u; i <= max_us; i++) {
        if ((hal_gpio_read(HAL_DI_DRV_EN_RB) == drv_en) && (hal_gpio_read(HAL_DI_ASC_CMD_RB) == asc_cmd)) {
            return true;
        }
        hal_delay_us(1u);
    }
    return false;
}

static bool expect_pin(hal_di_t pin, bool level, uint32_t max_us)
{
    for (uint32_t i = 0u; i <= max_us; i++) {
        if (hal_gpio_read(pin) == level) {
            return true;
        }
        hal_delay_us(1u);
    }
    return false;
}

static st_res_t fail(st_t *t)
{
    t->failed = t->s;
    t->s = ST_FAIL;
    t->in_h = false;
    (void)hal_gpio_flt_pad_drive_low(HAL_DI_FLT_HS_N, false);
    (void)hal_gpio_flt_pad_drive_low(HAL_DI_FLT_LS_N, false);
    hal_gpio_write(HAL_DO_ASC_REQ, false);
    br_asc_clear_pulse();
    hal_gpio_write(HAL_DO_MCU_GATE_EN, false);
    hal_pwm_force_off();
    return ST_RES_FAIL;
}

static void go(st_t *t, st_state_t s, uint32_t now_ms)
{
    t->s = s;
    t->t_ms = now_ms;
}

/* step h for one bank; ASC latch set on entry */
static bool step_h_bank(hal_di_t pad, uint8_t fflag, const ti_params_t *p)
{
    bool ok = hal_gpio_flt_pad_drive_low(pad, true);
    ok = ok && expect_pin(HAL_DI_ASC_CMD_RB, false, 1u);            /* UASCG mask at once */
    ok = ok && expect_pin(HAL_DI_DRV_EN_RB, false, p->fw16_drven_rb_us); /* latch -> AND */
    ok = ok && ((hal_pwm_fault_flags() & fflag) != 0u);              /* eFlexPWM FAULT routing */
    (void)hal_gpio_flt_pad_drive_low(pad, false);                    /* release, pad re-locked */
    br_flt_clear_pulse();
    hal_delay_us(p->cal_oneshot_wait_us);
    ok = ok && expect(true, true, 5u); /* latch cleared; ASC latch still set, FLT gone */
    ok = ok && ((hal_pwm_fault_clear(fflag) & fflag) == 0u);
    return ok;
}

static st_res_t run_h(st_t *t, const ti_params_t *p)
{
    t->in_h = true;
    hal_gpio_write(HAL_DO_ASC_REQ, false);
    hal_gpio_write(HAL_DO_ASC_REQ, true);
    bool ok = expect(true, true, p->fw16_asc_rb_us);
    ok = ok && step_h_bank(HAL_DI_FLT_HS_N, HAL_PWM_FAULT_FLT_HS, p);
    ok = ok && step_h_bank(HAL_DI_FLT_LS_N, HAL_PWM_FAULT_FLT_LS, p);
    hal_gpio_write(HAL_DO_ASC_REQ, false);
    br_asc_clear_pulse();
    ok = ok && expect(true, false, p->fw16_asc_rb_us);
    t->in_h = false;
    if (!ok) {
        return fail(t);
    }
    hal_gpio_write(HAL_DO_MCU_GATE_EN, false); /* not armed until §9 step 8 */
    t->s = ST_PASS;
    return ST_RES_PASS;
}

static st_res_t start(st_t *t, const st_cond_t *c, dis_t *dis, const vdc_t *v, ti_contactor_t cont,
                      uint32_t now_ms, const ti_params_t *p)
{
    if (!st_conditions(c)) {
        t->s = ST_SKIP;
        return ST_RES_SKIP;
    }
    const st_energy_t e = st_energy(c, p);
    if (e == ST_ENERGY_REFUSE) {
        t->s = ST_SKIP;
        return ST_RES_SKIP;
    }
    if (e == ST_ENERGY_TOPUP) {
        if (dis_request(dis, cont, v, false, now_ms, p) != DIS_REQ_OK) {
            t->s = ST_SKIP;
            return ST_RES_SKIP;
        }
        go(t, ST_TOPUP, now_ms);
        return ST_RES_BUSY;
    }
    go(t, ST_A, now_ms);
    return ST_RES_BUSY;
}

st_res_t st_step(st_t *t, const st_cond_t *c, fs26_t *fs, dis_t *dis, const vdc_t *v, ti_contactor_t cont,
                 uint32_t now_ms, const ti_params_t *p)
{
    const uint32_t dwell = ti_age(now_ms, t->t_ms);
    switch (t->s) {
    case ST_IDLE:
        return start(t, c, dis, v, cont, now_ms, p);
    case ST_TOPUP:
        if (dwell < (uint32_t)(p->qdis_2tau_s * 1000.0f)) {
            return ST_RES_BUSY;
        }
        dis_abort(dis);
        if (!st_conditions(c) || (st_energy(c, p) == ST_ENERGY_REFUSE)) {
            t->s = ST_SKIP;
            return ST_RES_SKIP;
        }
        go(t, ST_A, now_ms);
        return ST_RES_BUSY;
    case ST_A:
        hal_gpio_write(HAL_DO_MCU_GATE_EN, true);
        if ((fs26_request_fs0b(fs) != FS26_OK) || !expect(false, true, p->fw16_asc_rb_us)) {
            return fail(t);
        }
        go(t, ST_B, now_ms);
        return ST_RES_BUSY;
    case ST_B: {
        const fs26_status_t s = fs26_release_safety_outputs(fs);
        if (s == FS26_BUSY) {
            return (dwell > ST_RELEASE_TIMEOUT_MS) ? fail(t) : ST_RES_BUSY;
        }
        br_asc_clear_pulse();
        if ((s != FS26_OK) || !expect(true, false, p->fw16_asc_rb_us)) {
            return fail(t);
        }
        go(t, ST_C, now_ms);
        return ST_RES_BUSY;
    }
    case ST_C:
        hal_gpio_write(HAL_DO_MCU_GATE_EN, false);
        if (!expect_pin(HAL_DI_DRV_EN_RB, false, ST_GATE_EN_US)) {
            return fail(t);
        }
        hal_gpio_write(HAL_DO_MCU_GATE_EN, true);
        if (!expect_pin(HAL_DI_DRV_EN_RB, true, ST_GATE_EN_US)) {
            return fail(t);
        }
        (void)fs26_set_gpio1(fs, false);
        hal_gpio_write(HAL_DO_EN_FLYBK_HS, false);
        go(t, ST_D_DROP, now_ms);
        return ST_RES_BUSY;
    case ST_D_DROP:
    case ST_E_DROP: {
        const bool hs = (t->s == ST_D_DROP);
        if (!hal_gpio_read(hs ? HAL_DI_RDY_HS : HAL_DI_RDY_LS)) {
            if (hal_gpio_read(HAL_DI_DRV_EN_RB)) {
                return fail(t); /* RDY low must pull DRV_EN low: the term is not permissive */
            }
            *(hs ? &t->drop_hs_ms : &t->drop_ls_ms) = (dwell > 0u) ? dwell : 1u;
            hal_gpio_write(hs ? HAL_DO_EN_FLYBK_HS : HAL_DO_EN_FLYBK_LS, true);
            go(t, hs ? ST_D_RISE : ST_E_RISE, now_ms);
        } else if (dwell > p->cal_fw16_rdy_drop_ms) {
            return fail(t); /* RDY did not follow: the path is stuck, or FS_GPIO1 still holds it */
        } else {
            /* waiting for the rail to drop */
        }
        return ST_RES_BUSY;
    }
    case ST_D_RISE:
    case ST_E_RISE: {
        const bool hs = (t->s == ST_D_RISE);
        if (hal_gpio_read(hs ? HAL_DI_RDY_HS : HAL_DI_RDY_LS)) {
            if (!expect_pin(HAL_DI_DRV_EN_RB, true, ST_GATE_EN_US)) {
                return fail(t);
            }
            if (hs) {
                hal_gpio_write(HAL_DO_EN_FLYBK_LS, false);
                go(t, ST_E_DROP, now_ms);
            } else {
                go(t, ST_F, now_ms);
            }
        } else if (dwell > p->cal_fw16_rdy_rise_ms) {
            return fail(t);
        } else {
            /* rails rising */
        }
        return ST_RES_BUSY;
    }
    case ST_F:
        hal_gpio_write(HAL_DO_ASC_REQ, false);
        hal_gpio_write(HAL_DO_ASC_REQ, true);
        if (!expect_pin(HAL_DI_ASC_CMD_RB, true, p->fw16_asc_rb_us)) {
            return fail(t);
        }
        hal_gpio_write(HAL_DO_ASC_REQ, false);
        br_asc_clear_pulse();
        if (!expect_pin(HAL_DI_ASC_CMD_RB, false, p->fw16_asc_rb_us)) {
            return fail(t);
        }
        (void)fs26_set_gpio1(fs, true);
        hal_gpio_write(HAL_DO_EN_FLYBK_HS, false);
        hal_gpio_write(HAL_DO_EN_FLYBK_LS, false);
        go(t, ST_G, now_ms);
        return ST_RES_BUSY;
    case ST_G: {
        if (!hal_gpio_read(HAL_DI_RDY_HS) || !hal_gpio_read(HAL_DI_RDY_LS)) {
            return fail(t); /* FS_GPIO1 must hold both banks alone */
        }
        const uint32_t hold = 2u * ((t->drop_hs_ms > t->drop_ls_ms) ? t->drop_hs_ms : t->drop_ls_ms);
        if (dwell >= hold) {
            hal_gpio_write(HAL_DO_EN_FLYBK_HS, true);
            hal_gpio_write(HAL_DO_EN_FLYBK_LS, true);
            go(t, ST_H, now_ms);
        }
        return ST_RES_BUSY;
    }
    case ST_H:
        return run_h(t, p);
    case ST_PASS:
        return ST_RES_PASS;
    case ST_FAIL:
        return ST_RES_FAIL;
    default:
        return ST_RES_SKIP;
    }
}

bool st_in_step_h(const st_t *t) { return t->in_h; }
