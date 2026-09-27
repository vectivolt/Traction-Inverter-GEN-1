/* test_commission.c — FW-39 (round 23): motor self-commissioning in the interlocked service mode, end to end on the
 * virtual PMSM (src/platform/host/sim_pmsm.h) behind the simulated card: the routines recover the plant's Rs, Ld, Lq,
 * psi and electrical zero; every precondition refuses the start; every loss aborts a running routine to the normal
 * safe state; the record is written only through FW-20 and only after confirmation; no torque command is accepted. */
#include <string.h>

#include "commission.h"
#include "dtc.h"
#include "harness.h"
#include "nvlog.h"
#include "sim_pmsm.h"
#include "test.h"

/* The plant differs from the record (the screening motor: 0.35 / 0.35 mH, 25 mOhm, 0.15 Wb, zero 0): salient,
 * 16 % less Rs, psi 15 % down, the resolver's electrical zero 0.35 rad (20 deg el) off — every quantity beyond its
 * band, so each needs its confirming run. Its inverter loses 10 % more dead time than the FOC compensates, with a
 * softer knee (tanh, 3 A) than the FOC's linear 5 A band. */
#define P_RS 0.021f
#define P_LD 0.30e-3f
#define P_LQ 0.55e-3f
#define P_PSI 0.128f
#define P_ZERO 0.35f
#define DYNO_RPM 300.0f

static sim_pmsm_cfg_t plant_cfg(void)
{
    const sim_pmsm_cfg_t c = {.rs_ohm = P_RS, .ld_h = P_LD, .lq_h = P_LQ, .psi_wb = P_PSI, .pp = 4u, .rpp = 1u,
                              .zero_rad = P_ZERO, .rslv_reversed = false, .i_knee_a = 3.0f, .j_kgm2 = 0.05f,
                              .noise_a = 0.55f}; /* the inverter's dead time and f_sw: the booted SKU's (boot_armed) */
    return c;
}

static void plant(void) { sim_pmsm_step(H.link_v); }

/* The plant at the rotor angle 0.9 rad; its inverter from the SKU set h_setup loaded (10 % more dead time than the FOC
 * compensates). */
static void plant_init(sim_pmsm_cfg_t c)
{
    c.t_dead_s = 1.1f * (float)h_p.dead_time_ns * 1e-9f;
    c.fsw_hz = (float)h_p.fsw_hz[0];
    sim_pmsm_init(&c, 0.9f);
}

static bool test_key(const uint8_t seed[UDS_SA_LEN], uint8_t key[UDS_SA_LEN])
{
    for (uint32_t i = 0u; i < UDS_SA_LEN; i++) {
        key[i] = (uint8_t)(seed[(i + 1u) % UDS_SA_LEN] ^ (0xA5u + i));
    }
    return true;
}

/* h_setup + the plant + a VCU that reports the vehicle at rest, then armed through the normal path (no evidence,
 * record or self-test is bypassed: h_setup provisions what the EOL rig would). */
static bool boot_armed(ti_sku_t sku, const sim_pmsm_cfg_t *c_or_null)
{
    sim_reset(); /* a power-on: every model, retained RAM and the DTC store fresh (the NVM kept) */
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
    dtc_init();
    h_setup(sku);
    plant_init((c_or_null != NULL) ? *c_or_null : plant_cfg());
    H.plant = plant;
    H.veh_speed_valid = true;
    H.veh_speed_kmh = 0.0f;
    h_boot();
    g_app.uds.key_fn = test_key;
    return h_to_armed();
}

/* One single-frame request; the response the tick sends. */
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

static bool unlock(void)
{
    uint8_t r[8];
    const uint8_t sq[2] = {0x27u, 0x01u};
    if (!uds_req(sq, 2u, r) || (r[1] != 0x67u)) {
        return false;
    }
    if ((r[3] | r[4] | r[5] | r[6]) == 0u) {
        return true; /* already unlocked: the zero seed */
    }
    uint8_t kq[2u + UDS_SA_LEN] = {0x27u, 0x02u};
    (void)test_key(&r[3], &kq[2]);
    return uds_req(kq, (uint8_t)sizeof kq, r) && (r[1] == 0x67u) && (r[2] == 0x02u);
}

/* 0 = positive response, else the NRC (0xFF: no response). */
static uint8_t rc(uint8_t sub, uint16_t rid, const uint8_t *opt, uint8_t n_opt, uint8_t out[3])
{
    uint8_t q[7] = {0x31u, sub, (uint8_t)(rid >> 8), (uint8_t)(rid & 0xFFu)};
    (void)memcpy(&q[4], opt, n_opt);
    uint8_t r[8];
    if (!uds_req(q, (uint8_t)(4u + n_opt), r)) {
        return 0xFFu;
    }
    if ((r[1] == 0x7Fu) && (r[2] == 0x31u)) {
        return r[3];
    }
    if ((r[1] != 0x71u) || (r[2] != sub) || (r[3] != q[2]) || (r[4] != q[3])) {
        return 0xFEu;
    }
    if (out != NULL) {
        out[0] = r[5];
        out[1] = r[6];
        out[2] = r[7];
    }
    return 0u;
}

static uint8_t start(mc_routine_t rt, uint16_t att)
{
    const uint8_t opt[3] = {(uint8_t)rt, (uint8_t)(att >> 8), (uint8_t)(att & 0xFFu)};
    return rc(0x01u, UDS_RID_MC_RUN, opt, 3u, NULL);
}

static bool result(uint8_t idx, uint8_t out[3])
{
    return (rc(0x03u, UDS_RID_MC_RUN, &idx, 1u, out) == 0u) && (out[0] == idx);
}

static float value(mc_qty_t q, bool unc)
{
    static const float UNIT[MC_Q_COUNT] = {MC_UNIT_RS, MC_UNIT_L, MC_UNIT_L, MC_UNIT_PSI, MC_UNIT_ZERO};
    uint8_t o[3];
    const uint8_t idx = (uint8_t)((unc ? 0x30u : 0x20u) + (uint32_t)q);
    return result(idx, o) ? ((float)(((uint32_t)o[1] << 8) | o[2]) * UNIT[q]) : -1.0f;
}

static uint8_t verdict(mc_qty_t q)
{
    uint8_t o[3];
    return result((uint8_t)(0x10u + (uint32_t)q), o) ? o[1] : 0xFFu;
}

/* The tool: polls the results every 50 ms — its heartbeat — until the routine has ended. */
static bool run_to_end(uint32_t max_ms)
{
    for (uint32_t t = 0u; t < max_ms; t += 50u) {
        h_run_ms(49u);
        uint8_t o[3];
        if (!result(0u, o)) {
            return false;
        }
        if (o[1] != (uint8_t)MC_RUNNING) {
            return true;
        }
    }
    return false;
}

static bool run(mc_routine_t rt, uint16_t att)
{
    return unlock() && (start(rt, att) == 0u) && run_to_end(3000u) && (g_mc.st == MC_DONE);
}

/* The dyno changes speed within the resolver's acceleration plausibility. */
static void dyno_ramp(float rpm, uint32_t ms)
{
    const float r0 = sim_pmsm_rpm();
    for (uint32_t k = 1u; k <= ms; k++) {
        sim_pmsm_rotor(SIM_ROTOR_DYNO, r0 + ((rpm - r0) * (float)k / (float)ms));
        h_run_ms(1u);
    }
}

static void lock_rotor(void)
{
    dyno_ramp(0.0f, 150u);
    sim_pmsm_rotor(SIM_ROTOR_LOCKED, 0.0f);
    h_run_ms(50u);
}

static float rel(float x, float ref) { return fabsf(x - ref) / fabsf(ref); }

static bool staged(mc_qty_t q) { return g_mc.q[q].staged; }
static bool pending(mc_qty_t q) { return g_mc.q[q].pending; }

/* The service CALs: every default inside its range; out of range, the relations, NaN — each counted. */
TEST(service_cals_are_range_checked)
{
    CHECK(mc_cal_validate(&MC_CAL_DEFAULT) == 0u);
    mc_cal_t c = MC_CAL_DEFAULT;
    c.hb_timeout_ms = 10u;
    CHECK(mc_cal_validate(&c) == 1u);
    c = MC_CAL_DEFAULT;
    c.rs_i1_a = 50.0f; /* the second level must be >= 1.5 x the first */
    CHECK(mc_cal_validate(&c) == 1u);
    c = MC_CAL_DEFAULT;
    c.hf_bias_a = 30.0f; /* the bias must keep every phase current off zero: >= 2 x the HF amplitude */
    CHECK(mc_cal_validate(&c) == 1u);
    c = MC_CAL_DEFAULT;
    c.dyno_rpm_min = 500.0f;
    CHECK(mc_cal_validate(&c) == 1u);
    c = MC_CAL_DEFAULT;
    c.lock_rad = nanf("");
    CHECK(mc_cal_validate(&c) == 1u);
}

/* Each routine recovers the plant: 8XX SiC (20 kHz current loop, 500 Hz injection, 750 V) and 4XX IGBT (10 kHz,
 * 250 Hz, 400 V). Tolerances stated on the host plant: Rs, Ld, Lq 1 %, psi 0.5 %, the electrical zero 3.5 mrad
 * (0.2 deg el) — achieved <= 0.17 %, <= 0.15 %, <= 0.75 mrad. The results as the tool reads them (UDS, their units),
 * each with its uncertainty; the state machine never leaves ARMED_ZERO_TORQUE and the bridge ends each routine armed
 * idle, the PWM off. */
TEST(the_routines_recover_the_plant_on_sic_and_igbt)
{
    const ti_sku_t sku[2] = {TI_SKU_8XX_SIC, TI_SKU_4XX_IGBT};
    for (uint32_t s = 0u; s < 2u; s++) {
        const unsigned before = t_fails;
        CHECK(boot_armed(sku[s], NULL));
        CHECK(run(MC_RT_RS, MC_ATTEST_LOCKED));
        CHECK(verdict(MC_Q_RS) == (uint8_t)MC_V_VALID);
        CHECK(rel(value(MC_Q_RS, false), P_RS) < 0.01f);
        CHECK((value(MC_Q_RS, true) > 0.0f) && (value(MC_Q_RS, true) < (0.02f * P_RS)));
        CHECK(g_app.sm.st == SM_ARMED_ZERO_TORQUE && g_app.br.mode == BR_IDLE && hal_pwm_mode() == HAL_PWM_OFF);
        dyno_ramp(DYNO_RPM, 150u);
        h_run_ms(100u);
        CHECK(run(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_FWD));
        CHECK(verdict(MC_Q_PSI) == (uint8_t)MC_V_VALID && verdict(MC_Q_ZERO) == (uint8_t)MC_V_VALID);
        CHECK(rel(value(MC_Q_PSI, false), P_PSI) < 0.005f);
        CHECK_NEAR(value(MC_Q_ZERO, false), P_ZERO, 3.5e-3);
        CHECK(value(MC_Q_ZERO, true) > 0.0f && value(MC_Q_ZERO, true) < 0.01f);
        lock_rotor();
        CHECK(run(MC_RT_LDQ, MC_ATTEST_LOCKED));
        CHECK(verdict(MC_Q_LD) == (uint8_t)MC_V_VALID && verdict(MC_Q_LQ) == (uint8_t)MC_V_VALID);
        CHECK(rel(value(MC_Q_LD, false), P_LD) < 0.01f);
        CHECK(rel(value(MC_Q_LQ, false), P_LQ) < 0.01f);
        CHECK(g_app.sm.st == SM_ARMED_ZERO_TORQUE && g_app.br.mode == BR_IDLE && hal_pwm_mode() == HAL_PWM_OFF);
        CHECK(!dtc_active(DTC_MC_ABORTED) && (g_app.cal.motor.rs_ohm == h_cal.motor.rs_ohm)); /* nothing applied */
        if (t_fails != before) {
            printf("    ^ %s: Rs %.5f Ld %.4g Lq %.4g psi %.5f zero %.5f\n", h_p.name, (double)g_mc.q[MC_Q_RS].value,
                   (double)g_mc.q[MC_Q_LD].value, (double)g_mc.q[MC_Q_LQ].value, (double)g_mc.q[MC_Q_PSI].value,
                   (double)g_mc.q[MC_Q_ZERO].value);
        }
    }
}

/* Ld and Lq are the eigenvalues of the measured inductance in the frame of the zero used: exact within 45 deg el
 * of the truth, but which axis is d only the back-EMF can say. With the record's zero 20 deg off and no dyno run
 * this key cycle the saliency axis disagrees with it: AXES, nothing pending or staged. With the record's zero right,
 * the same routine is VALID without the dyno. */
TEST(ld_lq_axes_come_from_the_zero_and_a_wrong_one_is_reported)
{
    CHECK(boot_armed(TI_SKU_8XX_SIC, NULL));
    CHECK(run(MC_RT_LDQ, MC_ATTEST_LOCKED));
    CHECK(verdict(MC_Q_LD) == (uint8_t)MC_V_AXES && verdict(MC_Q_LQ) == (uint8_t)MC_V_AXES);
    CHECK(!pending(MC_Q_LD) && !staged(MC_Q_LD) && !pending(MC_Q_LQ) && !staged(MC_Q_LQ));
    CHECK(rel(g_mc.q[MC_Q_LD].value, P_LD) < 0.01f && rel(g_mc.q[MC_Q_LQ].value, P_LQ) < 0.01f);
    sim_pmsm_cfg_t c = plant_cfg();
    c.zero_rad = 0.0f; /* the record's zero is the truth */
    CHECK(boot_armed(TI_SKU_8XX_SIC, &c));
    CHECK(run(MC_RT_LDQ, MC_ATTEST_LOCKED));
    CHECK(verdict(MC_Q_LD) == (uint8_t)MC_V_VALID && verdict(MC_Q_LQ) == (uint8_t)MC_V_VALID);
    CHECK(rel(g_mc.q[MC_Q_LD].value, P_LD) < 0.01f && rel(g_mc.q[MC_Q_LQ].value, P_LQ) < 0.01f);
}

/* Confirmation: a result beyond its band of the record waits for a second run that agrees within confirm_k combined
 * uncertainties; one that disagrees replaces the pending one; one inside the band is staged at once. The back-EMF
 * run confirmed in the other direction stages psi and the zero at their mean. */
TEST(a_change_beyond_its_band_needs_a_confirming_run)
{
    CHECK(boot_armed(TI_SKU_8XX_SIC, NULL));
    CHECK(run(MC_RT_RS, MC_ATTEST_LOCKED));
    uint8_t o[3];
    CHECK(pending(MC_Q_RS) && !staged(MC_Q_RS) && result(0x10u, o) && o[1] == (uint8_t)MC_V_VALID && o[2] == 0x05u);
    sim_pmsm_cfg_t c = plant_cfg();
    c.rs_ohm = 0.8f * P_RS; /* another machine (or a bad contact) since: the second run disagrees */
    plant_init(c);
    CHECK(run(MC_RT_RS, MC_ATTEST_LOCKED));
    CHECK(pending(MC_Q_RS) && !staged(MC_Q_RS) && rel(g_mc.q[MC_Q_RS].pend_value, 0.8f * P_RS) < 0.01f);
    CHECK(run(MC_RT_RS, MC_ATTEST_LOCKED)); /* the third agrees with the second */
    CHECK(staged(MC_Q_RS) && !pending(MC_Q_RS) && rel(g_mc.q[MC_Q_RS].staged_value, 0.8f * P_RS) < 0.01f);
    CHECK(result(0x10u, o) && o[2] == 0x06u && result(0x01u, o) && o[2] == 0x01u);
    dyno_ramp(DYNO_RPM, 150u);
    h_run_ms(100u);
    CHECK(run(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_FWD));
    CHECK(pending(MC_Q_PSI) && pending(MC_Q_ZERO) && !staged(MC_Q_PSI) && !staged(MC_Q_ZERO));
    dyno_ramp(-DYNO_RPM, 300u);
    h_run_ms(100u);
    CHECK(run(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_REV));
    CHECK(staged(MC_Q_PSI) && staged(MC_Q_ZERO) && !pending(MC_Q_PSI) && !pending(MC_Q_ZERO));
    CHECK(rel(g_mc.q[MC_Q_PSI].staged_value, P_PSI) < 0.005f);
    CHECK_NEAR(g_mc.q[MC_Q_ZERO].staged_value, P_ZERO, 3.5e-3);
    /* inside the band: staged by one run */
    c = plant_cfg();
    c.rs_ohm = 0.98f * h_cal.motor.rs_ohm;
    c.zero_rad = 0.0f;
    CHECK(boot_armed(TI_SKU_8XX_SIC, &c));
    CHECK(run(MC_RT_RS, MC_ATTEST_LOCKED));
    CHECK(staged(MC_Q_RS) && !pending(MC_Q_RS) && !g_mc.q[MC_Q_RS].beyond);
}

/* FW-20 only, and only after confirmation: nothing staged => the commit is refused (NVM untouched); the commit needs a
 * fresh unlock; it seals the active record with the staged values (CRC, ranges, SKU, serial = the device UID, motor
 * ID), queues it as a new NV_REC_CALIB version and records DTC_MC_CAL_WRITTEN; the running key cycle keeps its
 * record. The next power-up validates the new one (FW-20) and arms with it; on another card it is refused. */
TEST(the_record_is_written_only_through_fw20_after_confirmation)
{
    CHECK(boot_armed(TI_SKU_8XX_SIC, NULL));
    calib_t rd;
    CHECK(!nv_read(NV_REC_CALIB, &rd, (uint16_t)sizeof rd));
    CHECK(unlock() && rc(0x01u, UDS_RID_MC_COMMIT, NULL, 0u, NULL) == UDS_NRC_CONDITIONS);
    CHECK(run(MC_RT_RS, MC_ATTEST_LOCKED) && pending(MC_Q_RS));
    CHECK(unlock() && rc(0x01u, UDS_RID_MC_COMMIT, NULL, 0u, NULL) == UDS_NRC_CONDITIONS); /* pending only */
    h_run_ms(20u);
    CHECK(!nv_read(NV_REC_CALIB, &rd, (uint16_t)sizeof rd) && !dtc_active(DTC_MC_CAL_WRITTEN));
    CHECK(run(MC_RT_RS, MC_ATTEST_LOCKED) && staged(MC_Q_RS));
    dyno_ramp(DYNO_RPM, 150u);
    h_run_ms(100u);
    CHECK(run(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_FWD) && run(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_FWD));
    lock_rotor();
    CHECK(run(MC_RT_LDQ, MC_ATTEST_LOCKED) && run(MC_RT_LDQ, MC_ATTEST_LOCKED));
    CHECK(staged(MC_Q_PSI) && staged(MC_Q_ZERO) && staged(MC_Q_LD) && staged(MC_Q_LQ));
    CHECK(rc(0x01u, UDS_RID_MC_COMMIT, NULL, 0u, NULL) == UDS_NRC_SECURITY_DENIED); /* the start used the unlock */
    H.enable = true; /* driving: the bridge switches — no commit (its record copy is a PRIMASK section) */
    H.torque_nm = 20.0f;
    CHECK(h_run_until(SM_RUN, 200u) && unlock());
    CHECK(rc(0x01u, UDS_RID_MC_COMMIT, NULL, 0u, NULL) == UDS_NRC_CONDITIONS && !dtc_active(DTC_MC_CAL_WRITTEN));
    H.enable = false;
    H.torque_nm = 0.0f;
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 500u));
    h_run_ms(5u);
    CHECK(g_app.br.mode == BR_IDLE && rc(0x01u, UDS_RID_MC_COMMIT, NULL, 0u, NULL) == 0u);
    CHECK(dtc_active(DTC_MC_CAL_WRITTEN) && g_mc.committed && !g_app.uds.unlocked);
    CHECK(memcmp(&g_app.cal, &h_cal, sizeof h_cal) == 0); /* never applied in this key cycle */
    CHECK(unlock() && rc(0x01u, UDS_RID_MC_COMMIT, NULL, 0u, NULL) == UDS_NRC_CONDITIONS); /* nothing staged now */
    h_run_ms(20u);
    CHECK(nv_read(NV_REC_CALIB, &rd, (uint16_t)sizeof rd) && calib_check(&rd, &h_p, h_serial()) == 0u);
    CHECK(rel(rd.motor.rs_ohm, P_RS) < 0.01f && rel(rd.motor.ld_h, P_LD) < 0.01f && rel(rd.motor.lq_h, P_LQ) < 0.01f);
    CHECK(rel(rd.motor.psi_wb, P_PSI) < 0.005f && fabsf(rd.rslv.zero_rad - P_ZERO) < 3.5e-3f);
    CHECK(rd.motor_id == h_cal.motor_id && rd.mtpa.n == 0u && memcmp(rd.hw_serial, h_serial(), 8u) == 0);
    /* the next key cycle: retained RAM lost, the NVM kept; the record read from NVM (as the target does) */
    sim_reset();
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    plant_init(plant_cfg());
    H.plant = plant;
    H.veh_speed_valid = true;
    app_init(&g_app, &h_p, NULL, h_serial());
    sim_set_fault_isr(app_fault_isr_entry);
    CHECK(g_app.cal_err == 0u && g_app.init == SM_OK && memcmp(&g_app.cal, &rd, sizeof rd) == 0);
    CHECK(g_app.n_x_rpm > motor_n_x_rpm(&h_cal.motor, &h_p)); /* the new psi: the new column split */
    CHECK(h_to_armed());
    /* another card: the record is sealed to this one's UID */
    sim_reset();
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    const uint8_t other[8] = {'T', 'I', '-', '0', '0', '0', '0', '2'};
    app_init(&g_app, &h_p, NULL, other);
    CHECK(((g_app.cal_err & CAL_ERR_SERIAL) != 0u) && g_app.init == SM_FAIL && dtc_active(DTC_CALIB_INVALID));
}

/* Every precondition refuses the start (NRC 0x22 with its reason in results index 0, nothing started, the unlock
 * kept), each alone; the SecurityAccess is the only entry (0x33), malformed requests are refused; the service
 * records (DTC_MC_ABORTED, DTC_SERVICE_LOCK_CLEARED) are not "active DTCs". */
static uint8_t refused(mc_routine_t rt, uint16_t att)
{
    const uint8_t nrc = start(rt, att);
    uint8_t o[3] = {0u, 0u, 0u};
    const bool idle = (g_mc.st != MC_RUNNING) && !g_mc.run && result(0u, o);
    return idle ? ((nrc == UDS_NRC_CONDITIONS) ? o[2] : (uint8_t)(0x80u | nrc)) : 0xFFu;
}

TEST(every_precondition_refuses_the_start)
{
    CHECK(boot_armed(TI_SKU_8XX_SIC, NULL));
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (0x80u | UDS_NRC_SECURITY_DENIED));
    CHECK(unlock());
    CHECK(refused(MC_RT_NONE, MC_ATTEST_LOCKED) == (0x80u | UDS_NRC_OUT_OF_RANGE));
    CHECK(refused(MC_RT_COUNT, MC_ATTEST_LOCKED) == (0x80u | UDS_NRC_OUT_OF_RANGE));
    const uint8_t short_opt[2] = {(uint8_t)MC_RT_RS, 0x4Cu};
    CHECK(rc(0x01u, UDS_RID_MC_RUN, short_opt, 2u, NULL) == UDS_NRC_LENGTH);
    CHECK(rc(0x04u, UDS_RID_MC_RUN, NULL, 0u, NULL) == UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    CHECK(rc(0x02u, UDS_RID_MC_RUN, NULL, 0u, NULL) == UDS_NRC_SEQUENCE); /* nothing to stop */
    CHECK(refused(MC_RT_RS, MC_ATTEST_DYNO_FWD) == (uint8_t)MC_R_ATTEST);
    CHECK(refused(MC_RT_LDQ, 0x0000u) == (uint8_t)MC_R_ATTEST);
    CHECK(refused(MC_RT_PSI_ZERO, MC_ATTEST_LOCKED) == (uint8_t)MC_R_ATTEST);
    mc_cal_t bad = MC_CAL_DEFAULT;
    bad.hb_timeout_ms = 5000u;
    g_mc.cal = &bad;
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_CAL);
    g_mc.cal = &MC_CAL_DEFAULT;
    g_app.evidence = (uint8_t)(ARM_EV_ALL & (uint8_t)~ARM_EV_OVP_ROUTE_VALIDATED);
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_EVIDENCE);
    g_app.evidence = ARM_EV_ALL;
    g_app.cal_err = CAL_ERR_RANGE;
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_EVIDENCE);
    g_app.cal_err = 0u;
    const fm_ctx_t fc = {.speed_known = true, .battery_present = true, .vdc_v = 750.0f, .now_ms = hal_time_ms()};
    fm_raise(&g_app.fm, SS_ROW_CMD_LOST, false, &fc, &g_app.cal.motor, g_app.p); /* cleared by the tick's detect() */
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_FAULT);
    dtc_set(DTC_TEMP_BOARD, hal_time_ms());
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_DTC);
    dtc_pass(DTC_TEMP_BOARD);
    H.enable = true; /* the VCU asks torque (enable), even at 0 Nm */
    h_run_ms(20u);
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_VCU);
    H.enable = false;
    H.veh_speed_valid = false;
    h_run_ms(20u);
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_VEHICLE_SPEED);
    H.veh_speed_valid = true;
    H.veh_speed_kmh = 3.0f;
    h_run_ms(20u);
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_VEHICLE_SPEED);
    H.veh_speed_kmh = 0.0f;
    H.v_pack = 480.0f; /* below the 8XX window (500 V) */
    h_run_ms(50u);
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_HV);
    H.v_pack = 750.0f;
    h_run_ms(50u);
    CHECK(refused(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_FWD) == (uint8_t)MC_R_SPEED); /* the dyno is not turning */
    dyno_ramp(DYNO_RPM, 150u);
    h_run_ms(50u);
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_SPEED); /* the "locked" rotor turns */
    dyno_ramp(600.0f, 100u);
    h_run_ms(50u);
    CHECK(refused(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_FWD) == (uint8_t)MC_R_SPEED); /* above the dyno window */
    lock_rotor();
    dtc_set(DTC_MC_ABORTED, hal_time_ms()); /* records, not failures */
    dtc_set(DTC_SERVICE_LOCK_CLEARED, hal_time_ms());
    CHECK(start(MC_RT_RS, MC_ATTEST_LOCKED) == 0u && g_mc.st == MC_RUNNING); /* every precondition met */
    CHECK(rc(0x02u, UDS_RID_MC_RUN, NULL, 0u, NULL) == 0u && g_mc.st == MC_ABORTED && g_mc.reason == MC_R_STOPPED);
    CHECK(unlock());
    H.enable = true; /* RUN: the bridge modulates for the VCU — armed, but not through ARMED_ZERO_TORQUE */
    H.torque_nm = 20.0f;
    CHECK(h_run_until(SM_RUN, 200u));
    CHECK(refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_STATE);
    H.enable = false;
    H.torque_nm = 0.0f;
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 500u));
    H.contactors = TI_CONT_OPEN; /* the normal opening at zero torque: disarmed, PRECHARGE_WAIT */
    h_run_ms(50u);
    CHECK(g_app.sm.st != SM_ARMED_ZERO_TORQUE && refused(MC_RT_RS, MC_ATTEST_LOCKED) == (uint8_t)MC_R_STATE);
}

/* A running routine aborts on each loss, to the normal safe state: the service modulation ends in the tick that sees
 * it (the bridge armed idle, the PWM off — or the §6 decision's when a fault row caused it), DTC_MC_ABORTED (not for
 * the tool's stop), no result, nothing staged. */
typedef enum {
    L_VSPEED = 0, L_DTC, L_HEARTBEAT, L_HV, L_FAULT, L_TORQUE, L_STOP, L_MOVED, L_CREEP, L_DYNO, L_COUNT
} loss_t;

TEST(a_running_routine_aborts_on_every_loss)
{
    static const mc_reason_t WANT[L_COUNT] = {MC_R_VEHICLE_SPEED, MC_R_DTC, MC_R_HEARTBEAT, MC_R_HV, MC_R_FAULT,
                                              MC_R_VCU, MC_R_STOPPED, MC_R_MOVED, MC_R_MOVED, MC_R_SPEED};
    static const uint32_t WITHIN_MS[L_COUNT] = {12u, 2u, 205u, 60u, 130u, 12u, 2u, 30u, 200u, 30u};
    for (uint32_t l = 0u; l < (uint32_t)L_COUNT; l++) {
        const unsigned before = t_fails;
        sim_pmsm_cfg_t c = plant_cfg();
        c.j_kgm2 = (l == (uint32_t)L_CREEP) ? 1.5f : 0.05f; /* L_CREEP: a heavy free rotor, below lock_rpm */
        CHECK(boot_armed(TI_SKU_8XX_SIC, &c));
        const bool dyno = (l == (uint32_t)L_DYNO);
        if (dyno) {
            dyno_ramp(DYNO_RPM, 150u);
            h_run_ms(100u);
        }
        CHECK(unlock() && start(dyno ? MC_RT_PSI_ZERO : MC_RT_RS, dyno ? MC_ATTEST_DYNO_FWD : MC_ATTEST_LOCKED) == 0u);
        uint8_t o[3];
        CHECK(result(0u, o));
        h_run_ms(80u);
        CHECK(result(0u, o) && o[1] == (uint8_t)MC_RUNNING);
        const float th0 = sim_pmsm_theta_e();
        switch ((loss_t)l) {
        case L_VSPEED: H.veh_speed_kmh = 2.0f; break;
        case L_DTC: sim_adc_set_v(HAL_ADC_NTC_A, 4.99f); break; /* a board NTC open: DTC_TEMP_BOARD (round 23: monitored) */
        case L_HEARTBEAT: break; /* the tool stops polling */
        case L_HV: H.v_pack = 480.0f; break;
        case L_FAULT: sim_hvil_set(SIM_HVIL_OPEN); break;
        case L_TORQUE: H.enable = true; H.torque_nm = 100.0f; break;
        case L_STOP: CHECK(rc(0x02u, UDS_RID_MC_RUN, NULL, 0u, NULL) == 0u); break;
        case L_MOVED:
        case L_CREEP: sim_pmsm_rotor(SIM_ROTOR_FREE, 0.0f); break; /* the rig's brake released */
        default: sim_pmsm_rotor(SIM_ROTOR_DYNO, 1.1f * DYNO_RPM); break; /* the dyno's speed steps 10 % */
        }
        uint32_t t = 0u;
        for (; (t < 400u) && (g_mc.st == MC_RUNNING); t++) {
            h_run_ms(1u);
        }
        CHECK(g_mc.st == MC_ABORTED && g_mc.reason == WANT[l] && t <= WITHIN_MS[l]);
        h_run_ms(2u);
        CHECK(!g_mc.run && (hal_pwm_mode() != HAL_PWM_MOD || fm_any(&g_app.fm)));
        CHECK((l == (uint32_t)L_FAULT) || (g_app.br.mode == BR_IDLE));
        CHECK(dtc_active(DTC_MC_ABORTED) == (l != (uint32_t)L_STOP));
        CHECK(!staged(MC_Q_RS) && !pending(MC_Q_RS) && g_mc.q[MC_Q_RS].verdict == (uint8_t)MC_V_NONE);
        CHECK(!staged(MC_Q_PSI) && !pending(MC_Q_ZERO));
        CHECK(result(0u, o) && o[1] == (uint8_t)MC_ABORTED && o[2] == (uint8_t)WANT[l]);
        if ((l == (uint32_t)L_MOVED) || (l == (uint32_t)L_CREEP)) { /* caught within the lock band, not later */
            CHECK(fabsf(ti_wrap_pi(sim_pmsm_theta_e() - th0)) < (1.5f * MC_CAL_DEFAULT.lock_rad));
        }
        if (t_fails != before) {
            printf("    ^ loss %u: st %u reason %u after %u ms\n", (unsigned)l, (unsigned)g_mc.st,
                   (unsigned)g_mc.reason, (unsigned)t);
        }
    }
}

/* No torque command is accepted in service mode: a torque value without enable changes nothing (the routine runs to
 * its end at zero torque command, the state machine in ARMED_ZERO_TORQUE, the currents the routine's); an enable ends
 * the routine in the tick that sees it and is never executed — the state machine stays in ARMED_ZERO_TORQUE, no
 * torque current, until the VCU withdraws the enable; a new enable after that is normal operation (RUN). */
TEST(no_torque_command_is_accepted_in_service_mode)
{
    CHECK(boot_armed(TI_SKU_8XX_SIC, NULL));
    H.torque_nm = 150.0f; /* no enable */
    CHECK(unlock() && start(MC_RT_RS, MC_ATTEST_LOCKED) == 0u);
    float t_max = 0.0f;
    float i_max = 0.0f;
    bool run_seen = false;
    for (uint32_t t = 0u; (t < 1000u) && (g_mc.st == MC_RUNNING); t++) {
        h_run_ms(1u);
        if ((t % 50u) == 49u) {
            uint8_t o[3];
            (void)result(0u, o);
        }
        float id;
        float iq;
        sim_pmsm_idq(&id, &iq);
        t_max = fmaxf(t_max, fabsf(g_app.t_cmd_nm));
        i_max = fmaxf(i_max, sqrtf((id * id) + (iq * iq)));
        run_seen = run_seen || (g_app.sm.st == SM_RUN);
    }
    CHECK(g_mc.st == MC_DONE && t_max == 0.0f && !run_seen && i_max < (1.2f * MC_CAL_DEFAULT.rs_i2_a));
    CHECK(unlock() && start(MC_RT_RS, MC_ATTEST_LOCKED) == 0u);
    h_run_ms(60u);
    H.enable = true;
    for (uint32_t t = 0u; (t < 30u) && (g_mc.st == MC_RUNNING); t++) {
        CHECK(g_app.sm.st == SM_ARMED_ZERO_TORQUE && g_app.t_cmd_nm == 0.0f);
        h_run_ms(1u);
    }
    CHECK(g_mc.st == MC_ABORTED && g_mc.reason == MC_R_VCU && !g_mc.run);
    h_run_ms(300u); /* the VCU keeps asking */
    CHECK(g_app.sm.st == SM_ARMED_ZERO_TORQUE && g_app.t_cmd_nm == 0.0f && hal_pwm_mode() != HAL_PWM_MOD);
    H.enable = false;
    h_run_ms(30u);
    CHECK(!mc_torque_barred());
    H.enable = true; /* a new request, outside service mode */
    CHECK(h_run_until(SM_RUN, 100u));
}

/* The resolver direction: a resolver turning against the phase sequence (sin/cos swapped) is DIR_PHASES; a resolver
 * right but turning against the attested dyno direction is DIR_DYNO; neither is pending or staged, and the zero
 * does not become the LDQ routine's axis. Attested right, the reverse run is VALID. */
TEST(the_resolver_direction_is_checked_against_the_phases_and_the_dyno)
{
    sim_pmsm_cfg_t c = plant_cfg();
    c.rslv_reversed = true;
    CHECK(boot_armed(TI_SKU_8XX_SIC, &c));
    dyno_ramp(DYNO_RPM, 150u);
    h_run_ms(100u);
    CHECK(g_app.speed_rpm < -250.0f); /* the resolver says backwards */
    CHECK(run(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_REV));
    CHECK(verdict(MC_Q_PSI) == (uint8_t)MC_V_DIR_PHASES && verdict(MC_Q_ZERO) == (uint8_t)MC_V_DIR_PHASES);
    CHECK(!pending(MC_Q_ZERO) && !staged(MC_Q_ZERO) && !pending(MC_Q_PSI) && !g_mc.eps_valid);
    CHECK(boot_armed(TI_SKU_8XX_SIC, NULL));
    dyno_ramp(-DYNO_RPM, 300u);
    h_run_ms(100u);
    CHECK(run(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_FWD));
    CHECK(verdict(MC_Q_PSI) == (uint8_t)MC_V_DIR_DYNO && verdict(MC_Q_ZERO) == (uint8_t)MC_V_DIR_DYNO);
    CHECK(!pending(MC_Q_ZERO) && !staged(MC_Q_ZERO) && !g_mc.eps_valid);
    CHECK(run(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_REV));
    CHECK(verdict(MC_Q_PSI) == (uint8_t)MC_V_VALID && verdict(MC_Q_ZERO) == (uint8_t)MC_V_VALID);
    CHECK_NEAR(value(MC_Q_ZERO, false), P_ZERO, 3.5e-3);
}

/* The FW-20 class limits (calib_check's own ranges): a machine with psi = 0.55 Wb (above 0.5) is measured — VALID as a
 * measurement — but CLASS as a record value: never pending, never staged, the commit has nothing to write. The record's
 * 0.15 Wb leaves the loop's feed-forward 50 V short when the routine starts at speed: a start transient of about
 * 21 A (delta psi x w_e / k_p) that the default 20 A bound refuses (CURRENT, the modulation off) — then the
 * range-checked margin is raised. */
TEST(a_value_outside_the_fw20_class_is_never_staged)
{
    sim_pmsm_cfg_t c = plant_cfg();
    c.psi_wb = 0.55f;
    CHECK(boot_armed(TI_SKU_8XX_SIC, &c));
    dyno_ramp(DYNO_RPM, 150u);
    h_run_ms(100u);
    CHECK(unlock() && start(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_FWD) == 0u && run_to_end(1000u)); /* the default margin */
    CHECK(g_mc.st == MC_ABORTED && g_mc.reason == MC_R_CURRENT && !g_mc.run && dtc_active(DTC_MC_ABORTED));
    h_run_ms(2u);
    CHECK(hal_pwm_mode() != HAL_PWM_MOD && g_app.br.mode == BR_IDLE);
    static mc_cal_t cal;
    cal = MC_CAL_DEFAULT;
    cal.i_margin_a = 60.0f;
    g_mc.cal = &cal;
    CHECK(run(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_FWD) && run(MC_RT_PSI_ZERO, MC_ATTEST_DYNO_FWD));
    CHECK(verdict(MC_Q_PSI) == (uint8_t)MC_V_CLASS && rel(g_mc.q[MC_Q_PSI].value, 0.55f) < 0.005f);
    CHECK(!pending(MC_Q_PSI) && !staged(MC_Q_PSI) && verdict(MC_Q_ZERO) == (uint8_t)MC_V_VALID);
}

void suite_commission(void)
{
    RUN(service_cals_are_range_checked);
    RUN(the_routines_recover_the_plant_on_sic_and_igbt);
    RUN(ld_lq_axes_come_from_the_zero_and_a_wrong_one_is_reported);
    RUN(a_change_beyond_its_band_needs_a_confirming_run);
    RUN(the_record_is_written_only_through_fw20_after_confirmation);
    RUN(every_precondition_refuses_the_start);
    RUN(a_running_routine_aborts_on_every_loss);
    RUN(no_torque_command_is_accepted_in_service_mode);
    RUN(the_resolver_direction_is_checked_against_the_phases_and_the_dyno);
    RUN(a_value_outside_the_fw20_class_is_never_staged);
}
