/* test_r23_fixes.c — round 23: the defects the closed-loop simulator bridge (tool/bridge) and the FW-38 update work
 * found, end to end on the simulated card (docs/firmware-contract.md §10k; the module-level tests are in their modules'
 * files: temp, nvlog, resolver, update, uds_diag). */
#include <string.h>

#include "dtc.h"
#include "harness.h"
#include "nvlog.h"
#include "sim_pmsm.h"
#include "test.h"

/* ======================= item 1: FW-13 rate check ======================= */

/* The simulator's finding: the module temperatures rising slowly through the FW-04 derating band (60 -> 112 degC at
 * 8 degC/s, well inside cal_temp_rate_c_s). Per 1 ms sample the rate check read one ADC code as 40-500 degC/s: the
 * channels flickered invalid (DTC_TEMP_MODULE) and above ~70 degC three rejections in a row latched TEMP_RATE for the
 * key cycle — every module NTC invalid, the derating stuck at the continuous rating (185/340 = 0.54), the 90 -> 115 degC
 * curve never used. Now: every channel valid all the way, no DTC, and at 100 degC the derating is the curve's (the
 * hysteresis active: 105 degC effective, 0.4). */
TEST(module_temperatures_rising_through_the_derating_band_keep_their_channels)
{
    const hal_adc_sig_t mod[3] = {HAL_ADC_TMOD_U, HAL_ADC_TMOD_V, HAL_ADC_TMOD_W};
    h_setup(TI_SKU_8XX_SIC);
    for (uint32_t k = 0u; k < 3u; k++) {
        sim_set_temp(mod[k], 60.0f); /* warm from the start: the first sample primes the channels */
    }
    h_boot();
    CHECK(h_run_until(SM_PRECHARGE_WAIT, 3000u));
    h_run_ms(1000u);
    bool all_valid = true;
    float derate_at_100 = -1.0f;
    for (uint32_t ms = 0u; ms <= 6500u; ms++) {
        const float t = 60.0f + (8.0f * (float)ms * 1.0e-3f);
        for (uint32_t k = 0u; k < 3u; k++) {
            sim_set_temp(mod[k], t);
        }
        h_run_ms(1u);
        bool any = false;
        bool all = false;
        (void)temp_module_max(&g_app.temp, &any, &all);
        all_valid = all_valid && all;
        if ((derate_at_100 < 0.0f) && (t >= 100.0f)) {
            derate_at_100 = g_app.tlim.derate;
        }
    }
    CHECK(all_valid && dtc_occurrences(DTC_TEMP_MODULE) == 0u);
    for (uint32_t k = 0u; k < 3u; k++) {
        CHECK(g_app.temp.ch[k].fault == TEMP_OK);
    }
    CHECK_NEAR(derate_at_100, 0.4, 0.07); /* the curve, not the invalid-NTC fallback 0.54 */
    CHECK(g_app.tlim.derate < 0.05f);    /* 112 degC (117 effective): at the curve's end */
}

/* ======================= item 4: DTCs declared but never set ======================= */

/* §9: a driver FLT still low at boot (a DESAT pending from before the reset, V5GD healthy) is FAULT and no arming — and
 * had no DTC: DTC_DESAT_PENDING_BOOT. */
TEST(a_desat_pending_at_boot_is_recorded)
{
    h_setup(TI_SKU_8XX_SIC);
    sim_chain_desat(true, true); /* the high-side driver's FLT latched before this power-up, a hard short */
    h_boot();
    CHECK(h_run_until(SM_FAULT, 2000u));
    h_run_ms(50u);
    CHECK(dtc_active(DTC_DESAT_PENDING_BOOT) && g_app.sm.st == SM_FAULT && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
}

/* §9 step 3: a sensor not valid within cal_sensor_selftest_ms — the resolver, its amplitude outside the window — is
 * FAULT, and had no DTC (the resolver's own DTCs need a resolver that was valid once): DTC_SENSOR_SELFTEST. */
TEST(a_sensor_self_test_timeout_is_recorded)
{
    h_setup(TI_SKU_8XX_SIC);
    H.rslv_amp = 0.3f;
    h_set_speed(0.0f);
    h_boot();
    CHECK(h_run_until(SM_FAULT, 3000u));
    CHECK(dtc_active(DTC_SENSOR_SELFTEST) && !dtc_active(DTC_FS26_RELEASE) && !dtc_active(DTC_ISNS_OFFSET));
}

/* The key-on current offset beyond cal_isns_offset_tol_v of the EOL record: the self-test never passes, and the offset
 * had no DTC of its own: DTC_ISNS_OFFSET (then DTC_SENSOR_SELFTEST at the timeout). */
TEST(a_current_offset_beyond_its_tolerance_is_recorded)
{
    h_setup(TI_SKU_8XX_SIC);
    h_cal.isns[1].offset_v = h_p.isns_zero_v + 0.15f; /* the EOL record 150 mV from what the sensor reads at 0 A */
    calib_seal(&h_cal);
    h_boot();
    CHECK(h_run_until(SM_FAULT, 3000u));
    CHECK(dtc_active(DTC_ISNS_OFFSET) && dtc_active(DTC_SENSOR_SELFTEST));
}

/* Found on the way: §9 step 4, the FS0B/FS1B release not achieved (FLT_ERR_CNT never back to 0) within
 * cal_fs0b_release_ms more, is FAULT — DTC_FS26_RELEASE was reachable only from fs26_init, which never returns that
 * status: no DTC. Now the timeout sets it. */
TEST(an_fs0b_release_timeout_is_recorded)
{
    h_setup(TI_SKU_8XX_SIC);
    const sim_fs26_cfg_t fc = {.prog_id = 0x4A21u, .device_id = 0x2600u, .flt_err_stuck = true};
    sim_fs26_config(&fc);
    sim_fs26_reset();
    h_boot();
    CHECK(h_run_until(SM_FAULT, 3000u));
    CHECK(dtc_active(DTC_FS26_RELEASE) && !dtc_active(DTC_SENSOR_SELFTEST) && !g_app.fs0b_released);
}

/* FW-13/FW-04: a board NTC open, a motor sensor open, a module above its derating end (115 degC) — none had a DTC:
 * DTC_TEMP_BOARD, DTC_TEMP_MOTOR, DTC_OVERTEMP. Each reports its pass when the condition ends (one occurrence per event,
 * as DTC_TEMP_MODULE now does too); the over-temperature passes below the end less the derating hysteresis. */
TEST(board_motor_and_over_temperatures_are_recorded_and_pass)
{
    const hal_adc_sig_t mod[3] = {HAL_ADC_TMOD_U, HAL_ADC_TMOD_V, HAL_ADC_TMOD_W};
    h_setup(TI_SKU_8XX_SIC);
    for (uint32_t k = 0u; k < 3u; k++) {
        sim_set_temp(mod[k], 108.0f);
    }
    h_boot();
    CHECK(h_run_until(SM_PRECHARGE_WAIT, 3000u));
    CHECK(!dtc_active(DTC_TEMP_BOARD) && !dtc_active(DTC_TEMP_MOTOR) && !dtc_active(DTC_OVERTEMP));
    sim_adc_set_v(HAL_ADC_NTC_A, 4.99f); /* the ambient board NTC open */
    sim_adc_set_v(HAL_ADC_MT2, 4.99f);   /* the second motor sensor open */
    h_run_ms(5u);
    CHECK(dtc_active(DTC_TEMP_BOARD) && dtc_active(DTC_TEMP_MOTOR) && !dtc_active(DTC_TEMP_MODULE));
    sim_set_temp(HAL_ADC_NTC_A, 25.0f);
    sim_set_temp(HAL_ADC_MT2, 25.0f);
    h_run_ms(300u);
    CHECK(!dtc_active(DTC_TEMP_BOARD) && !dtc_active(DTC_TEMP_MOTOR));
    CHECK(dtc_occurrences(DTC_TEMP_BOARD) == 1u && dtc_occurrences(DTC_TEMP_MOTOR) == 1u);
    for (uint32_t ms = 0u; ms <= 1000u; ms++) { /* 108 -> 118 degC at 10 degC/s */
        for (uint32_t k = 0u; k < 3u; k++) {
            sim_set_temp(mod[k], 108.0f + (10.0f * (float)ms * 1.0e-3f));
        }
        h_run_ms(1u);
    }
    h_run_ms(300u);
    CHECK(dtc_active(DTC_OVERTEMP) && !dtc_active(DTC_TEMP_MODULE) && g_app.tlim.derate == 0.0f);
    for (uint32_t ms = 0u; ms <= 1500u; ms++) { /* back to 103 degC */
        for (uint32_t k = 0u; k < 3u; k++) {
            sim_set_temp(mod[k], 118.0f - (10.0f * (float)ms * 1.0e-3f));
        }
        h_run_ms(1u);
    }
    h_run_ms(300u);
    CHECK(!dtc_active(DTC_OVERTEMP) && dtc_occurrences(DTC_OVERTEMP) == 1u);
    sim_adc_set_v(HAL_ADC_TMOD_V, 4.99f); /* a module NTC open: DTC_TEMP_MODULE, then its pass */
    h_run_ms(5u);
    CHECK(dtc_active(DTC_TEMP_MODULE));
    sim_set_temp(HAL_ADC_TMOD_V, 103.0f);
    h_run_ms(300u);
    CHECK(!dtc_active(DTC_TEMP_MODULE) && dtc_occurrences(DTC_TEMP_MODULE) == 1u);
}

/* ======================= item 8: FW-10's debounce ======================= */

/* A power-on (every model, retained RAM, the DTC store and the NVM fresh) in RUN at rpm with the torque (the harness's
 * ideal current loop). */
static void power_on(void)
{
    sim_reset();
    sim_nvm_wipe();
    dtc_init();
    (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
    (void)memset(&g_app_session, 0, sizeof g_app_session);
}

static bool run_at(float rpm, float torque_nm)
{
    power_on();
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    if (!h_to_run(torque_nm)) {
        return false;
    }
    h_ramp_speed(rpm, 600u);
    h_run_ms(20u);
    return g_app.sm.st == SM_RUN;
}

/* Item 4, found by the bridge self-test (item 13): FW-15 — a DESAT recorded in NVM in this or the previous key cycle
 * blocks automatic arming at the next power-up (fm_boot: the state machine goes to FAULT), and the DTC store is RAM:
 * that FAULT named nothing. The recorded bank's DESAT DTC is raised again at the power-up. */
TEST(a_desat_recorded_before_the_power_up_names_the_fault_it_blocks)
{
    for (unsigned k = 0u; k < 2u; k++) {
        const bool hs = (k == 0u);
        CHECK(run_at(1500.0f, 100.0f));
        sim_chain_desat(hs, false);
        h_run_ms(200u); /* the §6 action, the record queued */
        for (uint32_t i = 0u; (i < 400u) && !nv_idle(); i++) {
            h_run_ms(1u);
        }
        CHECK(nv_idle() && dtc_active(hs ? DTC_DESAT_HS : DTC_DESAT_LS));
        /* a power cycle: the card model, the retained RAM and the DTC store fresh, the NVM kept */
        sim_reset();
        dtc_init();
        (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
        (void)memset(&g_app_session, 0, sizeof g_app_session);
        h_setup(TI_SKU_8XX_SIC);
        h_boot();
        CHECK(g_app.fm.desat_blocked && (g_app.fm.desat_bank == (hs ? 1u : 2u)));
        CHECK(dtc_active(hs ? DTC_DESAT_HS : DTC_DESAT_LS) && !dtc_active(hs ? DTC_DESAT_LS : DTC_DESAT_HS));
        CHECK(h_run_until(SM_FAULT, 3000u)); /* after the self-test, before arming: blocked */
        CHECK(dtc_active(hs ? DTC_DESAT_HS : DTC_DESAT_LS) && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    }
}

/* In RUN at 3000 rpm under 80 N m: one carrier period of the resolver at 40 % amplitude, and one 5 deg (el) angle error.
 * Each made the resolver invalid for that frame or latched its acceleration plausibility, and the current loop raised
 * the latched control-lost row — SPO, FAULT, the resolver gone for the key cycle (the FW-42 finding). Now each is one
 * count: RUN, modulating, no row, no DTC. Three out-of-window frames in a row still latch the row (DTC_RSLV_AMPLITUDE). */
TEST(a_single_corrupt_resolver_frame_does_not_withdraw_the_resolver)
{
    for (unsigned k = 0u; k < 2u; k++) {
        CHECK(run_at(3000.0f, 80.0f));
        h_run_ms(1u);
        if (k == 0u) {
            H.rslv_amp = 0.4f;
            h_set_speed(3000.0f);
            h_isr_only_us(100u);
            H.rslv_amp = 1.0f;
            h_set_speed(3000.0f);
        } else {
            sim_resolver_glitch(5.0f * 0.01745329f / 4.0f); /* 5 deg el = 1.25 deg resolver (1 pole pair, motor 4) */
            h_isr_only_us(100u);
            sim_resolver_glitch(-5.0f * 0.01745329f / 4.0f);
        }
        bool withdrawn = false;
        for (uint32_t t = 0u; t < 50u; t++) {
            h_run_ms(1u);
            withdrawn = withdrawn || !g_app.rslv.valid || fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID);
        }
        CHECK(!withdrawn && g_app.sm.st == SM_RUN && hal_pwm_mode() == HAL_PWM_MOD);
        CHECK(!dtc_active(DTC_RSLV_AMPLITUDE) && !dtc_active(DTC_RSLV_ACCEL) && !dtc_active(DTC_RSLV_TRACKING));
    }
    H.rslv_amp = 0.4f; /* three frames and more: the debounce ends, the row */
    h_set_speed(3000.0f);
    h_run_ms(2u);
    CHECK(fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && dtc_active(DTC_RSLV_AMPLITUDE) && g_app.rslv.amp_fault);
}

/* ======================= item 7: the resolver angle's reference instant ======================= */

/* The simulator bridge measured the firmware's angle leading the rotor by 7.7 us (1.85 deg el at 10 000 rpm) on the host
 * SDADC model and called it a host-model artefact; it is the demodulator's reference time (resolver.c: centroid_us). On
 * the card's model — per-channel eDMA blocks, the rotor moving within each block, the 24 deg filter lag — the angle the
 * current loop uses at its entry is now the rotor's, forwards and backwards at 10 000 rpm and at 3000 rpm. */
TEST(the_angle_the_current_loop_uses_is_the_rotors_at_speed)
{
    const float rpm[3] = {10000.0f, -10000.0f, 3000.0f};
    for (unsigned k = 0u; k < 3u; k++) {
        CHECK(run_at(0.0f, 20.0f));
        h_ramp_speed(rpm[k], 1500u);
        h_run_ms(30u);
        CHECK(g_app.rslv.valid);
        float worst = 0.0f;
        for (uint32_t i = 0u; i < 40u; i++) {
            h_isr_only_us(37u);
            const float err = ti_wrap_pi(rslv_theta_e_at(&g_app.rslv, &g_app.cal.rslv, hal_time_us(), g_app.p) -
                                         h_rotor_theta_e());
            worst = ti_maxf(worst, ti_absf(err));
        }
        CHECK_NEAR(worst * 57.2957795f, 0.0, 0.15); /* deg el; 1.85 before at 10 000 rpm */
    }
}

/* ======================= item 6: the speed bound after a resolver loss ======================= */

/* The §6 action while the resolver is lost (40 % amplitude: its window, a latched fault) at rpm under a small torque
 * (the winding current small: rule (a) holds), sampled every 1 ms for ms. Returns the first ms at which the action is
 * LS-ASC (0: never), and whether the bound followed |n| + a_max t at every sample (1 rpm + one task of slack). */
static uint32_t asc_after_resolver_loss(float rpm, uint32_t ms, bool *bound_ok)
{
    *bound_ok = true;
    if (!run_at(rpm, 10.0f)) {
        return 0xFFFFFFFFu;
    }
    H.i_pk_a = 5.0f;
    const uint32_t t0 = hal_time_ms();
    const float a_max = g_app.p->cal_speed_accel_max_rpm_s;
    H.rslv_amp = 0.4f;
    h_set_speed(rpm);
    uint32_t asc_at = 0u;
    for (uint32_t t = 1u; t <= ms; t++) {
        h_run_ms(1u);
        if (!g_app.rslv.valid) {
            const float want = ti_absf(g_app.speed_rpm) + (a_max * (float)ti_age(hal_time_ms(), g_app.speed_valid_ms) * 1e-3f);
            const float got = app_speed_hi_rpm(&g_app, hal_time_ms());
            *bound_ok = *bound_ok && (ti_absf(got - want) <= (1.0f + (a_max * 1e-3f))) && g_app.speed_known;
            *bound_ok = *bound_ok && (ti_age(hal_time_ms(), t0) >= t - 1u);
        }
        if ((asc_at == 0u) && (g_app.fm.dec.action == SS_ACT_LS_ASC)) {
            asc_at = t;
        }
    }
    return asc_at;
}

/* §6 "Unknown speed" (round 17): the last valid speed was held for cal_speed_hold_ms (200 ms), then the speed was
 * unknown — the n >= n_x column: a resolver fault at 2000 rpm took SPO, then PWM-ASC 200 ms later (-32 N m of braking on
 * the bench). Round 23: the speed is bounded, |n| <= |n_last| + cal_speed_accel_max_rpm_s x t — SPO while the bound stays
 * below n_x (8086 rpm, the screening motor at 880 V), LS-ASC once it reaches it: at 2000 rpm SPO for (8086 - 2000) / 2500
 * = 2.4 s, at 7000 rpm LS-ASC after (8086 - 7000) / 2500 = 434 ms; the bound grows with time. */
TEST(a_resolver_loss_is_decided_on_the_speed_bound)
{
    bool ok = false;
    const float a_max = ti_params_get(TI_SKU_8XX_SIC)->cal_speed_accel_max_rpm_s;
    const uint32_t at_2000 = asc_after_resolver_loss(2000.0f, 1500u, &ok);
    CHECK(at_2000 == 0u && ok && g_app.fm.dec.action == SS_ACT_SPO && g_app.br.mode != BR_ASC);
    CHECK(fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && g_app.speed_known);
    const uint32_t at_7000 = asc_after_resolver_loss(7000.0f, 700u, &ok);
    const float expect = (g_app.n_x_rpm - 7000.0f) / a_max * 1000.0f;
    CHECK(ok && (float)at_7000 >= (expect - 5.0f) && (float)at_7000 <= (expect + 5.0f));
    CHECK(g_app.fm.dec.action == SS_ACT_LS_ASC && g_app.br.mode == BR_ASC);
}

/* Beyond the calibration's n_max the bound says nothing: the speed is unknown (the n >= n_x column, rule (a) at n_max),
 * as when the resolver was never valid. With the bound at its range ceiling (50 000 rpm/s) from 2000 rpm: known for
 * (16 000 - 2000) / 50 000 = 280 ms, then unknown. */
TEST(the_speed_is_unknown_once_the_bound_passes_n_max)
{
    CHECK(run_at(2000.0f, 10.0f));
    h_p.cal_speed_accel_max_rpm_s = 50000.0f;
    H.i_pk_a = 5.0f;
    H.rslv_amp = 0.4f;
    h_set_speed(2000.0f);
    uint32_t unknown_at = 0u;
    for (uint32_t t = 1u; (t <= 400u) && (unknown_at == 0u); t++) {
        h_run_ms(1u);
        unknown_at = g_app.speed_known ? 0u : t;
    }
    CHECK(unknown_at >= 275u && unknown_at <= 290u && g_app.fm.dec.action == SS_ACT_LS_ASC);
}

/* ======================= the virtual PMSM behind the card (sim_pmsm.h) ======================= */

static void pmsm(void) { sim_pmsm_step(H.link_v); }

/* A fresh key cycle with the calibration record's motor (the S6 screening motor: 0.35 mH, 25 mOhm, 0.15 Wb, 4 pole
 * pairs) as a voltage-driven PMSM on a dyno, its inverter the SKU's (10 % more dead time than the FOC compensates); armed,
 * RUN at torque_nm, the dyno ramped to rpm in ramp_ms. */
static bool on_plant(float rpm, float torque_nm, uint32_t ramp_ms, float v_pack)
{
    power_on();
    h_setup(TI_SKU_8XX_SIC);
    H.v_pack = v_pack;
    const motor_t m = h_cal.motor;
    const sim_pmsm_cfg_t c = {.rs_ohm = m.rs_ohm, .ld_h = m.ld_h, .lq_h = m.lq_h, .psi_wb = m.psi_wb, .pp = m.pp,
                              .rpp = 1u, .zero_rad = h_cal.rslv.zero_rad, .rslv_reversed = false,
                              .t_dead_s = 1.1f * (float)h_p.dead_time_ns * 1e-9f, .fsw_hz = (float)h_p.fsw_hz[0],
                              .i_knee_a = 3.0f, .j_kgm2 = 0.05f, .noise_a = 0.55f};
    sim_pmsm_init(&c, 0.9f);
    H.plant = pmsm;
    H.p_chg_w = 200000.0f; /* the BMS takes 200 kW of regen */
    h_boot();
    if (!((torque_nm != 0.0f) ? h_to_run(torque_nm) : h_to_armed())) {
        return false;
    }
    for (uint32_t k = 1u; k <= ramp_ms; k++) {
        sim_pmsm_rotor(SIM_ROTOR_DYNO, rpm * (float)k / (float)ramp_ms);
        h_run_ms(1u);
    }
    h_run_ms(50u);
    return (g_app.sm.st == ((torque_nm != 0.0f) ? SM_RUN : SM_ARMED_ZERO_TORQUE)) && g_app.rslv.valid;
}

static bool run_on_plant(float rpm, float torque_nm, uint32_t ramp_ms) { return on_plant(rpm, torque_nm, ramp_ms, 750.0f); }

/* ======================= item 3: the torque command's slew ======================= */

/* The simulator's finding: a fresh command was applied at once — only the FW-11 ramp-down of a stale command existed —
 * so a VCU stepping +200 -> -150 N m at 10 000 rpm (deep field weakening, 750 V) reversed i_q faster than the loop's
 * voltage allows and tripped FW-05 (the control-lost row, LS-ASC). The command path now slews at cal_torque_slew_nm_s
 * (both directions; 2000 N m/s): the same step on the voltage-driven motor stays in RUN, no over-current, the torque
 * applied reaches -150 N m. */
TEST(a_torque_reversal_at_10000_rpm_is_slewed_and_never_trips)
{
    CHECK(run_on_plant(10000.0f, 200.0f, 1200u));
    CHECK(ti_absf(g_app.t_act_nm - 200.0f) < 2.0f && hal_pwm_mode() == HAL_PWM_MOD);
    H.torque_nm = -150.0f;
    bool tripped = false;
    float slew_max = 0.0f;
    float prev = g_app.t_cmd_nm;
    for (uint32_t k = 0u; k < 300u; k++) {
        h_run_ms(1u);
        tripped = tripped || dtc_active(DTC_OVERCURRENT) || fm_active(&g_app.fm, SS_ROW_OVERCURRENT);
        slew_max = ti_maxf(slew_max, ti_absf(g_app.t_cmd_nm - prev) * 1000.0f);
        prev = g_app.t_cmd_nm;
    }
    CHECK(!tripped && g_app.sm.st == SM_RUN && hal_pwm_mode() == HAL_PWM_MOD);
    CHECK(ti_absf(g_app.t_act_nm + 150.0f) < 2.0f);
    CHECK(slew_max <= (g_app.p->cal_torque_slew_nm_s * 1.001f));
}

/* The next VCU command frame at once, with the harness state (its alive counter in sequence). */
static void vcu_frame_now(void)
{
    hal_can_frame_t f;
    can_encode_vcu_cmd(&f, H.ctr, H.gear, H.enable, H.fault_reset, H.torque_nm, H.contactors, H.retry_auth,
                       H.discharge, H.shutdown, H.coolant_c);
    sim_can_inject(HAL_CAN_VEHICLE, &f);
    H.ctr = (uint8_t)((H.ctr + 1u) & 0x0Fu);
}

/* The slew is the fresh command's only: the zeroings are not slewed. At 1000 rpm and 200 N m — the slew set to its
 * ceiling (20 000 N m/s), which a fresh step then follows — a battery-path loss zeroes the command in the task that sees
 * it, a DESAT in the task after the fault ISR (both the §6 decision: SPO / zero current), and an HVIL opening takes the
 * FW-11 ramp at cal_torque_ramp_nm_s (2000 N m/s), not the slew. */
TEST(the_fault_paths_still_zero_the_torque_at_once)
{
    CHECK(run_at(1000.0f, 50.0f));
    h_p.cal_torque_slew_nm_s = 20000.0f;
    H.torque_nm = 200.0f;
    h_run_ms(22u); /* the frame (<= 10 ms), then 150 N m in 7.5 ms */
    CHECK(ti_absf(g_app.t_cmd_nm - 200.0f) < 0.01f);
    H.contactors = TI_CONT_OPEN;
    vcu_frame_now();
    h_tick();
    CHECK(fm_active(&g_app.fm, SS_ROW_BATTERY_LOST) && g_app.t_cmd_nm == 0.0f && g_app.iq_ref == 0.0f);
    CHECK(run_at(1000.0f, 200.0f));
    sim_chain_desat(true, false);
    h_tick();
    CHECK(fm_active(&g_app.fm, SS_ROW_FLT_HS) && g_app.t_cmd_nm == 0.0f);
    CHECK(run_at(1000.0f, 200.0f));
    h_p.cal_torque_slew_nm_s = 20000.0f;
    sim_hvil_set(SIM_HVIL_OPEN);
    bool ramped = true;
    float prev = g_app.t_cmd_nm;
    for (uint32_t k = 0u; k < 150u; k++) {
        h_run_ms(1u);
        ramped = ramped && ((prev - g_app.t_cmd_nm) <= ((g_app.p->cal_torque_ramp_nm_s * 1.0e-3f) + 1.0e-3f));
        prev = g_app.t_cmd_nm;
    }
    CHECK(ramped && fm_active(&g_app.fm, SS_ROW_CMD_LOST) && g_app.t_cmd_nm == 0.0f);
}

/* ======================= item 2: field weakening from the measured link ======================= */

/* Armed at zero torque on the voltage-driven motor at rpm with the pack at v_pack, for 200 ms: whether the bridge ever
 * idled while the line-line back-EMF peak sqrt(3) w_e psi exceeded the link (the free-wheeling diodes then conduct: an
 * uncommanded brake and regen into the pack), whether it modulated throughout, the largest |torque| the plant made and
 * the extremes of the power the bridge exchanged (1.5 (v_d i_d + v_q i_q), the FW-43 estimate; + drawn from the link). */
typedef struct {
    bool diodes;
    bool modulating;
    float t_max_nm;
    float p_max_w;
    float p_min_w;
} idle_run_t;

static idle_run_t zero_torque_at(float rpm, float v_pack)
{
    idle_run_t r = {.diodes = false, .modulating = true, .t_max_nm = 0.0f, .p_max_w = 0.0f, .p_min_w = 0.0f};
    if (!on_plant(rpm, 0.0f, (uint32_t)(ti_absf(rpm) / 10.0f), v_pack)) {
        r.diodes = true;
        r.modulating = false;
        return r;
    }
    const motor_t *m = &g_app.cal.motor;
    const float e_ll = TI_SQRT3 * ti_absf(motor_omega_e(rpm, m)) * m->psi_wb;
    for (uint32_t k = 0u; k < 200u; k++) {
        h_run_ms(1u);
        const bool mod = (g_app.br.mode == BR_MOD) && (hal_pwm_mode() == HAL_PWM_MOD);
        r.diodes = r.diodes || (!mod && (e_ll > g_app.vdc.vdc));
        r.modulating = r.modulating && mod;
        r.t_max_nm = ti_maxf(r.t_max_nm, ti_absf(sim_pmsm_torque_nm()));
        const float pw = 1.5f * ((g_app.foc.vd * g_app.foc.id) + (g_app.foc.vq * g_app.foc.iq));
        r.p_max_w = ti_maxf(r.p_max_w, pw);
        r.p_min_w = ti_minf(r.p_min_w, pw);
    }
    return r;
}

/* The simulator's finding: field weakening was decided from n_x alone (the speed at which the back-EMF reaches the
 * 880 V OV trip, 8086 rpm on the screening motor) — at a 750 V link the back-EMF exceeds the link from 6 890 rpm, and
 * between there and n_x the bridge idled at zero torque: the diodes braked the shaft (50-65 N m) and pushed ~40 kW into
 * the pack, uncommanded. Now the bridge takes over (id control at zero torque) once w_e psi reaches (1 -
 * cal_fw_emf_margin_frac) of what a reference may use of the measured link: at 750 V and 7 500 rpm it modulates, i_d
 * negative, the torque and the power ~0; at 850 V the old behaviour stands where it was right — idle at 5000 rpm (the
 * back-EMF far below the link), modulating above n_x (8 500 rpm). The §6 decisions keep n_x (a row at 7 500 rpm takes
 * the n < n_x column). */
TEST(zero_torque_below_n_x_takes_the_back_emf_off_the_diodes)
{
    const idle_run_t a = zero_torque_at(7500.0f, 750.0f);
    CHECK(!a.diodes && a.modulating && g_app.id_ref < -20.0f && ti_absf(g_app.t_act_nm) < 0.5f);
    CHECK(a.t_max_nm < 3.0f && a.p_min_w > -500.0f && a.p_max_w < 3000.0f); /* no brake, no regen: losses (~1 kW) */
    const idle_run_t b = zero_torque_at(5000.0f, 850.0f);
    CHECK(!b.diodes && g_app.br.mode == BR_IDLE && hal_pwm_mode() == HAL_PWM_OFF && b.t_max_nm < 0.5f);
    const idle_run_t c = zero_torque_at(8500.0f, 850.0f);
    CHECK(!c.diodes && c.modulating && g_app.id_ref < -20.0f);
    CHECK(run_at(7500.0f, 20.0f)); /* §6: HVIL open at 7 500 rpm is the n < n_x column still */
    sim_hvil_set(SIM_HVIL_OPEN);
    h_run_ms(60u); /* the HVIL debounce: <= 100 ms (FW-09) */
    CHECK(fm_active(&g_app.fm, SS_ROW_CMD_LOST) && !g_app.fm.row_dec[SS_ROW_CMD_LOST].high_speed &&
          g_app.fm.row_dec[SS_ROW_CMD_LOST].action == SS_ACT_RAMP_THEN_SPO);
}

/* The take-over speed follows the MEASURED link: armed at zero torque, the dyno sweeping up at 1 rpm/ms, the bridge starts
 * modulating where w_e psi = (1 - margin) x torque_v_available(V_DC): at 750 V 5 909 rpm, at 650 V 5 121 rpm (+/- 40 rpm)
 * — before the diodes' point (6 890 / 5 972 rpm) and where zero current still fits the loop's voltage. */
TEST(the_take_over_follows_the_measured_link)
{
    const float v_pack[2] = {750.0f, 650.0f};
    for (unsigned k = 0u; k < 2u; k++) {
        CHECK(on_plant(4500.0f, 0.0f, 450u, v_pack[k]));
        const motor_t *m = &g_app.cal.motor;
        const float v_av = torque_v_available(g_app.vdc.vdc, g_app.p);
        const float want = ((1.0f - g_app.p->cal_fw_emf_margin_frac) * v_av / m->psi_wb) / (float)m->pp * TI_RPM_PER_RAD_S;
        float at = 0.0f;
        for (uint32_t ms = 1u; (ms <= 2500u) && (at == 0.0f); ms++) {
            sim_pmsm_rotor(SIM_ROTOR_DYNO, 4500.0f + (float)ms);
            h_run_ms(1u);
            at = (g_app.br.mode == BR_MOD) ? sim_pmsm_rpm() : 0.0f;
        }
        CHECK_NEAR(at, want, 40.0);
        CHECK(want < (v_pack[k] / (TI_SQRT3 * m->psi_wb) / (float)m->pp * TI_RPM_PER_RAD_S)); /* before the diodes */
    }
}

/* ======================= item 5: the ASC entry's transient ======================= */

/* LS-ASC entered at speed on the voltage-driven motor — the battery path lost at 8 500 and 10 000 rpm (n_x 8086 rpm) —
 * shorts a winding carrying its back-EMF: the current overshoots the 601 A FW-05 compare (730 / 690 A on this plant) for
 * ~13 ms (L/R 14 ms). The analysis, on the card's model: the compare's fault input FAULT1 is mapped (DISMAP) to the high
 * sides only, so at every current-loop sample of the transient the low sides are on (the ASC latch and PWM-ASC) and the
 * high sides off — the over-current path never opens what the ASC must hold. The classification: until round 23 each
 * entry latched DTC_OVERCURRENT and the control-lost row; now, inside cal_asc_oc_window_ms (20) of the entry, it is
 * DTC_ASC_OC_TRANSIENT (information, passed once over), no row, and the compare is re-armed — its flags cleared — once the
 * current is back inside it. */
TEST(an_asc_entry_transient_is_information_and_the_low_sides_hold)
{
    const float rpm[2] = {8500.0f, 10000.0f};
    for (unsigned k = 0u; k < 2u; k++) {
        CHECK(on_plant(rpm[k], 20.0f, (uint32_t)(rpm[k] / 8.0f), 750.0f));
        H.contactors = TI_CONT_OPEN; /* the battery path lost at speed: LS-ASC */
        vcu_frame_now();
        const uint32_t per = app_isr_period_us(&g_app);
        bool entered = false;
        bool ls_held = true;
        bool tripped = false;
        float i_max = 0.0f;
        for (uint32_t i = 0u; i < (40000u / per); i++) {
            h_isr_only_us(per);
            if (g_app.br.mode == BR_ASC) {
                entered = true;
                float id = 0.0f;
                float iq = 0.0f;
                sim_pmsm_idq(&id, &iq);
                i_max = ti_maxf(i_max, sqrtf((id * id) + (iq * iq)));
                ls_held = ls_held && sim_chain_ls_on() && !sim_chain_hs_on() && sim_chain_asc_latch();
                tripped = tripped || ((hal_pwm_fault_flags() & HAL_PWM_FAULT_ADC_WD) != 0u);
            }
            if ((i % (1000u / per)) == 0u) {
                h_tick();
            }
        }
        CHECK(entered && ls_held && tripped && (i_max > g_app.p->i_oc_trip_a));
        CHECK(dtc_occurrences(DTC_ASC_OC_TRANSIENT) >= 1u && !dtc_active(DTC_ASC_OC_TRANSIENT));
        CHECK(!dtc_active(DTC_OVERCURRENT) && !fm_active(&g_app.fm, SS_ROW_OVERCURRENT) && g_app.br.mode == BR_ASC);
        CHECK((hal_pwm_fault_flags() & HAL_PWM_FAULT_ADC_WD) == 0u); /* re-armed for the next event */
    }
}

/* Outside the window the existing behaviour stays (the harness's current loop; LS-ASC at 10 000 rpm after the battery
 * path is lost): an over-current 30 ms after the entry is DTC_OVERCURRENT and the latched control-lost row; one that starts
 * inside the window and outlasts it is information first, then the same fault once the window is over. */
TEST(an_over_current_outside_the_asc_window_is_still_the_fault)
{
    for (unsigned k = 0u; k < 2u; k++) {
        CHECK(run_at(10000.0f, 20.0f));
        H.contactors = TI_CONT_OPEN;
        vcu_frame_now();
        h_tick();
        h_tick();
        CHECK(g_app.br.mode == BR_ASC);
        const uint32_t win = g_app.p->cal_asc_oc_window_ms;
        if (k == 0u) {
            h_run_ms(win + 10u);
            CHECK(!dtc_active(DTC_OVERCURRENT) && dtc_occurrences(DTC_ASC_OC_TRANSIENT) == 0u);
            H.i_pk_a = 700.0f; /* beyond the 601 A compare */
            h_run_ms(2u);
            CHECK(dtc_active(DTC_OVERCURRENT) && fm_active(&g_app.fm, SS_ROW_OVERCURRENT));
            CHECK(dtc_occurrences(DTC_ASC_OC_TRANSIENT) == 0u);
        } else {
            h_run_ms(2u);
            H.i_pk_a = 700.0f; /* inside the window, and it stays */
            h_run_ms(2u);
            CHECK(dtc_active(DTC_ASC_OC_TRANSIENT) && !dtc_active(DTC_OVERCURRENT));
            h_run_ms(win);
            CHECK(dtc_active(DTC_OVERCURRENT) && fm_active(&g_app.fm, SS_ROW_OVERCURRENT));
        }
        H.i_pk_a = 0.0f;
    }
}

/* An ASC entered by FW-06 (the link at 900 V, the harness's current loop at 3000 rpm) and an over-current transient
 * inside its window: information, as above — and the re-arm clears the phase flags only: FAULT1 is FW-06's too, and it
 * stays set with the over-voltage flag (the latched row's VCU reset clears both, as before). */
TEST(an_over_voltage_asc_keeps_its_fault_flag_through_the_transients_rearm)
{
    CHECK(run_at(3000.0f, 20.0f));
    H.link_override = true;
    sim_set_link_v(900.0f, 900.0f);
    h_run_ms(1u);
    CHECK(g_app.br.mode == BR_ASC && fm_active(&g_app.fm, SS_ROW_OVERVOLTAGE));
    H.i_pk_a = 700.0f;
    h_run_ms(3u);
    CHECK(dtc_active(DTC_ASC_OC_TRANSIENT) && !dtc_active(DTC_OVERCURRENT));
    H.i_pk_a = 0.0f;
    sim_set_link_v(750.0f, 750.0f);
    h_run_ms(g_app.p->cal_asc_oc_window_ms + 10u);
    const uint32_t ov = (1u << HAL_ADC_VDC1) | (1u << HAL_ADC_VDC2);
    const uint32_t ph = (1u << HAL_ADC_ISNS_U) | (1u << HAL_ADC_ISNS_V) | (1u << HAL_ADC_ISNS_W);
    CHECK(!dtc_active(DTC_ASC_OC_TRANSIENT) && ((hal_adc_watchdog_status() & ph) == 0u));
    CHECK(((hal_adc_watchdog_status() & ov) != 0u) && ((hal_pwm_fault_flags() & HAL_PWM_FAULT_ADC_WD) != 0u));
    CHECK(g_app.br.mode == BR_ASC && fm_active(&g_app.fm, SS_ROW_OVERVOLTAGE));
}

void suite_r23_fixes(void)
{
    RUN(module_temperatures_rising_through_the_derating_band_keep_their_channels);
    RUN(a_desat_pending_at_boot_is_recorded);
    RUN(a_desat_recorded_before_the_power_up_names_the_fault_it_blocks);
    RUN(a_sensor_self_test_timeout_is_recorded);
    RUN(a_current_offset_beyond_its_tolerance_is_recorded);
    RUN(an_fs0b_release_timeout_is_recorded);
    RUN(board_motor_and_over_temperatures_are_recorded_and_pass);
    RUN(a_single_corrupt_resolver_frame_does_not_withdraw_the_resolver);
    RUN(the_angle_the_current_loop_uses_is_the_rotors_at_speed);
    RUN(a_resolver_loss_is_decided_on_the_speed_bound);
    RUN(the_speed_is_unknown_once_the_bound_passes_n_max);
    RUN(a_torque_reversal_at_10000_rpm_is_slewed_and_never_trips);
    RUN(the_fault_paths_still_zero_the_torque_at_once);
    RUN(zero_torque_below_n_x_takes_the_back_emf_off_the_diodes);
    RUN(the_take_over_follows_the_measured_link);
    RUN(an_asc_entry_transient_is_information_and_the_low_sides_hold);
    RUN(an_over_current_outside_the_asc_window_is_still_the_fault);
    RUN(an_over_voltage_asc_keeps_its_fault_flag_through_the_transients_rearm);
}
