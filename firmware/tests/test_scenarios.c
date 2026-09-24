/* test_scenarios.c — the whole firmware (app.c) on the simulated card, power board, FS26, VCU
 * and link. Each scenario is a contract edge case end to end. */
#include "dtc.h"
#include "harness.h"
#include "nvlog.h"
#include "test.h"

#define LOW_RPM 1000.0f
#define HIGH_RPM 10000.0f /* screening motor n_x = 8086 rpm (8XX) */

static bool ever_armed;

static void run_watch(uint32_t ms)
{
    for (uint32_t k = 0u; k < ms; k++) {
        h_run_ms(1u);
        ever_armed = ever_armed || hal_gpio_out_state(HAL_DO_MCU_GATE_EN);
    }
}

static bool run_at(float rpm, float torque)
{
    sim_reset();
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

TEST(stale_can_ramps_to_zero_not_held)
{
    CHECK(run_at(0.0f, 200.0f));
    CHECK_NEAR(g_app.t_cmd_nm, 200.0, 1.0);
    H.send_cmd = false; /* the VCU goes silent */
    h_run_ms(21u);
    const float t_stale = g_app.t_cmd_nm;
    h_run_ms(30u);
    CHECK(g_app.t_cmd_nm < t_stale - 50.0f); /* decreasing, not held */
    CHECK(g_app.t_cmd_nm > t_stale - 70.0f); /* at the calibrated ramp rate */
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 200u));
    CHECK(g_app.t_cmd_nm == 0.0f && dtc_active(DTC_CAN_TIMEOUT));
}

TEST(frozen_alive_counter_is_stale)
{
    CHECK(run_at(0.0f, 150.0f));
    H.freeze_ctr = true; /* frames keep arriving, all with the same counter */
    h_run_ms(80u);
    CHECK(g_app.t_cmd_nm < 100.0f);
    CHECK(!can_cmd_fresh(&g_app.can, hal_time_us() / 1000u, g_app.p));
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

void suite_scenarios(void)
{
    RUN(boot_to_run_follows_section_9);
    RUN(stale_can_ramps_to_zero_not_held);
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
}
