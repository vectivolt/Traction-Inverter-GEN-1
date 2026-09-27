/* test_fw42_44.c — round 23: FW-42 overspeed protection, FW-43 run-time statistics, FW-44 key-on current-offset
 * refresh (docs/firmware-contract.md §10j). */
#include <string.h>

#include "dtc.h"
#include "harness.h"
#include "nvlog.h"
#include "test.h"
#include "uds.h"

#define DEG (0.01745329f)

/* ---------------- helpers ---------------- */

/* The next key cycle: retained RAM lost, the NVM kept; the calibration's motor record (psi, n_max) and the EOL
 * current offsets as given (NULL: nominal), the rotor already turning at rpm. */
static void key_on(float psi_wb, float n_max_rpm, const float *eol_v, float rpm)
{
    sim_reset();
    dtc_init();
    (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    h_setup(TI_SKU_8XX_SIC);
    h_cal.motor.psi_wb = psi_wb;
    h_cal.motor.n_max_rpm = n_max_rpm;
    for (uint32_t i = 0u; (eol_v != NULL) && (i < 3u); i++) {
        h_cal.isns[i].offset_v = eol_v[i];
    }
    calib_seal(&h_cal);
    h_set_speed(rpm);
    h_boot();
}

/* A fresh key cycle (the NVM wiped) in RUN at rpm with the torque. */
static bool run_motor(float psi_wb, float n_max_rpm, float rpm, float torque_nm)
{
    sim_nvm_wipe();
    key_on(psi_wb, n_max_rpm, NULL, 0.0f);
    if (!h_to_run(torque_nm)) {
        return false;
    }
    h_ramp_speed(rpm, 600u);
    h_run_ms(20u);
    return g_app.sm.st == SM_RUN;
}

static bool speed_limit_bit(void)
{
    hal_can_frame_t f;
    bool got = false;
    bool bit = false;
    while (sim_can_pop_tx(HAL_CAN_VEHICLE, &f)) {
        if (f.id == CAN_ID_INV_STATUS) {
            got = true;
            bit = (f.data[14] & 0x08u) != 0u; /* b14 [3] speed limit requested */
        }
    }
    return got && bit;
}

static uint32_t be(const uint8_t *b, uint32_t n)
{
    uint32_t v = 0u;
    for (uint32_t i = 0u; i < n; i++) {
        v = (v << 8) | b[i];
    }
    return v;
}

/* One request on the diagnostic bus, the 1 ms task that serves it, its response. */
static bool diag_request(const uint8_t *data, uint8_t len, hal_can_frame_t *rsp)
{
    hal_can_frame_t rq = {.id = UDS_ID_REQ, .len = len};
    (void)memset(rq.data, 0xAA, sizeof rq.data);
    (void)memcpy(rq.data, data, len);
    sim_can_inject(HAL_CAN_DIAG, &rq);
    h_run_ms(1u);
    bool got = false;
    while (sim_can_pop_tx(HAL_CAN_DIAG, rsp)) {
        got = true;
    }
    return got;
}

static ovs_band_t feed(ovs_t *o, float rpm, uint32_t ms, const ti_params_t *p)
{
    ovs_band_t b = o->band;
    for (uint32_t k = 0u; k < ms; k++) {
        b = ovs_step(o, rpm, true, 16000.0f, p);
    }
    return b;
}

static void nv_drain(void)
{
    for (int k = 0; (k < 400) && !nv_idle(); k++) {
        nv_service();
    }
}

/* ======================= CALs ======================= */

/* The six round-23 CALs: defaults and ranges, every SKU. The two overspeed bands cannot swap: the trip's floor (1.02)
 * is above the warning's ceiling (1.00). */
TEST(round23_cal_defaults_and_ranges)
{
    for (int k = TI_SKU_8XX_SIC; k < TI_SKU_COUNT; k++) {
        const ti_params_t *q = ti_params_get((ti_sku_t)k);
        CHECK(q->cal_ovs_warn_frac == 1.0f && q->cal_ovs_trip_frac == 1.05f && q->cal_ovs_hyst_frac == 0.02f);
        CHECK(q->cal_ovs_debounce_ms == 10u && q->cal_rs_save_s == 600u && q->cal_isns_ofs_step_v == 0.002f);
        CHECK(ti_params_validate(q) == 0u);
    }
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    p.cal_ovs_warn_frac = 0.9f;
    p.cal_ovs_trip_frac = 1.2f;
    p.cal_ovs_hyst_frac = 0.05f;
    p.cal_ovs_debounce_ms = 100u;
    p.cal_rs_save_s = 3600u;
    p.cal_isns_ofs_step_v = 0.02f;
    CHECK(ti_params_validate(&p) == 0u);
    const ti_params_t ok = p;
    p.cal_ovs_warn_frac = 1.01f;
    CHECK(ti_params_validate(&p) >= 1u);
    p = ok;
    p.cal_ovs_trip_frac = 1.01f;
    CHECK(ti_params_validate(&p) >= 1u);
    p = ok;
    p.cal_ovs_hyst_frac = 0.06f;
    CHECK(ti_params_validate(&p) >= 1u);
    p = ok;
    p.cal_ovs_debounce_ms = 1u;
    CHECK(ti_params_validate(&p) >= 1u);
    p = ok;
    p.cal_rs_save_s = 59u;
    CHECK(ti_params_validate(&p) >= 1u);
    p = ok;
    p.cal_isns_ofs_step_v = 0.03f;
    CHECK(ti_params_validate(&p) >= 1u);
}

/* ======================= FW-42 overspeed ======================= */

/* n_max 16 000 rpm: warning from 16 000, trip from 16 800, each left 320 rpm below its start, 10 ms debounce. */
TEST(overspeed_warning_trip_recovery_with_hysteresis)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    ovs_t o;
    ovs_init(&o);
    CHECK(feed(&o, 15999.0f, 100u, p) == OVS_NONE);
    CHECK(feed(&o, 16000.0f, 9u, p) == OVS_NONE);
    CHECK(feed(&o, 16000.0f, 1u, p) == OVS_WARN); /* the 10th consecutive sample */
    CHECK(feed(&o, 16799.0f, 100u, p) == OVS_WARN);
    CHECK(feed(&o, 16800.0f, 10u, p) == OVS_TRIP);
    CHECK(feed(&o, 16481.0f, 100u, p) == OVS_TRIP); /* above 16 800 - 320: held */
    CHECK(feed(&o, 16480.0f, 9u, p) == OVS_TRIP);
    CHECK(feed(&o, 16480.0f, 1u, p) == OVS_WARN);
    CHECK(feed(&o, 15681.0f, 100u, p) == OVS_WARN); /* above 16 000 - 320: held */
    CHECK(feed(&o, 15680.0f, 10u, p) == OVS_NONE);
    CHECK(feed(&o, 15999.0f, 100u, p) == OVS_NONE); /* back below the start: nothing */
    CHECK(feed(&o, -16100.0f, 10u, p) == OVS_WARN); /* both directions of rotation */
    CHECK(feed(&o, -17000.0f, 10u, p) == OVS_TRIP);
    CHECK(feed(&o, 15000.0f, 10u, p) == OVS_NONE); /* straight down: the band the speed asks for */
    CHECK(feed(&o, 18000.0f, 10u, p) == OVS_TRIP); /* straight up */
}

/* The debounce counts consecutive samples of a measured speed: one sample in between restarts it; a speed that is
 * not measured (the resolver invalid), non-finite, or without an n_max holds the band and counts nothing. */
TEST(overspeed_debounce_in_ms_and_no_evidence_without_a_valid_resolver)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    ovs_t o;
    ovs_init(&o);
    CHECK(feed(&o, 17000.0f, 9u, p) == OVS_NONE);
    CHECK(feed(&o, 15000.0f, 1u, p) == OVS_NONE); /* one sample below: the count restarts */
    CHECK(feed(&o, 17000.0f, 9u, p) == OVS_NONE);
    CHECK(feed(&o, 17000.0f, 1u, p) == OVS_TRIP);
    for (uint32_t k = 0u; k < 100u; k++) {
        (void)ovs_step(&o, 0.0f, false, 16000.0f, p); /* the resolver invalid: its speed is no evidence */
    }
    CHECK(o.band == OVS_TRIP);
    CHECK(feed(&o, 10000.0f, 9u, p) == OVS_TRIP);
    (void)ovs_step(&o, 10000.0f, false, 16000.0f, p); /* an unmeasured sample restarts the count too */
    CHECK(feed(&o, 10000.0f, 9u, p) == OVS_TRIP);
    CHECK(ovs_step(&o, NAN, true, 16000.0f, p) == OVS_TRIP);
    CHECK(ovs_step(&o, 10000.0f, true, 0.0f, p) == OVS_TRIP);
    CHECK(feed(&o, 10000.0f, 1u, p) == OVS_TRIP && o.n_ms == 1u);
    CHECK(feed(&o, 10000.0f, 9u, p) == OVS_NONE);
}

/* The warning band, end to end, on two motors: the screening motor (n_x 8086 rpm, n_max 16 000 rpm: the band above
 * n_x) and a low-EMF motor (psi 0.05 Wb: n_x 24 258 rpm, n_max 6000 rpm: the band below n_x). §6, the command-lost row
 * (FW-09/FW-33's): the torque ramped to zero, then current control kept above n_x (field weakening, battery present)
 * and SPO below it (the pulses off, the bridge armed: RUN, no FAULT); the speed-limit request on CAN; DTC_OVERSPEED.
 * Back below the warning's exit the command returns, the request clears and the DTC is passed (one occurrence). */
TEST(overspeed_warning_zeroes_the_torque_and_requests_a_speed_limit)
{
    const float psi[2] = {0.15f, 0.05f};
    const float n_max[2] = {16000.0f, 6000.0f};
    const float torque[2] = {50.0f, 20.0f}; /* the harness plant ignores voltage: at 100 N m and psi 0.05 Wb the
                                               * R iq the loop never supplies reads R iq / psi = 166 rad/s el of
                                               * FW-10 rate error */
    for (unsigned c = 0u; c < 2u; c++) {
        const unsigned before = t_fails;
        CHECK(run_motor(psi[c], n_max[c], 0.97f * n_max[c], torque[c]));
        CHECK(g_app.ovs.band == OVS_NONE && !g_app.speed_limit_req && g_app.t_cmd_nm == torque[c]);
        h_ramp_speed(1.02f * n_max[c], 100u);
        h_run_ms(80u);
        CHECK(g_app.ovs.band == OVS_WARN && fm_active(&g_app.fm, SS_ROW_CMD_LOST) && dtc_active(DTC_OVERSPEED));
        CHECK(!fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && g_app.sm.st == SM_RUN && g_app.t_cmd_nm == 0.0f);
        CHECK(g_app.speed_limit_req && speed_limit_bit());
        if (c == 0u) { /* above n_x: current control kept (field weakening at zero torque) */
            CHECK(g_app.fm.dec.action == SS_ACT_RAMP_KEEP_CC && g_app.br.mode == BR_MOD && g_app.id_ref < -100.0f);
        } else {       /* below n_x: then SPO — the pulses off, the bridge armed */
            CHECK(g_app.fm.dec.action == SS_ACT_RAMP_THEN_SPO && hal_pwm_mode() == HAL_PWM_OFF && g_app.br.mode == BR_IDLE);
        }
        h_ramp_speed(0.97f * n_max[c], 100u);
        h_run_ms(30u);
        CHECK(g_app.ovs.band == OVS_NONE && !fm_active(&g_app.fm, SS_ROW_CMD_LOST) && g_app.t_cmd_nm == torque[c]);
        CHECK(!g_app.speed_limit_req && !dtc_active(DTC_OVERSPEED) && dtc_occurrences(DTC_OVERSPEED) == 1u);
        CHECK(hal_pwm_mode() == HAL_PWM_MOD && g_app.sm.st == SM_RUN);
        if (t_fails != before) {
            printf("    ^ n_max %.0f rpm\n", (double)n_max[c]);
        }
    }
}

/* The trip band takes the §6 "Resolver invalid, or control lost" row — the matrix decides, from the speed: LS-ASC
 * at n >= n_x (the screening motor at 17 200 rpm), SPO below it under the energy rule (the low-EMF motor at 6500 rpm,
 * rule (a) holding: no ASC entered, ASC_REQ never raised). Latched: FAULT; the VCU's fault reset clears it only below
 * n_x (refused at 16 000 rpm, accepted at 5000 rpm once the speed is back). */
TEST(overspeed_trip_takes_the_control_lost_row_at_high_and_low_speed)
{
    CHECK(run_motor(0.15f, 16000.0f, 15500.0f, 50.0f));
    h_ramp_speed(17200.0f, 200u);
    h_run_ms(20u);
    CHECK(g_app.ovs.band == OVS_TRIP && fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && dtc_active(DTC_OVERSPEED));
    CHECK(g_app.fm.dec_row == SS_ROW_RESOLVER_INVALID && g_app.fm.dec.high_speed && g_app.fm.dec.action == SS_ACT_LS_ASC);
    CHECK(g_app.br.mode == BR_ASC && hal_pwm_mode() == HAL_PWM_ASC && g_app.sm.st == SM_FAULT);
    h_ramp_speed(16000.0f, 100u);
    H.fault_reset = true;
    h_run_ms(30u);
    CHECK(fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && g_app.sm.st == SM_FAULT); /* not at n >= n_x */

    CHECK(run_motor(0.05f, 6000.0f, 5800.0f, 20.0f));
    const uint32_t asc_entries = g_app.br.n_asc_entries;
    h_ramp_speed(6500.0f, 100u);
    h_run_ms(20u);
    CHECK(g_app.ovs.band == OVS_TRIP && fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && g_app.fm.dec_row == SS_ROW_RESOLVER_INVALID);
    CHECK(!g_app.fm.dec.high_speed && g_app.fm.dec.rule_a && g_app.fm.dec.action == SS_ACT_SPO);
    CHECK(g_app.br.mode == BR_DISARMED && hal_pwm_mode() == HAL_PWM_OFF && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    CHECK(g_app.br.n_asc_entries == asc_entries && !hal_gpio_out_state(HAL_DO_ASC_REQ) && g_app.sm.st == SM_FAULT);
    h_ramp_speed(5000.0f, 200u);
    h_run_ms(20u);
    CHECK(g_app.ovs.band == OVS_NONE && fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && g_app.sm.st == SM_FAULT);
    H.fault_reset = true;
    h_run_ms(30u);
    H.fault_reset = false;
    CHECK(!fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && g_app.sm.st != SM_FAULT);
}

/* One corrupt resolver frame at 15 800 rpm (just under the warning start). (a) A 60 deg angle error for one carrier
 * period, 850 us into the task's millisecond. Until round 23 the observer took it: its speed jumped (17 500-18 800 rpm
 * sampled once by the next task) and its ringing latched the acceleration plausibility within three frames — the
 * resolver withdrawn for the key cycle by one frame. Round 23 (item 8): the frame's innovation is beyond the debounce
 * times the acceleration bound's, so it is one count and kept out of the observer — the speed never moves, nothing
 * latches, no band. (b) A frame at half amplitude: the amplitude window keeps it out of the observer — the speed does not
 * move, and (round 23) one frame is one count of the debounce, not a withdrawn angle. */
TEST(a_single_bad_resolver_sample_never_trips_overspeed)
{
    CHECK(run_motor(0.15f, 16000.0f, 15800.0f, 20.0f));
    h_run_ms(1u);
    h_isr_only_us(850u);
    sim_resolver_glitch(60.0f * DEG);
    h_isr_only_us(100u);
    sim_resolver_glitch(-60.0f * DEG);
    unsigned spiked = 0u;
    bool band = false;
    for (unsigned t = 0u; t < 50u; t++) {
        h_run_ms(1u);
        spiked += (g_app.rslv.valid && (g_app.speed_rpm >= 16800.0f)) ? 1u : 0u;
        band = band || (g_app.ovs.band != OVS_NONE);
    }
    CHECK(spiked == 0u); /* round 23: the bad frame never reaches the speed */
    CHECK(!band && dtc_occurrences(DTC_OVERSPEED) == 0u);
    CHECK(!g_app.rslv.acc_fault && !g_app.rslv.trk_fault && g_app.rslv.valid); /* one count, not a withdrawal */
    CHECK(!fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID) && g_app.sm.st == SM_RUN);

    CHECK(run_motor(0.15f, 16000.0f, 15800.0f, 20.0f));
    H.rslv_amp = 0.5f;
    h_set_speed(15800.0f);
    h_isr_only_us(100u);
    H.rslv_amp = 1.0f;
    h_set_speed(15800.0f);
    bool steady = true;
    for (unsigned t = 0u; t < 50u; t++) {
        h_run_ms(1u);
        steady = steady && (fabsf(g_app.speed_rpm - 15800.0f) < 20.0f) && (g_app.ovs.band == OVS_NONE);
    }
    CHECK(steady && dtc_occurrences(DTC_OVERSPEED) == 0u); /* the observer never saw it: the speed did not move */
    CHECK(g_app.rslv.valid && !fm_active(&g_app.fm, SS_ROW_RESOLVER_INVALID)); /* round 23: one count, the row not raised */
}

/* Arming is refused while the band is active. Key cycle 1 at standstill stores the FW-16 pass; key cycle 2 starts
 * with the low-EMF motor turning at 6100 rpm (the warning band, below n_x: no §9 ASC hold): FW-16 is skipped (the
 * stored pass), precharge, ARMED_ZERO_TORQUE — and MCU_GATE_EN stays low, no FAULT, the speed-limit request on CAN.
 * Below the warning's exit it arms. FW-44 on the way: a key-on while turning adopts no offset. */
TEST(overspeed_refuses_arming_while_active)
{
    sim_nvm_wipe();
    key_on(0.05f, 6000.0f, NULL, 0.0f);
    CHECK(h_to_armed() && g_app.st.s == ST_PASS);
    key_on(0.05f, 6000.0f, NULL, 6100.0f);
    CHECK(g_app.key_cycle == 2u);
    CHECK(h_to_armed() && g_app.st.s == ST_SKIP && g_app.selftest == SM_OK);
    CHECK(g_app.ofs.decided && !g_app.ofs.adopted); /* FW-44: moving at key-on */
    bool armed = false;
    bool steady = true;
    for (unsigned t = 0u; t < 200u; t++) {
        h_run_ms(1u);
        armed = armed || hal_gpio_out_state(HAL_DO_MCU_GATE_EN);
        steady = steady && (g_app.sm.st == SM_ARMED_ZERO_TORQUE) && (g_app.ovs.band == OVS_WARN) && g_app.speed_limit_req;
    }
    CHECK(!armed && steady && g_app.br.mode == BR_DISARMED && dtc_active(DTC_OVERSPEED) && speed_limit_bit());
    h_ramp_speed(5700.0f, 100u);
    h_run_ms(30u);
    CHECK(g_app.ovs.band == OVS_NONE && hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && g_app.br.mode == BR_IDLE);
}

/* ======================= FW-43 run-time statistics ======================= */

TEST(runstats_energy_time_and_maxima_integrate_exactly)
{
    rs_t s;
    rs_init(&s, NULL, 0u);
    CHECK(s.rec.version == RS_LAYOUT_VERSION && s.rec.t_mod_max_c == RS_T_NONE && s.rec.e_mot_j == 0u);
    rs_in_t in = {.on = true, .run = true, .mod = true, .p_w = 220000.0f};
    for (uint32_t k = 0u; k < 1000u; k++) {
        rs_step(&s, &in);
    }
    CHECK(s.rec.e_mot_j == 220000u && s.rec.e_reg_j == 0u && s.rec.t_on_s == 1u && s.rec.t_run_s == 1u);
    in.p_w = -50000.0f; /* regenerating */
    for (uint32_t k = 0u; k < 500u; k++) {
        rs_step(&s, &in);
    }
    CHECK(s.rec.e_mot_j == 220000u && s.rec.e_reg_j == 25000u && s.rec.t_on_s == 1u);
    in.mod = false; /* SPO / ASC: no power counted */
    in.run = false;
    for (uint32_t k = 0u; k < 500u; k++) {
        rs_step(&s, &in);
    }
    CHECK(s.rec.e_mot_j == 220000u && s.rec.e_reg_j == 25000u && s.rec.t_on_s == 2u && s.rec.t_run_s == 1u);
    in.on = false; /* OFF / SAFE_POWERDOWN: nothing */
    in.mod = true;
    for (uint32_t k = 0u; k < 2000u; k++) {
        rs_step(&s, &in);
    }
    CHECK(s.rec.e_reg_j == 25000u && s.rec.t_on_s == 2u);
    /* 0.1 J steps on a total of 10^12 J are all counted (a float total would drop them) */
    s.rec.e_mot_j = 1000000000000ull;
    in = (rs_in_t){.on = true, .mod = true, .p_w = 100.0f};
    for (uint32_t k = 0u; k < 10000u; k++) {
        rs_step(&s, &in);
    }
    CHECK(s.rec.e_mot_j >= 1000000000999ull && s.rec.e_mot_j <= 1000000001000ull);
    in.p_w = NAN;
    rs_step(&s, &in);
    CHECK(s.rec.e_mot_j <= 1000000001000ull);
    /* maxima: valid readings only */
    in = (rs_in_t){.on = true, .t_mod_c = 60.0f, .t_mod_ok = true, .t_cool_c = 40.0f, .t_cool_ok = true, .t_mot_c = 99.0f};
    rs_step(&s, &in);
    in.t_mod_c = 55.0f;
    in.t_cool_c = NAN;
    rs_step(&s, &in);
    CHECK(s.rec.t_mod_max_c == 60.0f && s.rec.t_cool_max_c == 40.0f && s.rec.t_mot_max_c == RS_T_NONE);
    CHECK(rs_wh(3599u) == 0u && rs_wh(3600u) == 1u && rs_wh(360000000000ull) == 100000000u);
}

/* Each new occurrence in the DTC store counts once in its class; a DTC still failed is not a new occurrence; a
 * cleared store (UDS 0x14) restarts the baseline. */
TEST(runstats_dtc_occurrences_count_per_class)
{
    dtc_init();
    rs_t s;
    rs_init(&s, NULL, 0u);
    bool all = true;
    for (uint32_t i = 0u; i < (uint32_t)DTC_COUNT; i++) {
        all = all && (rs_dtc_class((dtc_id_t)i) < RS_CLS_COUNT);
    }
    CHECK(all && rs_dtc_class(DTC_COUNT) == RS_CLS_INFO);
    CHECK(rs_dtc_class(DTC_OVERSPEED) == RS_CLS_LIMIT && rs_dtc_class(DTC_DESAT_HS) == RS_CLS_POWER_STAGE &&
          rs_dtc_class(DTC_HVIL_OPEN) == RS_CLS_VEHICLE && rs_dtc_class(DTC_RSLV_ACCEL) == RS_CLS_SENSOR &&
          rs_dtc_class(DTC_V5GD) == RS_CLS_SUPPLY && rs_dtc_class(DTC_QDIS_STUCK_ON) == RS_CLS_HV_PATH &&
          rs_dtc_class(DTC_CALIB_INVALID) == RS_CLS_INTEGRITY && rs_dtc_class(DTC_LV_OVERVOLTAGE) == RS_CLS_INFO);
    dtc_set(DTC_HVIL_OPEN, 1u);
    rs_count_dtcs(&s);
    dtc_set(DTC_HVIL_OPEN, 2u); /* still failed: not a new occurrence */
    rs_count_dtcs(&s);
    CHECK(s.rec.n_dtc[RS_CLS_VEHICLE] == 1u);
    dtc_pass(DTC_HVIL_OPEN);
    dtc_set(DTC_HVIL_OPEN, 3u);
    dtc_set(DTC_OVERSPEED, 3u);
    dtc_set(DTC_LV_OVERVOLTAGE, 3u);
    rs_count_dtcs(&s);
    CHECK(s.rec.n_dtc[RS_CLS_VEHICLE] == 2u && s.rec.n_dtc[RS_CLS_LIMIT] == 1u && s.rec.n_dtc[RS_CLS_INFO] == 1u);
    dtc_clear_all();
    dtc_set(DTC_HVIL_OPEN, 4u);
    rs_count_dtcs(&s);
    rs_count_dtcs(&s);
    CHECK(s.rec.n_dtc[RS_CLS_VEHICLE] == 3u && s.rec.n_dtc[RS_CLS_POWER_STAGE] == 0u);
}

/* The record is queued every cal_rs_save_s (600 s), once on entering controlled shutdown, and again at once when
 * the queue was full. */
TEST(runstats_record_is_saved_at_a_bounded_rate_and_once_at_shutdown)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    nv_init();
    rs_t s;
    rs_init(&s, NULL, 1000u);
    s.rec.e_mot_j = 12345u;
    rs_persist(&s, false, false, 1000u + 599999u, p);
    CHECK(nv_pending() == 0u);
    rs_persist(&s, false, false, 1000u + 600000u, p);
    CHECK(nv_pending() == 1u);
    rs_persist(&s, false, false, 1000u + 600001u, p);
    CHECK(nv_pending() == 1u);
    nv_drain();
    nv_runtime_t r;
    CHECK(nv_read(NV_REC_RUNTIME, &r, (uint16_t)sizeof r) && r.e_mot_j == 12345u && r.version == RS_LAYOUT_VERSION);
    rs_persist(&s, true, false, 700000u, p); /* entering SAFE_POWERDOWN */
    CHECK(nv_pending() == 1u);
    nv_drain();
    rs_persist(&s, true, false, 700001u, p);
    rs_persist(&s, true, false, 700002u, p);
    CHECK(nv_pending() == 0u); /* once: SAFE_POWERDOWN waits for the queue to drain before LPOFF */
    rs_persist(&s, false, false, 700003u, p);
    rs_persist(&s, true, false, 700004u, p);
    CHECK(nv_pending() == 1u); /* a new shutdown, a new record */
    nv_drain();
    const nv_selftest_t fill = {0};
    for (uint32_t k = 0u; k < NV_QUEUE_DEPTH; k++) {
        (void)nv_queue(NV_REC_SELFTEST, &fill, (uint16_t)sizeof fill);
    }
    rs_persist(&s, false, true, 700005u, p); /* the queue full */
    CHECK(s.pending && nv_pending() == NV_QUEUE_DEPTH);
    nv_drain();
    rs_persist(&s, false, false, 700006u, p);
    CHECK(!s.pending && nv_pending() == 1u);
    nv_drain();
}

/* A scripted drive on the screening motor at 3000 rpm: 2 s at +100 N m, 1 s at -100 N m. The energy drawn is 2 s of
 * T w (62 832 J), the energy returned 1 s of it (31 416 J) — to 1 % (the plant is lossless; I_DC is the loop's
 * own); the run and key-on times 3 s; the maxima follow a module NTC and a motor sensor warming at 10 degC/s and the
 * VCU's 50 degC coolant. The DID reads the same, end to end on the diagnostic bus. */
TEST(runstats_accumulate_a_scripted_drive)
{
    sim_nvm_wipe();
    key_on(0.15f, 16000.0f, NULL, 0.0f);
    sim_set_temp(HAL_ADC_TMOD_V, 40.0f); /* the first samples prime the channels */
    sim_set_temp(HAL_ADC_MT2, 60.0f);
    CHECK(h_to_run(100.0f));
    h_ramp_speed(3000.0f, 600u);
    h_run_ms(20u);
    CHECK(g_app.sm.st == SM_RUN && g_app.t_cmd_nm == 100.0f);
    const nv_runtime_t r0 = g_app.rs.rec;
    const uint32_t ms0 = (r0.t_run_s * 1000u) + g_app.rs.run_ms;
    const double tw = 100.0 * (3000.0 / (double)TI_RPM_PER_RAD_S);
    for (uint32_t k = 1u; k <= 2000u; k++) {
        sim_set_temp(HAL_ADC_TMOD_V, 40.0f + (0.0025f * (float)k)); /* to 45 degC at 2.5 degC/s */
        h_run_ms(1u);
    }
    const nv_runtime_t r1 = g_app.rs.rec;
    H.torque_nm = -100.0f;
    uint32_t n = 0u;
    for (; (n < 150u) && (g_app.t_cmd_nm != -100.0f); n++) {
        h_run_ms(1u); /* the next VCU frame (every 10 ms) carries it; round 23: then it slews (200 N m in 100 ms) */
    }
    const nv_runtime_t r1b = g_app.rs.rec;
    for (uint32_t k = 1u; k <= 1000u; k++) {
        sim_set_temp(HAL_ADC_TMOD_V, 45.0f - (0.002f * (float)k)); /* cooling: the maximum stays */
        h_run_ms(1u);
    }
    const nv_runtime_t r2 = g_app.rs.rec;
    const uint32_t ms2 = (r2.t_run_s * 1000u) + g_app.rs.run_ms;
    CHECK_NEAR((double)(r1.e_mot_j - r0.e_mot_j), 2.0 * tw, 0.01 * 2.0 * tw);
    CHECK(r1.e_reg_j == r0.e_reg_j);
    CHECK(n >= 1u && n <= 112u && (r1b.e_mot_j - r1.e_mot_j) <= (uint64_t)(32u * n)); /* motoring until it arrives */
    CHECK_NEAR((double)(r2.e_reg_j - r1b.e_reg_j), tw, 0.01 * tw);
    CHECK(r2.e_mot_j == r1b.e_mot_j);
    CHECK((ms2 - ms0) == (3000u + n) && (r2.t_on_s - r0.t_on_s) >= 3u && r2.t_on_s >= r2.t_run_s);
    CHECK_NEAR(r2.t_mod_max_c, 45.0, 0.5);
    CHECK_NEAR(r2.t_mot_max_c, 60.0, 1.0);
    CHECK_NEAR(r2.t_cool_max_c, 50.0, 0.01);
    hal_can_frame_t rsp;
    const uint8_t rq[4] = {0x03u, 0x22u, 0xFEu, 0x43u};
    CHECK(diag_request(rq, 8u, &rsp) && rsp.id == UDS_ID_RSP && rsp.len == 64u);
    CHECK(rsp.data[0] == 0u && rsp.data[1] == 61u && rsp.data[2] == 0x62u && rsp.data[3] == 0xFEu && rsp.data[4] == 0x43u);
    const nv_runtime_t *r = &g_app.rs.rec;
    const uint8_t *d = &rsp.data[5];
    CHECK(be(&d[0], 4u) == rs_wh(r->e_mot_j) && be(&d[4], 4u) == rs_wh(r->e_reg_j) && be(&d[8], 4u) == r->t_on_s);
    CHECK(be(&d[12], 4u) == r->t_run_s && be(&d[16], 4u) == g_app.key_cycle && g_app.key_cycle == 1u);
    CHECK(rs_wh(r->e_mot_j) >= 17u && rs_wh(r->e_reg_j) == 8u); /* 17.5 + the speed ramp; 8.7 Wh */
    CHECK(fabs((double)(int16_t)be(&d[20], 2u) - (double)(r->t_mod_max_c * 10.0f)) <= 0.5);
    CHECK((int16_t)be(&d[22], 2u) == 500 && fabs((double)(int16_t)be(&d[24], 2u) - (double)(r->t_mot_max_c * 10.0f)) <= 0.5);
    for (uint32_t k = 0u; k < (uint32_t)RS_CLS_COUNT; k++) {
        CHECK(be(&d[26u + (4u * k)], 4u) == r->n_dtc[k]);
    }
}

/* Key cycle 1 ends with a controlled shutdown: the record queued on entering SAFE_POWERDOWN is written before LPOFF,
 * and key cycle 2 carries on from exactly it. Key cycle 2 drives again and ends with KL30 lost (no SAFE_POWERDOWN):
 * key cycle 3 starts from the last record written — the second drive is lost (the documented caveat). */
TEST(runstats_persist_across_a_key_cycle_and_lose_the_last_interval_on_power_loss)
{
    CHECK(run_motor(0.15f, 16000.0f, 3000.0f, 100.0f));
    h_run_ms(1000u);
    H.enable = false;
    H.torque_nm = 0.0f;
    h_ramp_speed(0.0f, 300u);
    H.contactors = TI_CONT_OPEN;
    h_run_ms(20u);
    sim_set_kl15(0.0f);
    CHECK(h_run_until(SM_SAFE_POWERDOWN, 200u) && h_run_until(SM_OFF, 10000u));
    const nv_runtime_t kc1 = g_app.rs.rec;
    CHECK(kc1.e_mot_j > 30000u && kc1.t_run_s >= 1u);
    key_on(0.15f, 16000.0f, NULL, 0.0f);
    CHECK(g_app.key_cycle == 2u && g_app.rs.rec.e_mot_j == kc1.e_mot_j && g_app.rs.rec.e_reg_j == kc1.e_reg_j);
    CHECK(g_app.rs.rec.t_on_s == kc1.t_on_s && g_app.rs.rec.t_run_s == kc1.t_run_s &&
          g_app.rs.rec.t_mod_max_c == kc1.t_mod_max_c && g_app.rs.rec.t_cool_max_c == kc1.t_cool_max_c);
    CHECK(memcmp(g_app.rs.rec.n_dtc, kc1.n_dtc, sizeof kc1.n_dtc) == 0);
    CHECK(h_to_run(100.0f));
    h_ramp_speed(3000.0f, 600u);
    h_run_ms(1000u);
    const uint64_t lost = g_app.rs.rec.e_mot_j;
    CHECK(lost > (kc1.e_mot_j + 30000u));
    sim_nvm_power_loss(); /* KL30 gone: nothing more is written */
    key_on(0.15f, 16000.0f, NULL, 0.0f);
    CHECK(g_app.key_cycle == 3u && g_app.rs.rec.e_mot_j == kc1.e_mot_j && g_app.rs.rec.e_mot_j < lost);
}

/* The DID handler answers exactly one request: ReadDataByIdentifier 0xFE43 alone, in a single frame (classic or CAN-FD
 * escape) to the diagnostic request ID; its response one CAN-FD single frame. Anything else is left to the UDS
 * server (e.g. SecurityAccess still answered end to end); there is no write service. */
TEST(runstats_did_answers_only_its_own_read)
{
    nv_runtime_t r = {.e_mot_j = 3600u * 1234u, .e_reg_j = 3600u * 56u + 3599u, .t_on_s = 7u, .t_run_s = 5u,
                      .t_mod_max_c = 81.26f, .t_cool_max_c = -12.3f, .t_mot_max_c = RS_T_NONE};
    r.n_dtc[RS_CLS_LIMIT] = 3u;
    hal_can_frame_t rq = {.id = UDS_ID_REQ, .len = 8u, .data = {0x03u, 0x22u, 0xFEu, 0x43u}};
    hal_can_frame_t rsp;
    CHECK(rs_uds_handle(&r, 9u, &rq, &rsp) && rsp.id == UDS_ID_RSP && rsp.len == 64u && rsp.data[1] == 61u);
    const uint8_t *d = &rsp.data[5];
    CHECK(be(&d[0], 4u) == 1234u && be(&d[4], 4u) == 56u && be(&d[8], 4u) == 7u && be(&d[12], 4u) == 5u);
    CHECK(be(&d[16], 4u) == 9u && (int16_t)be(&d[20], 2u) == 813 && (int16_t)be(&d[22], 2u) == -123);
    CHECK(be(&d[24], 2u) == 0x8000u && be(&d[26u + (4u * RS_CLS_LIMIT)], 4u) == 3u && rsp.data[63] == 0xAAu);
    const hal_can_frame_t fd = {.id = UDS_ID_REQ, .len = 12u, .data = {0x00u, 0x03u, 0x22u, 0xFEu, 0x43u}};
    CHECK(rs_uds_handle(&r, 9u, &fd, &rsp) && rsp.data[2] == 0x62u);
    const hal_can_frame_t other[5] = {
        {.id = UDS_ID_REQ, .len = 8u, .data = {0x03u, 0x22u, 0xFEu, 0x44u}},                  /* another DID */
        {.id = UDS_ID_REQ, .len = 8u, .data = {0x05u, 0x22u, 0xFEu, 0x43u, 0xFEu, 0x43u}},    /* two DIDs */
        {.id = UDS_ID_REQ, .len = 8u, .data = {0x07u, 0x2Eu, 0xFEu, 0x43u, 1u, 2u, 3u, 4u}},  /* a write */
        {.id = 0x7E0u, .len = 8u, .data = {0x03u, 0x22u, 0xFEu, 0x43u}},                      /* another ECU */
        {.id = UDS_ID_REQ, .len = 8u, .data = {0x10u, 0x03u, 0x22u, 0xFEu, 0x43u}},           /* not a single frame */
    };
    for (uint32_t k = 0u; k < 5u; k++) {
        CHECK(!rs_uds_handle(&r, 9u, &other[k], &rsp));
    }
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    h_run_ms(5u);
    const uint8_t sa[2] = {0x27u, 0x01u};
    uint8_t req[3] = {0x02u, sa[0], sa[1]};
    CHECK(diag_request(req, 8u, &rsp) && rsp.data[1] == 0x7Fu && rsp.data[2] == 0x27u); /* the UDS server's (NRC) */
}

/* ======================= FW-44 key-on offset refresh ======================= */

/* EOL 2.47 / 2.53 / 2.50 V, tolerance 0.1 V, step 2 mV. */
TEST(offset_tracker_adopts_a_bounded_step_inside_the_tolerance_only)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const isns_cal_t eol[3] = {{2.47f, 2.22e-3f, 1}, {2.53f, 2.22e-3f, -1}, {2.50f, 2.22e-3f, 1}};
    const float at_zero[3] = {2.50f, 2.50f, 2.50f};
    ofs_t o;
    ofs_init(&o, eol, NULL, p);
    CHECK(o.run[0].offset_v == 2.47f && o.run[1].sign == -1 && !o.decided);
    CHECK(ofs_decide(&o, at_zero, eol, true, p) && o.decided && o.adopted);
    CHECK_NEAR(o.run[0].offset_v, 2.472, 1e-6);
    CHECK_NEAR(o.run[1].offset_v, 2.528, 1e-6);
    CHECK_NEAR(o.run[2].offset_v, 2.500, 1e-6);
    CHECK(o.run[0].gain_v_per_a == 2.22e-3f && o.run[1].sign == -1);
    CHECK(!ofs_decide(&o, at_zero, eol, true, p)); /* once per boot */
    CHECK_NEAR(o.run[0].offset_v, 2.472, 1e-6);
    const float rec[3] = {2.49f, 2.51f, 2.50f};
    ofs_init(&o, eol, rec, p);
    CHECK(o.run[0].offset_v == 2.49f && o.run[1].offset_v == 2.51f);
    const float far[3] = {2.58f, 2.51f, 2.50f}; /* 0.11 V from EOL: not credited */
    ofs_init(&o, eol, far, p);
    CHECK(o.run[0].offset_v == 2.47f && o.run[1].offset_v == 2.53f);
    const float nan3[3] = {NAN, 2.51f, 2.50f};
    ofs_init(&o, eol, nan3, p);
    CHECK(o.run[0].offset_v == 2.47f && o.run[1].offset_v == 2.53f);
    const float drift_out[3] = {2.36f, 2.53f, 2.50f}; /* channel U 0.11 V from EOL: the key-on check refuses */
    ofs_init(&o, eol, NULL, p);
    CHECK(!ofs_decide(&o, drift_out, eol, true, p) && o.decided && !o.adopted && o.run[0].offset_v == 2.47f);
    ofs_init(&o, eol, NULL, p);
    CHECK(!ofs_decide(&o, at_zero, eol, false, p) && !o.adopted && o.run[0].offset_v == 2.47f); /* armed / moving */
    const float edge[3] = {2.56f, 2.44f, 2.50f}; /* 0.09 V off, inside: still one step */
    ofs_init(&o, eol, NULL, p);
    CHECK(ofs_decide(&o, edge, eol, true, p));
    CHECK_NEAR(o.run[0].offset_v, 2.472, 1e-6);
    CHECK_NEAR(o.run[1].offset_v, 2.528, 1e-6);
}

/* The sensors read 2.50 V at 0 A against EOL offsets 2.47 / 2.53 / 2.50 V (+30 / -30 / 0 mV of drift, inside the
 * 0.1 V tolerance). Key-on at standstill: the working offsets step 2 mV toward it, the EOL record is untouched, the
 * run-time record holds them for this calibration and key cycle, and the FW-05 hardware compare follows (a code at
 * the EOL-derived trip no longer trips). An MCU reset inside the key cycle keeps them without a second step; the next
 * key cycle takes one more. */
TEST(offset_refresh_at_key_on_rate_limited_per_key_cycle)
{
    const float eol[3] = {2.47f, 2.53f, 2.50f};
    const float step = ti_params_get(TI_SKU_8XX_SIC)->cal_isns_ofs_step_v;
    key_on(0.15f, 16000.0f, eol, 0.0f);
    CHECK(h_run_until(SM_VEHICLE_HANDSHAKE, 1000u));
    CHECK(g_app.offs_ok && g_app.ofs.decided && g_app.ofs.adopted);
    CHECK_NEAR(g_app.ofs.run[0].offset_v, eol[0] + step, 1e-5);
    CHECK_NEAR(g_app.ofs.run[1].offset_v, eol[1] - step, 1e-5);
    CHECK_NEAR(g_app.ofs.run[2].offset_v, 2.50, 1e-3);
    CHECK(g_app.cal.isns[0].offset_v == eol[0] && g_app.cal.isns[1].offset_v == eol[1] &&
          g_app.cal.isns[2].offset_v == eol[2]);
    h_run_ms(10u);
    nv_runtime_t r;
    CHECK(nv_read(NV_REC_RUNTIME, &r, (uint16_t)sizeof r) && r.ofs_key_cycle == 1u && r.cal_crc == h_cal.crc32);
    CHECK(r.ofs_v[0] == g_app.ofs.run[0].offset_v && r.ofs_v[1] == g_app.ofs.run[1].offset_v);
    uint16_t lo_e;
    uint16_t hi_e;
    uint16_t lo_w;
    uint16_t hi_w;
    isns_oc_codes(&g_app.cal.isns[0], h_p.i_oc_trip_a, &lo_e, &hi_e);
    isns_oc_codes(&g_app.ofs.run[0], h_p.i_oc_trip_a, &lo_w, &hi_w);
    CHECK(hi_w > hi_e);
    sim_adc_set_code(HAL_ADC_ISNS_U, hi_e);
    CHECK((hal_adc_watchdog_status() & (1u << HAL_ADC_ISNS_U)) == 0u);
    h_run_ms(1u);
    /* an MCU reset inside the key cycle */
    sim_fs26_reset();
    h_boot();
    CHECK(h_run_until(SM_VEHICLE_HANDSHAKE, 1000u) && g_app.key_cycle == 1u);
    CHECK(g_app.ofs.decided && !g_app.ofs.adopted);
    CHECK_NEAR(g_app.ofs.run[0].offset_v, eol[0] + step, 1e-5);
    /* the next key cycle */
    key_on(0.15f, 16000.0f, eol, 0.0f);
    CHECK(h_run_until(SM_VEHICLE_HANDSHAKE, 1000u) && g_app.key_cycle == 2u && g_app.ofs.adopted);
    CHECK_NEAR(g_app.ofs.run[0].offset_v, eol[0] + (2.0f * step), 1e-5);
    CHECK_NEAR(g_app.ofs.run[1].offset_v, eol[1] - (2.0f * step), 1e-5);
    /* a new calibration record (another CRC): the tracked offsets are not carried over */
    const float eol2[3] = {2.47f, 2.53f, 2.501f};
    key_on(0.15f, 16000.0f, eol2, 0.0f);
    CHECK(h_run_until(SM_VEHICLE_HANDSHAKE, 1000u) && g_app.ofs.adopted);
    CHECK_NEAR(g_app.ofs.run[0].offset_v, eol2[0] + step, 1e-5);
}

/* Drift beyond the tolerance (+120 / -120 / 0 mV): the key-on check refuses to arm exactly as before (SENSOR_SELFTEST
 * times out: FAULT, MCU_GATE_EN never high) and nothing is adopted or recorded. */
TEST(offset_outside_the_tolerance_still_refuses_and_adopts_nothing)
{
    const float eol[3] = {2.38f, 2.62f, 2.50f};
    sim_nvm_wipe();
    key_on(0.15f, 16000.0f, eol, 0.0f);
    bool armed = false;
    for (unsigned t = 0u; t < 1500u; t++) {
        h_run_ms(1u);
        armed = armed || hal_gpio_out_state(HAL_DO_MCU_GATE_EN);
    }
    CHECK(!g_app.offs_ok && g_app.sm.st == SM_FAULT && !armed);
    CHECK(g_app.ofs.decided && !g_app.ofs.adopted);
    CHECK(g_app.ofs.run[0].offset_v == eol[0] && g_app.ofs.run[1].offset_v == eol[1] && g_app.ofs.run[2].offset_v == eol[2]);
    nv_runtime_t r;
    CHECK(!nv_read(NV_REC_RUNTIME, &r, (uint16_t)sizeof r) || (r.ofs_key_cycle == 0u));
}

/* Never while moving or armed, never mid-run: a key-on with the rotor at 1000 rpm (above FW-16's standstill) adopts
 * nothing; a decision re-opened while the bridge is armed, or with the PWM on, refuses. */
TEST(no_offset_adoption_while_moving_or_armed)
{
    const float eol[3] = {2.47f, 2.53f, 2.50f};
    sim_nvm_wipe();
    key_on(0.15f, 16000.0f, eol, 1000.0f);
    h_run_ms(300u);
    CHECK(g_app.rslv.valid && g_app.ofs.decided && !g_app.ofs.adopted && g_app.ofs.run[0].offset_v == eol[0]);
    sim_nvm_wipe();
    key_on(0.15f, 16000.0f, eol, 0.0f);
    CHECK(h_to_run(50.0f));
    CHECK(g_app.ofs.adopted);
    const float held = g_app.ofs.run[0].offset_v;
    g_app.rs.rec.ofs_key_cycle = 0u; /* re-open the decision while armed and modulating */
    g_app.ofs.decided = false;
    g_app.ofs.adopted = false;
    h_run_ms(5u);
    CHECK(g_app.br.mode == BR_MOD && g_app.ofs.decided && !g_app.ofs.adopted && g_app.ofs.run[0].offset_v == held);
    H.enable = false;
    H.torque_nm = 0.0f;
    h_run_ms(100u); /* armed at standstill, the PWM off */
    g_app.ofs.decided = false;
    g_app.ofs.adopted = false;
    h_run_ms(5u);
    CHECK(g_app.br.mode == BR_IDLE && hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && !g_app.ofs.adopted &&
          g_app.ofs.run[0].offset_v == held);
}

void suite_fw42_44(void)
{
    RUN(round23_cal_defaults_and_ranges);
    RUN(overspeed_warning_trip_recovery_with_hysteresis);
    RUN(overspeed_debounce_in_ms_and_no_evidence_without_a_valid_resolver);
    RUN(overspeed_warning_zeroes_the_torque_and_requests_a_speed_limit);
    RUN(overspeed_trip_takes_the_control_lost_row_at_high_and_low_speed);
    RUN(a_single_bad_resolver_sample_never_trips_overspeed);
    RUN(overspeed_refuses_arming_while_active);
    RUN(runstats_energy_time_and_maxima_integrate_exactly);
    RUN(runstats_dtc_occurrences_count_per_class);
    RUN(runstats_record_is_saved_at_a_bounded_rate_and_once_at_shutdown);
    RUN(runstats_accumulate_a_scripted_drive);
    RUN(runstats_persist_across_a_key_cycle_and_lose_the_last_interval_on_power_loss);
    RUN(runstats_did_answers_only_its_own_read);
    RUN(offset_tracker_adopts_a_bounded_step_inside_the_tolerance_only);
    RUN(offset_refresh_at_key_on_rate_limited_per_key_cycle);
    RUN(offset_outside_the_tolerance_still_refuses_and_adopts_nothing);
    RUN(no_offset_adoption_while_moving_or_armed);
}
