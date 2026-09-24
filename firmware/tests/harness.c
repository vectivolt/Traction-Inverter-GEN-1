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

void h_set_speed(float rpm)
{
    /* integrate the angle so far, then continue at the new speed (resolver pp = 1) */
    const float w_old = H.speed_rpm / TI_RPM_PER_RAD_S;
    s_theta0 += w_old * (float)(int64_t)(sim_now_ns() - s_theta_t) * 1e-9f;
    s_theta0 = ti_wrap_2pi(s_theta0);
    s_theta_t = sim_now_ns();
    H.speed_rpm = rpm;
    const float k = (H.exc_scale > 0.0f) ? H.exc_scale : 1.0f;
    const sim_resolver_t r = {.theta0_rad = s_theta0, .omega_rad_s = rpm / TI_RPM_PER_RAD_S,
                              .sincos_amp = k * 16000.0f * ((H.rslv_amp > 0.0f) ? H.rslv_amp : 1.0f),
                              .exc_amp = k * 20000.0f, .lag_deg = 24.0f, .sin_gain = 1.0f, .cos_gain = 1.0f};
    sim_resolver_set(&r);
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
    sim_fs26_cfg_t fc = {.prog_id = H_PROG_ID, .device_id = 0x2600u};
    sim_fs26_config(&fc);
    sim_fs26_reset();
    sim_set_hwid_ohm(h_p.hwid_r_ohm);
    sim_set_link_v(0.0f, 0.0f);
    h_set_speed(0.0f);
}

void h_boot(void)
{
    app_init(&g_app, &h_p, &h_cal, SERIAL);
    sim_set_fault_isr(app_fault_isr_entry);
    H.t_ms = hal_time_us() / 1000u;
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
        const bool qdis = hal_gpio_out_state(HAL_DO_QDIS) && !H.qdis_stuck_off;
        const float tau = qdis ? H.tau_dis_s : (h_p.r_bleed_ohm * h_p.c_nom_f);
        v -= v * (dt / tau);
    }
    H.link_v = v;
    sim_set_link_v(v, v * (1.0f + H.ch2_err));
    /* phase currents: a balanced set at the model amplitude (angle from the resolver model) */
    const float th = s_theta0 + (H.speed_rpm / TI_RPM_PER_RAD_S) * (float)(int64_t)(sim_now_ns() - s_theta_t) * 1e-9f;
    const float i = H.i_pk_a;
    sim_set_phase_currents(i * cosf(th), i * cosf(th - 2.0944f), i * cosf(th + 2.0944f));
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

void h_isr_only_us(uint32_t us)
{
    const uint32_t per = app_isr_period_us(&g_app);
    for (uint32_t t = 0u; t < us; t += per) {
        sim_advance_us(per);
        app_isr_current(&g_app);
    }
}

void h_run_ms(uint32_t ms)
{
    const uint32_t per = app_isr_period_us(&g_app);
    const uint32_t n = 1000u / per;
    for (uint32_t k = 0u; k < ms; k++) {
        if ((H.t_ms % 10u) == 0u) {
            vcu_tx();
        }
        plant_1ms();
        for (uint32_t i = 0u; i < n; i++) {
            sim_advance_us(per);
            app_isr_current(&g_app);
        }
        app_task_1ms(&g_app);
        app_idle(&g_app);
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
