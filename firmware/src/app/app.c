/* app.c — integration of sensing, control, safety and comms on the HAL. */
#include "app.h"

#include <string.h>

#include "adc.h"
#include "can.h"
#include "commission.h" /* round 23: FW-39 */
#include "dtc.h"
#include "gpio.h"
#include "nvlog.h"
#include "pwm.h"
#include "sdadc.h"
#include "spi_fs26.h"
#include "swg.h"
#include "ti_crc.h"
#include "timer.h"
#include "wdog.h"

#define SESSION_MAGIC 0x4B435943u /* "KCYC" */
#define CARRIER_HZ 10000u
#define RSLV_TRIM_MS 5u /* SWG trim period: the ramp from cal_swg_code_init takes a few steps */
#define OFFSET_SAMPLES 64u
#define CAN_RX_MAX_PER_TICK 16u
#define STATUS_PERIOD_MS 10u
#define MOD_TORQUE_NM 0.5f
#define DIAG_RX_MAX_PER_TICK 4u

/* FW-32: the SecurityAccess key function is a build-time hook (-DTI_UDS_KEY_FN=<function>). None by default:
 * every seed request is refused, so the service lock can never be cleared (fail closed).
 * TODO(REL): the product build supplies the OEM key algorithm (e.g. a MAC in the HSE) and draws the seed from
 * the HSE TRNG (diag_seed below is unique, not random). */
#ifdef TI_UDS_KEY_FN
bool TI_UDS_KEY_FN(const uint8_t seed[UDS_SA_LEN], uint8_t key[UDS_SA_LEN]);
#define UDS_KEY_FN TI_UDS_KEY_FN
#else
#define UDS_KEY_FN NULL
#endif

#include "update.h" /* FW-38 — here, not above: the release-build marker above is cited by its line number */
#include "capture.h" /* FW-41 — also below the key-hook marker, so its cited line stays put */

app_t g_app;
TI_RETAINED app_session_t g_app_session;

void app_fault_isr_entry(void) { app_isr_fault(&g_app); }

uint32_t app_isr_period_us(const app_t *a) { return 500000u / a->gains.fsw_hz; }

/* Round 23 (item 6): after a resolver fault the speed is bounded, not forgotten — it cannot have grown faster than the
 * vehicle can accelerate the motor with the inverter off (cal_speed_accel_max_rpm_s: the grade, the other axle). While
 * the resolver is valid the bound is |n| (its age at most one task). */
float app_speed_hi_rpm(const app_t *a, uint32_t t_ms)
{
    return ti_absf(a->speed_rpm) + (a->p->cal_speed_accel_max_rpm_s * (float)ti_age(t_ms, a->speed_valid_ms) * 1.0e-3f);
}

/* Time (A12-R06): microsecond intervals use hal_time_us(); every millisecond time stamp comes from
 * hal_time_ms() (or the task's own 64-bit read), never from hal_time_us() / 1000. */

/* ======================= init ======================= */
static void set_watchdogs(app_t *a)
{
    uint16_t lo;
    uint16_t hi;
    const hal_adc_sig_t ph[3] = {HAL_ADC_ISNS_U, HAL_ADC_ISNS_V, HAL_ADC_ISNS_W};
    for (uint32_t i = 0u; i < 3u; i++) {
        isns_oc_codes(&a->ofs.run[i], a->p->i_oc_trip_a, &lo, &hi); /* FW-05; round 23 (FW-44): the working offsets */
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
    g_app_session.rslv_restarts = 0u; /* round 19: the budget is per key cycle, kept across an MCU reset inside it */
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
    ti_acq_last_t last = {0};
    bool fresh = true;
    for (uint32_t i = 0u; i < 8u; i++) {
        uint32_t t;
        hal_adc_start_slow(); /* round 15: HW_ID (ADC3_P0) is on the slow list, which nothing has run yet */
        hal_delay_us(100u);
        const bool seen = hal_adc_read(HAL_ADC_HW_ID, &codes[i], &t);
        /* round 24 (F241): FW-01's stable reading is eight conversions, each a new one — a converter that never ran
         * reads code 0 (it was "short"), one that stopped repeats one conversion eight times: the identity unknown */
        fresh = (ti_acq(&last, seen, t, hal_time_us(), a->p->cal_temp_hold_ms * 1000u) == TI_ACQ_NEW) && fresh;
    }
    const hwid_result_t r = fresh ? hwid_classify_stable(codes, 8u, &a->hw_sku) : HWID_UNSTABLE;
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
    /* round 23 (FW-46): the ripple table's mean (never applied: a feed-forward of the ripple, no torque of its own) and its
     * extremes around it inside cal_ripple_ff_max_a — what the task's constraint check scales */
    int32_t sum = 0; /* in counts: a constant table is exactly its mean */
    for (uint32_t k = 0u; k < TQ_RIPPLE_N; k++) {
        sum += a->cal.ripple_ff[k];
    }
    a->rip_mean = (float)sum / (float)TQ_RIPPLE_N;
    const float lim = a->p->cal_ripple_ff_max_a;
    a->rip_lo = 0.0f;
    a->rip_hi = 0.0f;
    for (uint32_t k = 0u; k < TQ_RIPPLE_N; k++) {
        const float v = ti_clampf(0.01f * ((float)a->cal.ripple_ff[k] - a->rip_mean), -lim, lim);
        a->rip_lo = ti_minf(a->rip_lo, v);
        a->rip_hi = ti_maxf(a->rip_hi, v);
    }
    a->rip_k = 0.0f;
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

/* FW-32 (round 17): the stuck-on QDIS lock is cleared only by the UDS routine, after the SecurityAccess
 * unlock, never with HV present or unknown or the bridge armed. The clear rewrites the NVM record as CLEARED
 * with this key cycle (it takes effect at the next power-up: this key cycle keeps the lock, so the latched
 * verdict cannot re-latch it) and sets DTC_SERVICE_LOCK_CLEARED. Nothing written: NRC, the lock stays. */
static uint8_t service_clear(void *ctx)
{
    app_t *a = (app_t *)ctx;
    if ((dis_hv_state(&a->vdc) != TI_HV_SAFE) || (a->br.mode != BR_DISARMED)) {
        return UDS_NRC_CONDITIONS;
    }
    if (!a->service_required) {
        return 0u; /* nothing to clear */
    }
    const nv_service_t r = {.magic = NV_SERVICE_CLEARED, .dtc = (uint16_t)DTC_QDIS_STUCK_ON, .key_cycle = a->key_cycle};
    if (!nv_queue(NV_REC_DTC, &r, (uint16_t)sizeof r)) {
        return UDS_NRC_PROGRAMMING;
    }
    dtc_set(DTC_SERVICE_LOCK_CLEARED, hal_time_ms());
    return 0u;
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

/* FW-38: the programming session (boot/update.h) is entered only with the bridge disarmed and the link discharged —
 * the FW-32 conditions — and the motor at standstill — the FW-16 condition: resolver valid, |n| < n_ss (no valid
 * calibration: no standstill proven). The activation checks them again. */
static uint8_t update_conditions(void *ctx)
{
    const app_t *a = (const app_t *)ctx;
    const float n_ss = (a->cal_err == 0u) ? motor_n_ss_rpm(&a->cal.motor, a->p) : 0.0f;
    const bool standstill = a->rslv.valid && (ti_absf(a->speed_rpm) < n_ss);
    return ((dis_hv_state(&a->vdc) == TI_HV_SAFE) && (a->br.mode == BR_DISARMED) && standstill) ? 0u
                                                                                               : UDS_NRC_CONDITIONS;
}

/* FW-38: the update state cannot arm. Entering it withdraws the EOL/HIL-validated arming evidence (FW-24: the record
 * vouches for the image that runs, and a programming session is about to change the one that will) and forbids
 * arming until the next power-up: FAULT, INV_STATUS b15 names the missing evidence, DTC_FW_UPDATE says why. The §6
 * protective actions keep their authority. */
static void update_enter(void *ctx)
{
    app_t *a = (app_t *)ctx;
    a->evidence = (uint8_t)(a->evidence & (uint8_t)~ARM_EV_VALIDATED);
    forbid(a, DTC_FW_UPDATE);
}

/* Round 23 (FW-43/FW-44): the run-time record — the statistics carry on from it; its tracked current offsets become
 * the working calibration only when bound to THIS calibration record (its CRC) and inside the EOL tolerance, else
 * the EOL offsets (the record's adoption key cycle is then cleared). Before set_watchdogs(): FW-05 follows them. */
static void init_runtime(app_t *a)
{
    nv_runtime_t r;
    const bool ok = nv_read(NV_REC_RUNTIME, &r, (uint16_t)sizeof r) && (r.version == RS_LAYOUT_VERSION);
    rs_init(&a->rs, ok ? &r : NULL, hal_time_ms());
    const bool bound = ok && (a->cal_err == 0u) && (r.ofs_key_cycle != 0u) && (r.cal_crc == a->cal.crc32);
    ofs_init(&a->ofs, a->cal.isns, bound ? a->rs.rec.ofs_v : NULL, a->p);
    if (!bound) {
        a->rs.rec.ofs_key_cycle = 0u;
    }
    ovs_init(&a->ovs);
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
    (void)hal_sdadc_init(CARRIER_HZ, p->cal_sd_irq_lat_max_us, p->cal_swg_start_lat_us);
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
    init_runtime(a); /* round 23: FW-43 statistics, FW-44 working current calibration */
    (void)hal_pwm_init(a->gains.fsw_hz, p->dead_time_ns);
    init_evidence(a);
    init_service_lock(a);
    set_watchdogs(a);
    a->swg_amp = p->cal_swg_code_init; /* A14-N01: low, then up under the trim — never the register maximum */
    (void)hal_swg_start(CARRIER_HZ, a->swg_amp);
    fm_init(&a->fm, a->key_cycle);
    nv_desat_t rec;
    const bool rec_ok = nv_read(NV_REC_DESAT, &rec, (uint16_t)sizeof rec);
    fm_boot(&a->fm, &rec, rec_ok);
    if (a->fm.desat_blocked) {
        /* round 23 (item 4, found by the bridge self-test): the block is a FAULT (fm_needs_fault_state) and the DTC store
         * is RAM — the recorded DESAT is raised again, or that FAULT names nothing (0x14 keeps it, as it keeps the block) */
        dtc_set((a->fm.desat_bank == 2u) ? DTC_DESAT_LS : DTC_DESAT_HS, hal_time_ms());
    }
    init_fs26(a);
    init_identity(a);
    isns_init(&a->isns);
    vdc_init(&a->vdc);
    temp_init(&a->temp);
    hvil_init(&a->hvil);
    vsup_init(&a->vsup);
    ign_init(&a->ign, true);
    rslv_init(&a->rslv);
    foc_reset(&a->foc);
    torque_lim_init(&a->tlim, p);
    dcl_reset(&a->dcl);
    can_cmd_init(&a->can);
    uds_init(&a->uds, UDS_KEY_FN, service_clear, a);
    mc_init(); /* round 23 (FW-39): the commissioning service mode — idle, nothing staged, its default CALs */
    upd_init(update_conditions, update_enter, a); /* FW-38: the programming session's conditions and entry */
    dis_init(&a->dis);
    pch_init(&a->pch);
    st_init(&a->st);
    sm_init(&a->sm);
    a->init = a->no_arm ? SM_FAIL : SM_OK;
    a->last_task_ms = hal_time_ms();
    a->t_isr_us = hal_time_us();
    cap_init(a); /* FW-41: the waveform capture, armed with its default configuration */
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
    /* round 23 (item 6): with the resolver invalid the §6 decisions take the speed's upper bound (its sign the last one) */
    const float n = a->rslv.valid ? a->speed_rpm : ((a->speed_rpm < 0.0f) ? -app_speed_hi_rpm(a, t) : app_speed_hi_rpm(a, t));
    fm_ctx_t c = {.speed_rpm = n, .speed_known = a->speed_known, .battery_present = battery_present(a, t),
                  .asc_active = (a->br.mode == BR_ASC), .vdc_v = a->vdc.vdc, .now_ms = t, .key_cycle = a->key_cycle};
    bool t_any = false; /* FW-40: the operating context of a fault record (nvlog.h: nv_fault_ctx_t) */
    bool t_all = false;
    const float t_mod = temp_module_max(&a->temp, &t_any, &t_all);
    c.op = (nv_fault_ctx_t){.ext = 1u, .state = (uint8_t)a->sm.st,
                            .t_valid = (uint8_t)((t_any ? 1u : 0u) | (a->temp.ch[TEMP_MT1].valid ? 2u : 0u) |
                                                 (a->temp.ch[TEMP_MT2].valid ? 4u : 0u)),
                            .t_cmd_nm = a->t_cmd_nm, .t_act_nm = a->t_act_nm, .t_mod_c = t_mod,
                            .t_mt1_c = a->temp.ch[TEMP_MT1].t_c, .t_mt2_c = a->temp.ch[TEMP_MT2].t_c};
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
    case SS_ACT_ZERO_CURRENT:
        if (in_asc) {
            /* FW-06a / round 13: leave PWM-ASC only below n_x, and only once SPO is energy-safe for
             * the current still circulating (rule (a) or (b)) or that current is gone */
            const bool release = !d->high_speed && (d->rule_a || d->rule_b || (i_mag(a) < a->p->cal_spo_release_a));
            if (br_exit_asc(&a->br, release)) {
                br_spo(&a->br, true);
            }
        } else if ((d->action == SS_ACT_SPO) || (i_mag(a) < a->p->cal_spo_release_a)) {
            a->mod_req = false; /* SPO, or §6 battery lost below n_x with the current gone (round 15) */
            br_spo(&a->br, true);
        } else {
            a->zero_now = true; /* FW-08: zero current (id = iq = 0) at the current-loop rate while it decays */
        }
        break;
    default:
        break; /* ramps are the torque path's job */
    }
}

/* Round 23 (item 5): an LS-ASC entry at speed shorts a winding that carries its back-EMF: the current overshoots its
 * steady state (psi/L) by up to about as much again and decays with L/R — 730-900 A on the screening motor against the
 * 601 A FW-05 compare, so every ASC entry at speed latched DTC_OVERCURRENT and the control-lost row. The compare's hardware
 * action is harmless there: FAULT1 is mapped to the high sides only (DISMAP), which the ASC holds off anyway, and the low
 * sides stay on (host-proven sample by sample). Inside cal_asc_oc_window_ms of the entry an over-current is that
 * transient: DTC_ASC_OC_TRANSIENT (information), no row; once the window is over and the current back inside the compare
 * the 1 ms task re-arms it (asc_oc_rearm). An over-current outside the window, or one that outlasts it, is the fault it
 * always was (the ISR's software backstop sees it at the next sample). */
/* Judged at the sample's own time, unsigned: an entry stamped after it — the same fault ISR's FW-06 action, or a fault
 * ISR preempting the current loop after its trigger — is not before the sample, so not its transient. */
static bool asc_transient(const app_t *a, uint32_t now_us)
{
    return (a->br.mode == BR_ASC) && !ti_elapsed(now_us, a->br.t_asc_us, a->p->cal_asc_oc_window_ms * 1000u);
}

static void over_current(app_t *a, uint32_t now_us, uint32_t t_ms, const fm_ctx_t *c)
{
    if (asc_transient(a, now_us)) {
        dtc_set(DTC_ASC_OC_TRANSIENT, t_ms);
        a->asc_oc_pending = true;
    } else {
        dtc_set(DTC_OVERCURRENT, t_ms);
        fm_raise(&a->fm, SS_ROW_OVERCURRENT, true, c, &a->cal.motor, a->p);
    }
}

/* ======================= current-loop ISR ======================= */
/* Round 18 (A16-R01): each freshness check uses a time read AFTER its acquisition reads. The target stamps a
 * sample when it reads it (s32k396_adc.c), later than the ISR entry, and a frame can be published by an SDADC
 * interrupt preempting this ISR; checked against the entry time such a stamp was 2^32 us old — stale, the
 * control lost. The checked channels are read last in their group (V_DC after VOFS/V5GD). */
static void sense_fast(app_t *a)
{
    uint16_t c[3];
    uint32_t t;
    /* A14-R03: a triplet only when all three channels of one trigger arrived; else the sample is lost
     * (the FW-05 failure path) — V_DC and the resolver below are still serviced */
    if (hal_adc_read_phase(c, &t)) {
        isns_update(&a->isns, c, t, hal_time_us(), a->ofs.run, a->p); /* round 23 (FW-44): the working offsets */
    } else {
        isns_lost(&a->isns);
    }
    uint16_t v[2];
    uint32_t tv[2];
    uint16_t vofs;
    uint16_t v5;
    uint32_t t_ofs;
    uint32_t t_gd;
    const bool ofs_seen = hal_adc_read(HAL_ADC_VOFS, &vofs, &t_ofs);
    const bool gd_seen = hal_adc_read(HAL_ADC_V5GD, &v5, &t_gd);
    /* V_DC: each stamp is judged in vdc.c (round 18; F244: its acquisition, sticky once expired); a channel never
     * converted reads code 0 — the AMC1311 fail-safe level, invalid whatever its stamp — so the returned flag adds
     * nothing there */
    (void)hal_adc_read(HAL_ADC_VDC1, &v[0], &tv[0]);
    (void)hal_adc_read(HAL_ADC_VDC2, &v[1], &tv[1]);
    const uint32_t now = hal_time_us();
    const uint32_t hold = a->p->cal_temp_hold_ms * 1000u; /* round 24 (F241): VOFS and V5GD are slow-list inputs */
    vdc_update(&a->vdc, v, tv, vofs, v5, ti_acq(&a->acq[HAL_ADC_VOFS], ofs_seen, t_ofs, now, hold),
               ti_acq(&a->acq[HAL_ADC_V5GD], gd_seen, t_gd, now, hold), now, a->cal.vdc, a->p);
    /* A14-R02: one coherent frame (EXC, SIN, COS of one epoch and its stamp) or nothing */
    hal_sd_frame_t f;
    if (hal_sdadc_read_frame(&f)) {
        rslv_update(&a->rslv, f.blk[HAL_SD_EXC], f.blk[HAL_SD_SIN], f.blk[HAL_SD_COS], 1.0f / (float)CARRIER_HZ, f.t_us,
                    &a->cal.rslv, a->p);
    }
    rslv_age(&a->rslv, hal_time_us(), a->p); /* A14-R01: every tick, whether a frame arrived or not */
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
        a->id_ref = 0.0f; /* FW-08: zero current at the current-loop rate */
        a->iq_ref = 0.0f;
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
    const float th = rslv_theta_e_at(&a->rslv, &a->cal.rslv, now_us, a->p);
    const float w = rslv_omega_e(&a->rslv, &a->cal.rslv);
    a->foc.id_ref = a->id_ref;
    a->foc.iq_ref = a->iq_ref;
    if ((a->rip_k > 0.0f) && !a->zero_now) { /* round 23 (FW-46): the ripple feed-forward at the FOC's own angle */
        const float lim = a->p->cal_ripple_ff_max_a;
        a->foc.iq_ref += a->rip_k * ti_clampf(torque_ripple_at(a->cal.ripple_ff, a->rip_mean, th), -lim, lim);
    }
    mc_isr_refs(a); /* round 23 (FW-39): a commissioning routine's references, per sample */
    if (!foc_step(&a->foc, a->isns.i_a, th, w, a->vdc.vdc, &a->cal.motor, &a->gains, a->p) ||
        !br_modulate(&a->br, a->foc.duty, a->p)) {
        br_spo(&a->br, false);
        a->mod_req = false;
        dtc_set(DTC_CTRL_NONFINITE, hal_time_ms());
    }
    mc_isr_sample(a); /* round 23 (FW-39): the measurement of this step */
}

/* Two times (round 18): now_us, the ENTRY, is the ISR's own — its liveness stamp (t_isr_us, FW-31), the WCET
 * reference, the angle the FOC uses (the currents were sampled at the trigger, just before the entry) and every
 * bridge action; the freshness of each sample is judged at a time read after it was read (sense_fast). */
void app_isr_current(app_t *a)
{
    const uint32_t now_us = hal_time_us();
    a->n_isr++;
    a->t_isr_us = now_us;
    br_service(&a->br); /* A12-R05: a drop left pending by the DESAT hold happens here once it has run */
    sense_fast(a);
    if (!a->offs_ok && a->isns.fresh && (hal_pwm_mode() == HAL_PWM_OFF) && (a->offs_n < OFFSET_SAMPLES)) {
        for (uint32_t i = 0u; i < 3u; i++) { /* standstill zero-current reference (§9 step 3) */
            a->offs_acc[i] += a->isns.v_pin[i];
        }
        a->offs_n++;
        if (a->offs_n == OFFSET_SAMPLES) {
            const float m[3] = {a->offs_acc[0] / (float)OFFSET_SAMPLES, a->offs_acc[1] / (float)OFFSET_SAMPLES,
                                a->offs_acc[2] / (float)OFFSET_SAMPLES};
            a->offs_ok = isns_offset_ok(m, a->cal.isns, a->p);
            if (!a->offs_ok) {
                dtc_set(DTC_ISNS_OFFSET, hal_time_ms()); /* round 23 (item 4): the §9 step 3 offset check's own DTC */
            }
        }
    }
    if (isns_oc(&a->isns, a->p) && !fm_active(&a->fm, SS_ROW_OVERCURRENT)) {
        const fm_ctx_t c = ctx_now(a); /* software backstop of the FW-05 hardware compare */
        over_current(a, now_us, c.now_ms, &c); /* round 23 (item 5): an ASC entry's transient is information */
        apply_decision(a, now_us);
    }
    control_fast(a, now_us);
    cap_isr(a); /* FW-41: a bounded copy of what this ISR computed into the capture ring — last, so it records it */
}

/* ======================= fault ISR (eFlexPWM FFLAG) ======================= */
void app_isr_fault(app_t *a)
{
    const uint32_t now_us = hal_time_us();
    br_note_pwm_off(&a->br, now_us); /* round 18: FFLAG inhibited the PWM in hardware at or before this read (FW-34 class) */
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
            over_current(a, now_us, c.now_ms, &c); /* round 23 (item 5): an ASC entry's transient is information */
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
/* Round 24 (F241): the slow-list inputs the task reads — the temperatures first, in temp_ch_t order. Each read is judged
 * by its acquisition (ti_acq: hal_adc_read's stamp against the one last taken, cal_temp_hold_ms): the platform returns
 * the last conversion and its stamp until the next one, forever after a converter stops, and each read used to be taken
 * as a new sample at the task's time — a stopped channel stayed valid at its last value. */
enum { SL_IGN = TEMP_COUNT, SL_HVIL, SL_VSUP, SL_N };
static const hal_adc_sig_t SLOW_IN[SL_N] = {HAL_ADC_TMOD_U, HAL_ADC_TMOD_V, HAL_ADC_TMOD_W, HAL_ADC_NTC_H, HAL_ADC_NTC_A,
                                            HAL_ADC_MT1,    HAL_ADC_MT2,    HAL_ADC_IGN,   HAL_ADC_INTRLOK_N,
                                            HAL_ADC_SBC_AMUX};

static void sense_slow(app_t *a, uint32_t t_ms)
{
    uint16_t c[SL_N];
    uint32_t t[SL_N];
    bool seen[SL_N];
    ti_acq_t q[SL_N];
    hal_adc_start_slow();
    for (uint32_t k = 0u; k < (uint32_t)SL_N; k++) {
        seen[k] = hal_adc_read(SLOW_IN[k], &c[k], &t[k]);
    }
    const uint32_t now_us = hal_time_us(); /* round 18: read after the acquisitions */
    uint32_t expired = 0u;
    for (uint32_t k = 0u; k < (uint32_t)SL_N; k++) {
        q[k] = ti_acq(&a->acq[SLOW_IN[k]], seen[k], t[k], now_us, a->p->cal_temp_hold_ms * 1000u);
        if (q[k] == TI_ACQ_EXPIRED) {
            expired |= 1u << (uint32_t)SLOW_IN[k];
            c[k] = 0u; /* read as never converted (the platform's code 0) */
        }
    }
    a->acq_expired = expired;
    /* KL15, INTRLOK_N, VSUP: a held conversion is not judged again (the debounce and the HVIL evaluation wait for a new
     * one); an expired one reads as never converted — KL15 absent, the HVIL signature lost (0 V), no VSUP reading: each
     * module's reading of a converter that never ran, as before round 24 */
    if (q[SL_IGN] != TI_ACQ_HELD) {
        ign_update(&a->ign, c[SL_IGN], t_ms, a->p);
    }
    if (q[SL_HVIL] != TI_ACQ_HELD) {
        hal_gpio_write(HAL_DO_INTRLOK_P, hvil_step(&a->hvil, c[SL_HVIL], t_ms, a->p));
    }
    if (q[SL_VSUP] != TI_ACQ_HELD) {
        vsup_update(&a->vsup, c[SL_VSUP], t_ms, a->p);
    }
    temp_update(&a->temp, c, q, t_ms, &a->cal.mt, a->p); /* the first TEMP_COUNT entries */
    if (a->rslv.valid) {
        a->speed_rpm = rslv_speed_rpm(&a->rslv, &a->cal.rslv);
        a->speed_valid_ms = t_ms;
        a->rslv_seen = true;
    }
    /* after a resolver fault or a stale resolver the §6 decisions use the speed's upper bound (app_speed_hi_rpm) — never
     * angle feedback; round 23 (item 6): the speed is unknown only when the resolver was never valid, there is no valid
     * calibration (no n_max), or the bound has passed the calibration's n_max — then the n >= n_x column, rule (a) at
     * n_max. It was forgotten cal_speed_hold_ms (200 ms) after the fault: ASC at 2000 rpm. */
    a->speed_known = a->rslv.valid || (a->rslv_seen && (a->cal_err == 0u) &&
                                        (app_speed_hi_rpm(a, t_ms) < a->cal.motor.n_max_rpm));
    /* round 18 (A16-R02): each re-acquisition of the resolver frame ring is one occurrence of an information
     * DTC (its count and first/last stamps); no §6 row — while frames are absent the FW-28 age-out acts.
     * Round 19 (A17-R01): a ring whose DMA lost the carrier phase (the clock/position check, or the platform's error
     * flags) stays down until a synchronized producer restart: at most cal_rslv_restart_max per key cycle, each one
     * occurrence of the same DTC; beyond that the resolver stays invalid (FW-28) for the key cycle. */
    const uint32_t n_reacq = hal_sdadc_reacquired();
    bool reacq = (n_reacq != a->sd_reacq);
    a->sd_reacq = n_reacq;
    if (hal_sdadc_lost() && (g_app_session.rslv_restarts < a->p->cal_rslv_restart_max)) {
        g_app_session.rslv_restarts++;
        (void)hal_sdadc_restart(); /* the SWG restarts at a->swg_amp: the trim goes on from it */
        reacq = true;
    }
    if (reacq) {
        dtc_set(DTC_RSLV_REACQUIRED, t_ms);
    } else {
        dtc_pass(DTC_RSLV_REACQUIRED);
    }
    if (a->rslv.primed && ((t_ms % RSLV_TRIM_MS) == 0u)) {
        const uint8_t amp = rslv_swg_trim(a->swg_amp, &a->rslv);
        if (a->rslv.swg_sat) {
            dtc_set(DTC_RSLV_SWG_SAT, t_ms); /* A14-N01: the setpoint is beyond this generator's reach */
        }
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

/* ponytail: unique per power-up and microsecond, not random — the key hook above names the product's TRNG seed */
static uint32_t diag_seed(const app_t *a)
{
    const uint64_t t = hal_time_us64();
    const uint32_t in[3] = {a->key_cycle, (uint32_t)t, (uint32_t)(t >> 32)};
    return ti_crc32(in, sizeof in);
}

/* FW-32: the diagnostic bus (uds.h) */
_Static_assert(DIAG_RX_MAX_PER_TICK >= UDS_DIAG_RX_BS, "a segmented request's block is read in one task (uds_diag.h)");
static void diag(app_t *a)
{
    hal_can_frame_t rq;
    hal_can_frame_t rsp;
    for (uint32_t i = 0u; (i < DIAG_RX_MAX_PER_TICK) && hal_can_rx(HAL_CAN_DIAG, &rq); i++) {
        if (mc_uds_handle(a, &rq, &rsp)) { /* round 23 (FW-39): the commissioning routines (commission.h) */
            (void)hal_can_tx(HAL_CAN_DIAG, &rsp);
            continue;
        }
        /* round 23 (FW-43): the read-only run-time statistics DID (0xFE43) first; anything else goes on */
        if (rs_uds_handle(&a->rs.rec, a->key_cycle, &rq, &rsp) || uds_handle(&a->uds, &rq, diag_seed(a), &rsp)) {
            (void)hal_can_tx(HAL_CAN_DIAG, &rsp);
        }
    }
    uds_diag_tick(&a->uds); /* FW-40: segmented responses, the 0x19 04 ring scan, one periodic DID — one frame at most */
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

static void dtc_report(bool failed, dtc_id_t d, uint32_t t_ms)
{
    if (failed) {
        dtc_set(d, t_ms);
    } else {
        dtc_pass(d);
    }
}

/* FW-13 / FW-04 (round 23, item 4): every temperature channel group has its DTC, and each reports its pass when the
 * condition ends — one occurrence per event: a module NTC invalid, a board NTC invalid, a motor sensor invalid, and the
 * over-temperature — the hottest valid module NTC at the end of its derating (no torque left), passed below that end
 * less the derating hysteresis. Information: the derating (FW-04) is the response, no §6 row. */
static void temp_dtcs(const app_t *a, uint32_t t_ms)
{
    const temp_ch_state_t *ch = a->temp.ch;
    bool any = false;
    bool all = false;
    const float t_mod = temp_module_max(&a->temp, &any, &all);
    dtc_report(!all, DTC_TEMP_MODULE, t_ms);
    dtc_report(!ch[TEMP_NTC_H].valid || !ch[TEMP_NTC_A].valid, DTC_TEMP_BOARD, t_ms);
    dtc_report(!ch[TEMP_MT1].valid || !ch[TEMP_MT2].valid, DTC_TEMP_MOTOR, t_ms);
    /* F243: any channel whose last new sample reads open or short — also one a latched TEMP_RATE keeps reporting RATE */
    bool wire = false;
    for (uint32_t k = 0u; k < (uint32_t)TEMP_COUNT; k++) {
        wire = wire || (ch[k].wire != TEMP_OK);
    }
    dtc_report(wire, DTC_TEMP_OPEN_SHORT, t_ms);
    if (any && (t_mod >= a->p->cal_tmod_derate_end_c)) {
        dtc_set(DTC_OVERTEMP, t_ms);
    } else if (!any || (t_mod < (a->p->cal_tmod_derate_end_c - a->p->cal_derate_hyst_c))) {
        dtc_pass(DTC_OVERTEMP);
    } else {
        /* inside the hysteresis: as it was */
    }
}

static void detect(app_t *a, const fm_ctx_t *c)
{
    const bool armed_states = (a->sm.st == SM_ARMED_ZERO_TORQUE) || (a->sm.st == SM_RUN) || (a->sm.st == SM_DERATE);
    if (monitoring(a)) {
        /* V5GD: §4c — SPO, ASC cleared, V_DC invalid, supply DTC, no arming */
        flag(a, !a->vdc.v5gd_ok, SS_ROW_V5GD_LOSS, true, DTC_V5GD, c);
        const bool vbad = a->vdc.v5gd_ok && !a->vdc.valid;
        const dtc_id_t vd = a->vdc.ref_stale ? DTC_ADC_SLOW_STALE : (!a->vdc.vofs_ok ? DTC_VOFS
                          : (a->vdc.disagree ? DTC_VDC_DISAGREE
                          : ((a->vdc.ch_failsafe[0] || a->vdc.ch_failsafe[1]) ? DTC_VDC_FAILSAFE : DTC_VDC_STALE)));
        flag(a, vbad, SS_ROW_VDC_INVALID, true, vbad ? vd : DTC_NONE, c);
        /* round 23 (item 8): a latched plausibility fault names itself; stale when none explains the invalid angle (a
         * frame kept out no longer renews the hold, so a latched fault ages the angle out too) */
        const dtc_id_t rd = a->rslv.amp_fault ? DTC_RSLV_AMPLITUDE : (a->rslv.exc_fault ? DTC_RSLV_EXCITATION
                          : (a->rslv.trk_fault ? DTC_RSLV_TRACKING : (a->rslv.acc_fault ? DTC_RSLV_ACCEL
                          : (a->rslv.rate_fault ? DTC_RSLV_RATE : DTC_RSLV_STALE))));
        /* control lost = a resolver that WAS valid (round 16: its first acquisition waits for the SWG
         * ramp; before it nothing can arm — SENSOR_SELFTEST needs it — and nothing is "lost") */
        const bool rbad = !a->rslv.valid && a->rslv_seen;
        flag(a, rbad, SS_ROW_RESOLVER_INVALID, true, rbad ? rd : DTC_NONE, c);
        const bool open = a->isns.open_wire[0] || a->isns.open_wire[1] || a->isns.open_wire[2];
        const bool ibad = !a->isns.valid;
        const dtc_id_t id = open ? DTC_ISNS_OPEN : (a->isns.sum_fault ? DTC_ISNS_SUM
                          : (a->isns.stuck_fault ? DTC_ISNS_STUCK : (!a->isns.fresh ? DTC_ISNS_STALE : DTC_ISNS_RANGE)));
        if (ibad) {
            flag(a, true, SS_ROW_RESOLVER_INVALID, true, id, c); /* control lost: same §6 row */
        }
    }
    /* Round 15 (A13-R02): armed, the battery path must be proven at every speed — contactors reported
     * CLOSED in a fresh VCU frame; OPEN, PRECHARGE, INVALID and a stale report (unknown) are all
     * "lost", as is V_DC leaving the pack. §6 picks the response from speed, winding current, V_DC and
     * the actuators left; the row stays while that response still energises the bridge (zero-torque
     * current control or PWM-ASC), then clears — unarmed, open contactors are the precharge sequence. */
    const bool path_ok = (a->can.contactors == TI_CONT_CLOSED) && can_cmd_fresh(&a->can, c->now_ms, a->p);
    const bool lost = armed_states && (!path_ok || a->vdc.bms_mismatch);
    const bool energised = (a->br.mode == BR_IDLE) || (a->br.mode == BR_MOD) || (a->br.mode == BR_ASC);
    flag(a, lost || (fm_active(&a->fm, SS_ROW_BATTERY_LOST) && energised), SS_ROW_BATTERY_LOST, false,
         (lost && a->vdc.bms_mismatch) ? DTC_VDC_BMS : DTC_NONE, c);
    if (armed_states) {
        const bool hvil_bad = (a->hvil.status != HVIL_CLOSED) && (a->hvil.status != HVIL_UNKNOWN);
        if (hvil_bad) {
            dtc_set((a->hvil.status == HVIL_OPEN) ? DTC_HVIL_OPEN : DTC_HVIL_SHORT, c->now_ms);
        }
        /* round 17: an LV overvoltage longer than its ISO 16750-2 band allows takes the same orderly ramp */
        /* round 23 (FW-42): so does the overspeed warning band (the trip adds the control-lost row: overspeed()) */
        const bool cmd_lost = !can_cmd_fresh(&a->can, c->now_ms, a->p) || hvil_bad || a->vsup.sustained ||
                              (a->ovs.band != OVS_NONE);
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
    temp_dtcs(a, c->now_ms);
    /* round 24 (F241): a slow-list input with no conversion for cal_temp_hold_ms (or none ever) — the task's or the
     * current loop's (VOFS, V5GD); its consumer's own response already acts (above, and in temp_dtcs / the modules) */
    dtc_report((a->acq_expired != 0u) || a->vdc.ref_stale, DTC_ADC_SLOW_STALE, c->now_ms);
    /* round 17: VSUPOV is information — logged with its duration, torque untouched — until it outlasts its band */
    if (a->vsup.ov) {
        dtc_set(DTC_LV_OVERVOLTAGE, c->now_ms);
    }
    if (a->vsup.sustained) {
        dtc_set(DTC_LV_OV_SUSTAINED, c->now_ms);
    }
    if (!a->vsup.valid && monitoring(a)) {
        dtc_set(DTC_LV_VSUP_UNKNOWN, c->now_ms);
    }
}

/* Round 23 (item 5): after an ASC entry's transient, once the window is over and the current back inside the FW-05
 * compare, its phase flags and FAULT1's are cleared — FAULT1 only when no V_DC over-voltage shares it — so the compare
 * (and FW-06's interrupt, on the same fault input) acts again on the next event; the information DTC passes. */
static void asc_oc_rearm(app_t *a, uint32_t now_us)
{
    if (!a->asc_oc_pending || asc_transient(a, now_us) || isns_oc(&a->isns, a->p)) {
        return;
    }
    const uint32_t ov = (1u << HAL_ADC_VDC1) | (1u << HAL_ADC_VDC2);
    hal_adc_watchdog_clear((1u << HAL_ADC_ISNS_U) | (1u << HAL_ADC_ISNS_V) | (1u << HAL_ADC_ISNS_W));
    if ((hal_adc_watchdog_status() & ov) == 0u) {
        (void)hal_pwm_fault_clear(HAL_PWM_FAULT_ADC_WD);
    }
    dtc_pass(DTC_ASC_OC_TRANSIENT);
    a->asc_oc_pending = false;
}

/* The FW-15 sequence. br_rec_step times the >= 1.5 ms low itself (round 18): the fault ISR may have stamped
 * t_fault_us after this task read its own time. */
static void recovery(app_t *a, bool for_retry)
{
    if (!a->rec_active) {
        br_rec_start(&a->br, a->t_fault_us);
        a->rec_active = true;
        a->rec_for_retry = for_retry;
    }
    const br_rec_t r = br_rec_step(&a->br, a->p);
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
        recovery(a, false); /* FLT_HS at n >= n_x: the FW-15 reset, then PWM-ASC */
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
    /* Round 15: a battery-path loss met with nothing to manage — its §6 response already SPO (the
     * FW-08 zero-torque opening) and V_DC at the pack — is not a FAULT; the state machine disarms */
    const bool bl_done = (a->br.mode == BR_DISARMED) && !a->vdc.bms_mismatch;
    *in = (sm_in_t){.now_ms = t_ms, .ign_on = a->ign.on, .init = a->init, .sensors_ok = sensors_ok(a),
                    .v5gd_ok = a->vdc.v5gd_ok, .fs0b_released = a->fs0b_released,
                    .gate_power_ready = (a->gp.st == GP_READY), .gate_power_failed = a->gp.timeout_dtc,
                    .cmd_fresh = fresh, .enable_req = a->can.enable_req && !mc_torque_barred(),
                    .contactors = a->can.contactors,
                    .shutdown_req = fresh && a->can.shutdown_req, .discharge_req = fresh && a->can.discharge_req,
                    .selftest = a->selftest, .fault_needed = fm_needs_fault_state(&a->fm, bl_done) || a->no_arm,
                    .battery_lost = fm_active(&a->fm, SS_ROW_BATTERY_LOST),
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
                        (app_speed_hi_rpm(a, t_ms) < a->n_x_rpm);
    in->recovery_done = a->rec_done_retry && a->fm.retry_used && (a->sm.st == SM_FAULT);
}

/* Round 23 (item 4): §9 steps 3-4 record why they did not complete. A driver FLT low at boot with V5GD healthy — a DESAT
 * pending from before the reset (§9) — is DTC_DESAT_PENDING_BOOT; the sensor self-test's timeout (a sensor not valid in
 * cal_sensor_selftest_ms) is DTC_SENSOR_SELFTEST, and the FS0B/FS1B release not achieved in cal_fs0b_release_ms more is
 * DTC_FS26_RELEASE. The other exits to FAULT have theirs where they are detected: V5GD (detect(), from FAULT on),
 * FS_GPIO1 (execute()), the gate power (gate_power.c). */
static void selftest_dtcs(const app_t *a, const sm_in_t *in, sm_state_t before, uint32_t t_ms)
{
    if (before != SM_SENSOR_SELFTEST) {
        return;
    }
    if (in->flt_low_at_boot) {
        dtc_set(DTC_DESAT_PENDING_BOOT, t_ms);
    } else if ((a->sm.st == SM_FAULT) && in->v5gd_ok && !in->rdy_before_gate_power && !in->gate_power_failed) {
        dtc_set(in->sensors_ok ? DTC_FS26_RELEASE : DTC_SENSOR_SELFTEST, t_ms);
    } else {
        /* still running, or its exit has its own DTC */
    }
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

static void execute(app_t *a, uint32_t t_ms)
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
        a->asc_hold = !a->speed_known || (app_speed_hi_rpm(a, t_ms) >= a->n_x_rpm); /* §9 step 5 */
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
        recovery(a, true); /* the one VCU-authorised retry (>= 1 s, n < n_x) */
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
    if (fm_any(&a->fm) && (a->fm.dec.action >= SS_ACT_ZERO_CURRENT)) {
        return; /* the §6 decision owns the bridge */
    }
    if (!a->so.arm || a->no_arm) {
        if (a->br.mode == BR_ASC) {
            (void)br_exit_asc(&a->br, a->speed_known && (app_speed_hi_rpm(a, t_ms) < a->n_x_rpm));
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
        const bool slow = a->speed_known && (app_speed_hi_rpm(a, t_ms) < a->n_x_rpm);
        if (slow || (battery_present(a, t_ms) && cc_ready)) {
            if (br_exit_asc(&a->br, true)) { /* FW-06a: MCU-commanded exit */
                a->asc_hold = false;
            }
        }
        return;
    }
    if ((a->br.mode == BR_DISARMED) && (a->ovs.band == OVS_NONE)) { /* round 23 (FW-42): no arming at overspeed */
        (void)br_arm_idle(&a->br);
    }
}

/* Round 23 (item 2): idle, the bridge's free-wheeling diodes conduct once the line-line back-EMF peak sqrt(3) w_e psi
 * exceeds V_DC — below n_x at any link under the OV trip (n_x is defined at the 880 V trip): at 750 V from 6 890 rpm on
 * the screening motor, where the idle bridge braked the shaft (50-65 N m) and pushed ~40 kW into the pack with no torque
 * asked (the simulator's finding). So the bridge takes the back-EMF — zero torque, id control — once the phase back-EMF
 * w_e psi (the speed's upper bound, the record's cold-magnet psi) reaches (1 - cal_fw_emf_margin_frac) of the voltage a
 * reference may use of the MEASURED link, while zero current still fits (no current step at the take-over). */
static bool emf_needs_control(const app_t *a, uint32_t t_ms)
{
    const float e = ti_absf(motor_omega_e(app_speed_hi_rpm(a, t_ms), &a->cal.motor)) * a->cal.motor.psi_wb;
    return a->vdc.valid && (e >= ((1.0f - a->p->cal_fw_emf_margin_frac) * torque_v_available(a->vdc.vdc, a->p)));
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
    /* normal RUN: torque_enable is granted only with the contactors reported CLOSED and no battery-path row
     * (st_run), so the battery path is proven here and only here */
    if (a->so.torque_enable && fresh && a->can.enable_req) {
        target = can_dir_interlock(&a->dir, a->can.gear, a->can.torque_req_nm, a->speed_rpm, p);
        if (a->so.torque_reduced) {
            const float lim = p->cal_desat_retry_torque_frac * p->cal_torque_max_nm;
            target = ti_clampf(target, -lim, lim);
        }
        target = torque_clamp(&a->tlim, target, w_mech);
        target = dcl_trim(&a->dcl, target, a->vdc.vdc, w_mech, 1.0e-3f, p); /* FW-08: regen back above vdc_max_v */
    } else {
        dcl_reset(&a->dcl); /* never engaged outside normal RUN (round 17, item 26) */
    }
    const ss_action_t act = fm_any(&a->fm) ? a->fm.dec.action : SS_ACT_NONE;
    if (act >= SS_ACT_ZERO_CURRENT) {
        a->t_cmd_nm = 0.0f; /* the §6 decision owns the bridge; the status reports what is applied: 0 Nm */
    } else if (fresh && (act != SS_ACT_RAMP_KEEP_CC) && (act != SS_ACT_RAMP_THEN_SPO)) {
        /* round 23 (item 3): a fresh command is slewed at cal_torque_slew_nm_s, both directions, after its limits — it
         * was applied at once, and a +200 -> -150 N m step at 10 000 rpm reversed i_q faster than the loop's voltage
         * allows (FW-05 tripped). The zeroings are not slewed: the §6 decision above, the solver's refusals below. */
        a->t_cmd_nm = ti_ramp(a->t_cmd_nm, target, p->cal_torque_slew_nm_s * 1.0e-3f);
        a->zero_now = false;
    } else {
        a->t_cmd_nm = ti_ramp(a->t_cmd_nm, 0.0f, p->cal_torque_ramp_nm_s * 1.0e-3f); /* FW-11: ramp, not hold */
    }
    const bool armed = (a->br.mode == BR_IDLE) || (a->br.mode == BR_MOD);
    /* n_x stays the floor (and the §6 decisions' column); round 23 (item 2): the measured link decides below it */
    const bool fw_needed = !a->speed_known || (app_speed_hi_rpm(a, t_ms) >= a->n_x_rpm) || emf_needs_control(a, t_ms);
    /* Round 15: while a §6 decision owns the bridge the only modulation is its own — zero-current
     * control while winding current remains after a battery-path loss below n_x — and it does not
     * depend on the operating state's arm (FAULT included). Ordinary modulation needs it. */
    const bool zero_cc = (act == SS_ACT_ZERO_CURRENT) && (i_mag(a) >= p->cal_spo_release_a);
    const bool ordinary = a->so.arm && ((ti_absf(a->t_cmd_nm) >= MOD_TORQUE_NM) || fw_needed || (act == SS_ACT_RAMP_KEEP_CC));
    a->mod_req = armed && !a->no_arm && ((act >= SS_ACT_ZERO_CURRENT) ? zero_cc : ordinary);
    a->mod_req = a->mod_req || (mc_modulating() && armed && !a->no_arm && (act == SS_ACT_NONE)); /* round 23 (FW-39) */
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
    } else if (tr == TQ_POSTCOND) {
        /* round 23 (FW-37): the solver refused its own vector — a fault, never a reference: zero torque, no current,
         * and while the bridge is armed the §6 "control lost" row decides it now (as a lost arming evidence does) */
        dtc_set(DTC_TORQUE_POSTCOND, t_ms);
        a->t_cmd_nm = 0.0f;
        if (armed) {
            const fm_ctx_t c = ctx_now(a);
            fm_raise(&a->fm, SS_ROW_RESOLVER_INVALID, true, &c, &a->cal.motor, p);
            apply_decision(a, hal_time_us());
        }
    } else if (tr == TQ_INFEASIBLE) {
        /* F23: not even iq = 0 fits the voltage inside the demagnetisation/current limits: zero
         * torque at the least-voltage id, ask the VCU to limit the speed, record it */
        a->t_cmd_nm = 0.0f;
        if (relevant) {
            dtc_set(DTC_TORQUE_INFEASIBLE, t_ms);
        }
    } else {
        /* TQ_OK: the command's torque; TQ_LIMITED: less — the most the voltage and current limits allow (FW-37) */
    }
    /* round 23 (FW-46): the ripple feed-forward for the ISR — only on a solved vector (TQ_OK / TQ_LIMITED) that the bridge
     * modulates for torque (no §6 decision, no zeroing, no commissioning), the resolver valid and the cogging fundamental
     * 6 f_e below cal_ripple_ff_fmax_hz; scaled down (never the base vector) until the vector with the table's extremes
     * still fits the current circle and the voltage ellipse (torque_ripple_scale) */
    const float w_e = motor_omega_e(a->speed_rpm, &a->cal.motor);
    const bool ff_on = (p->cal_ripple_ff_max_a > 0.0f) && ((a->rip_lo < 0.0f) || (a->rip_hi > 0.0f)) &&
                       ((tr == TQ_OK) || (tr == TQ_LIMITED)) && a->mod_req && !a->zero_now && (act == SS_ACT_NONE) &&
                       !mc_modulating() && a->rslv.valid && a->speed_known &&
                       ((6.0f * ti_absf(w_e) * (1.0f / TI_2PI)) < p->cal_ripple_ff_fmax_hz);
    const float rip_k = ff_on ? torque_ripple_scale(id, iq, a->rip_lo, a->rip_hi, w_e, a->vdc.vdc, i_max, &a->cal.motor, p)
                              : 0.0f;
    a->rip_k = ti_minf(a->rip_k, rip_k); /* the ISR preempts this task: until the new references are out, a scale that fits
                                            both the old vector and the new one (T-58) */
    a->speed_limit_req = (tr == TQ_INFEASIBLE) && relevant;
    a->speed_limit_req = a->speed_limit_req || (a->ovs.band != OVS_NONE); /* round 23 (FW-42): overspeed */
    a->id_ref = a->zero_now ? 0.0f : id; /* FW-08 row: zero current, field weakening included */
    a->iq_ref = a->zero_now ? 0.0f : iq;
    a->rip_k = rip_k; /* round 23 (FW-46): the new vector's scale, now that its references are out */
    /* round 23 (FW-37): the status reports the torque applied (FW-08): what the issued references represent, 0 Nm
     * without modulation; the command goes beside it */
    a->t_act_nm = a->mod_req ? torque_from_current(a->id_ref, a->iq_ref, &a->cal.motor) : 0.0f;
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
                            .torque_nm = a->t_act_nm, .torque_cmd_nm = a->t_cmd_nm, .speed_rpm = a->speed_rpm,
                            .vdc_v = a->vdc.vdc,
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

/* ======================= round 23: FW-42 overspeed, FW-43 statistics, FW-44 offset refresh ======================= */
/* FW-42: the measured speed against the calibration record's n_max_rpm (overspeed.h). The warning band takes the §6
 * command-lost row (detect(): the torque ramped to zero — SPO below n_x, current control kept above it with the
 * battery), the speed-limit request (torque_path) and no arming (arming()); the trip band adds the §6 "Resolver
 * invalid, or control lost" row, latched: SPO below n_x under the energy rule, LS-ASC at or above it — the matrix
 * decides, there is no ASC of this check's own. DTC_OVERSPEED is stamped over the event, passed after it. */
static void overspeed(app_t *a, const fm_ctx_t *c)
{
    const ovs_band_t b = ovs_step(&a->ovs, a->speed_rpm, a->rslv.valid, a->cal.motor.n_max_rpm, a->p);
    if (b == OVS_NONE) {
        dtc_pass(DTC_OVERSPEED); /* the next event is a new occurrence */
        return;
    }
    dtc_set(DTC_OVERSPEED, c->now_ms);
    if ((b == OVS_TRIP) && monitoring(a)) {
        flag(a, true, SS_ROW_RESOLVER_INVALID, true, DTC_OVERSPEED, c);
    }
}

/* FW-44: once per boot, when the key-on zero-current mean is complete and the speed is measured, the working offsets
 * move toward it by at most cal_isns_ofs_step_v: only when it passes the key-on check against the EOL record (which
 * still refuses to arm beyond it), once per key cycle (the record's key cycle), disarmed, PWM off, at standstill
 * (FW-16's n_ss) — never while armed or moving. The FW-05 hardware compare follows; the record is queued. */
static void offset_refresh(app_t *a, uint32_t t_ms)
{
    if (a->ofs.decided || (a->offs_n < OFFSET_SAMPLES) || !a->rslv.valid) {
        return;
    }
    const float m[3] = {a->offs_acc[0] / (float)OFFSET_SAMPLES, a->offs_acc[1] / (float)OFFSET_SAMPLES,
                        a->offs_acc[2] / (float)OFFSET_SAMPLES};
    const bool standstill = ti_absf(a->speed_rpm) < motor_n_ss_rpm(&a->cal.motor, a->p);
    const bool may = (a->cal_err == 0u) && (a->rs.rec.ofs_key_cycle != a->key_cycle) && (a->br.mode == BR_DISARMED) &&
                     (hal_pwm_mode() == HAL_PWM_OFF) && standstill;
    ofs_t o = a->ofs;
    const bool adopted = ofs_decide(&o, m, a->cal.isns, may, a->p);
    hal_crit_enter(); /* the current-loop ISR converts with the working offsets */
    a->ofs = o;
    hal_crit_exit();
    if (!adopted) {
        return;
    }
    for (uint32_t i = 0u; i < 3u; i++) {
        a->rs.rec.ofs_v[i] = o.run[i].offset_v;
    }
    a->rs.rec.ofs_key_cycle = a->key_cycle;
    a->rs.rec.cal_crc = a->cal.crc32;
    set_watchdogs(a);
    rs_persist(&a->rs, false, true, t_ms, a->p);
}

/* FW-43: statistics (no safety relevance) and the run-time record's cadence. V_DC x I_DC with I_DC the bridge's DC
 * current from the loop's own dq voltages and currents (no DC shunt): P = 1.5 (v_d i_d + v_q i_q), while modulating. */
static void run_stats(app_t *a, uint32_t t_ms)
{
    bool any = false;
    bool all = false;
    const float tmod = temp_module_max(&a->temp, &any, &all);
    const temp_ch_state_t *m1 = &a->temp.ch[TEMP_MT1];
    const temp_ch_state_t *m2 = &a->temp.ch[TEMP_MT2];
    const bool down = (a->sm.st == SM_SAFE_POWERDOWN);
    const rs_in_t in = {.on = (a->sm.st != SM_OFF) && !down,
                        .run = (a->sm.st == SM_RUN) || (a->sm.st == SM_DERATE),
                        .mod = (a->br.mode == BR_MOD) && a->vdc.valid,
                        .p_w = 1.5f * ((a->foc.vd * a->foc.id) + (a->foc.vq * a->foc.iq)),
                        .t_mod_c = tmod, .t_mod_ok = any,
                        .t_cool_c = a->can.coolant_c,
                        .t_cool_ok = a->can.coolant_valid && can_cmd_fresh(&a->can, t_ms, a->p),
                        .t_mot_c = (m1->valid && (!m2->valid || (m1->t_c >= m2->t_c))) ? m1->t_c : m2->t_c,
                        .t_mot_ok = m1->valid || m2->valid};
    rs_step(&a->rs, &in);
    rs_count_dtcs(&a->rs);
    rs_persist(&a->rs, down, false, t_ms, a->p); /* every cal_rs_save_s, and once on entering SAFE_POWERDOWN */
}

void app_task_1ms(app_t *a)
{
    const uint64_t t64 = hal_time_us64(); /* A12-R06: one read per tick keeps the 64-bit extension */
    const uint32_t now_us = (uint32_t)t64;
    const uint32_t t_ms = ti_ms_from_us64(t64);
    /* FW-12 first (round 17, T-32): the answer then trails the exact 1 ms tick only by the task-start latency and
     * two SPI frames, and fs26_wd_due() lands it on every second task — inside the FS26 window */
    if (fs26_wd_due(&a->fs, now_us) && (fs26_wd_refresh(&a->fs) != FS26_OK)) {
        dtc_set(DTC_FS26_WD, t_ms);
    }
    hal_wdog_kick();
    br_service(&a->br);
    /* Round 16 (A14-R03, BCTU stopped): only the current-loop ISR reads the phase currents and the
     * resolver frames. If it stops (no trigger, a list that never completes), its measurements go
     * stale from here instead of staying valid at their last values. Round 18 (A16-R01): signed — the ISR
     * preempts this task, so its entry can postdate now_us (read above, before the FS26 transfers): that is a
     * running loop, not a dead one. */
    if (ti_stale(now_us, a->t_isr_us, a->p->cal_isns_stale_us)) {
        isns_lost(&a->isns);
        rslv_age(&a->rslv, now_us, a->p);
    }
    sense_slow(a, t_ms);
    comms(a, t_ms);
    diag(a);
    if ((a->sm.st != SM_GATE_SELFTEST) || (a->selftest != SM_BUSY)) {
        (void)gp_step(&a->gp, t_ms, a->p); /* FW-16 d/e/g drop RDY on purpose */
    }
    fm_ctx_t c = ctx_now(a);
    overspeed(a, &c); /* round 23 (FW-42): before detect(), which reads its band */
    detect(a, &c);
    evidence_watch(a, &c);
    fm_update(&a->fm, &c, &a->cal.motor, a->p);
    fault_actions(a, &c, now_us);
    asc_oc_rearm(a, hal_time_us()); /* round 23 (item 5) */
    mc_task(a, t_ms); /* round 23 (FW-39): the service mode's preconditions and watch, before the state machine */
    sm_in_t in;
    gather(a, &in, t_ms);
    const sm_state_t before = a->sm.st;
    sm_step(&a->sm, &in, &a->so, a->p);
    selftest_dtcs(a, &in, before, t_ms);
    execute(a, t_ms);
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
    offset_refresh(a, t_ms); /* round 23 (FW-44) */
    run_stats(a, t_ms);      /* round 23 (FW-43) */
    a->last_task_ms = t_ms;
}

void app_idle(app_t *a)
{
    (void)a;
    nv_service();
    upd_service(); /* FW-38: STAGE programming, the image check, the reset, the trial boot's confirmation */
    if (nv_idle()) {
        fm_retained_commit(); /* the DESAT record reached NVM */
    }
}
