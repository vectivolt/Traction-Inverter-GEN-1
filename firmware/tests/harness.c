/* harness.c — see harness.h. */
#include "harness.h"

#include <string.h>

#include "can_cmd.h"
#include "gpio.h"
#include "nvlog.h"
#include "timer.h"

h_env_t H;
ti_params_t h_p;
calib_t h_cal;

static const uint8_t SERIAL[8] = {'T', 'I', '-', '0', '0', '0', '0', '1'};
#define H_PROG_ID 0x4A21u

static float s_theta0;
static uint64_t s_theta_t;
static uint32_t s_noise; /* plant_currents' ADC-level noise */

void h_set_speed(float rpm)
{
    /* integrate the angle so far, then continue at the new speed (resolver pp = 1) */
    const float w_old = H.speed_rpm / TI_RPM_PER_RAD_S;
    s_theta0 += w_old * (float)(int64_t)(sim_now_ns() - s_theta_t) * 1e-9f;
    s_theta0 = ti_wrap_2pi(s_theta0);
    s_theta_t = sim_now_ns();
    H.speed_rpm = rpm;
    const sim_resolver_t r = {.theta0_rad = s_theta0, .omega_rad_s = rpm / TI_RPM_PER_RAD_S, .lag_deg = 24.0f,
                              .sin_gain = 1.0f, .cos_gain = 1.0f,
                              .out_gain = (H.rslv_amp > 0.0f) ? H.rslv_amp : 1.0f};
    sim_resolver_set(&r);
}

const uint8_t *h_serial(void) { return SERIAL; }

void h_store_validation(const arm_validation_t *v)
{
    nv_init();
    (void)nv_queue(NV_REC_VALIDATION, v, (uint16_t)sizeof *v);
    for (int k = 0; (k < 200) && !nv_idle(); k++) {
        nv_service();
    }
}

void h_unprovision(void)
{
    sim_pwm_fault_route_bind(false);
    sim_nvm_wipe();
}

void h_setup(ti_sku_t sku)
{
    (void)memset(&H, 0, sizeof H);
    h_p = *ti_params_get(sku);
    h_p.cal_fs26_prog_id = H_PROG_ID;
    calib_nominal(&h_cal, &h_p, SERIAL);
    h_cal.motor_id = 0x1001u;
    calib_seal(&h_cal);
    H.send_cmd = true;
    H.send_bms = true;
    H.contactors = TI_CONT_OPEN;
    H.gear = TI_GEAR_D;
    H.coolant_c = 50.0f;
    H.v_pack = h_p.class8 ? 750.0f : 400.0f;
    H.p_chg_w = 100000.0f;
    H.p_dis_w = 250000.0f;
    H.plateau_frac = 0.998f;
    H.tau_pre_s = 0.1f;
    H.tau_dis_s = h_p.tau_dis_s;
    s_theta0 = 0.3f;
    s_theta_t = sim_now_ns();
    s_noise = 12345u;
    sim_fs26_cfg_t fc = {.prog_id = H_PROG_ID, .device_id = 0x2600u};
    sim_fs26_config(&fc);
    sim_fs26_reset();
    sim_set_hwid_ohm(h_p.hwid_r_ohm);
    sim_set_link_v(0.0f, 0.0f);
    h_set_speed(0.0f);
    /* EOL/HIL: the board configuration binds the route; the rig validated this image on this card
     * (pad -> PWM fault injection, FW-06 chain measured at 14.2 us of the 15.6 us budget) */
    sim_pwm_fault_route_bind(true);
    arm_validation_t v;
    arm_validation_make(&v, SERIAL, h_p.sku, TI_FW_ID, ARM_EV_VALIDATED, 14200u);
    h_store_validation(&v);
}

static void grids_restart(void);

void h_boot(void)
{
    app_init(&g_app, &h_p, &h_cal, SERIAL);
    sim_set_fault_isr(app_fault_isr_entry);
    H.t_ms = hal_time_ms();
    grids_restart();
}

static void vcu_tx(void)
{
    hal_can_frame_t f;
    if (H.send_cmd) {
        can_encode_vcu_cmd(&f, H.ctr, H.gear, H.enable, H.fault_reset, H.torque_nm, H.contactors, H.retry_auth,
                           H.discharge, H.shutdown, H.coolant_c);
        sim_can_inject(HAL_CAN_VEHICLE, &f);
        if (!H.freeze_ctr) {
            H.ctr = (uint8_t)((H.ctr + 1u) & 0x0Fu);
        }
    }
    if (H.send_bms) {
        can_encode_vcu_bms(&f, H.bms_ctr, H.v_pack, H.p_chg_w, H.p_dis_w);
        sim_can_inject(HAL_CAN_VEHICLE, &f);
        H.bms_ctr = (uint8_t)((H.bms_ctr + 1u) & 0x0Fu);
    }
}

static void plant_1ms(void)
{
    if (H.link_override) {
        return;
    }
    const float dt = 1e-3f;
    float v = H.link_v;
    if (H.contactors == TI_CONT_CLOSED) {
        v = H.v_pack;
    } else if (H.contactors == TI_CONT_PRECHARGE) {
        const float target = H.v_pack * H.plateau_frac;
        v += (target - v) * (dt / H.tau_pre_s);
    } else {
        const bool qdis = (hal_gpio_out_state(HAL_DO_QDIS) && !H.qdis_stuck_off) || H.qdis_stuck_on;
        const float tau = qdis ? H.tau_dis_s : (h_p.r_bleed_ohm * h_p.c_nom_f);
        v -= v * (dt / tau);
    }
    H.link_v = v;
    sim_set_link_v(v, v * (1.0f + H.ch2_err));
}

/* Phase currents for one current-loop sample: an ideal current loop (the FOC reference) while the
 * bridge modulates, else none; a test that sets i_pk_a imposes that amplitude (along the reference,
 * or the d axis without one). The loop is ideal in the controller's own dq frame (its resolver
 * angle; the model's angle when that is invalid): this plant does not respond to voltage, so any
 * frame error would leave a dq error the PI integrators walk after forever. */
/* ADC-level noise, ±0.55 A per phase (one LSB of the 2.22 mV/A sensor at 12 bit), deterministic. On the exact
 * tick grid the samples are phase-locked: at 1000 rpm they fall on the same electrical angles every period, so
 * without noise their quantization errors repeat as a constant dq offset, and this plant — which does not respond
 * to voltage — lets the current PIs integrate that offset forever (a false FW-10 rate fault after 3.4 s). A real
 * converter dithers it away; so does the noise here. */
static float noise_a(void)
{
    s_noise = (s_noise * 1664525u) + 1013904223u;
    return (((float)(s_noise >> 8) / 16777216.0f) - 0.5f) * 1.1f;
}

float h_rotor_theta_e(void)
{
    const float th_r = s_theta0 + (H.speed_rpm / TI_RPM_PER_RAD_S) * (float)(int64_t)(sim_now_ns() - s_theta_t) * 1e-9f;
    return ti_wrap_2pi((th_r * (float)h_cal.rslv.motor_pp / (float)h_cal.rslv.resolver_pp) - h_cal.rslv.zero_rad);
}

static void plant_currents(void)
{
    const float th = g_app.rslv.valid ? rslv_theta_e_at(&g_app.rslv, &g_app.cal.rslv, hal_time_us(), g_app.p)
                                      : h_rotor_theta_e();
    float d = 0.0f;
    float q = 0.0f;
    if (hal_pwm_mode() == HAL_PWM_MOD) {
        d = g_app.foc.id_ref;
        q = g_app.foc.iq_ref;
    }
    if (H.i_pk_a > 0.0f) {
        const float m = sqrtf((d * d) + (q * q));
        d = (m > 1.0f) ? (d * H.i_pk_a / m) : H.i_pk_a;
        q = (m > 1.0f) ? (q * H.i_pk_a / m) : 0.0f;
    }
    float i[3];
    for (uint32_t k = 0u; k < 3u; k++) {
        const float a = th - (2.0943951f * (float)k);
        i[k] = (d * cosf(a)) - (q * sinf(a)) + noise_a();
    }
    sim_set_phase_currents(i[0], i[1], i[2]);
    const hal_adc_sig_t ch[3] = {HAL_ADC_ISNS_U, HAL_ADC_ISNS_V, HAL_ADC_ISNS_W};
    for (uint32_t k = 0u; k < 3u; k++) {
        if ((H.isns_stuck & (1u << k)) != 0u) {
            sim_adc_set_v(ch[k], 2.5f); /* the zero-current level: inside the 0.2–4.8 V window */
        }
    }
    if (H.isns_u_open) {
        sim_adc_set_v(HAL_ADC_ISNS_U, 0.0f);
    }
}

void h_ramp_speed(float rpm, uint32_t ms)
{
    const float r0 = H.speed_rpm;
    for (uint32_t k = 1u; k <= ms; k++) {
        h_set_speed(r0 + ((rpm - r0) * (float)k / (float)ms));
        h_run_ms(1u);
    }
}

/* The target's two clocks (round 17, T-32): the current-loop ISR on the PWM trigger, every 1/(2 f_sw), and the
 * 1 ms task on the STM compare — both exact, whatever time the code spends: an SPI transfer or a busy wait takes
 * real time but moves neither trigger. The harness runs the ISR and the task in time order, each on its own grid.
 * It cannot preempt a task with an ISR, so ISR triggers that fall inside a long task (FW-16 step h, the FW-15
 * one-shot wait: on the target the ISR preempts them) are dropped, and neither grid is ever shifted. A clock the
 * test reset, or moved on by itself past a whole tick, restarts both grids there. (Before round 17 time advanced
 * in relative steps: every FS26 transfer shifted all later ticks, which hid the watchdog answers' cadence.) */
static uint64_t s_isr_ns;  /* next current-loop trigger */
static uint64_t s_task_ns; /* next 1 ms tick */
#define TICK_NS 1000000u

static uint64_t isr_per_ns(void) { return 500000000u / g_app.gains.fsw_hz; }

static void grids_restart(void)
{
    s_isr_ns = sim_now_ns() + isr_per_ns();
    s_task_ns = sim_now_ns() + TICK_NS;
}

static void grids_check(void)
{
    const uint64_t now = sim_now_ns();
    if (((now + TICK_NS) < s_task_ns) || (now >= (s_task_ns + TICK_NS))) {
        grids_restart();
    }
}

/* Runs every ISR trigger up to and including `until` (a trigger at the same instant as the tick runs first). */
static void isrs_until(uint64_t until)
{
    const uint64_t per = isr_per_ns();
    for (; s_isr_ns <= until; s_isr_ns += per) {
        const uint64_t now = sim_now_ns();
        if (s_isr_ns < now) {
            continue; /* inside a long task (or a clock the test moved): on the target it preempted the task */
        }
        sim_advance_ns(s_isr_ns - now);
        plant_currents();
        app_isr_current(&g_app);
    }
}

void h_isr_only_us(uint32_t us)
{
    grids_check();
    const uint64_t end = sim_now_ns() + ((uint64_t)us * 1000u);
    isrs_until(end);
    sim_advance_ns(end - sim_now_ns());
}

void h_isr_now(void)
{
    plant_currents();
    app_isr_current(&g_app);
}

void h_tick(void)
{
    grids_check();
    isrs_until(s_task_ns);
    if (sim_now_ns() < s_task_ns) {
        sim_advance_ns(s_task_ns - sim_now_ns());
    }
    s_task_ns += TICK_NS; /* a task that overran the next tick runs that one late: the STM trigger is pending */
    app_task_1ms(&g_app);
    app_idle(&g_app);
}

void h_run_ms(uint32_t ms)
{
    for (uint32_t k = 0u; k < ms; k++) {
        if ((H.t_ms % 10u) == 0u) {
            vcu_tx();
        }
        plant_1ms();
        h_tick();
        H.t_ms++;
    }
}

bool h_run_until(sm_state_t st, uint32_t max_ms)
{
    for (uint32_t k = 0u; k < max_ms; k++) {
        if (g_app.sm.st == st) {
            return true;
        }
        h_run_ms(1u);
    }
    return g_app.sm.st == st;
}

bool h_to_armed(void)
{
    if (!h_run_until(SM_PRECHARGE_WAIT, 3000u)) {
        return false;
    }
    H.contactors = TI_CONT_PRECHARGE;
    h_run_ms(800u);
    H.contactors = TI_CONT_CLOSED;
    return h_run_until(SM_ARMED_ZERO_TORQUE, 500u);
}

bool h_to_run(float torque_nm)
{
    if (!h_to_armed()) {
        return false;
    }
    H.enable = true;
    H.torque_nm = torque_nm;
    return h_run_until(SM_RUN, 200u);
}

bool h_fs_ready(fs26_t *fs, ti_params_t *p)
{
    const sim_fs26_cfg_t c = {.prog_id = H_PROG_ID, .device_id = 0x2600u};
    sim_fs26_config(&c);
    sim_fs26_reset();
    p->cal_fs26_prog_id = H_PROG_ID;
    if (fs26_init(fs, p) != FS26_OK) {
        return false;
    }
    for (uint32_t k = 0u; k < 40u; k++) {
        sim_advance_us(2000u);
        (void)fs26_wd_refresh(fs);
        if (fs26_release_safety_outputs(fs) == FS26_OK) {
            return true;
        }
    }
    return false;
}

void h_wait_ms(fs26_t *fs, uint32_t ms)
{
    for (uint32_t k = 0u; k < ms; k++) {
        sim_advance_us(1000u);
        if (fs26_wd_due(fs, hal_time_us())) {
            (void)fs26_wd_refresh(fs);
        }
    }
}
