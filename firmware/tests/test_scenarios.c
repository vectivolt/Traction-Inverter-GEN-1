/* test_scenarios.c — the whole firmware (app.c) on the simulated card, power board, FS26, VCU
 * and link. Each scenario is a contract edge case end to end. */
#include <string.h>

#include "dtc.h"
#include "harness.h"
#include "nvlog.h"
#include "test.h"

#define LOW_RPM 1000.0f
#define HIGH_RPM 10000.0f /* screening motor n_x = 8086 rpm (8XX) */
#define WRAP_US 4294967296ull /* the 32-bit microsecond counter wraps here (A12-R06) */

static bool ever_armed;

static void run_watch(uint32_t ms)
{
    for (uint32_t k = 0u; k < ms; k++) {
        h_run_ms(1u);
        ever_armed = ever_armed || hal_gpio_out_state(HAL_DO_MCU_GATE_EN);
    }
}

static bool run_at_epoch(float rpm, float torque, uint64_t epoch_us)
{
    sim_reset_at_us(epoch_us);
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    const bool ok = (torque != 0.0f) ? h_to_run(torque) : h_to_armed();
    if (!ok) {
        return false;
    }
    if (rpm != 0.0f) {
        h_ramp_speed(rpm, 600u);
    }
    h_run_ms(20u);
    return (g_app.sm.st == SM_RUN) || (g_app.sm.st == SM_DERATE) || (g_app.sm.st == SM_ARMED_ZERO_TORQUE);
}

static bool run_at(float rpm, float torque) { return run_at_epoch(rpm, torque, 1000000u); }

/* run the whole firmware until the microsecond counter is lead_us short of its wrap */
static void run_until_wrap_minus(uint64_t lead_us)
{
    for (uint32_t k = 0u; (k < 20000u) && ((hal_time_us64() + lead_us) < WRAP_US); k++) {
        h_run_ms(1u);
    }
}

static bool last_status(hal_can_frame_t *out)
{
    hal_can_frame_t f;
    bool got = false;
    while (sim_can_pop_tx(HAL_CAN_VEHICLE, &f)) {
        if (f.id == CAN_ID_INV_STATUS) {
            *out = f;
            got = true;
        }
    }
    return got;
}

/* ---- round 15 (A13-R02) helpers ---- */
/* One 1 ms tick in which the task is the first to see what the test just changed (no VCU frame, no
 * link update of the harness in between). */
static void tick_1ms(void)
{
    h_isr_only_us(1000u);
    app_task_1ms(&g_app);
    app_idle(&g_app);
}

/* The next VCU command frame at once, with the harness state (keeps its alive counter in sequence). */
static void vcu_frame_now(void)
{
    hal_can_frame_t f;
    can_encode_vcu_cmd(&f, H.ctr, H.gear, H.enable, H.fault_reset, H.torque_nm, H.contactors, H.retry_auth,
                       H.discharge, H.shutdown, H.coolant_c);
    sim_can_inject(HAL_CAN_VEHICLE, &f);
    H.ctr = (uint8_t)((H.ctr + 1u) & 0x0Fu);
}

TEST(boot_to_run_follows_section_9)
{
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    CHECK(g_app.init == SM_OK);
    CHECK(!hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && hal_gpio_out_state(HAL_DO_ASC_CLR_N));
    CHECK(h_run_until(SM_VEHICLE_HANDSHAKE, 1000u));
    CHECK(g_app.fs0b_released && !sim_fs26_fs0b_asserted());
    CHECK(!hal_gpio_read(HAL_DI_ASC_CMD_RB)); /* step 5 at standstill: ASC_CLR */
    CHECK(sim_fs26_gpio1());                  /* step 6 */
    CHECK(h_run_until(SM_PRECHARGE_WAIT, 2000u));
    CHECK(g_app.selftest == SM_OK && g_app.st.s == ST_PASS); /* FW-16 ran: link 0 V, contactors open */
    CHECK(!hal_gpio_out_state(HAL_DO_MCU_GATE_EN));          /* not armed before precharge */
    H.contactors = TI_CONT_PRECHARGE;
    h_run_ms(800u);
    CHECK(g_app.pch.res == PCH_OK);
    H.contactors = TI_CONT_CLOSED;
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 300u));
    h_run_ms(5u);
    CHECK(hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && hal_gpio_read(HAL_DI_DRV_EN_RB));
    H.enable = true;
    H.torque_nm = 100.0f;
    CHECK(h_run_until(SM_RUN, 100u));
    h_run_ms(20u);
    CHECK(hal_pwm_mode() == HAL_PWM_MOD);
    CHECK(g_app.iq_ref > 50.0f);
    hal_can_frame_t f;
    CHECK(last_status(&f) && ((f.data[1] & 0x10u) != 0u)); /* self-test done */
    CHECK(dtc_first_active() == DTC_NONE); /* a clean boot leaves no DTC (FW-16's RDY drops included) */
}

/* FW-11 + round 15 (A13-R02): a silent VCU is never held. Armed, its stale report also leaves the
 * contactor state unknown — a battery-path loss: at standstill with 200 Nm the §6 row takes the torque
 * to zero at the current-loop rate (not the slower FW-11 ramp), releases the bridge to SPO once the
 * current is gone and waits for a fresh command; the returning VCU re-arms it through precharge. */
TEST(stale_can_takes_torque_to_zero_not_held)
{
    CHECK(run_at(0.0f, 200.0f));
    CHECK_NEAR(g_app.t_cmd_nm, 200.0, 1.0);
    H.send_cmd = false; /* the VCU goes silent */
    while (can_cmd_fresh(&g_app.can, hal_time_ms() + 1u, g_app.p)) {
        h_run_ms(1u);
    }
    h_run_ms(1u); /* the first stale tick */
    CHECK(fm_active(&g_app.fm, SS_ROW_CMD_LOST) && fm_active(&g_app.fm, SS_ROW_BATTERY_LOST));
    CHECK(dtc_active(DTC_CAN_TIMEOUT) && !g_app.so.torque_enable && g_app.iq_ref == 0.0f);
    h_run_ms(30u);
    CHECK(g_app.t_cmd_nm == 0.0f && hal_pwm_mode() == HAL_PWM_OFF && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    CHECK(g_app.sm.st == SM_VEHICLE_HANDSHAKE && !fm_active(&g_app.fm, SS_ROW_BATTERY_LOST));
    H.send_cmd = true;
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 300u) && h_run_until(SM_RUN, 100u));
}

TEST(frozen_alive_counter_is_stale)
{
    CHECK(run_at(0.0f, 150.0f));
    H.freeze_ctr = true; /* frames keep arriving, all with the same counter */
    h_run_ms(80u);
    CHECK(g_app.t_cmd_nm < 100.0f);
    CHECK(!can_cmd_fresh(&g_app.can, hal_time_ms(), g_app.p));
}

TEST(bms_limit_zero_connected_is_not_asc_but_contactor_open_is)
{
    CHECK(run_at(HIGH_RPM, 0.0f));
    const uint32_t asc_before = sim_gpio_edge_count(HAL_DO_ASC_REQ, true);
    H.p_chg_w = 0.0f; /* full pack: regen permission 0, contactors stay closed */
    H.torque_nm = -100.0f;
    h_run_ms(100u);
    CHECK(sim_gpio_edge_count(HAL_DO_ASC_REQ, true) == asc_before); /* NOT an ASC case */
    CHECK(g_app.br.mode != BR_ASC && hal_pwm_mode() == HAL_PWM_MOD); /* current control kept */
    CHECK(g_app.t_cmd_nm == 0.0f);                                  /* zero regen */
    CHECK(fm_active(&g_app.fm, SS_ROW_BMS_LIMIT_ZERO) && g_app.fm.dec.action == SS_ACT_RAMP_KEEP_CC);
    H.contactors = TI_CONT_OPEN; /* now the pack is really disconnected at speed */
    h_run_ms(20u);
    CHECK(g_app.br.mode == BR_ASC && hal_pwm_mode() == HAL_PWM_ASC && hal_gpio_read(HAL_DI_ASC_CMD_RB));
    CHECK(sim_chain_ls_on() && !sim_chain_hs_on());
}

TEST(fw06_ov_to_asc_request_within_15p6us)
{
    const uint32_t phases[5] = {0u, 1000u, 2500u, 4000u, 4900u};
    for (unsigned k = 0u; k < 5u; k++) {
        sim_reset();
        h_setup(TI_SKU_8XX_SIC);
        h_boot();
        (void)br_arm_idle(&g_app.br); /* the bridge modulating when the pack is lost at full regen */
        const float d[3] = {0.5f, 0.4f, 0.6f};
        (void)br_modulate(&g_app.br, d, g_app.p);
        H.link_override = true;
        /* 0.9 V/us from 875 V, free-running 200 kS/s, 1 us conversion, 8.6 us analog allocation */
        sim_vdc_ramp(875.0f, 0.9f, 5000u, phases[k], 1000u, 8600u);
        const uint64_t t_cross = sim_vdc_ramp_crossing_ns(g_app.p->ov_trip_v);
        const uint64_t t0 = sim_now_ns();
        for (int us = 0; us < 60 && sim_gpio_edge_ns(HAL_DO_ASC_REQ, true, t0) == UINT64_MAX; us++) {
            sim_advance_us(1u);
        }
        const uint64_t t_req = sim_gpio_edge_ns(HAL_DO_ASC_REQ, true, t0);
        CHECK(t_req != UINT64_MAX);
        CHECK(t_req > t_cross);
        CHECK((t_req - t_cross) <= 15600u);                         /* FW-06 budget to ASC_REQ */
        CHECK(sim_pwm_hs_off_ns() <= t_req);                        /* high sides off in hardware first */
        CHECK(sim_pwm_asc_set_ns() >= sim_pwm_hs_off_ns() + 1000u); /* PWM-ASC >= dead time later */
        CHECK(hal_pwm_mode() == HAL_PWM_ASC && hal_gpio_read(HAL_DI_ASC_CMD_RB));
        CHECK(fm_active(&g_app.fm, SS_ROW_OVERVOLTAGE) && dtc_active(DTC_OVERVOLTAGE));
    }
}

TEST(flt_hs_at_speed_spo_then_reset_then_pwm_asc)
{
    CHECK(run_at(HIGH_RPM, 0.0f));
    const uint64_t t_fault = sim_now_ns();
    sim_chain_desat(true, false);
    h_isr_only_us(100u);
    CHECK(!hal_gpio_read(HAL_DI_DRV_EN_RB) && hal_pwm_mode() == HAL_PWM_OFF); /* SPO */
    CHECK(g_app.fm.keep_hv);                                                  /* FW-08b */
    CHECK(g_fm_retained.bank == 1u && g_fm_retained.committed == 0u);         /* FW-15 step 1 */
    h_run_ms(10u);
    const uint64_t t_clr = sim_gpio_edge_ns(HAL_DO_FLT_CLR, false, t_fault);
    CHECK(t_clr != UINT64_MAX && (t_clr - t_fault) >= 1500000u); /* the reset after >= 1.5 ms */
    CHECK(sim_gpio_edge_ns(HAL_DO_ASC_CLR_N, false, t_fault) < t_clr);
    CHECK(g_app.br.mode == BR_ASC && hal_pwm_mode() == HAL_PWM_ASC && hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    CHECK(sim_chain_ls_on() && !sim_chain_hs_on());
    CHECK(!g_app.fm.keep_hv); /* ASC re-established */
    h_run_ms(20u);
    hal_can_frame_t f;
    CHECK(last_status(&f) && ((f.data[3] & 0x03u) == 3u) && ((f.data[1] & 0x20u) == 0u));
}

TEST(flt_ls_at_speed_is_spo_only_even_on_overvoltage)
{
    CHECK(run_at(HIGH_RPM, 0.0f));
    sim_chain_desat(false, false);
    h_isr_only_us(100u);
    const uint64_t t_flt = sim_now_ns();
    CHECK(!hal_gpio_read(HAL_DI_DRV_EN_RB) && g_app.fm.keep_hv && !g_app.fm.asc_permitted);
    H.link_override = true;
    sim_vdc_ramp(870.0f, 0.9f, 5000u, 0u, 1000u, 8600u); /* the rectified charge raises the link */
    bool ls_ever = false;
    for (int k = 0; k < 200; k++) {
        h_isr_only_us(50u);
        ls_ever = ls_ever || sim_chain_ls_on();
    }
    h_run_ms(50u);
    CHECK(sim_gpio_edge_ns(HAL_DO_ASC_REQ, true, t_flt) == UINT64_MAX); /* no ASC request */
    CHECK(!ls_ever && !sim_chain_ls_on());
    CHECK(g_app.fm.dec.action == SS_ACT_SPO && g_app.br.mode != BR_ASC);
}

TEST(desat_one_authorised_retry_then_latch)
{
    CHECK(run_at(LOW_RPM, 300.0f));
    const uint64_t t_fault = sim_now_ns();
    sim_chain_desat(true, false);
    h_run_ms(5u);
    CHECK(g_app.sm.st == SM_FAULT && dtc_active(DTC_DESAT_HS));
    H.retry_auth = true;
    h_run_ms(900u);
    CHECK(sim_gpio_edge_ns(HAL_DO_FLT_CLR, false, t_fault) == UINT64_MAX); /* not before 1 s */
    CHECK(!hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    CHECK(h_run_until(SM_RUN, 800u));
    CHECK(g_app.so.torque_reduced);
    h_run_ms(20u);
    CHECK(g_app.t_cmd_nm <= g_app.p->cal_desat_retry_torque_frac * g_app.p->cal_torque_max_nm + 0.01f);
    const uint64_t t2 = sim_now_ns();
    sim_chain_desat(true, false); /* a second DESAT in the key cycle */
    h_run_ms(3000u);
    CHECK(dtc_active(DTC_DESAT_REPEAT) && g_app.sm.st == SM_FAULT);
    CHECK(sim_gpio_edge_ns(HAL_DO_FLT_CLR, false, t2) == UINT64_MAX); /* no further reset */
    CHECK(!hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
}

TEST(watchdog_missed_drops_drv_en)
{
    CHECK(run_at(0.0f, 50.0f));
    CHECK(hal_gpio_read(HAL_DI_DRV_EN_RB));
    const uint64_t t0 = sim_now_ns();
    uint64_t t_off = 0u;
    for (int us = 0; us < 10000; us += 100) { /* the MCU hangs: nothing runs */
        sim_advance_us(100u);
        if (!hal_gpio_read(HAL_DI_DRV_EN_RB) && (t_off == 0u)) {
            t_off = sim_now_ns();
        }
    }
    CHECK(sim_fs26_fs0b_asserted() && t_off != 0u && (t_off - t0) <= 6000000u);
}

TEST(brownout_during_nvm_write_never_blocks)
{
    CHECK(run_at(LOW_RPM, 100.0f));
    sim_nvm_set_write_polls(100u); /* a slow flash */
    const uint32_t writes = sim_nvm_writes_done();
    sim_chain_desat(false, false);
    h_isr_only_us(50u);
    CHECK(!hal_gpio_read(HAL_DI_DRV_EN_RB) && hal_pwm_mode() == HAL_PWM_OFF); /* safe first */
    CHECK(nv_pending() >= 1u && sim_nvm_writes_done() == writes);           /* only queued */
    h_run_ms(5u);                                                            /* write in flight */
    sim_nvm_power_loss();
    /* reboot inside the key cycle: retained RAM kept, the FS26 restarted */
    sim_nvm_set_write_polls(3u);
    sim_fs26_reset();
    h_boot();
    CHECK(g_app.fm.desat_blocked);
    ever_armed = false;
    run_watch(3000u);
    CHECK(!ever_armed); /* blocks automatic arming whatever FLT reads now */
    nv_desat_t r = {0};
    CHECK(nv_read(NV_REC_DESAT, &r, (uint16_t)sizeof r) && r.bank == 2u); /* re-queued and written */
    CHECK(g_fm_retained.committed == 1u);
}

TEST(hwid_wrong_open_short_never_arm)
{
    const float r[3] = {2200.0f, 1.0e6f, 0.0f};
    const dtc_id_t d[3] = {DTC_SKU_MISMATCH, DTC_HWID_OPEN, DTC_HWID_SHORT};
    for (unsigned k = 0u; k < 3u; k++) {
        sim_reset();
        dtc_init();
        h_setup(TI_SKU_8XX_SIC);
        sim_set_hwid_ohm(r[k]); /* a 4XX-IGBT power board / open / shorted identity */
        h_boot();
        CHECK(g_app.init == SM_FAIL && dtc_active(d[k]));
        H.contactors = TI_CONT_CLOSED;
        H.enable = true;
        H.torque_nm = 100.0f;
        ever_armed = false;
        run_watch(1500u);
        CHECK(!ever_armed && g_app.sm.st == SM_FAULT);
    }
}

TEST(v5gd_out_of_window_forces_spo_and_unknown_hv)
{
    CHECK(run_at(LOW_RPM, 100.0f));
    const uint64_t t0 = sim_now_ns();
    sim_set_v5gd(4.5f); /* hovering: FLT/RDY still read high, only the V5GD_SNS pin sees it */
    h_run_ms(5u);
    CHECK(!hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && hal_pwm_mode() == HAL_PWM_OFF);
    CHECK(sim_gpio_edge_ns(HAL_DO_ASC_CLR_N, false, t0) != UINT64_MAX);
    CHECK(dtc_active(DTC_V5GD) && g_app.sm.st == SM_FAULT);
    h_run_ms(20u);
    hal_can_frame_t f;
    CHECK(last_status(&f) && (((f.data[3] >> 2) & 0x03u) == (uint8_t)TI_HV_UNKNOWN));
    sim_set_v5gd(5.0f);
    ever_armed = false;
    run_watch(500u);
    CHECK(!ever_armed); /* latched: no arming */
}

TEST(vdc_disagreement_spo_and_unknown_hv)
{
    CHECK(run_at(LOW_RPM, 50.0f));
    H.i_pk_a = 30.0f;
    H.ch2_err = 0.08f; /* 8 % */
    h_run_ms(10u);
    CHECK(fm_active(&g_app.fm, SS_ROW_VDC_INVALID) && g_app.fm.dec.action == SS_ACT_SPO);
    CHECK(!hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && hal_pwm_mode() == HAL_PWM_OFF);
    h_run_ms(20u);
    hal_can_frame_t f;
    CHECK(last_status(&f) && (((f.data[3] >> 2) & 0x03u) == (uint8_t)TI_HV_UNKNOWN));
    CHECK(dtc_active(DTC_VDC_DISAGREE));
}

TEST(resolver_amplitude_low_asc_at_speed_spo_below)
{
    CHECK(run_at(HIGH_RPM, 0.0f));
    H.rslv_amp = 0.6f;
    h_set_speed(HIGH_RPM);
    h_run_ms(5u);
    CHECK(fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && dtc_active(DTC_RSLV_AMPLITUDE));
    CHECK(g_app.br.mode == BR_ASC && hal_pwm_mode() == HAL_PWM_ASC); /* no angle needed */
    CHECK(run_at(LOW_RPM, 50.0f));
    H.i_pk_a = 20.0f;
    H.rslv_amp = 0.6f;
    h_set_speed(LOW_RPM);
    h_run_ms(5u);
    CHECK(g_app.fm.dec.action == SS_ACT_SPO && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
}

TEST(nan_reference_never_reaches_pwm)
{
    CHECK(run_at(LOW_RPM, 80.0f));
    g_app.iq_ref = NAN;
    g_app.zero_now = false;
    h_isr_only_us(50u);
    CHECK(sim_pwm_nan_writes() == 0u && hal_pwm_mode() == HAL_PWM_OFF);
    CHECK(dtc_active(DTC_CTRL_NONFINITE) && g_app.foc.guard_trip);
}

TEST(key_off_discharge_witnessed_then_lpoff)
{
    CHECK(run_at(0.0f, 0.0f));
    H.enable = false;
    H.torque_nm = 0.0f;
    H.contactors = TI_CONT_OPEN; /* normal opening at zero torque */
    h_run_ms(20u);
    sim_set_kl15(0.0f);
    CHECK(h_run_until(SM_SAFE_POWERDOWN, 200u));
    h_run_ms(300u);
    CHECK(hal_gpio_out_state(HAL_DO_QDIS) || (g_app.dis.st == DIS_DONE));
    CHECK(!g_app.dis.stuck_off && !g_app.dis.tau_mismatch);
    CHECK(h_run_until(SM_OFF, 5000u));
    CHECK(sim_fs26_lpoff() && !hal_gpio_out_state(HAL_DO_QDIS) && (g_app.vdc.vdc < 60.0f));
}

TEST(qdis_stuck_off_detected)
{
    CHECK(run_at(0.0f, 0.0f));
    H.enable = false;
    H.contactors = TI_CONT_OPEN;
    H.qdis_stuck_off = true;
    H.discharge = true;
    h_run_ms(400u);
    CHECK(g_app.dis.stuck_off && dtc_active(DTC_QDIS_STUCK_OFF) && !hal_gpio_out_state(HAL_DO_QDIS));
    CHECK(g_app.dis.n_fire == 1u); /* not re-fired into a failed path */
    hal_can_frame_t f;
    h_run_ms(20u);
    CHECK(last_status(&f) && (((f.data[3] >> 2) & 0x03u) == (uint8_t)TI_HV_PRESENT)); /* still HV, reported */
}

TEST(mcu_reset_at_speed_keeps_asc_then_exits_with_battery)
{
    CHECK(run_at(0.0f, 0.0f));
    h_ramp_speed(HIGH_RPM, 600u);
    h_run_ms(10u);
    /* FS26 watchdog reaction: RSTB + FS0B/FS1B (FS1B presets the ASC latch, FS_GPIO1 keeps gate
     * power up: FS1B-ASC with EN low while the MCU restarts) */
    sim_fs26_mcu_reset();
    CHECK(sim_chain_ls_on() && !hal_gpio_read(HAL_DI_DRV_EN_RB));
    h_boot();
    CHECK(h_run_until(SM_VEHICLE_HANDSHAKE, 1500u));
    CHECK(g_app.asc_hold && hal_gpio_read(HAL_DI_ASC_CMD_RB)); /* §9 step 5 at n >= n_x */
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 1500u));
    CHECK(g_app.st.s == ST_SKIP && g_app.selftest == SM_OK); /* stored pass, never run at speed */
    h_run_ms(20u);
    CHECK(!g_app.asc_hold && g_app.br.mode != BR_ASC); /* battery present + current control ready */
    CHECK(hal_pwm_mode() == HAL_PWM_MOD);               /* field weakening at zero torque */
}

TEST(precharge_plateau_5pct_low_refuses)
{
    h_setup(TI_SKU_8XX_SIC);
    H.plateau_frac = 0.95f; /* shorted QDIS against the precharge resistor */
    h_boot();
    CHECK(h_run_until(SM_PRECHARGE_WAIT, 3000u));
    H.contactors = TI_CONT_PRECHARGE;
    ever_armed = false;
    run_watch(1000u);
    H.contactors = TI_CONT_CLOSED;
    run_watch(500u);
    CHECK(!ever_armed && g_app.sm.st == SM_FAULT && dtc_active(DTC_PRECHARGE_PLATEAU));
}

TEST(hvil_open_ramps_torque_within_100ms)
{
    CHECK(run_at(LOW_RPM, 200.0f));
    sim_hvil_set(SIM_HVIL_OPEN);
    h_run_ms(100u);
    CHECK(dtc_active(DTC_HVIL_OPEN) && fm_active(&g_app.fm, SS_ROW_CMD_LOST));
    CHECK(g_app.t_cmd_nm < 190.0f); /* ramping toward zero */
    CHECK(g_app.br.mode != BR_ASC);  /* not a licence to do anything drastic */
}

TEST(open_hall_wire_invalidates_and_stops_modulation)
{
    CHECK(run_at(LOW_RPM, 80.0f));
    H.i_pk_a = 20.0f;
    h_run_ms(5u);
    H.isns_u_open = true;
    h_run_ms(2u);
    CHECK(!g_app.isns.ch_valid[0] && g_app.isns.open_wire[0] && dtc_active(DTC_ISNS_OPEN));
    CHECK(hal_pwm_mode() != HAL_PWM_MOD && fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID));
}

TEST(overcurrent_crest_does_not_trip_620_does)
{
    CHECK(run_at(LOW_RPM, 100.0f));
    H.i_pk_a = 481.0f;
    h_run_ms(50u);
    CHECK(!dtc_active(DTC_OVERCURRENT) && hal_pwm_mode() == HAL_PWM_MOD);
    H.i_pk_a = 620.0f;
    h_run_ms(2u);
    CHECK(dtc_active(DTC_OVERCURRENT) && fm_active(&g_app.fm, SS_ROW_OVERCURRENT));
    CHECK(hal_pwm_mode() != HAL_PWM_MOD);
    /* 620 A into an isolated 8XX link fails rule (a); rule (b) is not released for this motor */
    CHECK(g_app.fm.dec.action == SS_ACT_LS_ASC && g_app.br.mode == BR_ASC);
}

TEST(asc_exit_only_below_n_x_by_mcu_command)
{
    CHECK(run_at(HIGH_RPM, 0.0f));
    H.contactors = TI_CONT_OPEN; /* battery path lost at speed */
    H.i_pk_a = 400.0f;          /* ASC current circulating */
    h_run_ms(10u);
    CHECK(g_app.br.mode == BR_ASC);
    const uint64_t t0 = sim_now_ns();
    h_run_ms(500u);
    CHECK(sim_gpio_edge_ns(HAL_DO_ASC_CLR_N, false, t0) == UINT64_MAX); /* held at n >= n_x */
    h_ramp_speed(6000.0f, 800u);
    h_run_ms(50u);
    CHECK(g_app.br.mode == BR_ASC); /* below n_x but 400 A still circulating: SPO not yet safe */
    H.i_pk_a = 4.0f;
    h_ramp_speed(300.0f, 600u);
    h_run_ms(20u);
    CHECK(g_app.br.mode != BR_ASC && !hal_gpio_read(HAL_DI_ASC_CMD_RB));
    CHECK(sim_gpio_edge_ns(HAL_DO_ASC_CLR_N, false, t0) != UINT64_MAX);
}

TEST(swg_trim_is_written_to_the_generator)
{
    h_setup(TI_SKU_8XX_SIC);
    H.exc_scale = 0.9f; /* a weak SWG: monitor at 0.9 of EOL at the nominal code */
    h_set_speed(0.0f);
    h_boot();
    h_run_ms(300u);
    CHECK(sim_swg_code() == 13u); /* 0.9 x 13/12 = 0.975: inside the +-5 % dead band */
    CHECK(g_app.rslv.exc_ratio > 0.95f && g_app.rslv.exc_ratio < 1.05f);
}

/* A12-R05 end to end. The driver latches FLT at t_flt; the fault ISR runs at t = 0 (+0.3 us). The
 * 1 ms task then runs inside the hold (at n >= n_x the FW-15 recovery starts: br_rec_start; below
 * n_x apply_decision's SPO) and the current-loop ISR polls. MCU_GATE_EN must stay high until the
 * hold has run (checked at hold - 1 and in the edge log), the PWM must be off from t = 0, and it
 * must drop by hold + 1; the §6 follow-up (FW-15 reset, PWM-ASC at speed) still happens. */
static void desat_hold_case(float rpm, bool hs)
{
    CHECK(run_at(rpm, 0.0f));
    CHECK(hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && hal_gpio_read(HAL_DI_DRV_EN_RB));
    const uint32_t hold = g_app.p->cal_desat_en_hold_us;
    const uint64_t t_flt = sim_now_ns();
    sim_chain_desat(hs, false);
    sim_advance_ns(300u); /* fault ISR entry: t = 0 */
    CHECK(hal_pwm_mode() == HAL_PWM_OFF && !sim_chain_hs_on() && !sim_chain_ls_on());
    CHECK(hal_gpio_out_state(HAL_DO_MCU_GATE_EN)); /* not dropped by the ISR */
    sim_advance_us(10u);
    app_task_1ms(&g_app); /* apply_decision / br_rec_start inside the hold */
    app_isr_current(&g_app);
    const uint64_t t_hm1 = t_flt + ((uint64_t)(hold - 1u) * 1000u);
    if (sim_now_ns() < t_hm1) {
        sim_advance_ns(t_hm1 - sim_now_ns());
    }
    app_isr_current(&g_app);
    CHECK(sim_gpio_edge_ns(HAL_DO_MCU_GATE_EN, false, t_flt) >= t_flt + ((uint64_t)hold * 1000u));
    CHECK(hal_pwm_mode() == HAL_PWM_OFF && !hal_gpio_read(HAL_DI_DRV_EN_RB)); /* the latch holds DRV_EN */
    sim_advance_ns(t_flt + ((uint64_t)(hold + 1u) * 1000u) - sim_now_ns());
    app_isr_current(&g_app);
    CHECK(!hal_gpio_out_state(HAL_DO_MCU_GATE_EN)); /* hold + 1: dropped */
    const uint64_t t_en = sim_gpio_edge_ns(HAL_DO_MCU_GATE_EN, false, t_flt);
    CHECK(t_en != UINT64_MAX && (t_en - t_flt) >= (uint64_t)hold * 1000u && (t_en - t_flt) <= 200000u);
}

TEST(desat_at_speed_holds_en_through_the_hold_then_reset_and_pwm_asc)
{
    desat_hold_case(HIGH_RPM, true);
    const uint64_t t_fault = sim_now_ns();
    h_run_ms(10u); /* the FW-15 reset, then PWM-ASC (§6 FLT_HS at n >= n_x) */
    CHECK(sim_gpio_edge_ns(HAL_DO_FLT_CLR, false, t_fault - 200000u) != UINT64_MAX);
    CHECK(g_app.br.mode == BR_ASC && hal_pwm_mode() == HAL_PWM_ASC && hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    CHECK(sim_chain_ls_on() && !sim_chain_hs_on());
}

TEST(desat_at_low_speed_spo_waits_for_the_hold)
{
    desat_hold_case(LOW_RPM, false);
    h_run_ms(20u);
    CHECK(g_app.fm.dec.action == SS_ACT_SPO && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && g_app.sm.st == SM_FAULT);
}

/* A12-R06: VCU and BMS frames every 10 ms keep arriving while the counter wraps: nothing may go
 * stale, no torque may ramp; after the wrap a silent VCU is still caught within FW-11's 20 ms. */
TEST(running_across_the_microsecond_wrap_keeps_fresh_frames_fresh)
{
    CHECK(run_at_epoch(LOW_RPM, 100.0f, WRAP_US - 3000000u));
    run_until_wrap_minus(40000u);
    bool stale = false;
    bool lost = false;
    for (uint32_t k = 0u; k < 100u; k++) {
        h_run_ms(1u);
        stale = stale || !can_cmd_fresh(&g_app.can, hal_time_ms(), g_app.p) ||
                !can_bms_fresh(&g_app.can, hal_time_ms(), g_app.p);
        lost = lost || fm_active(&g_app.fm, SS_ROW_CMD_LOST) || (g_app.t_cmd_nm < 99.0f);
    }
    CHECK(hal_time_us64() > WRAP_US && hal_time_us() < 100000u); /* crossed it */
    CHECK(!stale && !lost && !dtc_active(DTC_CAN_TIMEOUT) && !dtc_active(DTC_BMS_TIMEOUT));
    CHECK(g_app.sm.st == SM_RUN && hal_pwm_mode() == HAL_PWM_MOD);
    H.send_cmd = false; /* the VCU goes silent after the wrap */
    h_run_ms(35u);
    CHECK(dtc_active(DTC_CAN_TIMEOUT) && fm_active(&g_app.fm, SS_ROW_CMD_LOST) && g_app.t_cmd_nm < 99.0f);
}

/* A12-R06: the wrap placed inside every boot dwell timer (sensor self-test / FS0B release, gate-power
 * start 11–161 ms, FW-16 RDY rise 170–320 and 323–473 ms, precharge 483–1178 ms): each boot still
 * reaches ARMED_ZERO_TORQUE with no DTC. */
TEST(boot_across_the_microsecond_wrap_reaches_armed)
{
    const uint32_t wrap_after_boot_ms[5] = {6u, 80u, 250u, 400u, 800u};
    for (unsigned k = 0u; k < 5u; k++) {
        sim_reset_at_us(WRAP_US - (1000u * (uint64_t)wrap_after_boot_ms[k]));
        dtc_init();
        h_setup(TI_SKU_8XX_SIC);
        h_boot();
        CHECK(h_to_armed());
        h_run_ms(5u);
        CHECK(hal_time_us64() > WRAP_US && dtc_first_active() == DTC_NONE);
        CHECK(hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && g_app.selftest == SM_OK);
    }
}

/* A12-R06 / FW-15: a DESAT 100 ms before the wrap with the retry authorised at once: the FW-15
 * recovery may not start before 1 s after the event (the old ms stamps saw 4 290 000 s at the wrap). */
TEST(desat_retry_waits_1s_across_the_microsecond_wrap)
{
    CHECK(run_at_epoch(LOW_RPM, 100.0f, WRAP_US - 3000000u));
    run_until_wrap_minus(100000u);
    const uint64_t t_fault = sim_now_ns();
    sim_chain_desat(true, false);
    h_run_ms(5u);
    CHECK(g_app.sm.st == SM_FAULT && dtc_active(DTC_DESAT_HS));
    H.retry_auth = true;
    h_run_ms(1600u);
    const uint64_t t_clr = sim_gpio_edge_ns(HAL_DO_FLT_CLR, false, t_fault);
    CHECK(t_clr != UINT64_MAX && (t_clr - t_fault) >= 1000000000u);
}

/* A12-R06: DTC time stamps the application takes across the wrap keep their distance. Armed at zero
 * torque (round 15: a stale report under torque is a battery-path loss that disarms, after which the
 * timeout DTC is no longer re-stamped; at zero torque the inverter stays armed and it is). */
TEST(dtc_time_stamps_across_the_microsecond_wrap_in_the_application)
{
    CHECK(run_at_epoch(LOW_RPM, 0.0f, WRAP_US - 3000000u));
    run_until_wrap_minus(60000u);
    H.send_cmd = false; /* silent from 60 ms before the wrap to 60 ms after */
    h_run_ms(120u);
    uint32_t first = 0u;
    uint32_t last = 0u;
    CHECK(dtc_times(DTC_CAN_TIMEOUT, &first, &last));
    CHECK(ti_age(last, first) >= 90u && ti_age(last, first) <= 110u);
}

/* A12-R08 on the vehicle CAN: a DESAT at standstill with 340 A rms flowing, on a vehicle released
 * under rule (b): keep HV asserted; the pack disconnected: keep HV dropped, "no safe state proven"
 * set; reconnected and the current decayed: both clear. */
TEST(standstill_desat_at_rated_current_reports_keep_hv_on_can)
{
    sim_reset();
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    h_cal.motor.rule_b_released = true; /* HIL/dyno showed rule (b) for this vehicle */
    calib_seal(&h_cal);
    h_boot();
    CHECK(h_to_armed());
    H.i_pk_a = 480.8f; /* 340 A rms at 0 rpm */
    h_run_ms(5u);
    sim_chain_desat(true, false);
    h_run_ms(30u);
    hal_can_frame_t f;
    CHECK(last_status(&f) && ((f.data[1] & 0x20u) != 0u) && ((f.data[14] & 0x01u) == 0u));
    CHECK(g_app.fm.dec.action == SS_ACT_SPO && !hal_gpio_read(HAL_DI_DRV_EN_RB));
    H.contactors = TI_CONT_OPEN; /* the battery goes */
    h_run_ms(30u);
    CHECK(last_status(&f) && ((f.data[1] & 0x20u) == 0u) && ((f.data[14] & 0x01u) != 0u));
    CHECK(dtc_active(DTC_SPO_ENERGY));
    H.contactors = TI_CONT_CLOSED;
    H.i_pk_a = 5.0f; /* the winding current has decayed */
    h_run_ms(30u);
    CHECK(last_status(&f) && ((f.data[1] & 0x20u) == 0u) && ((f.data[14] & 0x01u) == 0u));
}

/* F06: the host default validates nothing. The firmware never leaves the inhibited state: FS0B is
 * never released, FW-16 never starts (no gate energised through ASC), MCU_GATE_EN and DRV_EN never
 * rise, gate power is never enabled; the status names every missing item. */
TEST(arming_refused_without_evidence_and_the_status_names_it)
{
    h_setup(TI_SKU_8XX_SIC);
    h_unprovision(); /* no IMCR route bound, no EOL/HIL validation record */
    h_boot();
    CHECK(g_app.init == SM_FAIL && dtc_active(DTC_ARM_EVIDENCE));
    H.enable = true;
    H.torque_nm = 100.0f;
    const uint64_t t0 = sim_now_ns();
    bool gates = false;
    for (uint32_t k = 0u; k < 3000u; k++) {
        if (g_app.sm.st == SM_PRECHARGE_WAIT) {
            H.contactors = TI_CONT_CLOSED;
        }
        h_run_ms(1u);
        gates = gates || hal_gpio_out_state(HAL_DO_MCU_GATE_EN) || hal_gpio_read(HAL_DI_DRV_EN_RB) ||
                hal_gpio_out_state(HAL_DO_EN_FLYBK_HS) || hal_gpio_out_state(HAL_DO_EN_FLYBK_LS);
    }
    CHECK(!gates && sim_fs26_fs0b_asserted() && g_app.st.s == ST_IDLE && g_app.sm.st == SM_FAULT);
    CHECK(sim_gpio_edge_ns(HAL_DO_ASC_REQ, true, t0) == UINT64_MAX); /* no FW-16 ASC energisation */
    hal_can_frame_t f;
    CHECK(last_status(&f) && (f.data[15] == (ARM_EV_ROUTE_BOUND | ARM_EV_FAULT_ROUTE_VALIDATED | ARM_EV_OVP_ROUTE_VALIDATED)));
}

/* F01: a board configuration with the TODO(RM) placeholders (host default: UNBOUND) never arms, even
 * with a valid validation record. */
TEST(unbound_fault_route_never_arms)
{
    h_setup(TI_SKU_8XX_SIC);
    sim_pwm_fault_route_bind(false);
    h_boot();
    CHECK(g_app.init == SM_FAIL && dtc_active(DTC_ARM_EVIDENCE));
    H.contactors = TI_CONT_CLOSED;
    H.enable = true;
    ever_armed = false;
    run_watch(2500u);
    hal_can_frame_t f;
    CHECK(!ever_armed && last_status(&f) && (f.data[15] == ARM_EV_ROUTE_BOUND));
}

/* F06: positive control — everything present: arming permitted and nothing reported missing. */
TEST(arming_permitted_with_a_valid_record)
{
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    CHECK(g_app.init == SM_OK && h_to_armed());
    h_run_ms(20u);
    hal_can_frame_t f;
    CHECK(hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && last_status(&f) && (f.data[15] == 0u));
}

/* F06: a record for another card, another image, another SKU, without the OVP validation, with the
 * FW-06 chain measured outside 15.6 us, or with a broken CRC validates nothing: arming refused. */
TEST(arming_refused_on_record_identity_or_crc_mismatch)
{
    for (unsigned k = 0u; k < 6u; k++) {
        sim_reset();
        dtc_init();
        h_setup(TI_SKU_8XX_SIC);
        arm_validation_t v;
        arm_validation_make(&v, h_serial(), h_p.sku, TI_FW_ID, ARM_EV_VALIDATED, 14200u);
        switch (k) {
        case 0u: v.hw_serial[7] ^= 0x01u; break;            /* another card */
        case 1u: v.fw_id += 1u; break;                      /* another image */
        case 2u: v.sku = (uint8_t)TI_SKU_4XX_SIC; break;    /* another SKU */
        case 3u: v.flags = ARM_EV_FAULT_ROUTE_VALIDATED; break; /* OVP chain not validated */
        case 4u: v.ovp_chain_ns = 16000u; break;            /* measured, but over 15.6 us */
        default: break;
        }
        arm_validation_seal(&v);
        if (k == 5u) {
            v.crc32 ^= 0x00010000u; /* a corrupted record */
        }
        sim_nvm_wipe();
        h_store_validation(&v);
        h_boot();
        H.contactors = TI_CONT_CLOSED;
        H.enable = true;
        ever_armed = false;
        run_watch(2000u);
        hal_can_frame_t f;
        CHECK(!ever_armed && dtc_active(DTC_ARM_EVIDENCE) && last_status(&f));
        const uint8_t miss = (k < 3u) || (k == 5u) ? (uint8_t)ARM_EV_VALIDATED : (uint8_t)ARM_EV_OVP_ROUTE_VALIDATED;
        CHECK(f.data[15] == miss);
    }
}

/* F02: after a watchdog (RSTB) reset the lock-down is re-written and re-locked before anything can
 * arm again, and stays write-protected. */
TEST(watchdog_reset_relocks_before_rearming)
{
    CHECK(run_at(0.0f, 0.0f));
    sim_fs26_mcu_reset();
    CHECK(!hal_pwm_protection_locked());
    h_boot();
    CHECK(hal_pwm_protection_locked() && hal_pwm_config_matches() && g_app.init == SM_OK);
    CHECK(!sim_pwm_reg_write(SIM_PWM_DISMAP0_SM2, 0x0000u, SIM_MASTER_CPU));
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 3000u));
    h_run_ms(20u);
    hal_can_frame_t f;
    CHECK(last_status(&f) && (f.data[15] == 0u));
}

/* F06 at run time: evidence read back every tick. If the route stops reading back bound while armed,
 * arming is withdrawn through the §6 "control lost" row (not a blind SPO) and the status says why. */
TEST(evidence_lost_while_armed_goes_through_section6)
{
    CHECK(run_at(LOW_RPM, 50.0f));
    sim_pwm_fault_route_bind(false); /* e.g. an IMCR corrupted after init */
    h_run_ms(20u);
    hal_can_frame_t f;
    CHECK(dtc_active(DTC_ARM_EVIDENCE) && fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID));
    CHECK(g_app.sm.st == SM_FAULT && hal_pwm_mode() != HAL_PWM_MOD && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    CHECK(last_status(&f) && (f.data[15] == ARM_EV_ROUTE_BOUND));
}

/* F23: a motor whose demagnetisation limit (100 A) is below the field weakening the speed needs:
 * no voltage-feasible current exists even at iq = 0, so the firmware commands zero torque, asks the
 * VCU for a speed limit and records the DTC. */
TEST(infeasible_current_gives_zero_torque_speed_limit_and_dtc)
{
    sim_reset();
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    h_cal.motor.id_demag_a = 100.0f;
    calib_seal(&h_cal);
    h_boot();
    CHECK(h_to_run(50.0f));
    h_ramp_speed(HIGH_RPM, 600u);
    h_run_ms(20u);
    hal_can_frame_t f;
    CHECK(g_app.t_cmd_nm == 0.0f && g_app.iq_ref == 0.0f && g_app.id_ref >= -100.0f - 1e-3f);
    CHECK(dtc_active(DTC_TORQUE_INFEASIBLE) && last_status(&f) && ((f.data[14] & 0x08u) != 0u));
    h_ramp_speed(LOW_RPM, 600u); /* back where it is feasible: torque returns, request withdrawn */
    h_run_ms(20u);
    CHECK(g_app.t_cmd_nm > 40.0f && last_status(&f) && ((f.data[14] & 0x08u) == 0u));
}

/* F24 end to end: all three sensor outputs stuck at the zero-current level (e.g. a failed shared
 * reference). At zero command nothing can be seen; once torque is commanded the KCL sum still reads
 * 0 A, but the activity check invalidates the currents: control lost, the §6 row, no modulation. */
TEST(all_three_current_channels_stuck_detected_under_command)
{
    CHECK(run_at(LOW_RPM, 0.0f));
    H.isns_stuck = 0x7u;
    h_run_ms(50u);
    CHECK(g_app.isns.valid && !dtc_active(DTC_ISNS_STUCK)); /* zero command: no evidence */
    H.enable = true;
    H.torque_nm = 100.0f;
    h_run_ms(30u);
    CHECK(!g_app.isns.sum_fault && dtc_active(DTC_ISNS_STUCK));
    CHECK(fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && hal_pwm_mode() != HAL_PWM_MOD);
}

/* F24 end to end: one channel stuck while the commanded current stays inside the 45 A KCL tolerance
 * (35 Nm = 39 A peak): the sum never trips, the activity check does. */
TEST(one_current_channel_stuck_below_the_kcl_tolerance_detected)
{
    CHECK(run_at(LOW_RPM, 35.0f));
    H.isns_stuck = 0x4u; /* phase W */
    h_run_ms(40u);
    CHECK(!g_app.isns.sum_fault && dtc_active(DTC_ISNS_STUCK));
    CHECK(fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && hal_pwm_mode() != HAL_PWM_MOD);
}

/* Item 9: a QDIS shorted while the battery is connected (its 5 s release cannot switch it off) shows
 * at the next contactor opening as a link falling at the active-discharge rate with no discharge
 * commanded: latched DTC, "service required / do not re-energise" and "open the contactors" on CAN,
 * no re-arming when the VCU closes the contactors again, and the lock survives a reboot. */
TEST(stuck_on_qdis_latches_service_required_and_never_rearms)
{
    CHECK(run_at(0.0f, 0.0f));
    H.qdis_stuck_on = true;
    H.enable = false;
    H.contactors = TI_CONT_OPEN; /* a normal opening at zero torque */
    h_run_ms(1300u);
    hal_can_frame_t f;
    CHECK(g_app.dis.stuck_on && dtc_active(DTC_QDIS_STUCK_ON));
    CHECK(!hal_gpio_out_state(HAL_DO_QDIS)); /* never commanded */
    CHECK(last_status(&f) && ((f.data[14] & 0x06u) == 0x06u));
    H.qdis_stuck_on = false; /* even if the part recovers: no re-energising before service */
    H.link_v = 0.0f;
    H.contactors = TI_CONT_PRECHARGE;
    ever_armed = false;
    run_watch(800u);
    H.contactors = TI_CONT_CLOSED;
    H.enable = true;
    run_watch(500u);
    CHECK(!ever_armed && g_app.sm.st == SM_FAULT);
    CHECK(last_status(&f) && ((f.data[14] & 0x06u) == 0x06u));
    sim_reset(); /* power off and on: the next key cycle (retained RAM lost, the NVM kept) */
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    CHECK(g_app.cold_start && g_app.init == SM_FAIL && dtc_active(DTC_QDIS_STUCK_ON));
    H.contactors = TI_CONT_CLOSED;
    H.enable = true;
    ever_armed = false;
    run_watch(1500u);
    CHECK(!ever_armed && last_status(&f) && ((f.data[14] & 0x06u) == 0x06u));
}

/* ======================= round 15 ======================= */

/* A13-R04 schedule: on the target the slow list converts nothing before it is first started, and
 * app_init classifies HW_ID (ADC3_P0, slow list) before the first 1 ms tick. The identity must come
 * from a real conversion: no false "HW_ID short", arming as usual. */
TEST(hw_id_is_converted_before_it_is_classified)
{
    h_setup(TI_SKU_8XX_SIC);
    sim_adc_require_slow_start(true);
    h_boot();
    CHECK(g_app.init == SM_OK && g_app.hw_sku == TI_SKU_8XX_SIC && !dtc_active(DTC_HWID_SHORT));
    CHECK(h_to_armed());
}

/* A13-R02, the reviewer's reproduction: a known low speed and the VCU reporting the contactors OPEN
 * while running. Before round 15 the detector needed n >= n_x (cont_lost = 0) and RUN moved to
 * ARMED_ZERO_TORQUE with arm = 1 and torque_enable = 1 in that same invocation: the request was still
 * applied. Now the row is dispatched and that invocation grants no torque. */
TEST(low_speed_open_contactor_is_a_battery_path_loss)
{
    CHECK(run_at(LOW_RPM, 100.0f));
    H.contactors = TI_CONT_OPEN;
    vcu_frame_now();
    tick_1ms();
    CHECK(fm_active(&g_app.fm, SS_ROW_BATTERY_LOST));
    CHECK(!(g_app.sm.st == SM_ARMED_ZERO_TORQUE && g_app.so.arm && g_app.so.torque_enable));
    CHECK(!g_app.so.torque_enable && g_app.iq_ref == 0.0f);
}

typedef enum { LOSS_OPEN = 0, LOSS_INVALID, LOSS_STALE } loss_t;
typedef enum { SPD_ZERO = 0, SPD_LOW, SPD_HIGH, SPD_UNKNOWN } spd_t;

/* One case of the matrix below, up to and including the task invocation that processes the loss. */
static bool loss_case(spd_t sp, loss_t l, float *t_before)
{
    sim_reset();
    sim_nvm_wipe();
    dtc_init();
    (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    h_setup(TI_SKU_8XX_SIC);
    if (sp == SPD_UNKNOWN) {
        h_p.cal_speed_hold_ms = 0u; /* no held speed: a resolver fault makes the speed unknown at once */
    }
    h_boot();
    if (!h_to_run(50.0f)) {
        return false;
    }
    if (sp != SPD_ZERO) {
        h_ramp_speed((sp == SPD_HIGH) ? HIGH_RPM : LOW_RPM, 600u);
    }
    H.torque_nm = (sp == SPD_ZERO) ? 200.0f : -150.0f; /* holding at standstill, else regenerating */
    h_run_ms(30u);
    *t_before = g_app.t_cmd_nm;
    if (l == LOSS_STALE) {
        H.send_cmd = false;
        while (can_cmd_fresh(&g_app.can, hal_time_ms() + 1u, g_app.p)) {
            h_run_ms(1u);
        }
    } else {
        H.contactors = (l == LOSS_OPEN) ? TI_CONT_OPEN : TI_CONT_INVALID;
        vcu_frame_now();
    }
    H.i_pk_a = 480.0f; /* 340 A rms in the winding at the loss */
    if (sp == SPD_UNKNOWN) {
        H.rslv_amp = 0.6f; /* resolver lost in the same tick: last valid speed LOW_RPM, now unknown */
        h_set_speed(H.speed_rpm);
    }
    tick_1ms();
    return true;
}

/* A13-R02: armed, a lost battery path — contactors reported OPEN, INVALID, or no fresh report — is the
 * §6 battery-lost row at zero, low, high and unknown speed, with 340 A rms in the winding and the
 * motor regenerating. In the invocation that processes it: the row is dispatched (with the command
 * row when stale), the state machine grants neither torque nor arm, the torque/current target is
 * the §6 one (zero at the current-loop rate below n_x, LS-ASC above or unknown), never the request.
 * Then: below n_x zero-torque current control keeps the energy under control (not all gates off)
 * until the current has decayed, then SPO and back to the unarmed sequence; above n_x or at unknown
 * speed PWM-ASC holds. The VCU sees FAULT, the bridge mode, no "keep HV" and no "no safe state". */
TEST(battery_path_loss_while_armed_at_every_speed)
{
    static const char *const SP[4] = {"zero", "low", "high", "unknown"};
    static const char *const LS[3] = {"OPEN", "INVALID", "stale"};
    for (unsigned sp = 0u; sp < 4u; sp++) {
        for (unsigned l = 0u; l < 3u; l++) {
            const unsigned before = t_fails;
            float t_before = 0.0f;
            CHECK(loss_case((spd_t)sp, (loss_t)l, &t_before));
            const bool slow = (sp == SPD_ZERO) || (sp == SPD_LOW);
            const ss_decision_t *d = &g_app.fm.row_dec[SS_ROW_BATTERY_LOST];
            /* the invocation that processed the loss */
            CHECK(fm_active(&g_app.fm, SS_ROW_BATTERY_LOST));
            CHECK((l != LOSS_STALE) || fm_active(&g_app.fm, SS_ROW_CMD_LOST));
            CHECK(d->action == (slow ? SS_ACT_ZERO_TORQUE_DCL : SS_ACT_LS_ASC) && d->high_speed == !slow);
            CHECK(g_app.sm.st == SM_FAULT && !g_app.so.arm && !g_app.so.torque_enable);
            CHECK(ti_absf(t_before) > 90.0f && g_app.iq_ref == 0.0f);
            CHECK(ti_absf(g_app.t_cmd_nm) <= g_app.p->cal_dcl_tmax_nm); /* not the request */
            CHECK(slow ? (g_app.mod_req && g_app.br.mode == BR_MOD && hal_gpio_out_state(HAL_DO_MCU_GATE_EN))
                       : (!g_app.mod_req && g_app.br.mode == BR_ASC && hal_pwm_mode() == HAL_PWM_ASC));
            /* afterwards */
            if (slow) {
                h_isr_only_us(500u); /* zero-torque current control, not all gates off */
                CHECK(hal_pwm_mode() == HAL_PWM_MOD && g_app.foc.iq_ref == 0.0f);
            }
            bool torque = false;
            for (uint32_t k = 0u; k < 12u; k++) {
                h_run_ms(1u);
                torque = torque || g_app.so.torque_enable || (g_app.iq_ref != 0.0f);
            }
            CHECK(!torque);
            hal_can_frame_t f;
            CHECK(last_status(&f) && (f.data[2] == (uint8_t)SM_FAULT) && ((f.data[1] & 0x80u) != 0u));
            CHECK((f.data[3] & 0x03u) == (slow ? 2u : 3u));                           /* modulating / PWM-ASC */
            CHECK(((f.data[1] & 0x20u) == 0u) && ((f.data[14] & 0x01u) == 0u)); /* keep HV 0, no safe state 0 */
            CHECK(slow || (sim_chain_ls_on() && !sim_chain_hs_on() && hal_gpio_read(HAL_DI_ASC_CMD_RB)));
            if (slow) {
                H.i_pk_a = 4.0f; /* the winding current has decayed */
                h_run_ms(5u);
                CHECK(hal_pwm_mode() == HAL_PWM_OFF && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN)); /* SPO */
                CHECK(h_run_until((l == LOSS_STALE) ? SM_VEHICLE_HANDSHAKE : SM_PRECHARGE_WAIT, 50u));
                CHECK(!fm_active(&g_app.fm, SS_ROW_BATTERY_LOST) && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
            }
            if (t_fails != before) {
                printf("    ^ speed %s, contactors %s\n", SP[sp], LS[l]);
            }
        }
    }
}

/* Round 15 guard: the FW-08 zero-torque opening at standstill stays a normal disarm. The row is
 * dispatched, §6 finds nothing to manage (SPO at once), no FAULT is ever entered or reported, and the
 * inverter waits in PRECHARGE_WAIT; closing again re-arms. */
TEST(zero_torque_opening_at_standstill_disarms_without_fault)
{
    CHECK(run_at(0.0f, 0.0f));
    H.enable = false;
    H.contactors = TI_CONT_OPEN;
    bool fault = false;
    for (uint32_t k = 0u; k < 60u; k++) {
        h_run_ms(1u);
        fault = fault || (g_app.sm.st == SM_FAULT);
    }
    hal_can_frame_t f;
    CHECK(!fault && g_app.sm.st == SM_PRECHARGE_WAIT && !fm_any(&g_app.fm));
    CHECK(hal_pwm_mode() == HAL_PWM_OFF && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    CHECK(last_status(&f) && ((f.data[1] & 0x80u) == 0u));
    H.contactors = TI_CONT_CLOSED;
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 200u));
}

void suite_scenarios(void)
{
    RUN(boot_to_run_follows_section_9);
    RUN(stale_can_takes_torque_to_zero_not_held);
    RUN(frozen_alive_counter_is_stale);
    RUN(bms_limit_zero_connected_is_not_asc_but_contactor_open_is);
    RUN(fw06_ov_to_asc_request_within_15p6us);
    RUN(flt_hs_at_speed_spo_then_reset_then_pwm_asc);
    RUN(flt_ls_at_speed_is_spo_only_even_on_overvoltage);
    RUN(desat_one_authorised_retry_then_latch);
    RUN(watchdog_missed_drops_drv_en);
    RUN(brownout_during_nvm_write_never_blocks);
    RUN(hwid_wrong_open_short_never_arm);
    RUN(v5gd_out_of_window_forces_spo_and_unknown_hv);
    RUN(vdc_disagreement_spo_and_unknown_hv);
    RUN(resolver_amplitude_low_asc_at_speed_spo_below);
    RUN(nan_reference_never_reaches_pwm);
    RUN(key_off_discharge_witnessed_then_lpoff);
    RUN(qdis_stuck_off_detected);
    RUN(mcu_reset_at_speed_keeps_asc_then_exits_with_battery);
    RUN(precharge_plateau_5pct_low_refuses);
    RUN(hvil_open_ramps_torque_within_100ms);
    RUN(open_hall_wire_invalidates_and_stops_modulation);
    RUN(overcurrent_crest_does_not_trip_620_does);
    RUN(asc_exit_only_below_n_x_by_mcu_command);
    RUN(swg_trim_is_written_to_the_generator);
    RUN(desat_at_speed_holds_en_through_the_hold_then_reset_and_pwm_asc);
    RUN(desat_at_low_speed_spo_waits_for_the_hold);
    RUN(running_across_the_microsecond_wrap_keeps_fresh_frames_fresh);
    RUN(boot_across_the_microsecond_wrap_reaches_armed);
    RUN(desat_retry_waits_1s_across_the_microsecond_wrap);
    RUN(dtc_time_stamps_across_the_microsecond_wrap_in_the_application);
    RUN(standstill_desat_at_rated_current_reports_keep_hv_on_can);
    RUN(arming_refused_without_evidence_and_the_status_names_it);
    RUN(unbound_fault_route_never_arms);
    RUN(arming_permitted_with_a_valid_record);
    RUN(arming_refused_on_record_identity_or_crc_mismatch);
    RUN(watchdog_reset_relocks_before_rearming);
    RUN(evidence_lost_while_armed_goes_through_section6);
    RUN(infeasible_current_gives_zero_torque_speed_limit_and_dtc);
    RUN(all_three_current_channels_stuck_detected_under_command);
    RUN(one_current_channel_stuck_below_the_kcl_tolerance_detected);
    RUN(stuck_on_qdis_latches_service_required_and_never_rearms);
    RUN(hw_id_is_converted_before_it_is_classified);
    RUN(low_speed_open_contactor_is_a_battery_path_loss);
    RUN(battery_path_loss_while_armed_at_every_speed);
    RUN(zero_torque_opening_at_standstill_disarms_without_fault);
}
