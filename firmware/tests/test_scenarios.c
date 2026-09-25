/* test_scenarios.c — the whole firmware (app.c) on the simulated card, power board, FS26, VCU
 * and link. Each scenario is a contract edge case end to end. */
#include <string.h>

#include "dtc.h"
#include "harness.h"
#include "nvlog.h"
#include "swg.h"
#include "test.h"
#include "uds.h"

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
static void tick_1ms(void) { h_tick(); }

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

/* A14-N01: the SWG starts low (cal_swg_code_init — never the register maximum, whose untrimmed max corner
 * would slew-limit the amplifier) and the trim ramps it up one code at a time until the MONITOR plane sits
 * within +-5 % of cal_rslv_exc_target_vpp. Where it settles follows the generator's corner: typical 12,
 * low corner 14, high corner 11 (a code at the top is not needed even at the low corner: 1.84 of 1.884 V pp).
 * The amplifier never exceeds the -40 degC slew ceiling, the cold winding keeps the resolver floor, and no
 * corner saturates the trim. */
TEST(swg_trim_is_written_to_the_generator)
{
    const float corner[3] = {2.093f, 1.884f, 2.302f};
    const uint8_t settle[3] = {12u, 14u, 11u};
    for (unsigned k = 0u; k < 3u; k++) {
        sim_reset();
        dtc_init();
        h_setup(TI_SKU_8XX_SIC);
        sim_swg_maxapp(corner[k]);
        h_boot();
        CHECK(sim_swg_code() == h_p.cal_swg_code_init && h_p.cal_swg_code_init < HAL_SWG_CODE_MAX);
        uint8_t last = sim_swg_code();
        bool monotonic = true;
        for (uint32_t ms = 0u; ms < 300u; ms++) {
            h_run_ms(1u);
            monotonic = monotonic && (sim_swg_code() >= last) && ((sim_swg_code() - last) <= 1u);
            last = sim_swg_code();
        }
        float amp = 0.0f;
        float mon = 0.0f;
        float wind = 0.0f;
        sim_exc_planes(&amp, &mon, &wind);
        CHECK(monotonic && sim_swg_code() == settle[k]);
        CHECK(g_app.rslv.exc_ratio >= 0.95f && g_app.rslv.exc_ratio <= 1.05f && g_app.rslv.exc_ready);
        CHECK_NEAR(g_app.rslv.mon_vpp, mon, 0.02 * mon); /* the firmware's monitor plane is the card's */
        CHECK(sim_exc_amp_vpp_max() <= h_p.exc_slew_max_vpp && wind >= h_p.rslv_floor_vpp);
        CHECK(!dtc_active(DTC_RSLV_SWG_SAT) && g_app.rslv.valid);
    }
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
            CHECK(d->action == (slow ? SS_ACT_ZERO_CURRENT : SS_ACT_LS_ASC) && d->high_speed == !slow);
            CHECK(g_app.sm.st == SM_FAULT && !g_app.so.arm && !g_app.so.torque_enable);
            CHECK(ti_absf(t_before) > 90.0f && g_app.iq_ref == 0.0f);
            CHECK(g_app.t_cmd_nm == 0.0f && (!slow || g_app.id_ref == 0.0f)); /* round 17 (item 26): zero, not a trim */
            CHECK(slow ? (g_app.mod_req && g_app.br.mode == BR_MOD && hal_gpio_out_state(HAL_DO_MCU_GATE_EN))
                       : (!g_app.mod_req && g_app.br.mode == BR_ASC && hal_pwm_mode() == HAL_PWM_ASC));
            /* afterwards */
            if (slow) {
                h_isr_only_us(500u); /* zero-current control, not all gates off */
                CHECK(hal_pwm_mode() == HAL_PWM_MOD && g_app.foc.iq_ref == 0.0f && g_app.foc.id_ref == 0.0f);
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

/* ======================= round 16 ======================= */

static void sd_freeze_all(bool on)
{
    sim_sdadc_freeze(HAL_SD_EXC, on);
    sim_sdadc_freeze(HAL_SD_SIN, on);
    sim_sdadc_freeze(HAL_SD_COS, on);
}

/* Current-loop ticks until one consumes a resolver frame; returns that frame's start. */
static uint32_t isr_until_frame(void)
{
    const uint32_t per = app_isr_period_us(&g_app);
    const uint32_t t0 = g_app.rslv.t_frame_us;
    for (uint32_t k = 0u; (k < 10u) && (g_app.rslv.t_frame_us == t0); k++) {
        h_isr_only_us(per);
    }
    return g_app.rslv.t_frame_us;
}

/* Delivery stops right after a frame. Every current-loop tick is watched: valid and modulating while the
 * newest frame is younger than cal_rslv_hold_us, withdrawn at the first tick past it — never later than
 * one tick — with no ordinary FOC update after it. Returns the age at the withdrawal (0: never). */
static uint32_t stop_and_watch(void)
{
    const uint32_t per = app_isr_period_us(&g_app);
    const uint32_t hold = g_app.p->cal_rslv_hold_us;
    const uint32_t t_f = isr_until_frame();
    sd_freeze_all(true);
    bool inside_ok = true;
    uint32_t age_off = 0u;
    for (uint32_t k = 0u; (k < 40u) && (age_off == 0u); k++) {
        h_isr_only_us(per);
        const uint32_t age = ti_age(hal_time_us(), t_f);
        if (age < hold) {
            inside_ok = inside_ok && g_app.rslv.valid && !g_app.rslv.stale && (hal_pwm_mode() == HAL_PWM_MOD);
        } else {
            age_off = age;
            CHECK(!g_app.rslv.valid && g_app.rslv.stale && !g_app.mod_req && hal_pwm_mode() != HAL_PWM_MOD);
            CHECK(fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID));
        }
    }
    CHECK(inside_ok && age_off >= hold && age_off < hold + per);
    return age_off;
}

/* A14-R01: all resolver delivery stops after a good acquisition — at standstill holding 200 Nm, at low
 * speed under torque with the microsecond counter wrapping inside the hold, and at 10 000 rpm in field
 * weakening. The angle is withdrawn at the hold, the §6 "resolver invalid" row takes the bridge (SPO
 * below n_x, PWM-ASC above), the torque permission goes (FAULT), DTC_RSLV_STALE, and the last speed is
 * kept only for the §6 column (cal_speed_hold_ms). When frames return the resolver re-acquires (not at
 * the first frame), the row stays latched, and a VCU fault reset below n_x brings torque back. */
TEST(resolver_frames_stopping_withdraws_the_angle_at_the_hold)
{
    const float rpm[3] = {0.0f, LOW_RPM, HIGH_RPM};
    const float tq[3] = {200.0f, 100.0f, 0.0f};
    const uint64_t epoch[3] = {1000000u, WRAP_US - 3000000u, 1000000u};
    for (unsigned k = 0u; k < 3u; k++) {
        sim_reset();
        sim_nvm_wipe();
        (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
        (void)memset(&g_app_session, 0, sizeof g_app_session);
        CHECK(run_at_epoch(rpm[k], tq[k], epoch[k]));
        if (k == 1u) { /* the deadline straddles the 32-bit wrap */
            run_until_wrap_minus(3000u);
            h_isr_only_us(WRAP_US - 300u - hal_time_us64());
        }
        CHECK(g_app.rslv.valid && hal_pwm_mode() == HAL_PWM_MOD);
        (void)stop_and_watch();
        CHECK((k != 1u) || (hal_time_us64() > WRAP_US));
        tick_1ms();
        const bool slow = (k < 2u);
        CHECK(dtc_active(DTC_RSLV_STALE) && g_app.sm.st == SM_FAULT && !g_app.so.torque_enable);
        CHECK(g_app.speed_known && ti_absf(g_app.speed_rpm - rpm[k]) < 20.0f); /* held for the §6 column */
        CHECK(slow ? (g_app.fm.dec.action == SS_ACT_SPO && hal_pwm_mode() == HAL_PWM_OFF)
                   : (g_app.fm.dec.action == SS_ACT_LS_ASC && g_app.br.mode == BR_ASC && hal_pwm_mode() == HAL_PWM_ASC));
        h_run_ms(30u);
        CHECK(!g_app.rslv.valid && g_app.sm.st == SM_FAULT);
        /* delivery returns: a controlled re-acquisition */
        sd_freeze_all(false);
        bool early = false;
        for (uint32_t us = 0u; us < 1500u; us += 50u) {
            h_isr_only_us(50u);
            early = early || g_app.rslv.valid;
        }
        CHECK(!early);
        h_run_ms(10u);
        CHECK(g_app.rslv.valid && !g_app.rslv.stale && g_app.sm.st == SM_FAULT); /* the row stays latched */
        if (slow) {
            H.fault_reset = true;
            CHECK(h_run_until(SM_RUN, 1500u));
            H.fault_reset = false;
            h_run_ms(20u);
            CHECK(hal_pwm_mode() == HAL_PWM_MOD && ti_absf(g_app.t_cmd_nm - tq[k]) < 1.0f);
        }
    }
}

/* A14-R01: a current loop faster than the frames (SiC 20 kHz: every other tick has nothing new; the
 * IGBT's 10 kHz beats against the 10 kHz carrier) and a channel completing 30 us late are normal: two
 * seconds under torque, the resolver never goes stale. */
TEST(temporary_empty_reads_never_fault)
{
    const ti_sku_t sku[2] = {TI_SKU_8XX_SIC, TI_SKU_8XX_IGBT};
    for (unsigned k = 0u; k < 2u; k++) {
        sim_reset();
        dtc_init();
        h_setup(sku[k]);
        h_boot();
        CHECK(h_to_run(100.0f));
        h_ramp_speed(LOW_RPM, 400u);
        sim_sdadc_delay_ns(HAL_SD_COS, 30000u);
        bool ok = true;
        for (uint32_t ms = 0u; ms < 2000u; ms++) {
            h_run_ms(1u);
            ok = ok && g_app.rslv.valid && (g_app.sm.st == SM_RUN);
        }
        CHECK(ok && !dtc_active(DTC_RSLV_STALE) && !fm_any(&g_app.fm));
    }
}

/* A14-R02 at the application: one channel's DMA freezes while running (the reviewer's frozen COS buffer
 * that SIN's heartbeat kept reading as fresh). No frame is published while it is frozen: the resolver goes
 * stale at the hold, the §6 row takes the bridge. Round 18 (A16-R02): the channel resumes in step (four
 * periods: its slot came round), so the ring re-acquires by itself (DTC_RSLV_REACQUIRED, once) and the
 * resolver re-primes and validates — while the row stays latched until a VCU fault reset (it used to stay
 * dead for the key cycle). */
TEST(a_frozen_resolver_channel_is_never_read_as_fresh)
{
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        sim_reset();
        dtc_init();
        CHECK(run_at(LOW_RPM, 100.0f));
        const uint32_t per = app_isr_period_us(&g_app);
        const uint32_t t_f = isr_until_frame();
        sim_sdadc_freeze((hal_sd_ch_t)ch, true);
        uint32_t age_off = 0u;
        for (uint32_t i = 0u; (i < 40u) && (age_off == 0u); i++) {
            h_isr_only_us(per);
            age_off = g_app.rslv.valid ? 0u : ti_age(hal_time_us(), t_f);
        }
        CHECK(age_off >= g_app.p->cal_rslv_hold_us && age_off < g_app.p->cal_rslv_hold_us + per);
        CHECK(g_app.rslv.t_frame_us == t_f); /* nothing consumed after the freeze */
        sim_sdadc_freeze((hal_sd_ch_t)ch, false);
        h_run_ms(100u);
        CHECK(g_app.rslv.valid && dtc_active(DTC_RSLV_STALE) && !sim_sdadc_ring()->broken);
        CHECK(hal_sdadc_reacquired() == 1u && dtc_occurrences(DTC_RSLV_REACQUIRED) == 1u);
        CHECK(fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && hal_pwm_mode() != HAL_PWM_MOD && g_app.sm.st == SM_FAULT);
    }
}

/* A14-R03: every combination of missing phase channels (U, V, W, UV, UW, VW, all: the BCTU or the
 * converters stopped), at 1000 rpm under 100 Nm. In the tick it happens: the measurement is lost — invalid,
 * not fresh, not an "open wire", its stamp the last complete triplet's — no ordinary FOC update (the FOC
 * state and the PWM are not written with it), while V_DC and the resolver are still serviced (a link step
 * during the loss is seen). Then the current-sensor path: the §6 "control lost" row, FAULT, DTC_ISNS_STALE.
 * Repeated for 20 ms it stays so; once the triplets return the measurement recovers and a VCU fault reset
 * brings torque back. */
TEST(lost_phase_current_triplets_take_the_failure_path)
{
    for (uint8_t mask = 1u; mask <= 7u; mask++) {
        sim_reset();
        dtc_init();
        CHECK(run_at(LOW_RPM, 100.0f));
        const uint32_t per = app_isr_period_us(&g_app);
        CHECK(g_app.isns.valid && hal_pwm_mode() == HAL_PWM_MOD);
        const uint32_t t_good = g_app.isns.t_us;
        const foc_t foc0 = g_app.foc;
        const float d0[3] = {sim_pwm_duty(0u), sim_pwm_duty(1u), sim_pwm_duty(2u)};
        const uint32_t frame0 = g_app.rslv.t_frame_us;
        H.link_override = true;
        sim_set_link_v(780.0f, 780.0f); /* the pack steps while the currents are lost */
        sim_adc_phase_stop(mask);
        h_isr_only_us(per); /* one current-loop tick */
        CHECK(!g_app.isns.valid && !g_app.isns.fresh && g_app.isns.t_us == t_good);
        CHECK(!g_app.isns.open_wire[0] && !g_app.isns.open_wire[1] && !g_app.isns.open_wire[2]);
        CHECK(g_app.foc.id == foc0.id && g_app.foc.iq == foc0.iq && g_app.foc.vd == foc0.vd && g_app.foc.vq == foc0.vq);
        CHECK(sim_pwm_duty(0u) == d0[0] && sim_pwm_duty(1u) == d0[1] && sim_pwm_duty(2u) == d0[2]);
        CHECK(!g_app.mod_req && hal_pwm_mode() != HAL_PWM_MOD);
        CHECK(ti_absf(g_app.vdc.vdc - 780.0f) < 5.0f && g_app.vdc.valid); /* V_DC serviced */
        h_isr_only_us(4u * per);
        CHECK(g_app.rslv.t_frame_us != frame0 && g_app.rslv.valid); /* resolver serviced */
        tick_1ms();
        CHECK(fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && dtc_active(DTC_ISNS_STALE) && !dtc_active(DTC_ISNS_OPEN));
        CHECK(g_app.sm.st == SM_FAULT && !g_app.so.torque_enable);
        H.link_override = false;
        h_run_ms(20u); /* repeated losses */
        CHECK(!g_app.isns.valid && g_app.isns.t_us == t_good && hal_pwm_mode() != HAL_PWM_MOD);
        sim_adc_phase_stop(0u); /* the triplets return */
        h_run_ms(2u);
        CHECK(g_app.isns.valid && g_app.isns.fresh && g_app.isns.t_us != t_good && g_app.sm.st == SM_FAULT);
        H.fault_reset = true;
        CHECK(h_run_until(SM_RUN, 1500u));
        H.fault_reset = false;
        h_run_ms(20u);
        CHECK(hal_pwm_mode() == HAL_PWM_MOD && g_app.isns.valid);
        if (t_fails != 0u) {
            printf("    ^ missing channels mask %u\n", mask);
        }
    }
}

/* A14-R03 across the 32-bit microsecond wrap: triplets lost from 300 us before it to 700 us after it. The
 * stamp stays the last complete triplet's (before the wrap) — defined, never an uninitialised value — the
 * sample is never taken as fresh, and the first complete triplet after the wrap is. */
TEST(lost_triplets_across_the_microsecond_wrap_keep_a_defined_stamp)
{
    CHECK(run_at_epoch(LOW_RPM, 100.0f, WRAP_US - 3000000u));
    run_until_wrap_minus(3000u);
    h_isr_only_us(WRAP_US - 300u - hal_time_us64());
    const uint32_t t_good = g_app.isns.t_us;
    CHECK(g_app.isns.valid && t_good > 0xFFF00000u);
    sim_adc_phase_stop(0x2u);
    bool ok = true;
    for (uint32_t us = 0u; us < 1000u; us += 50u) {
        h_isr_only_us(50u);
        ok = ok && !g_app.isns.valid && !g_app.isns.fresh && (g_app.isns.t_us == t_good);
    }
    CHECK(ok && hal_time_us64() > WRAP_US);
    sim_adc_phase_stop(0u);
    h_isr_only_us(50u);
    CHECK(g_app.isns.fresh && g_app.isns.t_us < 1000u && ti_age(hal_time_us(), g_app.isns.t_us) < 60u);
}

/* A14-R03, the BCTU stopped: no trigger, so no current-loop interrupt at all — nothing in the ISR can
 * notice. The 1 ms task does: within one tick the currents are lost (and the resolver frame ages out), the
 * §6 row takes the bridge off the last duty cycle it was left on, DTC_ISNS_STALE. With the current unknown
 * §6 assumes the SKU's crest: rule (a) fails for the screening motor, so the row is LS-ASC (PWM-ASC). */
TEST(a_stopped_current_loop_is_caught_by_the_task)
{
    CHECK(run_at(LOW_RPM, 100.0f));
    CHECK(hal_pwm_mode() == HAL_PWM_MOD);
    sim_advance_us(1000u); /* the PWM keeps running on its last duty; no current-loop interrupt */
    app_task_1ms(&g_app);
    CHECK(!g_app.isns.valid && fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && dtc_active(DTC_ISNS_STALE));
    CHECK(g_app.fm.dec.action == SS_ACT_LS_ASC && hal_pwm_mode() == HAL_PWM_ASC && g_app.br.mode == BR_ASC);
    CHECK(!g_app.rslv.valid && g_app.rslv.stale); /* its newest frame is > 1 ms old */
    sim_advance_us(1000u);
    app_task_1ms(&g_app);
    CHECK(g_app.sm.st == SM_FAULT && !g_app.so.torque_enable && hal_pwm_mode() == HAL_PWM_ASC);
}

/* A14-N01: the trim-saturated DTC fires only where the required SWG amplitude exceeds the part's maximum —
 * here a 25 ohm resolver (its load drops more across RSX, upstream of the monitor) on a low-corner SWG:
 * the trim reaches the top code with the monitor still below its band, DTC_RSLV_SWG_SAT; the winding
 * (6.1 V pp) is below the floor, so the resolver never validates and nothing arms. The 70 ohm screening
 * resolver at the same corner does not saturate (swg_trim_is_written_to_the_generator). */
TEST(a_low_impedance_resolver_saturates_the_trim_with_a_dtc)
{
    h_setup(TI_SKU_8XX_SIC);
    sim_swg_maxapp(1.884f);
    sim_resolver_load(25.0f, 1.3f);
    h_boot();
    ever_armed = false;
    run_watch(1500u);
    CHECK(sim_swg_code() == HAL_SWG_CODE_MAX && dtc_active(DTC_RSLV_SWG_SAT) && g_app.rslv.swg_sat);
    CHECK(g_app.rslv.exc_ratio < 0.95f && g_app.rslv.wind_vpp < h_p.rslv_floor_vpp);
    CHECK(!g_app.rslv.valid && !ever_armed && g_app.sm.st == SM_FAULT);
    CHECK(sim_exc_amp_vpp_max() <= h_p.exc_slew_max_vpp);
}

/* A14-N01: a harness fault tripped an exciter PTC; for an hour it sits near 5 ohm per line. The monitor
 * (protected node, before the PTC) still reads the 7.2 V pp setpoint, the winding gets 6.3 V pp — below the
 * resolver's 6.5 V pp floor. FW-10 judges the winding: the excitation fault, the §6 row, the DTC. A
 * restart while the PTC is still warm never arms; one after it has cooled arms normally. */
TEST(ptc_post_trip_is_flagged_at_the_winding_and_a_cool_restart_recovers)
{
    CHECK(run_at(LOW_RPM, 50.0f));
    const float mon0 = g_app.rslv.mon_vpp;
    CHECK(g_app.rslv.valid && g_app.rslv.wind_vpp > h_p.rslv_floor_vpp);
    sim_resolver_load(70.0f, 5.0f);
    h_run_ms(5u);
    CHECK(ti_absf(g_app.rslv.mon_vpp - mon0) < 0.01f * mon0); /* the monitor does not see it */
    CHECK_NEAR(g_app.rslv.wind_vpp, g_app.rslv.mon_vpp * 70.0 / 80.0, 0.03); /* 6.3 V pp at the setpoint */
    CHECK(g_app.rslv.exc_fault && !g_app.rslv.amp_fault && !g_app.rslv.valid && dtc_active(DTC_RSLV_EXCITATION));
    CHECK(fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && hal_pwm_mode() != HAL_PWM_MOD);
    for (unsigned k = 0u; k < 2u; k++) { /* restart with the PTC still warm, then cooled */
        const bool warm = (k == 0u);
        sim_reset(); /* power off and on (the next key cycle) */
        (void)memset(&g_app_session, 0, sizeof g_app_session);
        dtc_init();
        h_setup(TI_SKU_8XX_SIC);
        sim_resolver_load(70.0f, warm ? 5.0f : 1.3f);
        h_boot();
        if (warm) {
            ever_armed = false;
            run_watch(2000u);
            CHECK(!ever_armed && !g_app.rslv.valid && g_app.rslv.exc_fault && g_app.sm.st == SM_FAULT);
        } else {
            CHECK(h_to_armed() && g_app.rslv.valid && !dtc_active(DTC_RSLV_EXCITATION));
        }
    }
}

/* ======================= round 17 ======================= */

/* Item 26: with the battery path proven (normal RUN) the FW-08 trim is a regen limiter — idle while the pack
 * holds the link inside the normal range, taking regen back (never adding motoring torque) while the link sits
 * above vdc_max_v, handing it back when the link returns; the status reports the torque applied. */
TEST(dc_link_trim_limits_regen_with_the_battery_present)
{
    CHECK(run_at(LOW_RPM, 50.0f));
    H.torque_nm = -150.0f; /* braking at 1000 rpm */
    h_run_ms(30u);
    CHECK(g_app.sm.st == SM_RUN && g_app.t_cmd_nm == -150.0f && g_app.dcl.integ == 0.0f); /* 750 V: idle */
    for (uint32_t k = 0u; k < 120u; k++) { /* a full pack pushed above the range by the charge current */
        H.v_pack += 1.0f;
        h_run_ms(1u);
    }
    h_run_ms(30u);
    CHECK(H.v_pack > g_app.p->vdc_max_v && g_app.vdc.vdc > g_app.p->vdc_max_v);
    CHECK(g_app.sm.st == SM_RUN && !fm_any(&g_app.fm)); /* the battery path is proven: nothing else acts */
    CHECK(g_app.dcl.integ > 0.0f && g_app.t_cmd_nm > -149.0f && g_app.t_cmd_nm <= 0.0f);
    hal_can_frame_t f;
    (void)last_status(&f);
    for (uint32_t k = 0u; (k < 20u) && !last_status(&f); k++) {
        h_run_ms(1u); /* the tick that sends the next status frame */
    }
    CHECK_NEAR((double)(int16_t)(uint16_t)(f.data[4] | (f.data[5] << 8)) * 0.1, g_app.t_cmd_nm, 0.1); /* 0.1 Nm, truncated */
    for (uint32_t k = 0u; k < 120u; k++) {
        H.v_pack -= 1.0f;
        h_run_ms(1u);
    }
    h_run_ms(300u);
    CHECK(g_app.dcl.integ == 0.0f && g_app.t_cmd_nm == -150.0f); /* regen handed back */
}

/* Item 26: below n_x a lost battery path is zero CURRENT — id = iq = 0 at the current-loop rate while the
 * winding holds 340 A rms, not a DC-link trim — the status reports the torque applied (0 Nm, zero-torque bit),
 * and the trim never engages. Two cases: 1000 rpm with the link above the normal range (the trim was taking
 * regen back in RUN the moment before), and 7000 rpm at 750 V, where zero torque alone would still ask for
 * field-weakening current (n_x = 8086 rpm). */
TEST(battery_path_loss_below_n_x_applies_and_reports_zero_current)
{
    const float rpm[2] = {LOW_RPM, 7000.0f};
    for (unsigned c = 0u; c < 2u; c++) {
        const unsigned before = t_fails;
        sim_reset();
        sim_nvm_wipe();
        CHECK(run_at(rpm[c], 50.0f));
        H.torque_nm = -150.0f;
        h_run_ms(30u);
        for (uint32_t k = 0u; (c == 0u) && (k < 120u); k++) {
            H.v_pack += 1.0f;
            h_run_ms(1u);
        }
        h_run_ms(20u);
        CHECK(g_app.sm.st == SM_RUN && (g_app.t_cmd_nm < -50.0f));
        CHECK((c == 0u) ? (g_app.dcl.integ > 0.0f) : (g_app.id_ref < -20.0f)); /* trim engaged / field weakening */
        H.contactors = TI_CONT_OPEN;
        vcu_frame_now();
        H.i_pk_a = 480.0f;
        tick_1ms();
        CHECK(fm_active(&g_app.fm, SS_ROW_BATTERY_LOST) && g_app.fm.dec.action == SS_ACT_ZERO_CURRENT);
        CHECK(g_app.t_cmd_nm == 0.0f && g_app.id_ref == 0.0f && g_app.iq_ref == 0.0f && g_app.dcl.integ == 0.0f);
        bool engaged = false;
        bool current = false;
        for (uint32_t k = 0u; k < 25u; k++) {
            h_run_ms(1u);
            engaged = engaged || (g_app.dcl.integ != 0.0f) || (g_app.t_cmd_nm != 0.0f);
            current = current || (g_app.foc.id_ref != 0.0f) || (g_app.foc.iq_ref != 0.0f) || (g_app.id_ref != 0.0f);
        }
        CHECK(!engaged && !current);
        CHECK(hal_pwm_mode() == HAL_PWM_MOD && fm_active(&g_app.fm, SS_ROW_BATTERY_LOST)); /* current control, not SPO */
        hal_can_frame_t f;
        CHECK(last_status(&f) && (f.data[4] == 0u) && (f.data[5] == 0u) && ((f.data[3] & 0x10u) != 0u));
        if (t_fails != before) {
            printf("    ^ %.0f rpm\n", (double)rpm[c]);
        }
    }
}

/* ---- FW-32: the service-lock routine on the diagnostic bus ---- */
static bool test_key(const uint8_t seed[UDS_SA_LEN], uint8_t key[UDS_SA_LEN])
{
    for (uint32_t i = 0u; i < UDS_SA_LEN; i++) {
        key[i] = (uint8_t)(seed[(i + 1u) % UDS_SA_LEN] ^ (0xA5u + i));
    }
    return true;
}

static const uint8_t CLEAR_RQ[4] = {0x31u, 0x01u, (uint8_t)(UDS_RID_CLEAR_SERVICE_LOCK >> 8),
                                    (uint8_t)(UDS_RID_CLEAR_SERVICE_LOCK & 0xFFu)};

/* One single-frame request on the diagnostic bus; the response the next task tick sends. */
static bool uds_req(const uint8_t *req, uint8_t n, uint8_t rsp[8])
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u};
    (void)memset(f.data, 0xAA, 8u);
    f.data[0] = n;
    (void)memcpy(&f.data[1], req, n);
    hal_can_frame_t r;
    while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
    }
    sim_can_inject(HAL_CAN_DIAG, &f);
    h_run_ms(1u);
    if (!sim_can_pop_tx(HAL_CAN_DIAG, &r) || (r.id != UDS_ID_RSP) || (r.len != 8u)) {
        return false;
    }
    (void)memcpy(rsp, r.data, 8u);
    return true;
}

static bool uds_unlock(void)
{
    uint8_t r[8];
    const uint8_t sq[2] = {0x27u, 0x01u};
    if (!uds_req(sq, 2u, r) || (r[0] != 6u) || (r[1] != 0x67u) || (r[2] != 0x01u)) {
        return false;
    }
    uint8_t kq[2u + UDS_SA_LEN] = {0x27u, 0x02u};
    (void)test_key(&r[3], &kq[2]);
    return uds_req(kq, (uint8_t)sizeof kq, r) && (r[1] == 0x67u) && (r[2] == 0x02u);
}

/* A stuck-on QDIS latched in an earlier key cycle (the NVM record service_lock() writes). */
static void store_service_lock(void)
{
    const nv_service_t r = {.magic = NV_SERVICE_MAGIC, .dtc = (uint16_t)DTC_QDIS_STUCK_ON, .key_cycle = 1u};
    nv_init();
    (void)nv_queue(NV_REC_DTC, &r, (uint16_t)sizeof r);
    for (int k = 0; (k < 200) && !nv_idle(); k++) {
        nv_service();
    }
}

static uint32_t service_magic(uint32_t *key_cycle)
{
    nv_service_t r = {0};
    (void)nv_read(NV_REC_DTC, &r, (uint16_t)sizeof r);
    *key_cycle = r.key_cycle;
    return r.magic;
}

static void power_cycle_and_boot(void)
{
    sim_reset(); /* the next key cycle: retained RAM lost, the NVM kept */
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
}

/* FW-32, default build: no key function, so nothing unlocks. The seed request is refused (NRC 0x22), a key
 * without a seed is a sequence error, the routine is denied (0x33); the lock survives the next power-up. */
TEST(service_lock_clear_is_refused_without_a_key)
{
    h_setup(TI_SKU_8XX_SIC);
    store_service_lock();
    h_boot();
    CHECK(g_app.service_required && g_app.init == SM_FAIL && g_app.uds.key_fn == NULL);
    h_run_ms(300u);
    CHECK(dis_hv_state(&g_app.vdc) == TI_HV_SAFE && g_app.br.mode == BR_DISARMED); /* only the key is missing */
    uint8_t r[8];
    const uint8_t sq[2] = {0x27u, 0x01u};
    CHECK(uds_req(sq, 2u, r) && r[0] == 3u && r[1] == 0x7Fu && r[2] == 0x27u && r[3] == UDS_NRC_CONDITIONS);
    const uint8_t kq[2u + UDS_SA_LEN] = {0x27u, 0x02u, 1u, 2u, 3u, 4u};
    CHECK(uds_req(kq, (uint8_t)sizeof kq, r) && r[1] == 0x7Fu && r[3] == UDS_NRC_SEQUENCE);
    CHECK(uds_req(CLEAR_RQ, 4u, r) && r[1] == 0x7Fu && r[2] == 0x31u && r[3] == UDS_NRC_SECURITY_DENIED);
    h_run_ms(50u);
    uint32_t kc = 0u;
    CHECK(service_magic(&kc) == NV_SERVICE_MAGIC && !dtc_active(DTC_SERVICE_LOCK_CLEARED));
    power_cycle_and_boot();
    CHECK(g_app.service_required && g_app.init == SM_FAIL);
}

/* FW-32: unlocked with the key, the routine is still refused (NRC 0x22, nothing written) while the link holds
 * HV — the VCU closed the contactors again — and while the bridge is armed, even at a moment the link reads
 * below 60 V; the lock stays. */
TEST(service_lock_clear_is_refused_with_hv_present_or_armed)
{
    h_setup(TI_SKU_8XX_SIC);
    store_service_lock();
    h_boot();
    g_app.uds.key_fn = test_key;
    H.contactors = TI_CONT_PRECHARGE;
    h_run_ms(800u);
    H.contactors = TI_CONT_CLOSED;
    h_run_ms(100u);
    CHECK(dis_hv_state(&g_app.vdc) == TI_HV_PRESENT && g_app.br.mode == BR_DISARMED && g_app.sm.st == SM_FAULT);
    CHECK(uds_unlock());
    uint8_t r[8];
    CHECK(uds_req(CLEAR_RQ, 4u, r) && r[1] == 0x7Fu && r[2] == 0x31u && r[3] == UDS_NRC_CONDITIONS);
    h_run_ms(50u);
    uint32_t kc = 0u;
    CHECK(service_magic(&kc) == NV_SERVICE_MAGIC && !dtc_active(DTC_SERVICE_LOCK_CLEARED) && g_app.service_required);
    sim_nvm_wipe(); /* no lock: an armed inverter */
    CHECK(run_at(0.0f, 0.0f) && g_app.br.mode != BR_DISARMED);
    g_app.uds.key_fn = test_key;
    CHECK(uds_unlock());
    CHECK(uds_req(CLEAR_RQ, 4u, r) && r[1] == 0x7Fu && r[3] == UDS_NRC_CONDITIONS);
    H.link_override = true; /* the link reads 20 V for a moment: HV "safe", the bridge still armed */
    sim_set_link_v(20.0f, 20.0f);
    h_run_ms(3u);
    CHECK(dis_hv_state(&g_app.vdc) == TI_HV_SAFE && g_app.br.mode != BR_DISARMED);
    CHECK(uds_req(CLEAR_RQ, 4u, r) && r[1] == 0x7Fu && r[3] == UDS_NRC_CONDITIONS);
}

/* FW-32: with the key and the link discharged the routine clears the lock: positive response, the NVM record
 * rewritten as CLEARED with this key cycle, DTC_SERVICE_LOCK_CLEARED; the unlock is consumed. This key cycle
 * keeps its lock (status b14.1/2, FAULT); the next power-up has none and arms. */
TEST(service_lock_clear_with_the_key_takes_effect_at_the_next_power_up)
{
    h_setup(TI_SKU_8XX_SIC);
    store_service_lock();
    h_boot();
    g_app.uds.key_fn = test_key;
    h_run_ms(300u);
    CHECK(dis_hv_state(&g_app.vdc) == TI_HV_SAFE && g_app.sm.st == SM_FAULT && g_app.service_required);
    CHECK(uds_unlock());
    uint8_t r[8];
    CHECK(uds_req(CLEAR_RQ, 4u, r) && r[0] == 4u && r[1] == 0x71u && r[2] == 0x01u &&
          r[3] == (uint8_t)(UDS_RID_CLEAR_SERVICE_LOCK >> 8) && r[4] == (uint8_t)(UDS_RID_CLEAR_SERVICE_LOCK & 0xFFu));
    CHECK(dtc_active(DTC_SERVICE_LOCK_CLEARED));
    CHECK(uds_req(CLEAR_RQ, 4u, r) && r[1] == 0x7Fu && r[3] == UDS_NRC_SECURITY_DENIED); /* one run per unlock */
    h_run_ms(50u);
    uint32_t kc = 0u;
    CHECK(service_magic(&kc) == NV_SERVICE_CLEARED && kc == g_app.key_cycle);
    hal_can_frame_t f;
    CHECK(last_status(&f) && ((f.data[14] & 0x06u) == 0x06u) && g_app.sm.st == SM_FAULT);
    power_cycle_and_boot();
    CHECK(!g_app.service_required && g_app.init == SM_OK);
    CHECK(h_to_armed());
}

/* T-32 (round 17): the FS26 challenger window restarts at every answer and lasts 3 ms, the first half closed
 * (FS_WDW_DURATION, DS Rev.3 Tables 144/145), timed by the fail-safe oscillator, 20 MHz ±5 % (Table 143): closed
 * until 1.43–1.58 ms, open until 2.86–3.16 ms. On the target the 1 ms task runs on the STM's exact grid, which
 * the harness now keeps (SPI time no longer shifts later ticks). In RUN, at the oscillator's slow, nominal and
 * fast corners, the answers come every second task (2.0 ms) — never on the third (3.0 ms: the window's end,
 * late whenever the FS26 runs fast) — with no watchdog error and FS0B never asserted. */
TEST(fs26_is_answered_every_2ms_inside_its_window_at_both_oscillator_corners)
{
    const float osc[3] = {-0.05f, 0.0f, 0.05f};
    for (unsigned c = 0u; c < 3u; c++) {
        const unsigned before = t_fails;
        sim_reset();
        sim_nvm_wipe();
        (void)memset(&g_app_session, 0, sizeof g_app_session);
        dtc_init();
        h_setup(TI_SKU_8XX_SIC);
        const sim_fs26_cfg_t fc = {.prog_id = 0x4A21u, .device_id = 0x2600u, .osc_error = osc[c]};
        sim_fs26_config(&fc);
        h_boot();
        CHECK(h_to_run(100.0f));
        uint32_t n0 = g_app.fs.n_refresh;
        uint32_t last = g_app.fs.last_refresh_us;
        uint32_t lo = UINT32_MAX;
        uint32_t hi = 0u;
        for (uint32_t k = 0u; k < 400u; k++) {
            h_run_ms(1u);
            if (g_app.fs.n_refresh != n0) {
                const uint32_t d = g_app.fs.last_refresh_us - last;
                lo = (d < lo) ? d : lo;
                hi = (d > hi) ? d : hi;
                last = g_app.fs.last_refresh_us;
                n0 = g_app.fs.n_refresh;
            }
        }
        CHECK(lo >= 1900u && hi <= 2100u); /* every second task; the host's answer offset is constant */
        CHECK(sim_fs26_wd_err_cnt() == 0u && !sim_fs26_fs0b_asserted() && !dtc_active(DTC_FS26_WD));
        CHECK(g_app.sm.st == SM_RUN && hal_gpio_read(HAL_DI_DRV_EN_RB));
        if (t_fails != before) {
            printf("    ^ fail-safe oscillator %+.0f %%: answers every %u..%u us\n", (double)osc[c] * 100.0, lo, hi);
        }
    }
}

/* FW-06a step 3 (round 17): leaving ASC into modulation — here after an MCU reset at 10 000 rpm, the §9 step-5 ASC
 * handed over to field weakening once the battery and current control are proven. The exit runs in the 1 ms task
 * and the current-loop ISR (higher priority) can preempt it right behind the clear: the test fires one at once.
 * The first high-side pulse still never precedes the release deadline: the ASC pins release <= 1.07 us after the
 * clear's falling edge (VOW3120 tpHL 0.5 + DASCR 0.08 + NSI6611 tASC_f 0.48 us + logic, design-verify Safety A.8),
 * then the low sides turn off within the dead time. SiC (1.0 us dead time) and IGBT (2.5 us). */
TEST(asc_exit_first_high_side_pulse_after_the_release_deadline)
{
    const ti_sku_t sku[2] = {TI_SKU_8XX_SIC, TI_SKU_8XX_IGBT};
    for (unsigned k = 0u; k < 2u; k++) {
        const unsigned before = t_fails;
        sim_reset();
        sim_nvm_wipe();
        (void)memset(&g_app_session, 0, sizeof g_app_session);
        (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
        dtc_init();
        h_setup(sku[k]);
        h_boot();
        CHECK(h_to_armed());
        h_ramp_speed(HIGH_RPM, 600u);
        h_run_ms(10u);
        sim_fs26_mcu_reset(); /* FS1B-ASC with EN low while the MCU restarts */
        h_boot();
        const uint64_t t0 = sim_now_ns();
        bool held = false;
        for (uint32_t n = 0u; (n < 3000u) && !(held && !g_app.asc_hold); n++) {
            /* §9: step 5 keeps ASC; the arming exits it once the battery and current control are proven */
            h_run_ms(1u);
            held = held || g_app.asc_hold;
        }
        const uint64_t t_clr = sim_gpio_edge_ns(HAL_DO_ASC_CLR_N, false, t0);
        h_isr_now(); /* the current-loop trigger right behind the exit */
        CHECK(held && !g_app.asc_hold && hal_pwm_mode() == HAL_PWM_MOD && t_clr != UINT64_MAX);
        const uint64_t gap = sim_pwm_mod_ns() - t_clr;
        CHECK(sim_pwm_mod_ns() > t_clr && gap >= (1070u + g_app.p->dead_time_ns));
        if (t_fails != before) {
            printf("    ^ %s: first high-side pulse %llu ns after the clear (deadline %u ns)\n", g_app.p->name,
                   (unsigned long long)gap, 1070u + g_app.p->dead_time_ns);
        }
    }
}

/* LV supervision (round 17): the let-through design passes an ISO 16750-2 test-B pulse — KL30 at 35 V for 400 ms —
 * to parts rated for it, and the FS26 sees VSUPOV. It is information: RUN, the torque unchanged, no §6 row, a DTC
 * whose first/last stamps give the event's duration; nothing changes when the pulse ends. */
TEST(lv_load_dump_35v_for_400ms_is_information_not_a_fault)
{
    CHECK(run_at(LOW_RPM, 100.0f));
    sim_fs26_vsup(35.0f);
    bool steady = true;
    for (uint32_t k = 0u; k < 400u; k++) {
        h_run_ms(1u);
        steady = steady && (g_app.sm.st == SM_RUN) && (g_app.t_cmd_nm == 100.0f) && !fm_any(&g_app.fm) &&
                 !g_app.vsup.sustained;
    }
    CHECK(steady && g_app.vsup.ov && g_app.vsup.hi && dtc_active(DTC_LV_OVERVOLTAGE) &&
          !dtc_active(DTC_LV_OV_SUSTAINED));
    sim_fs26_vsup(13.5f);
    h_run_ms(50u);
    uint32_t first = 0u;
    uint32_t last = 0u;
    CHECK(dtc_times(DTC_LV_OVERVOLTAGE, &first, &last) && ti_age(last, first) >= 395u && ti_age(last, first) <= 400u);
    CHECK(!g_app.vsup.ov && g_app.sm.st == SM_RUN && g_app.t_cmd_nm == 100.0f && hal_pwm_mode() == HAL_PWM_MOD);
}

/* LV supervision: 35 V held past cal_vsup_ld_ms (500 ms) is sustained — DTC_LV_OV_SUSTAINED and the orderly ramp
 * the firmware already takes for HVIL open (the §6 command-lost row: torque ramped to zero, then SPO below n_x; no
 * ASC, no FAULT state); VSUP back in range ends it and the requested torque returns. */
TEST(lv_overvoltage_beyond_its_band_takes_the_orderly_ramp)
{
    CHECK(run_at(LOW_RPM, 100.0f));
    sim_fs26_vsup(35.0f);
    h_run_ms(g_app.p->cal_vsup_ld_ms);
    CHECK(!g_app.vsup.sustained && !fm_active(&g_app.fm, SS_ROW_CMD_LOST) && g_app.t_cmd_nm == 100.0f);
    h_run_ms(2u);
    CHECK(g_app.vsup.sustained && fm_active(&g_app.fm, SS_ROW_CMD_LOST) && dtc_active(DTC_LV_OV_SUSTAINED));
    h_run_ms(100u); /* below n_x the row's cell: the ramp, then SPO — pulses off, the bridge still armed */
    CHECK(g_app.t_cmd_nm == 0.0f && hal_pwm_mode() == HAL_PWM_OFF && g_app.br.mode == BR_IDLE && g_app.sm.st == SM_RUN);
    sim_fs26_vsup(13.5f);
    h_run_ms(20u);
    CHECK(!g_app.vsup.ov && !g_app.vsup.sustained && !fm_active(&g_app.fm, SS_ROW_CMD_LOST) &&
          g_app.t_cmd_nm == 100.0f && hal_pwm_mode() == HAL_PWM_MOD);
}

/* LV supervision: a 24 V jump start (IR-02, ISO 16750-2: 60 s) — and the 2023 edition's 26 V at the AMUX's highest
 * reading (26.5 V) — stays in the jump-start band (at or below cal_vsup_jump_max_v, 27 V): information for its whole
 * minute — RUN, torque unchanged, no row — and sustained only once it outlasts cal_vsup_jump_ms (65 s). */
TEST(lv_24v_jump_start_is_information_for_its_60s)
{
    const float level_v[2] = {24.0f, 26.5f};
    for (uint32_t j = 0u; j < 2u; j++) {
        CHECK(run_at(LOW_RPM, 100.0f));
        sim_fs26_vsup(level_v[j]);
        bool steady = true;
        for (uint32_t k = 0u; k < 60000u; k++) {
            h_run_ms(1u);
            steady = steady && (g_app.sm.st == SM_RUN) && (g_app.t_cmd_nm == 100.0f) && !fm_any(&g_app.fm);
        }
        CHECK(steady && g_app.vsup.ov && !g_app.vsup.hi && !g_app.vsup.sustained && dtc_active(DTC_LV_OVERVOLTAGE));
        h_run_ms(g_app.p->cal_vsup_jump_ms - 60000u + 2u);
        CHECK(g_app.vsup.sustained && fm_active(&g_app.fm, SS_ROW_CMD_LOST) && dtc_active(DTC_LV_OV_SUSTAINED));
    }
}

/* ======================= round 18 ======================= */

/* Every measurement the current-loop tick judges is fresh and valid, and the bridge modulates. */
static bool tick_fresh(void)
{
    return g_app.isns.fresh && g_app.isns.valid && g_app.vdc.valid && !g_app.vdc.ch_stale[0] && !g_app.vdc.ch_stale[1] &&
           g_app.rslv.valid && !g_app.rslv.stale && !fm_any(&g_app.fm) && (hal_pwm_mode() == HAL_PWM_MOD);
}

/* A16-R01, the reviewer's case: the target stamps every ADC sample when it reads it (s32k396_adc.c), after the
 * current-loop ISR read its entry time, and the freshness checks compared the stamps with that entry time: a
 * sample one tick newer read 2^32 us old — currents and V_DC stale, the control lost. The host clock stood
 * still during the ISR and hid it. Now every read takes 1, 5 or 50 us (sim_adc_read_delay_ns; the SDADC
 * interrupts run meanwhile, so a resolver frame can complete inside the tick too), at 6000 rpm under 100 Nm:
 * every tick keeps the currents, V_DC and the resolver fresh and valid and the bridge modulating — also with
 * one tick's reads straddling the 32-bit microsecond wrap (entry 3 us before it). */
TEST(samples_stamped_after_the_isr_entry_stay_fresh)
{
    const uint32_t delay_ns[3] = {1000u, 5000u, 50000u};
    for (unsigned w = 0u; w < 2u; w++) {
        for (unsigned d = 0u; d < 3u; d++) {
            sim_reset();
            sim_nvm_wipe();
            (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
            (void)memset(&g_app_session, 0, sizeof g_app_session);
            const unsigned fails0 = t_fails;
            CHECK(run_at_epoch(6000.0f, 100.0f, (w == 0u) ? 1000000u : (WRAP_US - 3000000u)));
            if (w == 1u) {
                run_until_wrap_minus(3000u);
                h_isr_only_us((uint32_t)(WRAP_US - 3u - hal_time_us64()));
            }
            const uint32_t per = app_isr_period_us(&g_app);
            CHECK(tick_fresh());
            sim_adc_read_delay_ns(delay_ns[d]);
            bool ok = true;
            for (unsigned k = 0u; k < 5u; k++) { /* isolated ticks: 5 reads x 50 us outlast the loop period */
                h_isr_now();
                ok = ok && tick_fresh() && (ti_age(hal_time_us(), g_app.isns.t_us) <= (4u * delay_ns[d] / 1000u) + 1u);
                sim_advance_us(per);
            }
            sim_adc_read_delay_ns(0u);
            CHECK(ok && ((w == 0u) || (hal_time_us64() > WRAP_US)));
            h_isr_now(); /* a tick just before the task: 5 x 50 us reads outlast the FW-31 liveness limit itself */
            h_run_ms(20u);
            CHECK(g_app.sm.st == SM_RUN && !dtc_active(DTC_ISNS_STALE) && !dtc_active(DTC_VDC_STALE) &&
                  !dtc_active(DTC_RSLV_STALE));
            if (t_fails != fails0) {
                printf("    ^ read delay %u ns, %s\n", delay_ns[d], (w == 0u) ? "no wrap" : "across the wrap");
            }
        }
    }
}

/* A16-R01 end to end: a whole run at 6000 rpm under 100 Nm with every ADC read taking 1 us, then 5 us (the
 * current-loop tick's reads 25 us of its 50 us period, the task's slow list 55 us): half a second each in RUN,
 * no §6 row, no stale DTC, the torque held. */
TEST(a_run_at_speed_with_the_adc_reads_taking_time)
{
    CHECK(run_at(6000.0f, 100.0f));
    const uint32_t delay_ns[2] = {1000u, 5000u};
    for (unsigned d = 0u; d < 2u; d++) {
        sim_adc_read_delay_ns(delay_ns[d]);
        bool ok = true;
        for (uint32_t ms = 0u; ms < 500u; ms++) {
            h_run_ms(1u);
            ok = ok && tick_fresh() && (g_app.sm.st == SM_RUN) && (ti_absf(g_app.t_cmd_nm - 100.0f) < 1.0f);
        }
        CHECK(ok && !dtc_active(DTC_ISNS_STALE) && !dtc_active(DTC_VDC_STALE) && !dtc_active(DTC_RSLV_STALE));
    }
    sim_adc_read_delay_ns(0u);
}

static unsigned s_preempts;
static void preempt_the_task(void)
{
    s_preempts++;
    h_isr_now(); /* the current-loop trigger arrives while the task waits on its FS26 transfer */
}

/* A16-R01 in the task (the same shape, found at another caller): the 1 ms task reads its time first, then
 * answers the FS26 — and on the target the current-loop ISR (priority 2) preempts it there. Its entry time,
 * newer than the task's, read 2^32 us old in the liveness check (FW-31): the currents were declared lost and
 * the resolver aged — the control-lost row at the first preemption. Now the age is signed: here the ISR runs
 * inside every FS26 transfer for 300 ms, RUN at 1000 rpm under 100 Nm stays undisturbed. */
TEST(a_current_loop_preempting_the_task_is_not_a_dead_loop)
{
    CHECK(run_at(LOW_RPM, 100.0f));
    s_preempts = 0u;
    sim_fs26_xfer_hook(preempt_the_task);
    bool ok = true;
    for (uint32_t ms = 0u; ms < 300u; ms++) {
        h_run_ms(1u);
        ok = ok && tick_fresh() && (g_app.sm.st == SM_RUN);
    }
    sim_fs26_xfer_hook(NULL);
    CHECK(ok && s_preempts >= 300u && !dtc_active(DTC_ISNS_STALE) && !dtc_active(DTC_RSLV_STALE));
    sim_advance_us(1000u); /* and a loop that really stopped is still caught (FW-31) */
    app_task_1ms(&g_app);
    CHECK(!g_app.isns.valid && fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && dtc_active(DTC_ISNS_STALE));
}

/* A16-R02 at the application: at 6000 rpm under 100 Nm the SDADC completion interrupts are held off once by
 * 60 us (past the 30 us deadline). That block is not published, the ring re-acquires within three carrier
 * periods, the resolver bridges the gap (shorter than cal_rslv_hold_us, also with the IGBT's 10 kHz loop) and
 * RUN goes on: no §6 row, no DTC_RSLV_STALE — the event is one occurrence of the information DTC
 * DTC_RSLV_REACQUIRED; a second, 100 ms later, is the second occurrence. (Before, the frame was stamped 60 us
 * too new: the observer's acceleration check latched a resolver fault — FAULT under torque.) */
TEST(a_late_resolver_interrupt_at_speed_is_counted_and_reacquired)
{
    const ti_sku_t sku[2] = {TI_SKU_8XX_SIC, TI_SKU_8XX_IGBT};
    for (unsigned s = 0u; s < 2u; s++) {
        sim_reset();
        dtc_init();
        h_setup(sku[s]);
        h_boot();
        CHECK(h_to_run(100.0f));
        h_ramp_speed(6000.0f, 600u);
        h_run_ms(20u);
        for (unsigned n = 1u; n <= 2u; n++) {
            sim_sdadc_irq_latency_ns(60000u);
            h_isr_only_us(150u); /* one carrier boundary: its interrupts run 60 us late */
            sim_sdadc_irq_latency_ns(1000u);
            bool ok = true;
            for (uint32_t ms = 0u; ms < 100u; ms++) {
                h_run_ms(1u);
                ok = ok && tick_fresh() && (g_app.sm.st == SM_RUN);
            }
            CHECK(ok && hal_sdadc_reacquired() == n && dtc_occurrences(DTC_RSLV_REACQUIRED) == n);
            CHECK(!dtc_active(DTC_RSLV_STALE) && !fm_any(&g_app.fm));
        }
    }
}

static bool s_fault_armed;
static uint64_t s_fault_ns;

/* The driver latches FLT while the 1 ms task waits on its FS26 transfer — after the task read its time. The fault
 * ISR runs (0.3 us later) and the task goes on past the 60 us DESAT hold before it reaches recovery(). */
static void fault_inside_the_task(void)
{
    if (s_fault_armed) {
        s_fault_armed = false;
        s_fault_ns = sim_now_ns();
        sim_chain_desat(true, false);
        sim_advance_us(80u);
    }
}

/* Round 18, the same class at the FW-15 recovery, end to end: at 10 000 rpm FLT_HS arrives while the 1 ms task
 * waits on its FS26 transfer — after it read its time — and the task goes on past the DESAT hold before it reaches
 * the recovery (EN low, nothing pending). br_rec_step used to time the >= 1.5 ms low with that older time against
 * the newer fault stamp: the age wrapped, EN came back high and FLT_CLR pulsed ~0.1 ms after the fault. Now the
 * release and the pulse come >= fw15_low_us after the fault, the driver resets and PWM-ASC follows. (The wrap:
 * bridge: fw15_low_wait_runs_on_the_bridges_own_clock.) */
TEST(fw15_low_wait_counts_from_a_fault_that_preempted_the_task)
{
    CHECK(run_at(HIGH_RPM, 0.0f));
    const uint32_t t_task = g_app.fs.last_refresh_us + 1900u; /* the next FS26 answer, on its usual cadence */
    h_isr_only_us(ti_age(t_task, hal_time_us()));
    CHECK(fs26_wd_due(&g_app.fs, hal_time_us()) && (hal_time_us() == t_task));
    s_fault_armed = true;
    sim_fs26_xfer_hook(fault_inside_the_task);
    app_task_1ms(&g_app); /* the task starts at t_task; the fault comes inside its first FS26 transfer */
    sim_fs26_xfer_hook(NULL);
    CHECK(!s_fault_armed && ti_age(g_app.t_fault_us, t_task) >= 10u && ti_age(g_app.t_fault_us, t_task) < 20u);
    CHECK(!br_en_drop_pending(&g_app.br) && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN)); /* the hold is over: EN low */
    h_run_ms(10u);
    const uint64_t low_ns = (uint64_t)g_app.p->fw15_low_us * 1000u;
    const uint64_t t_clr = sim_gpio_edge_ns(HAL_DO_FLT_CLR, false, s_fault_ns);
    const uint64_t t_en = sim_gpio_edge_ns(HAL_DO_MCU_GATE_EN, true, s_fault_ns);
    CHECK(t_clr != UINT64_MAX && (t_clr - s_fault_ns) >= low_ns);
    CHECK(t_en != UINT64_MAX && (t_en - s_fault_ns) >= low_ns);
    CHECK(!dtc_active(DTC_FLT_RECOVERY_FAIL) && g_app.br.mode == BR_ASC && hal_pwm_mode() == HAL_PWM_ASC);
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
    RUN(resolver_frames_stopping_withdraws_the_angle_at_the_hold);
    RUN(temporary_empty_reads_never_fault);
    RUN(a_frozen_resolver_channel_is_never_read_as_fresh);
    RUN(lost_phase_current_triplets_take_the_failure_path);
    RUN(lost_triplets_across_the_microsecond_wrap_keep_a_defined_stamp);
    RUN(a_stopped_current_loop_is_caught_by_the_task);
    RUN(a_low_impedance_resolver_saturates_the_trim_with_a_dtc);
    RUN(ptc_post_trip_is_flagged_at_the_winding_and_a_cool_restart_recovers);
    RUN(dc_link_trim_limits_regen_with_the_battery_present);
    RUN(battery_path_loss_below_n_x_applies_and_reports_zero_current);
    RUN(service_lock_clear_is_refused_without_a_key);
    RUN(service_lock_clear_is_refused_with_hv_present_or_armed);
    RUN(service_lock_clear_with_the_key_takes_effect_at_the_next_power_up);
    RUN(fs26_is_answered_every_2ms_inside_its_window_at_both_oscillator_corners);
    RUN(asc_exit_first_high_side_pulse_after_the_release_deadline);
    RUN(lv_load_dump_35v_for_400ms_is_information_not_a_fault);
    RUN(lv_overvoltage_beyond_its_band_takes_the_orderly_ramp);
    RUN(lv_24v_jump_start_is_information_for_its_60s);
    RUN(samples_stamped_after_the_isr_entry_stay_fresh);
    RUN(a_run_at_speed_with_the_adc_reads_taking_time);
    RUN(a_current_loop_preempting_the_task_is_not_a_dead_loop);
    RUN(a_late_resolver_interrupt_at_speed_is_counted_and_reacquired);
    RUN(fw15_low_wait_counts_from_a_fault_that_preempted_the_task);
}
