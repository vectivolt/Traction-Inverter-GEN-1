/* app.c — integration of sensing, control, safety and comms on the HAL. */
#include "app.h"

#include <string.h>

#include "adc.h"
#include "can.h"
#include "dtc.h"
#include "gpio.h"
#include "nvlog.h"
#include "pwm.h"
#include "sdadc.h"
#include "spi_fs26.h"
#include "swg.h"
#include "timer.h"
#include "wdog.h"

#define SESSION_MAGIC 0x4B435943u /* "KCYC" */
#define CARRIER_HZ 10000u
#define SWG_AMP_DEFAULT 12u
#define OFFSET_SAMPLES 64u
#define CAN_RX_MAX_PER_TICK 16u
#define STATUS_PERIOD_MS 10u
#define MOD_TORQUE_NM 0.5f

app_t g_app;
TI_RETAINED app_session_t g_app_session;

void app_fault_isr_entry(void) { app_isr_fault(&g_app); }

uint32_t app_isr_period_us(const app_t *a) { return 500000u / a->gains.fsw_hz; }

/* Time (A12-R06): microsecond intervals use hal_time_us(); every millisecond time stamp comes from
 * hal_time_ms() (or the task's own 64-bit read), never from hal_time_us() / 1000. */

/* ======================= init ======================= */
static void set_watchdogs(app_t *a)
{
    uint16_t lo;
    uint16_t hi;
    const hal_adc_sig_t ph[3] = {HAL_ADC_ISNS_U, HAL_ADC_ISNS_V, HAL_ADC_ISNS_W};
    for (uint32_t i = 0u; i < 3u; i++) {
        isns_oc_codes(&a->cal.isns[i], a->p->i_oc_trip_a, &lo, &hi); /* FW-05 */
        (void)hal_adc_set_watchdog(ph[i], lo, hi);
    }
    (void)hal_adc_set_watchdog(HAL_ADC_VDC1, 0u, vdc_ov_code(&a->cal.vdc[0], a->p)); /* FW-06 */
    (void)hal_adc_set_watchdog(HAL_ADC_VDC2, 0u, vdc_ov_code(&a->cal.vdc[1], a->p));
}

static void key_cycle_init(app_t *a)
{
    if (g_app_session.magic == SESSION_MAGIC) {
        a->key_cycle = g_app_session.key_cycle; /* MCU reset inside the key cycle */
        a->cold_start = false;
        return;
    }
    a->cold_start = true;
    uint32_t kc = 0u;
    (void)nv_read(NV_REC_KEYCYCLE, &kc, (uint16_t)sizeof kc);
    a->key_cycle = kc + 1u;
    (void)nv_queue(NV_REC_KEYCYCLE, &a->key_cycle, (uint16_t)sizeof a->key_cycle);
    g_app_session.magic = SESSION_MAGIC;
    g_app_session.key_cycle = a->key_cycle;
}

static void forbid(app_t *a, dtc_id_t d)
{
    dtc_set(d, hal_time_ms());
    a->no_arm = true;
}

static void init_fs26(app_t *a)
{
    static const dtc_id_t MAP[] = {DTC_NONE, DTC_NONE, DTC_FS26_SPI, DTC_FS26_SPI, DTC_FS26_PROGID,
                                   DTC_FS26_OTP_CORRUPT, DTC_FS26_DEBUG_MODE, DTC_FS26_INIT_READBACK,
                                   DTC_FS26_RELEASE};
    const fs26_status_t s = fs26_init(&a->fs, a->p);
    if (s != FS26_OK) {
        forbid(a, MAP[((uint32_t)s < TI_ARRAY_LEN(MAP)) ? (uint32_t)s : 2u]);
    }
    if (a->fs.fs1b_short_high) {
        forbid(a, DTC_FS1B_SHORT_HIGH); /* no arming until the FAULT_OUT wire is repaired */
    }
}

static void init_identity(app_t *a)
{
    uint16_t codes[8];
    uint32_t t;
    for (uint32_t i = 0u; i < 8u; i++) {
        (void)hal_adc_read(HAL_ADC_HW_ID, &codes[i], &t);
        hal_delay_us(100u);
    }
    const hwid_result_t r = hwid_classify_stable(codes, 8u, &a->hw_sku);
    static const dtc_id_t MAP[] = {DTC_NONE, DTC_HWID_OPEN, DTC_HWID_SHORT, DTC_HWID_UNKNOWN, DTC_HWID_UNKNOWN};
    if (r != HWID_OK) {
        forbid(a, MAP[r]);
    } else if (!hwid_identity_ok(a->hw_sku, a->p->sku, (ti_sku_t)a->cal.sku)) {
        forbid(a, DTC_SKU_MISMATCH); /* FW-02: refuses MCU_GATE_EN */
    } else {
        /* identity proven */
    }
}

static void init_calibration(app_t *a, const calib_t *cal)
{
    if (cal != NULL) {
        (void)memcpy(&a->cal, cal, sizeof a->cal); /* memcpy keeps the padding the CRC covers */
    } else if (!nv_read(NV_REC_CALIB, &a->cal, (uint16_t)sizeof a->cal)) {
        calib_nominal(&a->cal, a->p, a->serial); /* scalings for monitoring only: motor_id 0 fails below */
    } else {
        /* loaded */
    }
    a->cal_err = calib_check(&a->cal, a->p, a->serial);
    if (a->cal_err != 0u) {
        forbid(a, DTC_CALIB_INVALID); /* FW-20: any failure => no torque */
        a->n_x_rpm = 0.0f;            /* unknown motor: every decision takes the n >= n_x column */
    } else {
        a->n_x_rpm = motor_n_x_rpm(&a->cal.motor, a->p);
    }
    const float l = ti_minf(a->cal.motor.ld_h, a->cal.motor.lq_h); /* both axes stay under the ceiling */
    a->gains_ok = gains_default(a->p, a->cal.fsw_hz, l, a->cal.motor.rs_ohm, &a->gains);
    if (!a->gains_ok) {
        forbid(a, DTC_GAINS);
        a->gains.fsw_hz = a->p->fsw_hz[0];
    }
}

/* Round 14 (F01/F02/F06): arming needs every piece of evidence (arm_evidence.h); each missing one
 * is a DTC, forbids arming for the key cycle and is named in the CAN status. */
static void init_evidence(app_t *a)
{
    arm_validation_t v;
    const bool present = nv_read(NV_REC_VALIDATION, &v, (uint16_t)sizeof v);
    a->evidence = (uint8_t)(arm_evidence_platform() |
                            arm_validation_flags(&v, present, a->serial, a->p->sku, TI_FW_ID, a->p));
    const uint8_t pwm = ARM_EV_CONFIG_MATCHES | ARM_EV_PROTECTION_LOCKED;
    if ((a->evidence & pwm) != pwm) {
        forbid(a, DTC_PWM_LOCK); /* a matching image is not a locked one */
    }
    if ((a->evidence & (uint8_t)(ARM_EV_ROUTE_BOUND | ARM_EV_VALIDATED)) != (uint8_t)(ARM_EV_ROUTE_BOUND | ARM_EV_VALIDATED)) {
        forbid(a, DTC_ARM_EVIDENCE);
    }
}

/* Item 9: a stuck-on QDIS found in an earlier key cycle still forbids re-energising. */
static void init_service_lock(app_t *a)
{
    nv_service_t r;
    if (nv_read(NV_REC_DTC, &r, (uint16_t)sizeof r) && (r.magic == NV_SERVICE_MAGIC)) {
        a->service_required = true;
        forbid(a, DTC_QDIS_STUCK_ON);
    }
}

void app_init(app_t *a, const ti_params_t *p, const calib_t *cal, const uint8_t serial[8])
{
    (void)memset(a, 0, sizeof *a);
    a->p = p;
    (void)memcpy(a->serial, serial, 8u);
    dtc_init();
    br_init(&a->br, p); /* §9 step 1: every enable low, ASC_CLR latch high */
    gp_init(&a->gp);
    (void)hal_adc_init();
    (void)hal_sdadc_init(CARRIER_HZ);
    (void)hal_fs26_spi_init();
    (void)hal_can_init(HAL_CAN_VEHICLE);
    (void)hal_can_init(HAL_CAN_DIAG);
    (void)hal_wdog_init(50u);
    nv_init();
    key_cycle_init(a);
    dtc_new_cycle();
    if (ti_params_validate(p) != 0u) {
        forbid(a, DTC_PARAMS_INVALID);
    }
    init_calibration(a, cal);
    (void)hal_pwm_init(a->gains.fsw_hz, p->dead_time_ns);
    init_evidence(a);
    init_service_lock(a);
    set_watchdogs(a);
    a->swg_amp = SWG_AMP_DEFAULT;
    (void)hal_swg_start(CARRIER_HZ, a->swg_amp);
    fm_init(&a->fm, a->key_cycle);
    nv_desat_t rec;
    const bool rec_ok = nv_read(NV_REC_DESAT, &rec, (uint16_t)sizeof rec);
    fm_boot(&a->fm, &rec, rec_ok);
    init_fs26(a);
    init_identity(a);
    isns_init(&a->isns);
    vdc_init(&a->vdc);
    temp_init(&a->temp);
    hvil_init(&a->hvil);
    ign_init(&a->ign, true);
    rslv_init(&a->rslv);
    foc_reset(&a->foc);
    torque_lim_init(&a->tlim, p);
    dcl_reset(&a->dcl);
    can_cmd_init(&a->can);
    dis_init(&a->dis);
    pch_init(&a->pch);
    st_init(&a->st);
    sm_init(&a->sm);
    a->init = a->no_arm ? SM_FAIL : SM_OK;
    a->last_task_ms = hal_time_ms();
}

/* ======================= shared helpers ======================= */
static bool battery_present(const app_t *a, uint32_t t_ms)
{
    const bool closed = (a->can.contactors == TI_CONT_CLOSED) && can_cmd_fresh(&a->can, t_ms, a->p);
    const bool bms = can_bms_fresh(&a->can, t_ms, a->p);
    const bool at_pack = !a->vdc.valid || (ti_absf(a->vdc.vdc - a->can.v_pack) <= (a->p->vdc_bms_frac * a->can.v_pack));
    return closed && bms && at_pack;
}

static fm_ctx_t ctx_now(const app_t *a)
{
    const uint32_t t = hal_time_ms();
    fm_ctx_t c = {.speed_rpm = a->speed_rpm, .speed_known = a->speed_known, .battery_present = battery_present(a, t),
                  .asc_active = (a->br.mode == BR_ASC), .vdc_v = a->vdc.vdc, .now_ms = t, .key_cycle = a->key_cycle};
    if (a->isns.valid) {
        float al;
        float be;
        foc_clarke(a->isns.i_a, &al, &be);
        if (a->rslv.valid) {
            foc_park(al, be, rslv_theta_e_at(&a->rslv, &a->cal.rslv, hal_time_us(), a->p), &c.id_a, &c.iq_a);
        } else { /* no angle: the whole amplitude on the larger inductance (worst energy) */
            const float mag = sqrtf((al * al) + (be * be));
            *((a->cal.motor.lq_h >= a->cal.motor.ld_h) ? &c.iq_a : &c.id_a) = mag;
        }
    } else {
        c.iq_a = a->p->i_crest_a; /* unknown current: assume the worst the SKU allows */
    }
    return c;
}

/* Present phase-current amplitude (A peak, amplitude-invariant); the worst case when unknown. */
static float i_mag(const app_t *a)
{
    if (!a->isns.valid) {
        return a->p->i_crest_a;
    }
    float al;
    float be;
    foc_clarke(a->isns.i_a, &al, &be);
    return sqrtf((al * al) + (be * be));
}

/* Apply the combined §6 decision to the bridge (ISR or task context). */
static void apply_decision(app_t *a, uint32_t now_us)
{
    const ss_decision_t *d = &a->fm.dec;
    const bool in_asc = (a->br.mode == BR_ASC);
    if (d->spo_forced && (d->action == SS_ACT_SPO)) {
        a->mod_req = false; /* FLT latch / V5GD: the hardware already holds SPO */
        if (fm_active(&a->fm, SS_ROW_V5GD_LOSS) && (!a->v5gd_cleared || hal_gpio_read(HAL_DI_ASC_CMD_RB))) {
            hal_gpio_write(HAL_DO_ASC_REQ, false);
            br_asc_clear_pulse(); /* §4c: ASC_CLR, MCU_GATE_EN low, PWM low */
            a->v5gd_cleared = true;
        }
        br_spo(&a->br, true);
        return;
    }
    switch (d->action) {
    case SS_ACT_LS_ASC:
        a->mod_req = false;
        if (!in_asc && a->fm.asc_permitted) {
            br_enter_pwm_asc(&a->br, now_us, a->p);
        } else if (!a->fm.asc_permitted) {
            br_spo(&a->br, true);
        } else {
            /* holding PWM-ASC */
        }
        break;
    case SS_ACT_SPO_THEN_PWM_ASC:
        a->mod_req = false;
        if (!in_asc) {
            br_spo(&a->br, true);
        }
        break;
    case SS_ACT_SPO:
    case SS_ACT_ZERO_TORQUE_DCL:
        if (in_asc) {
            /* FW-06a / round 13: leave PWM-ASC only below n_x, and only once SPO is energy-safe for
             * the current still circulating (rule (a) or (b)) or that current is gone */
            const bool release = !d->high_speed && (d->rule_a || d->rule_b || (i_mag(a) < a->p->cal_spo_release_a));
            if (br_exit_asc(&a->br, release)) {
                br_spo(&a->br, true);
            }
        } else if (d->action == SS_ACT_SPO) {
            a->mod_req = false;
            br_spo(&a->br, true);
        } else {
            a->zero_now = true; /* FW-08: zero torque at the current-loop rate */
        }
        break;
    default:
        break; /* ramps are the torque path's job */
    }
}

/* ======================= current-loop ISR ======================= */
static void sense_fast(app_t *a, uint32_t now_us)
{
    uint16_t c[3];
    uint32_t t;
    (void)hal_adc_read_phase(c, &t);
    isns_update(&a->isns, c, t, now_us, a->cal.isns, a->p);
    uint16_t v[2];
    uint32_t tv[2];
    uint16_t vofs;
    uint16_t v5;
    uint32_t tx;
    (void)hal_adc_read(HAL_ADC_VDC1, &v[0], &tv[0]);
    (void)hal_adc_read(HAL_ADC_VDC2, &v[1], &tv[1]);
    (void)hal_adc_read(HAL_ADC_VOFS, &vofs, &tx);
    (void)hal_adc_read(HAL_ADC_V5GD, &v5, &tx);
    vdc_update(&a->vdc, v, tv, vofs, v5, now_us, a->cal.vdc, a->p);
    int16_t be[HAL_SDADC_BLOCK_N];
    int16_t bs[HAL_SDADC_BLOCK_N];
    int16_t bc[HAL_SDADC_BLOCK_N];
    if (hal_sdadc_read_block(HAL_SD_EXC, be, &t) && hal_sdadc_read_block(HAL_SD_SIN, bs, &t) &&
        hal_sdadc_read_block(HAL_SD_COS, bc, &t)) {
        rslv_update(&a->rslv, be, bs, bc, 1.0f / (float)CARRIER_HZ, t, &a->cal.rslv, a->p);
    }
}

/* F24: while modulating, every phase the reference asks for current must show it (current.c). The
 * reference is the one the loop tracked while these currents were sampled. */
static void stuck_check(app_t *a, uint32_t now_us)
{
    float ra;
    float rb;
    foc_ipark(a->foc.id_ref, a->foc.iq_ref, rslv_theta_e_at(&a->rslv, &a->cal.rslv, now_us, a->p), &ra, &rb);
    const float r3[3] = {ra, (-0.5f * ra) + ((0.5f * TI_SQRT3) * rb), (-0.5f * ra) - ((0.5f * TI_SQRT3) * rb)};
    isns_activity(&a->isns, r3, a->p);
}

static void control_fast(app_t *a, uint32_t now_us)
{
    if (a->zero_now) {
        a->iq_ref = 0.0f; /* FW-08: zero torque at the current-loop rate */
    }
    const bool can_modulate = a->mod_req && ((a->br.mode == BR_IDLE) || (a->br.mode == BR_MOD));
    if (!can_modulate) {
        if (a->br.mode == BR_MOD) {
            br_spo(&a->br, false); /* armed idle: PWM off, EN high */
        }
        return;
    }
    if ((a->br.mode == BR_MOD) && a->isns.valid && a->rslv.valid) {
        stuck_check(a, now_us);
    }
    if (!a->isns.valid || !a->rslv.valid || !a->vdc.valid) {
        /* no angle / current / voltage => no modulation; the §6 row at the current-loop rate */
        const fm_ctx_t c = ctx_now(a);
        const ss_row_t row = (!a->vdc.valid && a->vdc.v5gd_ok) ? SS_ROW_VDC_INVALID
                           : (!a->vdc.v5gd_ok ? SS_ROW_V5GD_LOSS : SS_ROW_RESOLVER_INVALID);
        br_spo(&a->br, false);
        a->mod_req = false;
        fm_raise(&a->fm, row, true, &c, &a->cal.motor, a->p);
        apply_decision(a, now_us);
        return;
    }
    a->foc.id_ref = a->id_ref;
    a->foc.iq_ref = a->iq_ref;
    const float th = rslv_theta_e_at(&a->rslv, &a->cal.rslv, now_us, a->p);
    const float w = rslv_omega_e(&a->rslv, &a->cal.rslv);
    if (!foc_step(&a->foc, a->isns.i_a, th, w, a->vdc.vdc, &a->cal.motor, &a->gains, a->p) ||
        !br_modulate(&a->br, a->foc.duty, a->p)) {
        br_spo(&a->br, false);
        a->mod_req = false;
        dtc_set(DTC_CTRL_NONFINITE, hal_time_ms());
    }
}

void app_isr_current(app_t *a)
{
    const uint32_t now_us = hal_time_us();
    a->n_isr++;
    br_service(&a->br); /* A12-R05: a drop left pending by the DESAT hold happens here once it has run */
    sense_fast(a, now_us);
    if (!a->offs_ok && (hal_pwm_mode() == HAL_PWM_OFF) && (a->offs_n < OFFSET_SAMPLES)) {
        for (uint32_t i = 0u; i < 3u; i++) { /* standstill zero-current reference (§9 step 3) */
            a->offs_acc[i] += a->isns.v_pin[i];
        }
        a->offs_n++;
        if (a->offs_n == OFFSET_SAMPLES) {
            const float m[3] = {a->offs_acc[0] / (float)OFFSET_SAMPLES, a->offs_acc[1] / (float)OFFSET_SAMPLES,
                                a->offs_acc[2] / (float)OFFSET_SAMPLES};
            a->offs_ok = isns_offset_ok(m, a->cal.isns, a->p);
        }
    }
    if (isns_oc(&a->isns, a->p) && !fm_active(&a->fm, SS_ROW_OVERCURRENT)) {
        const fm_ctx_t c = ctx_now(a); /* software backstop of the FW-05 hardware compare */
        dtc_set(DTC_OVERCURRENT, c.now_ms);
        fm_raise(&a->fm, SS_ROW_OVERCURRENT, true, &c, &a->cal.motor, a->p);
        apply_decision(a, now_us);
    }
    control_fast(a, now_us);
}

/* ======================= fault ISR (eFlexPWM FFLAG) ======================= */
void app_isr_fault(app_t *a)
{
    const uint32_t now_us = hal_time_us();
    const uint8_t f = hal_pwm_fault_flags();
    if (st_in_step_h(&a->st)) {
        return; /* FW-16 step h injects this FLT and checks FFLAG itself */
    }
    if ((f & HAL_PWM_FAULT_ADC_WD) != 0u) {
        const uint32_t wd = hal_adc_watchdog_status();
        const uint32_t ov = (1u << HAL_ADC_VDC1) | (1u << HAL_ADC_VDC2);
        if (((wd & ov) != 0u) && a->fm.asc_permitted && (a->br.mode != BR_ASC)) {
            br_enter_pwm_asc(&a->br, now_us, a->p); /* FW-06: first action, ASC_REQ edge */
        }
        a->zero_now = true;
        a->mod_req = false;
        const fm_ctx_t c = ctx_now(a);
        if ((wd & ov) != 0u) {
            dtc_set(DTC_OVERVOLTAGE, c.now_ms);
            fm_raise(&a->fm, SS_ROW_OVERVOLTAGE, true, &c, &a->cal.motor, a->p);
        }
        if ((wd & ~ov) != 0u) {
            dtc_set(DTC_OVERCURRENT, c.now_ms);
            fm_raise(&a->fm, SS_ROW_OVERCURRENT, true, &c, &a->cal.motor, a->p);
        }
        apply_decision(a, now_us);
    }
    if ((f & (HAL_PWM_FAULT_FLT_HS | HAL_PWM_FAULT_FLT_LS)) != 0u) {
        br_service(&a->br); /* A12-R05: the first sight of the FLT starts the DESAT hold */
        const bool hs_low = !hal_gpio_read(HAL_DI_FLT_HS_N);
        const bool ls_low = !hal_gpio_read(HAL_DI_FLT_LS_N);
        /* FW-15 step 1: PWM off now (the FAULT0/2 input already did it). MCU_GATE_EN is NOT dropped
         * here: it is an undelayed AND input, while the fault latch takes DRV_EN low only 22–53 us
         * after the FLT so the NSI6611 can finish its soft turn-off. The bridge drops EN after the
         * DESAT hold (cal_desat_en_hold_us). */
        br_spo(&a->br, true);
        a->mod_req = false;
        const fm_ctx_t c = ctx_now(a);
        if (ls_low && !fm_active(&a->fm, SS_ROW_FLT_LS)) {
            fm_desat(&a->fm, false, &c, &a->cal.motor, a->p);
        }
        if (hs_low && !fm_active(&a->fm, SS_ROW_FLT_HS)) {
            fm_desat(&a->fm, true, &c, &a->cal.motor, a->p);
        }
        if (hs_low || ls_low) {
            a->t_fault_us = now_us;
            a->rec_done_asc = false;
            a->rec_done_retry = false;
            apply_decision(a, now_us);
        }
    }
}

/* ======================= 1 kHz task ======================= */
static void sense_slow(app_t *a, uint32_t t_ms)
{
    uint16_t c;
    uint32_t t;
    hal_adc_start_slow();
    (void)hal_adc_read(HAL_ADC_IGN, &c, &t);
    ign_update(&a->ign, c, t_ms, a->p);
    (void)hal_adc_read(HAL_ADC_INTRLOK_N, &c, &t);
    hal_gpio_write(HAL_DO_INTRLOK_P, hvil_step(&a->hvil, c, t_ms, a->p));
    const hal_adc_sig_t ts[TEMP_COUNT] = {HAL_ADC_TMOD_U, HAL_ADC_TMOD_V, HAL_ADC_TMOD_W, HAL_ADC_NTC_H,
                                          HAL_ADC_NTC_A,  HAL_ADC_MT1,    HAL_ADC_MT2};
    uint16_t codes[TEMP_COUNT];
    for (uint32_t i = 0u; i < (uint32_t)TEMP_COUNT; i++) {
        (void)hal_adc_read(ts[i], &codes[i], &t);
    }
    temp_update(&a->temp, codes, t_ms, &a->cal.mt, a->p);
    if (a->rslv.valid) {
        a->speed_rpm = rslv_speed_rpm(&a->rslv, &a->cal.rslv);
        a->speed_valid_ms = t_ms;
    }
    /* after a resolver fault the last valid speed stays the basis of the §6 column for a bounded
     * time (inertia); then unknown = the n >= n_x column */
    a->speed_known = a->rslv.valid || (a->rslv.locked && !ti_elapsed(t_ms, a->speed_valid_ms, a->p->cal_speed_hold_ms));
    if (a->rslv.primed && ((t_ms % 50u) == 0u)) {
        const uint8_t amp = rslv_swg_trim(a->swg_amp, &a->rslv);
        if (amp != a->swg_amp) {
            a->swg_amp = amp;
            (void)hal_swg_start(CARRIER_HZ, amp); /* the trim only acts once written to SWG1 */
        }
    }
}

static void comms(app_t *a, uint32_t t_ms)
{
    hal_can_frame_t f;
    for (uint32_t i = 0u; (i < CAN_RX_MAX_PER_TICK) && hal_can_rx(HAL_CAN_VEHICLE, &f); i++) {
        (void)can_cmd_rx(&a->can, &f, t_ms, a->p);
    }
    if (a->can.n_crc > 0u) {
        dtc_set(DTC_CAN_E2E, t_ms);
        a->can.n_crc = 0u;
    }
    vdc_bms_check(&a->vdc, a->can.contactors == TI_CONT_CLOSED, can_bms_fresh(&a->can, t_ms, a->p), a->can.v_pack,
                  t_ms, a->p);
}

static bool monitoring(const app_t *a)
{
    return (a->sm.st != SM_OFF) && (a->sm.st != SM_INIT) && (a->sm.st != SM_SENSOR_SELFTEST) &&
           (a->sm.st != SM_SAFE_POWERDOWN);
}

static void flag(app_t *a, bool bad, ss_row_t row, bool latching, dtc_id_t d, const fm_ctx_t *c)
{
    if (bad) {
        if (d != DTC_NONE) {
            dtc_set(d, c->now_ms);
        }
        fm_raise(&a->fm, row, latching, c, &a->cal.motor, a->p);
    } else if (!latching) {
        fm_clear(&a->fm, row);
    } else {
        /* latched rows clear through a VCU reset */
    }
}

static void detect(app_t *a, const fm_ctx_t *c)
{
    const bool armed_states = (a->sm.st == SM_ARMED_ZERO_TORQUE) || (a->sm.st == SM_RUN) || (a->sm.st == SM_DERATE);
    if (monitoring(a)) {
        /* V5GD: §4c — SPO, ASC cleared, V_DC invalid, supply DTC, no arming */
        flag(a, !a->vdc.v5gd_ok, SS_ROW_V5GD_LOSS, true, DTC_V5GD, c);
        const bool vbad = a->vdc.v5gd_ok && !a->vdc.valid;
        const dtc_id_t vd = !a->vdc.vofs_ok ? DTC_VOFS : (a->vdc.disagree ? DTC_VDC_DISAGREE
                          : ((a->vdc.ch_failsafe[0] || a->vdc.ch_failsafe[1]) ? DTC_VDC_FAILSAFE : DTC_VDC_STALE));
        flag(a, vbad, SS_ROW_VDC_INVALID, true, vbad ? vd : DTC_NONE, c);
        const dtc_id_t rd = a->rslv.amp_fault ? DTC_RSLV_AMPLITUDE : (a->rslv.exc_fault ? DTC_RSLV_EXCITATION
                          : (a->rslv.trk_fault ? DTC_RSLV_TRACKING : (a->rslv.acc_fault ? DTC_RSLV_ACCEL : DTC_RSLV_RATE)));
        const bool rbad = !a->rslv.valid;
        flag(a, rbad, SS_ROW_RESOLVER_INVALID, true, rbad ? rd : DTC_NONE, c);
        const bool open = a->isns.open_wire[0] || a->isns.open_wire[1] || a->isns.open_wire[2];
        const bool ibad = !a->isns.valid;
        const dtc_id_t id = open ? DTC_ISNS_OPEN : (a->isns.sum_fault ? DTC_ISNS_SUM
                          : (a->isns.stuck_fault ? DTC_ISNS_STUCK : (!a->isns.fresh ? DTC_ISNS_STALE : DTC_ISNS_RANGE)));
        if (ibad) {
            flag(a, true, SS_ROW_RESOLVER_INVALID, true, id, c); /* control lost: same §6 row */
        }
    }
    if (armed_states) {
        const bool cont_lost = (a->can.contactors != TI_CONT_CLOSED) && (ti_absf(a->speed_rpm) >= a->n_x_rpm);
        flag(a, cont_lost || a->vdc.bms_mismatch, SS_ROW_BATTERY_LOST, false,
             a->vdc.bms_mismatch ? DTC_VDC_BMS : DTC_NONE, c);
        const bool hvil_bad = (a->hvil.status != HVIL_CLOSED) && (a->hvil.status != HVIL_UNKNOWN);
        if (hvil_bad) {
            dtc_set((a->hvil.status == HVIL_OPEN) ? DTC_HVIL_OPEN : DTC_HVIL_SHORT, c->now_ms);
        }
        const bool cmd_lost = !can_cmd_fresh(&a->can, c->now_ms, a->p) || hvil_bad;
        if (!can_cmd_fresh(&a->can, c->now_ms, a->p)) {
            dtc_set(DTC_CAN_TIMEOUT, c->now_ms);
        }
        flag(a, cmd_lost, SS_ROW_CMD_LOST, false, DTC_NONE, c);
        const bool bms = can_bms_fresh(&a->can, c->now_ms, a->p);
        if (!bms) {
            dtc_set(DTC_BMS_TIMEOUT, c->now_ms);
        }
        flag(a, bms && (a->can.p_chg_w <= 0.0f), SS_ROW_BMS_LIMIT_ZERO, false, DTC_NONE, c);
    }
    bool any = false;
    bool all = false;
    (void)temp_module_max(&a->temp, &any, &all);
    if (!all) {
        dtc_set(DTC_TEMP_MODULE, c->now_ms);
    }
}

static void recovery(app_t *a, uint32_t now_us, bool for_retry)
{
    if (!a->rec_active) {
        br_rec_start(&a->br, a->t_fault_us);
        a->rec_active = true;
        a->rec_for_retry = for_retry;
    }
    const br_rec_t r = br_rec_step(&a->br, now_us, a->p);
    if (r == BR_REC_DONE) {
        a->rec_active = false;
        a->br.rec = BR_REC_IDLE;
        if (a->rec_for_retry) {
            a->rec_done_retry = true;
        } else {
            a->rec_done_asc = true;
        }
    } else if (r == BR_REC_FAIL) {
        a->rec_active = false;
        a->br.rec = BR_REC_IDLE;
        dtc_set(DTC_FLT_RECOVERY_FAIL, hal_time_ms());
    } else {
        /* waiting */
    }
}

static void fault_actions(app_t *a, const fm_ctx_t *c, uint32_t now_us)
{
    if (!fm_active(&a->fm, SS_ROW_V5GD_LOSS)) {
        a->v5gd_cleared = false;
    }
    if (!fm_any(&a->fm)) {
        return;
    }
    const ss_decision_t *d = &a->fm.dec;
    if ((d->action == SS_ACT_SPO_THEN_PWM_ASC) && !a->fm.hs_reset_done && !a->rec_done_asc) {
        recovery(a, now_us, false); /* FLT_HS at n >= n_x: the FW-15 reset, then PWM-ASC */
        if (a->rec_done_asc) {
            fm_hs_reset_done(&a->fm);
            fm_update(&a->fm, c, &a->cal.motor, a->p);
            br_enter_pwm_asc(&a->br, now_us, a->p);
        }
        return;
    }
    if ((d->action == SS_ACT_SPO_THEN_PWM_ASC) && a->fm.hs_reset_done && (a->br.mode != BR_ASC)) {
        br_enter_pwm_asc(&a->br, now_us, a->p);
        return;
    }
    if (a->rec_active) {
        return; /* the FW-15 recovery owns MCU_GATE_EN for its reset pulse */
    }
    apply_decision(a, now_us);
}

static bool sensors_ok(const app_t *a)
{
    bool any = false;
    bool all = false;
    (void)temp_module_max(&a->temp, &any, &all);
    return a->rslv.valid && a->vdc.valid && a->isns.valid && a->offs_ok && any;
}

static void gather(app_t *a, sm_in_t *in, uint32_t t_ms)
{
    const bool fresh = can_cmd_fresh(&a->can, t_ms, a->p);
    *in = (sm_in_t){.now_ms = t_ms, .ign_on = a->ign.on, .init = a->init, .sensors_ok = sensors_ok(a),
                    .v5gd_ok = a->vdc.v5gd_ok, .fs0b_released = a->fs0b_released,
                    .gate_power_ready = (a->gp.st == GP_READY), .gate_power_failed = a->gp.timeout_dtc,
                    .cmd_fresh = fresh, .enable_req = a->can.enable_req, .contactors = a->can.contactors,
                    .shutdown_req = fresh && a->can.shutdown_req, .discharge_req = fresh && a->can.discharge_req,
                    .selftest = a->selftest, .fault_needed = fm_needs_fault_state(&a->fm) || a->no_arm,
                    .derate_active = a->tlim.derate_active,
                    .discharge_done = (a->dis.st == DIS_DONE) || (a->dis.st == DIS_ABORTED) ||
                                      (a->vdc.valid && (a->vdc.hv == TI_HV_SAFE)),
                    .nvm_idle = nv_idle(), .speed_rpm = a->speed_rpm, .n_x_rpm = a->n_x_rpm};
    in->torque_req_nm = can_dir_interlock(&a->dir, a->can.gear, a->can.torque_req_nm, a->speed_rpm, a->p);
    in->torque_ramped_out = ti_absf(a->t_cmd_nm) < MOD_TORQUE_NM;
    in->link_at_pack = battery_present(a, t_ms);
    const pch_result_t pr = a->pch.res;
    in->precharge = ((pr == PCH_REFUSE_PLATEAU) || (pr == PCH_REFUSE_TAU) || (pr == PCH_REFUSE_TIMEOUT)) ? SM_FAIL
                    : ((pr == PCH_OK) ? SM_OK : SM_BUSY);
    in->flt_low_at_boot = a->vdc.v5gd_ok && (!hal_gpio_read(HAL_DI_FLT_HS_N) || !hal_gpio_read(HAL_DI_FLT_LS_N));
    in->rdy_before_gate_power = a->rdy_early;
    in->retry_allowed = fm_retry_allowed(&a->fm, fresh && a->can.desat_retry_auth, t_ms, a->p) && a->speed_known &&
                        (ti_absf(a->speed_rpm) < a->n_x_rpm);
    in->recovery_done = a->rec_done_retry && a->fm.retry_used && (a->sm.st == SM_FAULT);
}

static void selftest_step(app_t *a, uint32_t t_ms)
{
    if (a->selftest != SM_BUSY) {
        return;
    }
    const st_cond_t c = {.rdy_both = gp_rdy_both(), .vdc_both_valid = a->vdc.valid,
                         .v_ch = {a->vdc.v_ch[0], a->vdc.v_ch[1]}, .contactors_open = (a->can.contactors == TI_CONT_OPEN),
                         .resolver_valid = a->rslv.valid, .speed_rpm = a->speed_rpm,
                         .n_ss_rpm = (a->cal_err == 0u) ? motor_n_ss_rpm(&a->cal.motor, a->p) : 0.0f,
                         .pwm_low = (hal_pwm_mode() == HAL_PWM_OFF),
                         .flt_high = hal_gpio_read(HAL_DI_FLT_HS_N) && hal_gpio_read(HAL_DI_FLT_LS_N)};
    const st_res_t r = st_step(&a->st, &c, &a->br, &a->fs, &a->dis, &a->vdc, a->can.contactors, t_ms, a->p);
    if (r == ST_RES_PASS) {
        const nv_selftest_t rec = {.key_cycle = a->key_cycle, .passed = 1u};
        (void)nv_queue(NV_REC_SELFTEST, &rec, (uint16_t)sizeof rec);
        a->selftest = SM_OK;
    } else if (r == ST_RES_FAIL) {
        const nv_selftest_t rec = {.key_cycle = a->key_cycle, .passed = 0u, .failed_step = (uint8_t)a->st.failed};
        (void)nv_queue(NV_REC_SELFTEST, &rec, (uint16_t)sizeof rec);
        forbid(a, DTC_SELFTEST_FAIL);
        a->selftest = SM_FAIL;
    } else if (r == ST_RES_SKIP) {
        nv_selftest_t rec;
        const bool ok = nv_read(NV_REC_SELFTEST, &rec, (uint16_t)sizeof rec) && (rec.passed != 0u) &&
                        ((rec.key_cycle + 1u) >= a->key_cycle);
        if (!ok) {
            forbid(a, DTC_SELFTEST_NO_PASS);
        }
        a->selftest = ok ? SM_OK : SM_FAIL;
    } else {
        /* running */
    }
}

static void execute(app_t *a, uint32_t t_ms, uint32_t now_us)
{
    const sm_out_t *o = &a->so;
    if (o->req_flt_clear) {
        br_flt_clear_pulse();
    }
    if (a->cold_start && !a->gate_power_requested && (hal_gpio_read(HAL_DI_RDY_HS) || hal_gpio_read(HAL_DI_RDY_LS))) {
        /* after a power-on, gate power before §9 step 6 means FS_GPIO1 is slotted in OTP (after an
         * MCU reset the FS26 holds FS_GPIO1 high on purpose: gate power for FS1B-ASC) */
        a->rdy_early = true;
        dtc_set(DTC_FS26_GPIO1_OTP, t_ms);
    }
    if (o->req_fs0b_release && !a->fs0b_released) {
        const fs26_status_t s = fs26_release_safety_outputs(&a->fs);
        a->fs0b_released = (s == FS26_OK);
    }
    if (o->req_asc_decision) {
        a->asc_hold = !a->speed_known || (ti_absf(a->speed_rpm) >= a->n_x_rpm); /* §9 step 5 */
        if (a->asc_hold) {
            hal_gpio_write(HAL_DO_ASC_REQ, false);
            hal_gpio_write(HAL_DO_ASC_REQ, true); /* idempotent: PWM-ASC takes over once armed */
        } else {
            br_asc_clear_pulse();
        }
    }
    if (o->req_gate_power && !a->gate_power_requested) {
        a->gate_power_requested = gp_request_on(&a->gp, &a->fs, a->fs0b_released, t_ms);
    }
    if (o->req_selftest) {
        selftest_step(a, t_ms);
    }
    if (o->req_recovery && !a->rec_done_retry) {
        recovery(a, now_us, true); /* the one VCU-authorised retry (>= 1 s, n < n_x) */
        if (a->rec_done_retry) {
            const fm_ctx_t c = ctx_now(a);
            fm_retry_consumed(&a->fm, &c);
        }
    }
    if (o->req_discharge && (a->dis.st == DIS_IDLE)) {
        (void)dis_request(&a->dis, a->can.contactors, &a->vdc, true, t_ms, a->p);
    }
    if (!o->req_discharge && ((a->dis.st == DIS_DONE) || (a->dis.st == DIS_ABORTED)) && (a->st.s != ST_TOPUP)) {
        a->dis.st = DIS_IDLE;
    }
    if ((a->sm.st == SM_FAULT) && can_cmd_fresh(&a->can, t_ms, a->p) && a->can.fault_reset_req) {
        const fm_ctx_t c = ctx_now(a);
        if (fm_reset_latched(&a->fm, &c, &a->cal.motor, a->p) && !fm_active(&a->fm, SS_ROW_OVERVOLTAGE) &&
            !fm_active(&a->fm, SS_ROW_OVERCURRENT)) {
            hal_adc_watchdog_clear(0xFFFFFFFFu);
            (void)hal_pwm_fault_clear(HAL_PWM_FAULT_ADC_WD);
        }
    }
    if (o->req_lpoff) {
        br_spo(&a->br, true);
        gp_off(&a->gp, &a->fs);
        (void)fs26_goto_lpoff(&a->fs);
    }
}

static void arming(app_t *a, uint32_t now_us, uint32_t t_ms)
{
    if (fm_any(&a->fm) && (a->fm.dec.action >= SS_ACT_ZERO_TORQUE_DCL)) {
        return; /* the §6 decision owns the bridge */
    }
    if (!a->so.arm || a->no_arm) {
        if (a->br.mode == BR_ASC) {
            (void)br_exit_asc(&a->br, a->speed_known && (ti_absf(a->speed_rpm) < a->n_x_rpm));
        }
        if ((a->br.mode == BR_IDLE) || (a->br.mode == BR_MOD)) {
            a->mod_req = false;
            br_spo(&a->br, true);
        }
        return;
    }
    if (a->asc_hold) {
        if (a->br.mode != BR_ASC) {
            br_enter_pwm_asc(&a->br, now_us, a->p); /* §9 step 5/8: PWM-ASC takes over */
        }
        const bool cc_ready = a->isns.valid && a->rslv.valid && a->vdc.valid && a->gains_ok;
        const bool slow = a->speed_known && (ti_absf(a->speed_rpm) < a->n_x_rpm);
        if (slow || (battery_present(a, t_ms) && cc_ready)) {
            if (br_exit_asc(&a->br, true)) { /* FW-06a: MCU-commanded exit */
                a->asc_hold = false;
            }
        }
        return;
    }
    if (a->br.mode == BR_DISARMED) {
        (void)br_arm_idle(&a->br);
    }
}

static void torque_path(app_t *a, uint32_t t_ms)
{
    const ti_params_t *p = a->p;
    const bool fresh = can_cmd_fresh(&a->can, t_ms, p);
    const float w_mech = a->speed_rpm / TI_RPM_PER_RAD_S;
    bool any = false;
    bool all = false;
    const float tmod = temp_module_max(&a->temp, &any, &all);
    const float i_now = sqrtf(0.5f * ((a->foc.id * a->foc.id) + (a->foc.iq * a->foc.iq)));
    torque_derate_update(&a->tlim, tmod, any, a->can.coolant_c, a->can.coolant_valid, i_now, 1.0e-3f, p);
    torque_limits(&a->tlim, a->vdc.vdc, w_mech, a->can.p_chg_w, a->can.p_dis_w, can_bms_fresh(&a->can, t_ms, p), p);
    float target = 0.0f;
    if (a->so.torque_enable && fresh && a->can.enable_req) {
        target = can_dir_interlock(&a->dir, a->can.gear, a->can.torque_req_nm, a->speed_rpm, p);
        if (a->so.torque_reduced) {
            const float lim = p->cal_desat_retry_torque_frac * p->cal_torque_max_nm;
            target = ti_clampf(target, -lim, lim);
        }
        target = torque_clamp(&a->tlim, target, w_mech);
    }
    const ss_action_t act = fm_any(&a->fm) ? a->fm.dec.action : SS_ACT_NONE;
    if (act >= SS_ACT_SPO) {
        a->t_cmd_nm = 0.0f; /* the §6 decision owns the bridge */
    } else if (act == SS_ACT_ZERO_TORQUE_DCL) {
        a->t_cmd_nm = dcl_step(&a->dcl, p->vdc_max_v, a->vdc.vdc, w_mech, 1.0e-3f, p); /* FW-08 */
    } else if (fresh && (act != SS_ACT_RAMP_KEEP_CC) && (act != SS_ACT_RAMP_THEN_SPO)) {
        a->t_cmd_nm = target;
        a->zero_now = false;
    } else {
        a->t_cmd_nm = ti_ramp(a->t_cmd_nm, 0.0f, p->cal_torque_ramp_nm_s * 1.0e-3f); /* FW-11: ramp, not hold */
    }
    const bool armed = (a->br.mode == BR_IDLE) || (a->br.mode == BR_MOD);
    const bool fw_needed = !a->speed_known || (ti_absf(a->speed_rpm) >= a->n_x_rpm);
    const bool dcl = (act == SS_ACT_ZERO_TORQUE_DCL) && (i_mag(a) >= p->cal_spo_release_a);
    a->mod_req = armed && a->so.arm && !a->no_arm &&
                 ((ti_absf(a->t_cmd_nm) >= MOD_TORQUE_NM) || fw_needed || dcl || (act == SS_ACT_RAMP_KEEP_CC));
    if (!a->mod_req) {
        a->foc.xi_d = 0.0f;
        a->foc.xi_q = 0.0f;
    }
    float id = 0.0f;
    float iq = 0.0f;
    const float i_max = TI_SQRT2 * a->tlim.i_limit_rms_a;
    const tq_res_t tr = torque_to_current(a->t_cmd_nm, motor_omega_e(a->speed_rpm, &a->cal.motor), a->vdc.vdc, i_max,
                                          &a->cal.motor, &a->cal.mtpa, p, &id, &iq);
    const bool relevant = armed && a->vdc.valid; /* the reference is used: not a blind link reading */
    if (tr == TQ_NONFINITE) {
        dtc_set(DTC_CTRL_NONFINITE, t_ms);
        a->t_cmd_nm = 0.0f;
    } else if (tr == TQ_INFEASIBLE) {
        /* F23: not even iq = 0 fits the voltage inside the demagnetisation/current limits: zero
         * torque at the least-voltage id, ask the VCU to limit the speed, record it */
        a->t_cmd_nm = 0.0f;
        if (relevant) {
            dtc_set(DTC_TORQUE_INFEASIBLE, t_ms);
        }
    } else {
        /* TQ_OK or TQ_LIMITED: the pair passed the voltage-feasibility witness */
    }
    a->speed_limit_req = (tr == TQ_INFEASIBLE) && relevant;
    a->id_ref = id;
    a->iq_ref = a->zero_now ? 0.0f : iq;
}

static void status_tx(app_t *a, uint32_t t_ms)
{
    if (!ti_elapsed(t_ms, a->last_tx_ms, STATUS_PERIOD_MS)) {
        return;
    }
    a->last_tx_ms = t_ms;
    bool any = false;
    bool all = false;
    const uint8_t bridge[4] = {0u, 1u, 2u, 3u};
    const can_status_t s = {.state = (uint8_t)a->sm.st, .bridge = bridge[a->br.mode], .hv = dis_hv_state(&a->vdc),
                            .self_test_done = a->so.self_test_done, .keep_hv = a->fm.keep_hv,
                            .derate = a->tlim.derate_active, .fault = (a->sm.st == SM_FAULT),
                            .zero_torque = ti_absf(a->t_cmd_nm) < MOD_TORQUE_NM, .discharging = dis_output(&a->dis),
                            .precharge_refused = (a->pch.res >= PCH_REFUSE_PLATEAU), .speed_valid = a->rslv.valid,
                            .torque_nm = a->t_cmd_nm, .speed_rpm = a->speed_rpm, .vdc_v = a->vdc.vdc,
                            .vdc_valid = a->vdc.valid, .t_module_c = temp_module_max(&a->temp, &any, &all),
                            .n_dtc = dtc_confirmed_count(), .first_dtc = (uint16_t)dtc_first_active(),
                            .no_safe_state = a->fm.no_safe_state, .service_required = a->service_required,
                            .open_contactors_req = a->service_required, .speed_limit_req = a->speed_limit_req,
                            .evidence_missing = (uint8_t)(ARM_EV_ALL & (uint8_t)~a->evidence)};
    hal_can_frame_t f;
    can_status_encode(&s, a->tx_ctr++, &f);
    (void)hal_can_tx(HAL_CAN_VEHICLE, &f);
}

/* Item 9: an unexpected link discharge (stuck-on QDIS) latches "service required": no arming, the
 * VCU is asked to open the contactors and not to re-energise; kept in NVM across key cycles. */
static void service_lock(app_t *a)
{
    if (a->dis.stuck_on && !a->service_required) {
        a->service_required = true;
        forbid(a, DTC_QDIS_STUCK_ON);
        const nv_service_t r = {.magic = NV_SERVICE_MAGIC, .dtc = (uint16_t)DTC_QDIS_STUCK_ON, .key_cycle = a->key_cycle};
        (void)nv_queue(NV_REC_DTC, &r, (uint16_t)sizeof r);
    }
}

/* Round 14: the platform evidence is read back every tick. A loss forbids arming; if the bridge is
 * armed it is "control lost", so §6 (not a blind SPO) decides the bridge action. */
static void evidence_watch(app_t *a, const fm_ctx_t *c)
{
    const uint8_t now = (uint8_t)(arm_evidence_platform() | (a->evidence & ARM_EV_VALIDATED));
    const uint8_t lost = (uint8_t)(a->evidence & (uint8_t)~now);
    a->evidence = (uint8_t)(a->evidence & now);
    if (lost != 0u) {
        forbid(a, ((lost & ARM_EV_ROUTE_BOUND) != 0u) ? DTC_ARM_EVIDENCE : DTC_PWM_LOCK);
        if ((a->br.mode == BR_IDLE) || (a->br.mode == BR_MOD) || (a->br.mode == BR_ASC)) {
            fm_raise(&a->fm, SS_ROW_RESOLVER_INVALID, true, c, &a->cal.motor, a->p);
        }
    }
}

void app_task_1ms(app_t *a)
{
    const uint64_t t64 = hal_time_us64(); /* A12-R06: one read per tick keeps the 64-bit extension */
    const uint32_t now_us = (uint32_t)t64;
    const uint32_t t_ms = ti_ms_from_us64(t64);
    hal_wdog_kick();
    br_service(&a->br);
    sense_slow(a, t_ms);
    comms(a, t_ms);
    if (fs26_wd_due(&a->fs, now_us) && (fs26_wd_refresh(&a->fs) != FS26_OK)) {
        dtc_set(DTC_FS26_WD, t_ms);
    }
    if ((a->sm.st != SM_GATE_SELFTEST) || (a->selftest != SM_BUSY)) {
        (void)gp_step(&a->gp, t_ms, a->p); /* FW-16 d/e/g drop RDY on purpose */
    }
    fm_ctx_t c = ctx_now(a);
    detect(a, &c);
    evidence_watch(a, &c);
    fm_update(&a->fm, &c, &a->cal.motor, a->p);
    fault_actions(a, &c, now_us);
    sm_in_t in;
    gather(a, &in, t_ms);
    sm_step(&a->sm, &in, &a->so, a->p);
    execute(a, t_ms, now_us);
    arming(a, now_us, t_ms);
    if ((a->sm.st == SM_PRECHARGE_WAIT) || (a->pch.res == PCH_RUNNING)) {
        const pch_result_t pr = pch_step(&a->pch, a->can.contactors, &a->vdc, a->can.v_pack,
                                         can_bms_fresh(&a->can, t_ms, a->p), t_ms, a->p);
        if ((pr == PCH_REFUSE_PLATEAU) || (pr == PCH_REFUSE_TAU) || (pr == PCH_REFUSE_TIMEOUT)) {
            a->no_arm = true; /* FW-19: refuse to arm (a shorted QDIS/string does not heal) */
        }
    }
    dis_step(&a->dis, a->can.contactors, &a->vdc, a->br.mode == BR_MOD, t_ms, a->p);
    service_lock(a);
    hal_gpio_write(HAL_DO_QDIS, dis_output(&a->dis));
    torque_path(a, t_ms);
    if (a->rslv.valid && (a->br.mode == BR_MOD)) {
        bool mv = false;
        const float wm = foc_omega_model(&a->foc, &a->cal.motor, &mv);
        rslv_rate_check(&a->rslv, wm, mv, &a->cal.rslv, a->p);
    }
    status_tx(a, t_ms);
    a->last_task_ms = t_ms;
}

void app_idle(app_t *a)
{
    (void)a;
    nv_service();
    if (nv_idle()) {
        fm_retained_commit(); /* the DESAT record reached NVM */
    }
}
