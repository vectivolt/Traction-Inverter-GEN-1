/* bridge.c — the Traction Tool simulator bridge.
 *
 * The firmware's host build has no free-running loop: its tests drive the public entry points
 * (app_init, app_isr_current, app_isr_fault via the sim's fault hook, app_task_1ms, app_idle) on the host
 * simulation's clock (tests/harness.c). This program does the same, unmodified firmware sources, with:
 *   - the scheduler of the harness: the current-loop ISR on its own exact grid (2 f_sw) and the 1 ms task on
 *     another, in simulated time; triggers that fall inside a long task are dropped (the target would preempt);
 *   - a physical plant (plant.c) in place of the harness's ideal current loop: a voltage-driven PMSM on a dyno,
 *     the DC link with pack/precharge/contactor/bleeder/QDIS, module/board/motor temperatures;
 *   - a VCU + BMS on the vehicle CAN (the firmware's own frame encoders, E2E CRC + alive counters) with the
 *     arm/disarm sequences a bench VCU runs (round 23: VCU_CMD carries the vehicle speed), and the diagnostic bus
 *     through an ISO 15765-2 tester (round 23: segmented responses under its flow control, the periodic frames);
 *   - the EOL/HIL provisioning the harness does (fault route bound, validation record, bound FS26 PROG_ID,
 *     calibration record with a motor ID; round 23: the bench SecurityAccess key), switchable to demonstrate
 *     fail-closed arming;
 *   - fault injection through the sim's own hooks and the plant;
 *   - wall-clock pacing with a time factor, and newline-delimited JSON on stdio (json.h).
 * Protocol: stdin one flat JSON command per line; stdout "hello", "tel", "ack", "log", "can" and (round 23) "periodic"
 * lines (tool/PROTOCOL.md). */
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "verify.h" /* round 23: img_root() for hello.root */
#include "boot.h"
#include "cal_ranges.h"
#include "dtc.h"
#include "json.h"
#include "nvlog.h"
#include "plant.h"
#include "sim.h"
#include "sim_flash.h"
#include "timer.h"
#include "uds.h"
#include "uds_diag.h"

#include "dtc_names.h" /* generated from dtc.h (Makefile) */

_Static_assert((sizeof DTC_NAMES / sizeof DTC_NAMES[0]) == (size_t)DTC_COUNT,
               "dtc_names.h out of step with dtc.h: the Makefile's enum parse missed an entry");

#define TICK_NS 1000000u
#define PROG_ID 0x4A21u
#define QMAX 128
#define CMD_LINE_MAX 4096

static const uint8_t SERIAL[8] = {'T', 'T', '-', 'S', 'I', 'M', '0', '1'};
static const ti_cal_range_t RANGES[] = TI_CAL_RANGES_INIT;

/* ---------------- state ---------------- */
static ti_sku_t s_sku = TI_SKU_8XX_SIC;
static ti_params_t s_p;         /* the live parameter set: g_app.p points here */
static ti_params_t s_p_default; /* the build's generated set for this SKU */
static calib_t s_cal;
static plant_t s_pl;
static struct {
    bool val;   /* EOL/HIL validation record in NVM */
    bool route; /* board configuration binds the FLT route */
    bool otp;   /* cal_fs26_prog_id bound to the procured FS26 */
    bool cal;   /* calibration record bound to a motor */
    bool key;   /* round 23: the bench SecurityAccess key provisioned (as a product build's TI_UDS_KEY_FN) */
} s_prov = {true, true, true, true, true};

typedef enum { VS_IDLE = 0, VS_ARM, VS_PRECHARGE, VS_CLOSED, VS_DISARM } vseq_t;
static const char *const VSEQ[] = {"idle", "arming", "precharge", "closed", "disarming"};
static struct {
    bool send;
    ti_gear_t gear;
    bool enable;
    float torque_nm;     /* the request (the tool's setpoint) */
    float torque_tx_nm;  /* what the frames carry: the request through the VCU's slew limit */
    float slew_nm_s;     /* 0 = none */
    ti_contactor_t cont; /* commanded = reported */
    float coolant_c;
    float p_chg_w, p_dis_w;
    bool vspeed_valid;   /* round 23 (FW-39): the vehicle speed in VCU_CMD (b4 [5], b6-7) */
    float vspeed_kmh;
    uint8_t ctr, bms_ctr;
    vseq_t seq;
    uint64_t seq_t_ms;
    uint64_t fault_reset_until, retry_until, discharge_until;
    bool shutdown;
    const char *note;
} V;

enum {
    INJ_OVERTEMP = 0, INJ_VDC_SENSE, INJ_RESOLVER, INJ_CAN, INJ_HVIL, INJ_V5GD, INJ_LV_OV, INJ_PERSIST_COUNT
};
static const char *const INJ[INJ_PERSIST_COUNT] = {"overtemp", "vdc_sense_loss", "resolver_loss", "can_loss",
                                                   "hvil_open", "v5gd_loss", "lv_overvoltage"};
static bool s_inj[INJ_PERSIST_COUNT];
static uint32_t s_oc_samples;
static float s_overtemp_dt = 60.0f;

static uint64_t s_isr_ns, s_task_ns, s_plant_ns;
static uint64_t s_tick;
static double s_session_ms0; /* session time of this boot's sim start: t_ms is monotonic across reboots */
static uint32_t s_boot;
static double s_factor = 1.0; /* 0 = as fast as possible */
static bool s_paused;
static uint32_t s_tel_ms = 10u;
static double s_last_tel;
static uint64_t s_seq;
static double s_wall0, s_sim0;
static double s_rt = 1.0;
static double s_rt_wall, s_rt_sim;
static uint32_t s_noise = 12345u;
static char s_status_hex[(2u * HAL_CAN_MAX_LEN) + 1u]; /* the last INV_STATUS frame as sent, hex */
#define UDS_QMAX 8
#define UDS_MSG_MAX 4096u
static struct {
    uint32_t n;              /* requests waiting for their response, oldest first */
    double id[UDS_QMAX];     /* the request's "id" (0 = none) */
    uint64_t deadline[UDS_QMAX];
    /* round 23: the response being received — a segmented one (ISO 15765-2: a first frame, then consecutive frames under
     * this tester's flow control) */
    uint8_t msg[UDS_MSG_MAX];
    uint32_t len, got, frames;
    uint8_t sn;
    bool seg;
    char first[(3u * HAL_CAN_MAX_LEN) + 1u]; /* the first frame, hex */
    /* the bridge's own sequences (dtc_clear): the response taken here, not acknowledged */
    bool sync;
    bool sync_got;
    uint8_t sync_msg[64];
    uint32_t sync_len;
} s_uds;
/* the smallest CAN-FD data length (8, 12, 16, 20, 24, 32, 48, 64) holding n bytes, as the firmware's uds_diag.c */
static uint8_t fd_len(uint32_t n)
{
    static const uint8_t L[] = {8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u};
    uint32_t k = 0u;
    while ((k < 7u) && (L[k] < n)) {
        k++;
    }
    return L[k];
}
static bool s_can_tap;       /* every vehicle-bus frame the firmware sends as a "can" line */
static bool s_ext_cmd;       /* VCU_CMD comes from outside (can_rx): the built-in VCU is silent */
static bool s_ext_bms;       /* VCU_BMS comes from outside */
static double s_hold_wall;   /* torque deadman: wall-clock expiry of the last torque command (0 = none) */
static struct {
    double due;
    jin_t o;
} s_q[QMAX];
static int s_qn;

/* ---------------- helpers ---------------- */
static double wall_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec * 1.0e-6);
}

static double sim_ms(void) { return (double)sim_now_ns() * 1.0e-6; }
static double session_ms(void) { return s_session_ms0 + sim_ms(); }
static uint64_t now_ms_u(void) { return sim_now_ns() / 1000000u; }

static float noise_a(void)
{
    s_noise = (s_noise * 1664525u) + 1013904223u;
    return (((float)(s_noise >> 8) / 16777216.0f) - 0.5f) * 1.1f; /* one LSB of the 12-bit current channel */
}

static double session_ms(void);
static void log_line(const char *level, const char *msg)
{
    jo_begin();
    jo_s("type", "log");
    jo_s("level", level);
    jo_d("t_ms", session_ms());
    jo_s("msg", msg);
    jo_end();
}

static void ack(const jin_t *o, bool ok, const char *err)
{
    double id = 0.0;
    const bool have_id = jin_num(o, "id", &id);
    jo_begin();
    jo_s("type", "ack");
    if (have_id) {
        jo_d("id", id);
    }
    const char *c = jin_str(o, "cmd");
    jo_s("cmd", (c != NULL) ? c : "");
    jo_b("ok", ok);
    if (err != NULL) {
        jo_s(ok ? "info" : "err", err);
    }
    jo_d("t_ms", session_ms());
    jo_end();
}

static const char *gear_name(ti_gear_t g)
{
    static const char *const N[] = {"N", "D", "R", "P"};
    return ((uint32_t)g < 4u) ? N[g] : "?";
}

/* ---------------- boot / provisioning ---------------- */
/* Round 23: the bench tester's SecurityAccess key — the simulated card provisioned like a product build (TI_UDS_KEY_FN;
 * the default firmware build has none, every seed request NRC 0x22): key[i] = seed[(i + 1) % 4] ^ (0xA5 + i). */
static bool bench_key(const uint8_t seed[UDS_SA_LEN], uint8_t key[UDS_SA_LEN])
{
    for (uint32_t i = 0u; i < UDS_SA_LEN; i++) {
        key[i] = (uint8_t)(seed[(i + 1u) % UDS_SA_LEN] ^ (0xA5u + i));
    }
    return true;
}

static void resolver_and_temps(void);
static void hex_of(const uint8_t *d, uint32_t n, char *out, bool spaced);
static void provision_params(void)
{
    s_p_default = *ti_params_get(s_sku);
    s_p_default.cal_fs26_prog_id = PROG_ID; /* the default the tool shows: a bound OTP variant */
}

static void power_up(void)
{
    const double t_end = (s_boot == 0u) ? 0.0 : session_ms();
    sim_fs26_config(NULL);
    sim_reset();
    s_session_ms0 = t_end - sim_ms() + ((s_boot == 0u) ? sim_ms() : 0.0);
    if (s_boot == 0u) {
        s_session_ms0 = 0.0;
        sim_nvm_wipe();
    }
    s_boot++;
    s_uds.seg = false; /* a response being received is lost with the card */
    (void)memset(&g_fm_retained, 0, sizeof g_fm_retained); /* a power cycle loses retained RAM */
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    const sim_fs26_cfg_t fc = {.prog_id = PROG_ID, .device_id = 0x2600u};
    sim_fs26_config(&fc);
    sim_fs26_reset();
    s_p.cal_fs26_prog_id = s_prov.otp ? PROG_ID : 0xFFFFu;
    calib_nominal(&s_cal, &s_p, SERIAL);
    s_cal.motor_id = s_prov.cal ? 0x1001u : 0u;
    calib_seal(&s_cal);
    sim_set_hwid_ohm(s_p.hwid_r_ohm);
    sim_pwm_fault_route_bind(s_prov.route);
    nv_init();
    if (s_prov.val) {
        arm_validation_t v;
        arm_validation_make(&v, SERIAL, s_p.sku, TI_FW_ID, ARM_EV_VALIDATED, 14200u);
        (void)nv_queue(NV_REC_VALIDATION, &v, (uint16_t)sizeof v);
    } else {
        const uint8_t blank[sizeof(arm_validation_t)] = {0};
        (void)nv_queue(NV_REC_VALIDATION, blank, (uint16_t)sizeof blank);
    }
    boot_rec_t brec;
    if (!boot_rec_read(&brec)) { /* FW-38: the EOL station's boot record for this card — its SKU, anti-rollback counter 1 */
        boot_rec_make(&brec, (uint32_t)s_p.sku, 1u);
        (void)boot_rec_queue(&brec);
    }
    for (int k = 0; (k < 400) && !nv_idle(); k++) {
        nv_service();
    }
    const float v_ocv = (s_pl.v_ocv > 0.0f) ? s_pl.v_ocv : (s_p.class8 ? 750.0f : 400.0f);
    const float t_cool = (s_boot > 1u) ? s_pl.t_cool_c : 50.0f;
    const float rpm = s_pl.speed_rpm;
    plant_init(&s_pl, &s_p, &s_cal, s_p.fsw_hz[0], v_ocv, sim_now_ns());
    s_pl.t_cool_c = t_cool;
    s_pl.speed_rpm = rpm;
    s_pl.target_rpm = rpm;
    s_pl.t_mod_c[0] = t_cool;
    s_pl.t_mod_c[1] = t_cool + 0.35f;
    s_pl.t_mod_c[2] = t_cool - 0.25f;
    s_pl.t_motor_c = t_cool;
    s_pl.mod_offset_target_c = s_inj[INJ_OVERTEMP] ? s_overtemp_dt : 0.0f;
    resolver_and_temps(); /* before the first reading: a step would trip the dT/dt plausibility (FW-13) */
    /* The bench's sealed record stands in for the EOL station's flash image at the FIRST power-up only: once the NVM holds
     * a record (a FW-39 commit), the firmware loads that one, as on the target — "used from the next key cycle". */
    calib_t stored;
    const bool nvm_has_cal = nv_read(NV_REC_CALIB, &stored, (uint16_t)sizeof stored);
    app_init(&g_app, &s_p, nvm_has_cal ? NULL : &s_cal, SERIAL);
    g_app.uds.key_fn = s_prov.key ? bench_key : NULL; /* round 23: the bench key, when provisioned */
    sim_set_fault_isr(app_fault_isr_entry);
    s_isr_ns = sim_now_ns() + (500000000ull / g_app.gains.fsw_hz);
    s_task_ns = sim_now_ns() + TICK_NS;
    s_plant_ns = sim_now_ns();
    s_tick = 0u;
    s_last_tel = -1.0e9;
    V.cont = TI_CONT_OPEN;
    V.seq = VS_IDLE;
    V.enable = false;
    V.torque_nm = 0.0f;
    V.torque_tx_nm = 0.0f;
    V.shutdown = false;
    V.fault_reset_until = 0u;
    V.retry_until = 0u;
    V.discharge_until = 0u;
    s_wall0 = wall_ms();
    s_sim0 = sim_ms();
    char msg[96];
    (void)snprintf(msg, sizeof msg, "power-up %u: %s, key cycle %u, init %s", (unsigned)s_boot, s_p.name,
                   (unsigned)g_app.key_cycle, (g_app.init == SM_OK) ? "ok" : "FAILED (no arming this key cycle)");
    log_line("info", msg);
}

/* ---------------- plant <-> HAL ---------------- */
static void publish_link(void)
{
    sim_set_link_v(s_pl.v_link, s_pl.v_link);
    if (s_inj[INJ_VDC_SENSE]) {
        sim_adc_set_v(HAL_ADC_VDC1, 0.1f); /* channel 1 at the AMC1311 fail-safe level (FW-07) */
    }
}

static pl_bridge_t bridge_state(void)
{
    const bool hs = sim_chain_hs_on();
    const bool ls = sim_chain_ls_on();
    return (hs && ls) ? PL_BR_MOD : ((hs || ls) ? PL_BR_ASC : PL_BR_OFF);
}

/* Integrates the plant up to t, moving the sim clock with it: the bridge state is read at each step's start
 * and the link voltage published at its end (so the FW-06 compare sees the link as it moves). */
static void plant_to(uint64_t t)
{
    while (s_plant_ns < t) {
        const pl_bridge_t br = bridge_state();
        s_pl.qdis = hal_gpio_out_state(HAL_DO_QDIS);
        const bool off = (br == PL_BR_OFF) || ((br == PL_BR_MOD) && !s_pl.duty_now_ok);
        uint64_t h = (off && plant_off_active(&s_pl, s_plant_ns)) ? 1000u : 10000u;
        if ((s_plant_ns + h) > t) {
            h = t - s_plant_ns;
        }
        plant_elec(&s_pl, br, s_plant_ns, s_plant_ns + h);
        s_plant_ns += h;
        publish_link();
        if (s_plant_ns > sim_now_ns()) {
            sim_advance_ns(s_plant_ns - sim_now_ns());
        }
    }
}

static void publish_currents(void)
{
    float i[3];
    plant_phase_currents(&s_pl, sim_now_ns(), i);
    for (uint32_t k = 0u; k < 3u; k++) {
        i[k] += noise_a();
    }
    if (s_oc_samples > 0u) {
        i[0] += 1.3f * s_p.i_oc_trip_a; /* a sensed spike beyond the FW-05 compare */
        s_oc_samples--;
    }
    sim_set_phase_currents(i[0], i[1], i[2]);
}

static uint64_t isr_per_ns(void) { return 500000000ull / g_app.gains.fsw_hz; }

static void isrs_until(uint64_t until)
{
    const uint64_t per = isr_per_ns();
    for (; s_isr_ns <= until; s_isr_ns += per) {
        if (s_isr_ns < sim_now_ns()) {
            continue; /* inside a long task: on the target this trigger preempted it (harness rule) */
        }
        plant_to(s_isr_ns);
        plant_latch_duty(&s_pl); /* double update: what ISR k-1 wrote applies from this trigger */
        publish_currents();
        app_isr_current(&g_app);
        s_pl.duty_next_ok = hal_pwm_mode() == HAL_PWM_MOD;
        for (uint32_t k = 0u; k < 3u; k++) {
            s_pl.duty_next[k] = s_pl.duty_next_ok ? sim_pwm_duty(k) : 0.5f;
        }
    }
}

static void grids_check(void)
{
    const uint64_t now = sim_now_ns();
    if (((now + TICK_NS) < s_task_ns) || (now >= (s_task_ns + TICK_NS))) {
        s_isr_ns = now + isr_per_ns();
        s_task_ns = now + TICK_NS;
    }
}

static void resolver_and_temps(void)
{
    const sim_resolver_t r = {.theta0_rad = s_pl.theta0_mech, .omega_rad_s = s_pl.speed_rpm / TI_RPM_PER_RAD_S,
                              .lag_deg = 24.0f, .sin_gain = 1.0f, .cos_gain = 1.0f,
                              .out_gain = s_inj[INJ_RESOLVER] ? 0.2f : 1.0f};
    sim_resolver_set(&r);
    sim_set_temp(HAL_ADC_TMOD_U, s_pl.t_mod_c[0]);
    sim_set_temp(HAL_ADC_TMOD_V, s_pl.t_mod_c[1]);
    sim_set_temp(HAL_ADC_TMOD_W, s_pl.t_mod_c[2]);
    sim_set_temp(HAL_ADC_NTC_H, s_pl.t_board_c[0]);
    sim_set_temp(HAL_ADC_NTC_A, s_pl.t_board_c[1]);
    sim_set_temp(HAL_ADC_MT1, s_pl.t_motor_c);
    sim_set_temp(HAL_ADC_MT2, s_pl.t_motor_c + 0.5f);
}

/* ---------------- VCU / BMS ---------------- */
static float bms_pack_v(void) { return (s_pl.cont == PL_CONT_CLOSED) ? s_pl.v_link : s_pl.v_ocv; }

static void vcu_tx(void)
{
    if (!V.send) {
        return; /* CAN loss injection: the VCU and the BMS relay go silent */
    }
    const uint64_t t = now_ms_u();
    hal_can_frame_t f;
    if (!s_ext_cmd) {
        can_encode_vcu_cmd(&f, V.ctr, V.gear, V.enable, t < V.fault_reset_until, V.torque_tx_nm, V.cont,
                           t < V.retry_until, t < V.discharge_until, V.shutdown, V.coolant_c);
        can_vcu_cmd_vspeed(&f, V.vspeed_valid, V.vspeed_kmh); /* round 23 (FW-39): the bench vehicle's speed */
        sim_can_inject(HAL_CAN_VEHICLE, &f);
        V.ctr = (uint8_t)((V.ctr + 1u) & 0x0Fu);
    }
    if (!s_ext_bms) {
        can_encode_vcu_bms(&f, V.bms_ctr, bms_pack_v(), V.p_chg_w, V.p_dis_w);
        sim_can_inject(HAL_CAN_VEHICLE, &f);
        V.bms_ctr = (uint8_t)((V.bms_ctr + 1u) & 0x0Fu);
    }
}

static bool armed_state(sm_state_t st)
{
    return (st == SM_ARMED_ZERO_TORQUE) || (st == SM_RUN) || (st == SM_DERATE);
}

static void vcu_step(void)
{
    const uint64_t t = now_ms_u();
    if ((s_hold_wall > 0.0) && (wall_ms() > s_hold_wall)) {
        V.torque_nm = 0.0f; /* deadman: the torque command was not refreshed within its hold_ms */
        s_hold_wall = 0.0;
    }
    if (s_ext_cmd) {
        return; /* an external VCU owns the sequence and the contactors (can_rx) */
    }
    V.torque_tx_nm = (V.slew_nm_s > 0.0f) ? ti_ramp(V.torque_tx_nm, V.torque_nm, V.slew_nm_s * 1.0e-3f) : V.torque_nm;
    const sm_state_t st = g_app.sm.st;
    V.note = NULL;
    switch (V.seq) {
    case VS_ARM:
        V.enable = true;
        if (st == SM_PRECHARGE_WAIT) {
            V.cont = TI_CONT_PRECHARGE;
            V.seq = VS_PRECHARGE;
            V.seq_t_ms = t;
        } else if (armed_state(st)) {
            V.seq = VS_CLOSED;
        } else if (st == SM_FAULT) {
            V.note = "inverter in FAULT: a fault reset (below n_x) comes first";
        } else if ((st == SM_OFF) || (st == SM_SAFE_POWERDOWN)) {
            V.note = "ignition off";
        } else {
            V.note = "waiting for the inverter's self-test done (PRECHARGE_WAIT, section 9 step 7)";
        }
        break;
    case VS_PRECHARGE:
        if ((((t - V.seq_t_ms) >= 300u) && (s_pl.v_link >= (0.98f * s_pl.v_ocv))) || ((t - V.seq_t_ms) >= 2500u)) {
            V.cont = TI_CONT_CLOSED; /* FW-19 judges the precharge at this closing */
            V.seq = VS_CLOSED;
        }
        break;
    case VS_CLOSED:
        if (st == SM_FAULT) {
            V.note = "inverter in FAULT";
        }
        break;
    case VS_DISARM:
        V.torque_nm = 0.0f;
        V.enable = false;
        if (g_app.speed_known && (fabsf(g_app.speed_rpm) >= g_app.n_x_rpm) && (V.cont == TI_CONT_CLOSED)) {
            V.note = "waiting for |n| < n_x: opening now would be a battery-path loss at speed (section 6)";
        } else if (((t - V.seq_t_ms) >= 50u) && (fabsf(g_app.t_cmd_nm) < 0.5f)) {
            V.cont = TI_CONT_OPEN; /* the FW-08 zero-torque opening */
            V.seq = VS_IDLE;
        } else {
            V.note = "ramping the torque to zero before opening";
        }
        break;
    default:
        break;
    }
    s_pl.cont = (V.cont == TI_CONT_CLOSED) ? PL_CONT_CLOSED : ((V.cont == TI_CONT_PRECHARGE) ? PL_CONT_PRECHARGE : PL_CONT_OPEN);
}

/* ---------------- telemetry ---------------- */
static void dtc_array(void)
{
    jo_arr("dtc");
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        const dtc_id_t d = (dtc_id_t)i;
        if ((dtc_occurrences(d) == 0u) && !dtc_active(d)) {
            continue;
        }
        uint32_t f = 0u;
        uint32_t l = 0u;
        (void)dtc_times(d, &f, &l);
        jo_arr(NULL);
        jo_u(NULL, i);
        jo_u(NULL, dtc_status(d));
        jo_u(NULL, dtc_occurrences(d));
        jo_u(NULL, f);
        jo_u(NULL, l);
        jo_close();
    }
    jo_close();
}

static bool any_dtc(const dtc_id_t *ids, uint32_t n)
{
    for (uint32_t i = 0u; i < n; i++) {
        if (dtc_active(ids[i])) {
            return true;
        }
    }
    return false;
}

static void arm_checklist(void)
{
    const app_t *a = &g_app;
    static const dtc_id_t OTP[] = {DTC_FS26_PROGID, DTC_FS26_OTP_CORRUPT, DTC_FS26_DEBUG_MODE, DTC_FS26_INIT_READBACK,
                                   DTC_FS26_SPI, DTC_FS1B_SHORT_HIGH};
    static const dtc_id_t IDENT[] = {DTC_HWID_OPEN, DTC_HWID_SHORT, DTC_HWID_UNKNOWN, DTC_SKU_MISMATCH};
    const uint8_t plat = ARM_EV_ROUTE_BOUND | ARM_EV_CONFIG_MATCHES | ARM_EV_PROTECTION_LOCKED;
    bool any = false;
    bool all = false;
    (void)temp_module_max(&a->temp, &any, &all);
    jo_obj("arm");
    jo_b("cal", a->cal_err == 0u);
    jo_b("val", (a->evidence & ARM_EV_VALIDATED) == ARM_EV_VALIDATED);
    jo_b("otp", (a->p->cal_fs26_prog_id != 0xFFFFu) && !any_dtc(OTP, TI_ARRAY_LEN(OTP)));
    jo_b("platform", (a->evidence & plat) == plat);
    jo_b("identity", !any_dtc(IDENT, TI_ARRAY_LEN(IDENT)));
    jo_b("params", !dtc_active(DTC_PARAMS_INVALID) && a->gains_ok);
    jo_b("hvil", a->hvil.status == HVIL_CLOSED);
    jo_b("no_fault", !fm_any(&a->fm) && (a->sm.st != SM_FAULT) && !a->no_arm);
    jo_b("self_test", a->selftest == SM_OK);
    jo_b("precharge", a->pch.res == PCH_OK);
    jo_b("gate_power", a->gp.st == GP_READY);
    jo_b("sensors", a->rslv.valid && a->vdc.valid && a->isns.valid && a->offs_ok && any);
    jo_b("vcu", can_cmd_fresh(&a->can, hal_time_ms(), a->p));
    jo_b("contactors", a->can.contactors == TI_CONT_CLOSED);
    jo_b("armed", hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    jo_close();
}

static void telemetry(void)
{
    const app_t *a = &g_app;
    const uint32_t t_ms = hal_time_ms();
    bool any = false;
    bool all = false;
    const float tmod = temp_module_max(&a->temp, &any, &all);
    /* measured dq currents: the phase currents in the resolver frame (the loop's own id/iq stop at its last step) */
    float id_m = 0.0f;
    float iq_m = 0.0f;
    if (a->isns.valid && a->rslv.valid) {
        float al;
        float be;
        foc_clarke(a->isns.i_a, &al, &be);
        /* at the sample's own stamp: the task's FS26 transfers move the clock on after the ISR sampled */
        foc_park(al, be, rslv_theta_e_at(&a->rslv, &a->cal.rslv, a->isns.t_us, a->p), &id_m, &iq_m);
    }
    const float torque_est = 1.5f * (float)a->cal.motor.pp *
                             ((a->cal.motor.psi_wb * iq_m) + ((a->cal.motor.ld_h - a->cal.motor.lq_h) * id_m * iq_m));
    jo_begin();
    jo_s("type", "tel");
    jo_i("v", 1);
    jo_u("seq", s_seq++);
    jo_d("t_ms", session_ms());
    jo_s("src", "sim");
    jo_obj("state");
    jo_u("sm", (unsigned)a->sm.st);
    jo_u("bridge", (unsigned)a->br.mode);
    jo_u("hv", (unsigned)dis_hv_state(&a->vdc));
    jo_b("self_test_done", a->so.self_test_done);
    jo_b("fault", a->sm.st == SM_FAULT);
    jo_b("derate", a->tlim.derate_active);
    jo_b("zero_torque", fabsf(a->t_cmd_nm) < 0.5f);
    jo_b("discharging", dis_output(&a->dis));
    jo_b("precharge_refused", a->pch.res >= PCH_REFUSE_PLATEAU);
    jo_b("keep_hv", a->fm.keep_hv);
    jo_b("no_safe_state", a->fm.no_safe_state);
    jo_b("service_required", a->service_required);
    jo_b("speed_limit_req", a->speed_limit_req);
    jo_b("no_arm", a->no_arm);
    jo_u("evidence", a->evidence);
    jo_u("key_cycle", a->key_cycle);
    jo_u("boot", s_boot);
    jo_u("fw_ms", t_ms);
    jo_b("gate_en", hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    jo_b("drv_en", sim_chain_drv_en());
    jo_b("asc_latch", sim_chain_asc_latch());
    jo_u("pwm", (unsigned)hal_pwm_mode());
    jo_close();
    jo_obj("motion");
    jo_f("speed_rpm", a->speed_rpm);
    jo_b("speed_valid", a->rslv.valid);
    jo_b("speed_known", a->speed_known);
    jo_f("speed_bound_rpm", app_speed_hi_rpm(a, t_ms)); /* round 23: the upper bound of |n| the §6 decisions use */
    jo_f("n_x_rpm", a->n_x_rpm);
    jo_f("torque_req_nm", a->can.torque_req_nm);
    jo_f("torque_cmd_nm", a->t_cmd_nm);
    jo_f("torque_est_nm", torque_est);
    jo_s("gear", gear_name(a->can.gear));
    jo_close();
    jo_obj("foc");
    jo_f("id_a", id_m);
    jo_f("iq_a", iq_m);
    jo_f("id_ref_a", a->foc.id_ref);
    jo_f("iq_ref_a", a->foc.iq_ref);
    jo_f("vd_v", a->foc.vd);
    jo_f("vq_v", a->foc.vq);
    jo_f("vmax_v", a->foc.vmax);
    jo_b("sat", a->foc.sat);
    jo_f("ia_a", a->isns.i_a[0]);
    jo_f("ib_a", a->isns.i_a[1]);
    jo_f("ic_a", a->isns.i_a[2]);
    jo_f("i_rms_a", sqrtf(0.5f * ((id_m * id_m) + (iq_m * iq_m))));
    jo_f("duty_a", a->foc.duty[0]);
    jo_f("duty_b", a->foc.duty[1]);
    jo_f("duty_c", a->foc.duty[2]);
    jo_f("p_elec_w", (a->br.mode == BR_MOD) ? (1.5f * ((a->foc.vd * a->foc.id) + (a->foc.vq * a->foc.iq))) : 0.0f);
    jo_b("isns_valid", a->isns.valid);
    jo_close();
    jo_obj("link");
    jo_f("vdc_v", a->vdc.vdc);
    jo_b("vdc_valid", a->vdc.valid);
    jo_f("v_ch1", a->vdc.v_ch[0]);
    jo_f("v_ch2", a->vdc.v_ch[1]);
    jo_f("vofs_v", a->vdc.vofs_v);
    jo_f("v5gd_v", a->vdc.v5gd_v);
    jo_f("v_pack_v", a->can.v_pack);
    jo_u("contactors", (unsigned)a->can.contactors);
    jo_f("vsup_v", a->vsup.v);
    jo_b("ign_on", a->ign.on);
    jo_close();
    jo_obj("temps");
    jo_f("mod_u_c", a->temp.ch[TEMP_TMOD_U].t_c);
    jo_f("mod_v_c", a->temp.ch[TEMP_TMOD_V].t_c);
    jo_f("mod_w_c", a->temp.ch[TEMP_TMOD_W].t_c);
    jo_f("mod_max_c", tmod);
    jo_b("mod_valid", all);
    jo_f("board_h_c", a->temp.ch[TEMP_NTC_H].t_c);
    jo_f("board_a_c", a->temp.ch[TEMP_NTC_A].t_c);
    jo_f("motor1_c", a->temp.ch[TEMP_MT1].t_c);
    jo_f("motor2_c", a->temp.ch[TEMP_MT2].t_c);
    jo_f("coolant_c", a->can.coolant_valid ? a->can.coolant_c : NAN);
    jo_f("derate_start_c", a->p->cal_tmod_derate_start_c);
    jo_f("derate_end_c", a->p->cal_tmod_derate_end_c);
    jo_close();
    jo_obj("limits");
    jo_f("derate", a->tlim.derate);
    jo_b("derate_active", a->tlim.derate_active);
    jo_f("coolant_factor", a->tlim.coolant_factor);
    jo_f("i_limit_rms_a", a->tlim.i_limit_rms_a);
    jo_f("t_motor_nm", a->tlim.t_lim_motor_nm);
    jo_f("t_regen_nm", a->tlim.t_lim_regen_nm);
    jo_f("peak_used_s", a->tlim.peak_used_s);
    jo_b("peak_exhausted", a->tlim.peak_exhausted);
    jo_f("p_chg_w", a->can.p_chg_w);
    jo_f("p_dis_w", a->can.p_dis_w);
    jo_f("torque_max_nm", a->p->cal_torque_max_nm);
    jo_close();
    jo_obj("safety");
    jo_u("rows", a->fm.active);
    jo_u("latched", a->fm.latched);
    jo_u("action", fm_any(&a->fm) ? (unsigned)a->fm.dec.action : 0u);
    jo_u("row", (unsigned)a->fm.dec_row);
    jo_b("asc_permitted", a->fm.asc_permitted);
    jo_u("hvil", (unsigned)a->hvil.status);
    jo_b("rule_a", a->fm.dec.rule_a);
    jo_b("rule_b", a->fm.dec.rule_b);
    jo_close();
    jo_obj("wd");
    jo_u("fs26_state", sim_fs26_state());
    jo_u("fs26_wd_err", sim_fs26_wd_err_cnt());
    jo_u("fs26_refresh", a->fs.n_refresh);
    jo_b("fs0b", sim_fs26_fs0b_asserted());
    jo_b("fs1b", sim_fs26_fs1b_asserted());
    jo_u("isr_age_us", (uint32_t)(hal_time_us() - a->t_isr_us));
    jo_u("n_isr", a->n_isr);
    jo_u("wdog_kicks", sim_wdog_kicks());
    jo_b("cmd_fresh", can_cmd_fresh(&a->can, t_ms, a->p));
    jo_b("bms_fresh", can_bms_fresh(&a->can, t_ms, a->p));
    jo_u("can_crc", a->can.n_crc);
    jo_u("can_frozen", a->can.n_frozen);
    jo_u("can_jump", a->can.n_jump);
    jo_b("rslv_valid", a->rslv.valid);
    jo_f("rslv_amp", a->rslv.amp);
    jo_f("rslv_mon_vpp", a->rslv.mon_vpp);
    jo_f("rslv_err", a->rslv.err);
    jo_close();
    arm_checklist();
    dtc_array();
    jo_obj("plant");
    jo_f("speed_rpm", s_pl.speed_rpm);
    jo_f("target_rpm", s_pl.target_rpm);
    jo_f("torque_nm", s_pl.torque_nm);
    jo_f("id_a", s_pl.id);
    jo_f("iq_a", s_pl.iq);
    jo_f("v_link_v", s_pl.v_link);
    jo_f("v_ocv_v", s_pl.v_ocv);
    jo_f("i_dc_a", s_pl.i_dc_a);
    jo_f("p_dc_w", s_pl.p_dc_w);
    jo_f("p_mech_w", s_pl.p_mech_w);
    jo_f("p_loss_w", s_pl.p_loss_w);
    jo_f("t_coolant_c", s_pl.t_cool_c);
    /* the firmware's electrical angle against the true rotor (sim-only truth) */
    jo_f("theta_err_deg", a->rslv.valid ? (ti_wrap_pi(rslv_theta_e_at(&a->rslv, &a->cal.rslv, hal_time_us(), a->p) -
                                                      plant_theta_e(&s_pl, sim_now_ns())) * (180.0f / TI_PI))
                                        : NAN);
    jo_s("bridge", (s_pl.br == PL_BR_MOD) ? "mod" : ((s_pl.br == PL_BR_ASC) ? "asc" : "off"));
    jo_s("contactor", (s_pl.cont == PL_CONT_CLOSED) ? "closed" : ((s_pl.cont == PL_CONT_PRECHARGE) ? "precharge" : "open"));
    jo_arr("inject");
    for (uint32_t i = 0u; i < (uint32_t)INJ_PERSIST_COUNT; i++) {
        if (s_inj[i]) {
            jo_s(NULL, INJ[i]);
        }
    }
    jo_close();
    jo_close();
    jo_obj("vcu");
    jo_s("seq", VSEQ[V.seq]);
    if (V.note != NULL) {
        jo_s("note", V.note);
    }
    jo_b("enable", V.enable);
    jo_f("torque_nm", V.torque_nm);
    jo_f("torque_tx_nm", V.torque_tx_nm);
    jo_f("slew_nm_s", V.slew_nm_s);
    jo_s("gear", gear_name(V.gear));
    jo_b("vspeed_valid", V.vspeed_valid);
    jo_f("vspeed_kmh", V.vspeed_kmh);
    jo_b("send", V.send);
    jo_b("external_cmd", s_ext_cmd);
    jo_b("external_bms", s_ext_bms);
    jo_close();
    jo_obj("sim");
    jo_d("time_factor", s_factor);
    jo_d("rt", s_rt);
    jo_b("paused", s_paused);
    jo_d("rate_hz", 1000.0 / (double)s_tel_ms);
    jo_close();
    jo_s("can", s_status_hex);
    jo_end();
}

/* ---------------- hello / info ---------------- */
static float cal_get(const ti_params_t *p, const ti_cal_range_t *r)
{
    const uint8_t *b = (const uint8_t *)p + r->offset;
    switch (r->type) {
    case TI_CAL_F32: return *(const float *)(const void *)b;
    case TI_CAL_U32: return (float)*(const uint32_t *)(const void *)b;
    case TI_CAL_U16: return (float)*(const uint16_t *)(const void *)b;
    default: return (float)*b;
    }
}

static const char *cal_type(ti_cal_type_t t)
{
    return (t == TI_CAL_F32) ? "f32" : ((t == TI_CAL_U32) ? "u32" : ((t == TI_CAL_U16) ? "u16" : "u8"));
}

static void params_array(const char *key)
{
    jo_arr(key);
    for (uint32_t i = 0u; i < TI_ARRAY_LEN(RANGES); i++) {
        const ti_cal_range_t *r = &RANGES[i];
        jo_obj(NULL);
        jo_s("name", r->name);
        jo_s("type", cal_type(r->type));
        jo_f("min", r->min);
        jo_f("max", r->max);
        jo_f("value", cal_get(&s_p, r));
        jo_f("default", cal_get(&s_p_default, r));
        jo_close();
    }
    jo_close();
}

#define CAL_FIELD(f) {#f, offsetof(calib_t, f), sizeof(((calib_t *)0)->f)}
static void hello(void)
{
    static const struct {
        const char *name;
        size_t off, size;
    } CF[] = {CAL_FIELD(layout_version), CAL_FIELD(sku), CAL_FIELD(hw_serial), CAL_FIELD(motor_id), CAL_FIELD(fsw_hz),
              CAL_FIELD(isns), CAL_FIELD(vdc), CAL_FIELD(rslv), CAL_FIELD(motor), CAL_FIELD(mt), CAL_FIELD(mtpa),
              CAL_FIELD(crc32)};
    const app_t *a = &g_app;
    char buf[64];
    jo_begin();
    jo_s("type", "hello");
    jo_s("bridge", "traction-tool-bridge 1");
    (void)snprintf(buf, sizeof buf, "0x%08X", (unsigned)TI_FW_ID);
    jo_s("fw_id", buf);
    { /* round 23: the image verifier's root of trust (verify.h img_root; DID 0xFD23) */
        uint32_t key_id = 0u;
        const img_root_t kind = img_root(&key_id);
        jo_obj("root");
        jo_s("kind", (kind == IMG_ROOT_TEST) ? "test" : (kind == IMG_ROOT_OTP) ? "otp" : (kind == IMG_ROOT_BUILD) ? "build" : "none");
        (void)snprintf(buf, sizeof buf, "0x%08X", (unsigned)key_id);
        jo_s("key_id", buf);
        jo_close();
    }
    jo_u("sku", (unsigned)s_p.sku);
    jo_s("sku_name", s_p.name);
    jo_u("dtc_count", (unsigned)DTC_COUNT);
    jo_arr("dtcs"); /* names in id order: index = the DTC id (0 = DTC_NONE); UDS code = 0xD10000 | id */
    for (uint32_t i = 0u; i < (uint32_t)DTC_COUNT; i++) {
        jo_s(NULL, DTC_NAMES[i]);
    }
    jo_close();
    jo_u("fsw_hz", a->gains.fsw_hz);
    jo_u("isr_hz", 2u * a->gains.fsw_hz);
    jo_f("fc_hz", a->gains.fc_hz);
    jo_f("n_x_rpm", a->n_x_rpm);
    jo_u("carrier_hz", 10000u);
    jo_arr("states");
    for (uint32_t i = 0u; i < (uint32_t)SM_STATE_COUNT; i++) {
        jo_s(NULL, sm_name((sm_state_t)i));
    }
    jo_close();
    jo_arr("ss_rows");
    for (uint32_t i = 0u; i < (uint32_t)SS_ROW_COUNT; i++) {
        jo_s(NULL, ss_row_name((ss_row_t)i));
    }
    jo_close();
    jo_arr("ss_actions");
    for (uint32_t i = 0u; i <= (uint32_t)SS_ACT_SPO_THEN_PWM_ASC; i++) {
        jo_s(NULL, ss_action_name((ss_action_t)i));
    }
    jo_close();
    jo_obj("limits");
    jo_f("vdc_min_v", s_p.vdc_min_v);
    jo_f("vdc_max_v", s_p.vdc_max_v);
    jo_f("ov_trip_v", s_p.ov_trip_v);
    jo_f("i_pk_rms_a", s_p.i_pk_rms_a);
    jo_f("i_cont_rms_a", s_p.i_cont_rms_a);
    jo_f("i_oc_trip_a", s_p.i_oc_trip_a);
    jo_f("p_peak_w", s_p.p_peak_w);
    jo_f("p_cont_w", s_p.p_cont_w);
    jo_u("dead_time_ns", s_p.dead_time_ns);
    jo_close();
    jo_obj("motor");
    jo_f("ld_h", a->cal.motor.ld_h);
    jo_f("lq_h", a->cal.motor.lq_h);
    jo_f("rs_ohm", a->cal.motor.rs_ohm);
    jo_f("psi_wb", a->cal.motor.psi_wb);
    jo_u("pp", a->cal.motor.pp);
    jo_f("id_demag_a", a->cal.motor.id_demag_a);
    jo_f("n_max_rpm", a->cal.motor.n_max_rpm);
    jo_close();
    jo_obj("calib");
    jo_u("layout_version", a->cal.layout_version);
    jo_u("size", sizeof(calib_t));
    jo_u("err", a->cal_err);
    jo_u("motor_id", a->cal.motor_id);
    jo_u("fsw_hz", a->cal.fsw_hz);
    jo_u("sku", a->cal.sku);
    (void)snprintf(buf, sizeof buf, "%.8s", (const char *)a->cal.hw_serial);
    jo_s("serial", buf);
    (void)snprintf(buf, sizeof buf, "0x%08X", (unsigned)a->cal.crc32);
    jo_s("crc32", buf);
    jo_arr("fields");
    for (uint32_t i = 0u; i < TI_ARRAY_LEN(CF); i++) {
        jo_obj(NULL);
        jo_s("name", CF[i].name);
        jo_u("offset", CF[i].off);
        jo_u("size", CF[i].size);
        jo_close();
    }
    jo_close();
    jo_arr("isns");
    for (uint32_t i = 0u; i < 3u; i++) {
        jo_obj(NULL);
        jo_f("offset_v", a->cal.isns[i].offset_v);
        jo_f("gain_v_per_a", a->cal.isns[i].gain_v_per_a);
        jo_i("sign", a->cal.isns[i].sign);
        jo_close();
    }
    jo_close();
    jo_arr("vdc");
    for (uint32_t i = 0u; i < 2u; i++) {
        jo_obj(NULL);
        jo_f("gain", a->cal.vdc[i].gain);
        jo_f("offset_v", a->cal.vdc[i].offset_v);
        jo_close();
    }
    jo_close();
    jo_obj("rslv");
    jo_f("ratio_nom", a->cal.rslv.ratio_nom);
    jo_f("exc_code_per_vpp", a->cal.rslv.exc_code_per_vpp);
    jo_f("sin_gain", a->cal.rslv.sin_gain);
    jo_f("cos_gain", a->cal.rslv.cos_gain);
    jo_f("phase_trim_deg", a->cal.rslv.phase_trim_deg);
    jo_f("zero_rad", a->cal.rslv.zero_rad);
    jo_u("motor_pp", a->cal.rslv.motor_pp);
    jo_u("resolver_pp", a->cal.rslv.resolver_pp);
    jo_close();
    jo_u("mt_type", (unsigned)a->cal.mt.type);
    jo_u("mtpa_n", a->cal.mtpa.n);
    jo_close();
    arm_validation_t v;
    const bool present = nv_read(NV_REC_VALIDATION, &v, (uint16_t)sizeof v);
    jo_obj("validation");
    jo_b("present", present && (v.magic == ARM_VAL_MAGIC));
    if (present && (v.magic == ARM_VAL_MAGIC)) {
        jo_u("layout", v.layout);
        jo_u("sku", v.sku);
        jo_u("flags", v.flags);
        (void)snprintf(buf, sizeof buf, "0x%08X", (unsigned)v.fw_id);
        jo_s("fw_id", buf);
        jo_u("ovp_chain_ns", v.ovp_chain_ns);
        (void)snprintf(buf, sizeof buf, "%.8s", (const char *)v.hw_serial);
        jo_s("serial", buf);
    }
    jo_u("valid_flags", arm_validation_flags(&v, present, SERIAL, s_p.sku, TI_FW_ID, &s_p));
    jo_f("budget_us", s_p.fw06_budget_us);
    jo_close();
    nv_selftest_t st;
    jo_obj("selftest_record");
    const bool st_ok = nv_read(NV_REC_SELFTEST, &st, (uint16_t)sizeof st);
    jo_b("present", st_ok);
    if (st_ok) {
        jo_u("key_cycle", st.key_cycle);
        jo_b("passed", st.passed != 0u);
        jo_u("failed_step", st.failed_step);
    }
    jo_close();
    jo_obj("provision");
    jo_b("val", s_prov.val);
    jo_b("route", s_prov.route);
    jo_b("otp", s_prov.otp);
    jo_b("cal", s_prov.cal);
    jo_b("sa_key", s_prov.key);
    jo_close();
    boot_rec_t brec; /* FW-38 (no DID carries it) */
    if (boot_rec_read(&brec)) {
        jo_obj("boot_rec");
        jo_u("state", brec.state);
        jo_u("last", brec.last);
        jo_u("last_err", brec.last_err);
        jo_u("target", brec.target);
        jo_u("sec_counter", brec.sec_counter);
        jo_b("lkg_valid", brec.lkg_valid != 0u);
        jo_close();
    }
    jo_u("evidence", a->evidence);
    jo_u("key_cycle", a->key_cycle);
    jo_u("boot", s_boot);
    params_array("params");
    jo_end();
}

/* ---------------- commands ---------------- */
static const ti_cal_range_t *cal_find(const char *name)
{
    for (uint32_t i = 0u; (name != NULL) && (i < TI_ARRAY_LEN(RANGES)); i++) {
        if (strcmp(RANGES[i].name, name) == 0) {
            return &RANGES[i];
        }
    }
    return NULL;
}

static void cal_put(ti_params_t *p, const ti_cal_range_t *r, double v)
{
    uint8_t *b = (uint8_t *)p + r->offset;
    switch (r->type) {
    case TI_CAL_F32: *(float *)(void *)b = (float)v; break;
    case TI_CAL_U32: *(uint32_t *)(void *)b = (uint32_t)v; break;
    case TI_CAL_U16: *(uint16_t *)(void *)b = (uint16_t)v; break;
    default: *b = (uint8_t)v; break;
    }
}

static void cmd_param_set(const jin_t *o)
{
    const ti_cal_range_t *r = cal_find(jin_str(o, "name"));
    double v = 0.0;
    if (r == NULL) {
        ack(o, false, "unknown parameter (only cal_* rows of include/cal_ranges.h are writable)");
        return;
    }
    if (!jin_num(o, "value", &v)) {
        ack(o, false, "value must be a finite number");
        return;
    }
    const bool integer = r->type != TI_CAL_F32;
    if (integer && ((v < 0.0) || (v != floor(v)) || (v > 4294967295.0))) {
        ack(o, false, "this parameter is an unsigned integer");
        return;
    }
    ti_params_t cand = s_p;
    cal_put(&cand, r, v);
    const uint32_t bad = ti_params_validate(&cand); /* the firmware's own boot-time validation */
    char msg[160];
    jo_begin();
    jo_s("type", "ack");
    double id = 0.0;
    if (jin_num(o, "id", &id)) {
        jo_d("id", id);
    }
    jo_s("cmd", "param_set");
    jo_s("name", r->name);
    jo_b("ok", bad == 0u);
    jo_u("violations", bad);
    if (bad == 0u) {
        s_p = cand;
        jo_f("value", cal_get(&s_p, r));
    } else {
        (void)snprintf(msg, sizeof msg, "ti_params_validate() refused the set: %u violation(s); nothing written",
                       (unsigned)bad);
        jo_s("err", msg);
        jo_f("value", cal_get(&s_p, r));
    }
    jo_d("t_ms", session_ms());
    jo_end();
}

static void set_inject(const char *f, bool on, const jin_t *o)
{
    for (uint32_t i = 0u; i < (uint32_t)INJ_PERSIST_COUNT; i++) {
        if (strcmp(INJ[i], f) != 0) {
            continue;
        }
        s_inj[i] = on;
        switch (i) {
        case INJ_OVERTEMP: {
            double dt = 60.0;
            if (on && jin_num(o, "dt_c", &dt)) {
                s_overtemp_dt = (float)fmin(fmax(dt, 1.0), 120.0);
            }
            s_pl.mod_offset_target_c = on ? s_overtemp_dt : 0.0f;
            break;
        }
        case INJ_CAN: V.send = !on; break;
        case INJ_HVIL: sim_hvil_set(on ? SIM_HVIL_OPEN : SIM_HVIL_CLOSED); break;
        case INJ_V5GD: sim_set_v5gd(on ? 4.5f : 5.0f); break;
        case INJ_LV_OV: sim_fs26_vsup(on ? 35.0f : 13.5f); break;
        default: break; /* applied where the plant publishes (vdc_sense_loss, resolver_loss) */
        }
        ack(o, true, NULL);
        return;
    }
    if (!on) {
        ack(o, false, "not a persistent injection");
        return;
    }
    if ((strcmp(f, "desat_hs") == 0) || (strcmp(f, "desat_ls") == 0)) {
        sim_chain_desat(f[6] == 'h', false); /* a driver latches FLT: the hardware chain acts, then FW-15 */
    } else if (strcmp(f, "overcurrent") == 0) {
        s_oc_samples = 2u; /* two samples beyond the FW-05 compare on phase U */
    } else if (strcmp(f, "battery_loss") == 0) {
        V.cont = TI_CONT_OPEN; /* the contactor opens under load; the VCU reports it open */
        V.seq = VS_IDLE;
        s_pl.cont = PL_CONT_OPEN;
    } else {
        ack(o, false, "unknown fault (desat_hs desat_ls overcurrent overtemp vdc_sense_loss battery_loss "
                      "resolver_loss can_loss hvil_open v5gd_loss lv_overvoltage)");
        return;
    }
    ack(o, true, "event injected");
}

static void step_ticks(uint32_t n);
static void dtc_clear_uds(const jin_t *o);

static void exec(const jin_t *o)
{
    const char *c = jin_str(o, "cmd");
    double x = 0.0;
    bool b = false;
    if (c == NULL) {
        ack(o, false, "missing cmd");
    } else if (strcmp(c, "ping") == 0) {
        ack(o, true, NULL);
    } else if (strcmp(c, "info") == 0) {
        hello();
        ack(o, true, NULL);
    } else if (strcmp(c, "rate") == 0) {
        if (jin_num(o, "hz", &x) && (x > 0.0)) {
            s_tel_ms = (uint32_t)fmin(fmax(floor((1000.0 / x) + 0.5), 1.0), 1000.0);
            ack(o, true, NULL);
        } else {
            ack(o, false, "hz > 0 required");
        }
    } else if (strcmp(c, "time") == 0) {
        if (jin_num(o, "factor", &x) && (x >= 0.0) && (x <= 100.0)) {
            s_factor = x;
            s_wall0 = wall_ms();
            s_sim0 = sim_ms();
            ack(o, true, NULL);
        } else {
            ack(o, false, "factor in [0, 100] (0 = as fast as possible)");
        }
    } else if (strcmp(c, "pause") == 0) {
        s_paused = jin_bool(o, "on", &b) ? b : !s_paused;
        s_wall0 = wall_ms();
        s_sim0 = sim_ms();
        ack(o, true, NULL);
    } else if (strcmp(c, "step") == 0) {
        const double ms = jin_num(o, "ms", &x) ? fmin(fmax(x, 1.0), 10000.0) : 1.0;
        step_ticks((uint32_t)ms);
        ack(o, true, NULL);
    } else if (strcmp(c, "torque") == 0) {
        if (jin_num(o, "nm", &x) && (fabs(x) <= 3000.0)) {
            V.torque_nm = (V.seq == VS_DISARM) ? 0.0f : (float)x;
            if (jin_num(o, "slew_nm_s", &x) && (x >= 0.0)) {
                V.slew_nm_s = (float)fmin(x, 1.0e6);
            }
            s_hold_wall = (jin_num(o, "hold_ms", &x) && (x > 0.0)) ? (wall_ms() + fmin(x, 60000.0)) : 0.0;
            ack(o, true, NULL);
        } else {
            ack(o, false, "nm required, |nm| <= 3000");
        }
    } else if (strcmp(c, "speed") == 0) {
        if (jin_num(o, "rpm", &x) && (fabs(x) <= (double)s_cal.motor.n_max_rpm)) {
            s_pl.target_rpm = (float)x;
            if (jin_num(o, "ramp_rpm_s", &x) && (x > 0.0)) {
                s_pl.ramp_rpm_s = (float)fmin(x, 20000.0);
            }
            ack(o, true, NULL);
        } else {
            ack(o, false, "rpm required, within the motor's n_max");
        }
    } else if (strcmp(c, "gear") == 0) {
        const char *g = jin_str(o, "gear");
        const ti_gear_t n = (g == NULL) ? TI_GEAR_N : ((g[0] == 'D') ? TI_GEAR_D : ((g[0] == 'R') ? TI_GEAR_R
                          : ((g[0] == 'P') ? TI_GEAR_P : TI_GEAR_N)));
        V.gear = n;
        ack(o, true, NULL);
    } else if (strcmp(c, "enable") == 0) {
        V.enable = jin_bool(o, "on", &b) ? b : V.enable;
        ack(o, true, NULL);
    } else if (strcmp(c, "vspeed") == 0) {
        if (jin_num(o, "kmh", &x) && (x >= 0.0) && (x <= 655.35)) {
            V.vspeed_kmh = (float)x;
            V.vspeed_valid = jin_bool(o, "valid", &b) ? b : true;
            ack(o, true, NULL);
        } else {
            ack(o, false, "kmh in [0, 655.35] (0.01 km/h in VCU_CMD b6-7)");
        }
    } else if (strcmp(c, "coolant") == 0) {
        if (jin_num(o, "c", &x) && (x >= -40.0) && (x <= 120.0)) {
            V.coolant_c = (float)x;
            s_pl.t_cool_c = (float)x;
            ack(o, true, NULL);
        } else {
            ack(o, false, "c in [-40, 120]");
        }
    } else if (strcmp(c, "bms") == 0) {
        if (jin_num(o, "chg_kw", &x)) {
            V.p_chg_w = (float)fmax(0.0, x * 1000.0);
        }
        if (jin_num(o, "dis_kw", &x)) {
            V.p_dis_w = (float)fmax(0.0, x * 1000.0);
        }
        if (jin_num(o, "pack_v", &x) && (x > 0.0) && (x < 1000.0)) {
            s_pl.v_ocv = (float)x;
        }
        ack(o, true, NULL);
    } else if (strcmp(c, "arm") == 0) {
        V.seq = VS_ARM;
        V.seq_t_ms = now_ms_u();
        ack(o, true, NULL);
    } else if (strcmp(c, "disarm") == 0) {
        V.seq = VS_DISARM;
        V.seq_t_ms = now_ms_u();
        V.torque_nm = 0.0f;
        ack(o, true, NULL);
    } else if (strcmp(c, "fault_reset") == 0) {
        V.fault_reset_until = now_ms_u() + 100u;
        ack(o, true, NULL);
    } else if (strcmp(c, "retry_auth") == 0) {
        V.retry_until = now_ms_u() + 3000u;
        ack(o, true, NULL);
    } else if (strcmp(c, "discharge") == 0) {
        V.discharge_until = now_ms_u() + 6000u;
        ack(o, true, (V.cont == TI_CONT_OPEN) ? NULL : "the firmware fires QDIS only with the contactors reported open (FW-17)");
    } else if (strcmp(c, "key") == 0) {
        const bool on = jin_bool(o, "on", &b) ? b : true;
        if (on && (sim_fs26_lpoff() || (g_app.sm.st == SM_OFF))) {
            power_up(); /* LPOFF: the wake-up is a power-on */
        } else {
            sim_set_kl15(on ? 13.5f : 0.0f);
        }
        ack(o, true, NULL);
    } else if (strcmp(c, "reboot") == 0) {
        power_up();
        hello();
        ack(o, true, NULL);
    } else if (strcmp(c, "provision") == 0) {
        if (jin_bool(o, "val", &b)) { s_prov.val = b; }
        if (jin_bool(o, "route", &b)) { s_prov.route = b; }
        if (jin_bool(o, "otp", &b)) { s_prov.otp = b; }
        if (jin_bool(o, "cal", &b)) { s_prov.cal = b; }
        if (jin_bool(o, "sa_key", &b)) { s_prov.key = b; }
        power_up();
        hello();
        ack(o, true, "power-cycled with the new provisioning");
    } else if (strcmp(c, "asc") == 0) {
        const bool on = jin_bool(o, "on", &b) ? b : true;
        if (on) {
            if ((g_app.br.mode == BR_IDLE) || (g_app.br.mode == BR_MOD)) {
                g_app.mod_req = false;
                br_enter_pwm_asc(&g_app.br, hal_time_us(), g_app.p); /* the section 4c MCU path, sim test hook */
                ack(o, true, "PWM-ASC entered through the firmware's bridge sequence (test hook)");
            } else {
                ack(o, false, "the bridge must be armed (idle or modulating)");
            }
        } else {
            const bool allowed = g_app.speed_known && (fabsf(g_app.speed_rpm) < g_app.n_x_rpm);
            if ((g_app.br.mode == BR_ASC) && br_exit_asc(&g_app.br, allowed)) {
                ack(o, true, NULL);
            } else {
                ack(o, false, "ASC exit refused: FW-06a exits only below n_x (or not in ASC)");
            }
        }
    } else if (strcmp(c, "inject") == 0) {
        const char *f = jin_str(o, "fault");
        if (f == NULL) {
            ack(o, false, "fault required");
        } else {
            set_inject(f, true, o);
        }
    } else if (strcmp(c, "clear") == 0) {
        const char *f = jin_str(o, "fault");
        if ((f == NULL) || (strcmp(f, "all") == 0)) {
            for (uint32_t i = 0u; i < (uint32_t)INJ_PERSIST_COUNT; i++) {
                if (s_inj[i]) {
                    jin_t tmp = *o;
                    set_inject(INJ[i], false, &tmp);
                }
            }
            s_oc_samples = 0u;
            ack(o, true, NULL);
        } else {
            set_inject(f, false, o);
        }
    } else if (strcmp(c, "param_set") == 0) {
        cmd_param_set(o);
    } else if (strcmp(c, "param_get") == 0) {
        const ti_cal_range_t *r = cal_find(jin_str(o, "name"));
        if (r == NULL) {
            ack(o, false, "unknown parameter (the cal_* rows of include/cal_ranges.h)");
        } else {
            jo_begin();
            jo_s("type", "ack");
            if (jin_num(o, "id", &x)) {
                jo_d("id", x);
            }
            jo_s("cmd", "param_get");
            jo_b("ok", true);
            jo_s("name", r->name);
            jo_s("ptype", cal_type(r->type));
            jo_f("value", cal_get(&s_p, r));
            jo_f("default", cal_get(&s_p_default, r));
            jo_f("min", r->min);
            jo_f("max", r->max);
            jo_d("t_ms", session_ms());
            jo_end();
        }
    } else if (strcmp(c, "param_reset") == 0) {
        s_p = s_p_default;
        s_p.cal_fs26_prog_id = s_prov.otp ? PROG_ID : 0xFFFFu;
        ack(o, true, NULL);
    } else if (strcmp(c, "params") == 0) {
        jo_begin();
        jo_s("type", "params");
        params_array("params");
        jo_end();
        ack(o, true, NULL);
    } else if (strcmp(c, "dtc_clear") == 0) {
        dtc_clear_uds(o); /* round 23: through the image's 0x14 (FW-40), its gates and its kept latches */
    } else if (strcmp(c, "uds") == 0) {
        const char *h = jin_str(o, "hex");
        hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u};
        uint32_t n = 0u;
        for (const char *p = h; (p != NULL) && (*p != '\0') && (n < HAL_CAN_MAX_LEN);) {
            char *end = NULL;
            const unsigned long by = strtoul(p, &end, 16);
            if (end == p) {
                break;
            }
            f.data[n++] = (uint8_t)by;
            p = end;
        }
        f.len = fd_len(n); /* round 23: a CAN-FD escape single frame (0x00 SF_DL) takes up to 64 bytes */
        if (n == 0u) {
            ack(o, false, "hex bytes required (an ISO 15765-2 single frame: PCI + payload, classic or CAN-FD escape)");
        } else {
            if (s_uds.n >= UDS_QMAX) {
                ack(o, false, "too many UDS requests in flight");
                return;
            }
            sim_can_inject(HAL_CAN_DIAG, &f);
            s_uds.id[s_uds.n] = 0.0;
            (void)jin_num(o, "id", &s_uds.id[s_uds.n]);
            s_uds.deadline[s_uds.n] = now_ms_u() + 50u;
            s_uds.n++;
            const uint32_t n0 = s_uds.n;
            for (uint32_t k = 0u; s_paused && (k < 200u) && (s_uds.n >= n0); k++) {
                step_ticks(1u); /* paused: run until the response (a segmented one takes a task per frame) */
            }
        }
    } else if (strcmp(c, "can_rx") == 0) {
        /* a raw frame onto a bus the firmware receives: the external-VCU path (vcu_model) and codec tests */
        const char *h = jin_str(o, "hex");
        hal_can_frame_t f = {0};
        double bus = 0.0;
        (void)jin_num(o, "bus", &bus);
        if (!jin_num(o, "can_id", &x) || (x < 0.0) || (x > 2047.0) || (h == NULL)) {
            ack(o, false, "can_id (11-bit) and hex required");
            return;
        }
        f.id = (uint32_t)x;
        for (const char *p = h; (*p != '\0') && (f.len < HAL_CAN_MAX_LEN);) {
            while ((*p == ' ') || (*p == ':')) {
                p++;
            }
            if ((p[0] == '\0') || (p[1] == '\0')) {
                break;
            }
            const char pair[3] = {p[0], p[1], '\0'};
            char *end = NULL;
            const unsigned long by = strtoul(pair, &end, 16);
            if (end != &pair[2]) {
                ack(o, false, "hex must be pairs of hex digits");
                return;
            }
            f.data[f.len++] = (uint8_t)by;
            p += 2;
        }
        sim_can_inject((bus >= 1.0) ? HAL_CAN_DIAG : HAL_CAN_VEHICLE, &f);
        if (s_ext_cmd && (bus < 1.0) && (f.id == CAN_ID_VCU_CMD) && (f.len >= 8u)) {
            /* the vehicle wires the contactors to its VCU: they follow the external VCU's command */
            const uint8_t cs = (uint8_t)(f.data[4] & 0x03u);
            V.cont = (cs == 1u) ? TI_CONT_OPEN : ((cs == 2u) ? TI_CONT_PRECHARGE : ((cs == 3u) ? TI_CONT_CLOSED : V.cont));
            s_pl.cont = (V.cont == TI_CONT_CLOSED) ? PL_CONT_CLOSED
                      : ((V.cont == TI_CONT_PRECHARGE) ? PL_CONT_PRECHARGE : PL_CONT_OPEN);
        }
        ack(o, true, NULL);
    } else if (strcmp(c, "vcu_model") == 0) {
        if (jin_bool(o, "cmd_frames", &b)) {
            s_ext_cmd = !b;
            V.seq = VS_IDLE;
        }
        if (jin_bool(o, "bms_frames", &b)) {
            s_ext_bms = !b;
        }
        ack(o, true, NULL);
    } else if (strcmp(c, "can_tap") == 0) {
        s_can_tap = jin_bool(o, "on", &b) ? b : true;
        ack(o, true, NULL);
    } else {
        ack(o, false, "unknown command");
    }
}

/* ---------------- --golden: codec test vectors from the firmware's own encoders/decoder ---------------- */
static const char *cont_name(ti_contactor_t c)
{
    static const char *const N[] = {"invalid", "open", "precharge", "closed"};
    return ((uint32_t)c < 4u) ? N[c] : "?";
}

static void golden_frame(const char *msg, const hal_can_frame_t *f)
{
    char hex[3u * HAL_CAN_MAX_LEN + 1u];
    hex_of(f->data, f->len, hex, false);
    jo_obj("frame");
    jo_s("msg", msg);
    jo_u("id", f->id);
    jo_u("len", f->len);
    jo_s("hex", hex);
    jo_close();
}

static void golden_vcu(uint8_t ctr, ti_gear_t gear, bool en, bool fr, float tq, ti_contactor_t cont, bool retry,
                       bool dis, bool shut, float cool, bool vs_valid, float vs_kmh)
{
    hal_can_frame_t f;
    can_encode_vcu_cmd(&f, ctr, gear, en, fr, tq, cont, retry, dis, shut, cool);
    can_vcu_cmd_vspeed(&f, vs_valid, vs_kmh); /* round 23 (FW-39); invalid and 0 leave the frame as it was */
    jo_begin();
    jo_s("type", "golden");
    jo_obj("in");
    jo_u("ctr", ctr);
    jo_s("gear", gear_name(gear));
    jo_b("enable", en);
    jo_b("fault_reset", fr);
    jo_f("torque_nm", tq);
    jo_s("contactors", cont_name(cont));
    jo_b("desat_retry_auth", retry);
    jo_b("discharge_req", dis);
    jo_b("shutdown_req", shut);
    jo_f("coolant_c", cool);
    jo_b("vspeed_valid", vs_valid);
    jo_f("vspeed_kmh", vs_kmh);
    jo_close();
    golden_frame("VCU_CMD", &f);
    can_cmd_t c;
    can_cmd_init(&c);
    jo_obj("decoded"); /* the firmware's receive path (can_cmd_rx) on this frame, first frame of a stream */
    jo_b("accepted", can_cmd_rx(&c, &f, 1000u, &s_p));
    jo_s("gear", gear_name(c.gear));
    jo_b("enable", c.enable_req);
    jo_b("fault_reset", c.fault_reset_req);
    jo_f("torque_nm", c.torque_req_nm);
    jo_s("contactors", cont_name(c.contactors));
    jo_b("desat_retry_auth", c.desat_retry_auth);
    jo_b("discharge_req", c.discharge_req);
    jo_b("shutdown_req", c.shutdown_req);
    jo_b("coolant_valid", c.coolant_valid);
    jo_f("coolant_c", c.coolant_c);
    jo_b("vspeed_valid", c.vspeed_valid);
    jo_f("vspeed_kmh", c.vspeed_kmh);
    jo_close();
    jo_end();
}

static void golden_bms(uint8_t ctr, float v, float chg, float dis)
{
    hal_can_frame_t f;
    can_encode_vcu_bms(&f, ctr, v, chg, dis);
    jo_begin();
    jo_s("type", "golden");
    jo_obj("in");
    jo_u("ctr", ctr);
    jo_f("v_pack_v", v);
    jo_f("p_chg_w", chg);
    jo_f("p_dis_w", dis);
    jo_close();
    golden_frame("VCU_BMS", &f);
    can_cmd_t c;
    can_cmd_init(&c);
    jo_obj("decoded");
    jo_b("accepted", can_cmd_rx(&c, &f, 1000u, &s_p));
    jo_f("v_pack_v", c.v_pack);
    jo_f("p_chg_w", c.p_chg_w);
    jo_f("p_dis_w", c.p_dis_w);
    jo_close();
    jo_end();
}

static void golden_status(uint8_t ctr, const can_status_t *st)
{
    hal_can_frame_t f;
    can_status_encode(st, ctr, &f);
    jo_begin();
    jo_s("type", "golden");
    jo_obj("in");
    jo_u("ctr", ctr);
    jo_u("state", st->state);
    jo_u("bridge", st->bridge);
    jo_u("hv", (unsigned)st->hv);
    jo_b("self_test_done", st->self_test_done);
    jo_b("keep_hv", st->keep_hv);
    jo_b("derate", st->derate);
    jo_b("fault", st->fault);
    jo_b("zero_torque", st->zero_torque);
    jo_b("discharging", st->discharging);
    jo_b("precharge_refused", st->precharge_refused);
    jo_b("speed_valid", st->speed_valid);
    jo_f("torque_nm", st->torque_nm);
    jo_f("torque_cmd_nm", st->torque_cmd_nm);
    jo_f("speed_rpm", st->speed_rpm);
    jo_f("vdc_v", st->vdc_v);
    jo_b("vdc_valid", st->vdc_valid);
    jo_f("t_module_c", st->t_module_c);
    jo_u("n_dtc", st->n_dtc);
    jo_u("first_dtc", st->first_dtc);
    jo_b("no_safe_state", st->no_safe_state);
    jo_b("service_required", st->service_required);
    jo_b("open_contactors_req", st->open_contactors_req);
    jo_b("speed_limit_req", st->speed_limit_req);
    jo_u("evidence_missing", st->evidence_missing);
    jo_close();
    golden_frame("INV_STATUS", &f);
    jo_end();
}

static void golden_uds(const char *req_hex)
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u};
    uint32_t n = 0u;
    for (const char *p = req_hex; (*p != '\0') && (n < 8u);) {
        char *end = NULL;
        const unsigned long by = strtoul(p, &end, 16);
        if (end == p) {
            break;
        }
        f.data[n++] = (uint8_t)by;
        p = end;
    }
    uds_t u;
    uds_init(&u, NULL, NULL, NULL); /* the default build: no key function */
    hal_can_frame_t r;
    const bool answered = uds_handle(&u, &f, 0x12345678u, &r);
    char h[3u * HAL_CAN_MAX_LEN + 1u];
    jo_begin();
    jo_s("type", "golden");
    jo_obj("in");
    jo_s("uds_request", req_hex);
    jo_close();
    golden_frame("UDS_REQ", &f);
    jo_obj("decoded");
    jo_b("answered", answered);
    if (answered) {
        hex_of(r.data, r.len, h, false);
        jo_u("rsp_id", r.id);
        jo_s("rsp_hex", h);
    }
    jo_close();
    jo_end();
}

static void golden(void)
{
    golden_vcu(0u, TI_GEAR_D, true, false, 100.0f, TI_CONT_CLOSED, false, false, false, 50.0f, false, 0.0f);
    golden_vcu(5u, TI_GEAR_R, true, false, -123.4f, TI_CONT_PRECHARGE, true, false, false, 25.0f, false, 0.0f);
    golden_vcu(15u, TI_GEAR_N, false, true, 0.0f, TI_CONT_OPEN, false, true, true, -40.0f, false, 0.0f);
    golden_vcu(7u, TI_GEAR_P, false, false, 3276.7f, TI_CONT_INVALID, false, false, false, 214.0f, false, 0.0f);
    golden_vcu(9u, TI_GEAR_D, true, false, -5000.0f, TI_CONT_CLOSED, false, false, false, 300.0f, false, 0.0f);
    golden_vcu(3u, TI_GEAR_D, true, false, 0.05f, TI_CONT_CLOSED, false, false, false, 50.4f, false, 0.0f);
    golden_vcu(12u, TI_GEAR_D, true, false, 450.0f, TI_CONT_CLOSED, false, false, false, 65.0f, false, 0.0f);
    /* round 23 (FW-39): the vehicle speed — at rest and valid (the service-mode precondition), a speed, saturation */
    golden_vcu(1u, TI_GEAR_N, false, false, 0.0f, TI_CONT_CLOSED, false, false, false, 50.0f, true, 0.0f);
    golden_vcu(2u, TI_GEAR_D, true, false, 80.0f, TI_CONT_CLOSED, false, false, false, 50.0f, true, 123.45f);
    golden_vcu(4u, TI_GEAR_D, true, false, 0.0f, TI_CONT_CLOSED, false, false, false, 50.0f, true, 700.0f);
    golden_bms(0u, 750.0f, 100000.0f, 250000.0f);
    golden_bms(9u, 401.3f, 0.0f, 12345.0f);
    golden_bms(15u, 6553.5f, 6553500.0f, 0.0f);
    const can_status_t run = {.state = 7u, .bridge = 2u, .hv = TI_HV_PRESENT, .self_test_done = true, .torque_nm = 150.0f,
                              .torque_cmd_nm = 152.3f,
                              .speed_rpm = 3000.0f, .vdc_v = 748.6f, .vdc_valid = true, .t_module_c = 55.3f, .n_dtc = 1u,
                              .first_dtc = 40u, .speed_valid = true};
    golden_status(3u, &run);
    const can_status_t flt = {.state = 9u, .bridge = 3u, .hv = TI_HV_UNKNOWN, .self_test_done = true, .keep_hv = true,
                              .fault = true, .zero_torque = true, .speed_rpm = -512.7f, .vdc_v = 0.0f, .vdc_valid = false,
                              .t_module_c = -40.0f, .n_dtc = 300u, .first_dtc = 17u, .no_safe_state = true,
                              .service_required = true, .open_contactors_req = true, .speed_limit_req = true,
                              .evidence_missing = 0x1Fu};
    golden_status(15u, &flt);
    const can_status_t der = {.state = 8u, .bridge = 2u, .hv = TI_HV_PRESENT, .derate = true, .discharging = true,
                              .precharge_refused = true, .torque_nm = -99.95f, .torque_cmd_nm = -3500.0f,
                              .speed_rpm = 16000.0f, .vdc_v = 880.0f,
                              .vdc_valid = true, .t_module_c = 200.0f, .n_dtc = 2u, .first_dtc = 75u, .speed_valid = true,
                              .evidence_missing = 0x18u};
    golden_status(0u, &der);
    const can_status_t off = {.state = 0u, .bridge = 0u, .hv = TI_HV_SAFE, .torque_nm = 0.0f, .speed_rpm = 0.0f,
                              .vdc_v = 12.34f, .vdc_valid = true, .t_module_c = 25.0f};
    golden_status(1u, &off);
    golden_uds("02 27 01");
    golden_uds("04 31 01 F0 10");
    golden_uds("02 10 03");
    golden_uds("10 08 27 01 00 00 00 00");
}

/* ---------------- tick ---------------- */
static void run_due(void)
{
    const double now = session_ms();
    for (int i = 0; i < s_qn;) {
        if (s_q[i].due <= now) {
            const jin_t o = s_q[i].o;
            s_q[i] = s_q[s_qn - 1];
            s_qn--;
            exec(&o);
        } else {
            i++;
        }
    }
}

static void uds_pop(void)
{
    if (s_uds.n == 0u) {
        return;
    }
    for (uint32_t i = 1u; i < s_uds.n; i++) {
        s_uds.id[i - 1u] = s_uds.id[i];
        s_uds.deadline[i - 1u] = s_uds.deadline[i];
    }
    s_uds.n--;
}

/* A response is complete (or failed): the ack of the oldest request — "rsp" its first frame as received, "msg" the
 * UDS payload reassembled, "frames" — or, during the bridge's own sequence (dtc_clear), kept for it. */
static void uds_done(const char *first_hex, const uint8_t *msg, uint32_t len, uint32_t frames, const char *err)
{
    if (s_uds.sync) {
        s_uds.sync_got = true;
        s_uds.sync_len = (err != NULL) ? 0u : ((len < sizeof s_uds.sync_msg) ? len : (uint32_t)sizeof s_uds.sync_msg);
        (void)memcpy(s_uds.sync_msg, msg, s_uds.sync_len);
        return;
    }
    static char mhex[(3u * UDS_MSG_MAX) + 1u];
    hex_of(msg, len, mhex, true);
    jo_begin();
    jo_s("type", "ack");
    if ((s_uds.n > 0u) && (s_uds.id[0] != 0.0)) {
        jo_d("id", s_uds.id[0]);
    }
    jo_s("cmd", "uds");
    jo_b("ok", err == NULL);
    if (err != NULL) {
        jo_s("err", err);
    }
    jo_s("rsp", first_hex);
    jo_u("rsp_id", UDS_ID_RSP);
    jo_s("msg", mhex);
    jo_u("frames", frames);
    jo_d("t_ms", session_ms());
    jo_end();
    uds_pop();
}

/* One frame on 0x7E9 (ISO 15765-2): a single frame (classic 0x0L, CAN-FD escape 0x00 SF_DL), a first frame (this
 * tester answers ContinueToSend, BS 0, STmin 0: the ECU sends the rest one frame per task), a consecutive frame, or
 * the ECU's flow control for a tester's own first frame (reported as the response to it, "msg" empty). */
static void uds_frame(const hal_can_frame_t *f, uint32_t n)
{
    char hex[(3u * HAL_CAN_MAX_LEN) + 1u];
    hex_of(f->data, n, hex, true);
    const uint32_t pci = (uint32_t)f->data[0] >> 4;
    if (s_uds.seg && (pci == 2u)) {
        if ((f->data[0] & 0x0Fu) != s_uds.sn) {
            s_uds.seg = false;
            uds_done(s_uds.first, s_uds.msg, s_uds.got, s_uds.frames + 1u, "consecutive frame out of sequence");
            return;
        }
        const uint32_t left = s_uds.len - s_uds.got;
        const uint32_t take = (left < (n - 1u)) ? left : (n - 1u);
        (void)memcpy(&s_uds.msg[s_uds.got], &f->data[1], take);
        s_uds.got += take;
        s_uds.frames++;
        s_uds.sn = (uint8_t)((s_uds.sn + 1u) & 0x0Fu);
        s_uds.deadline[0] = now_ms_u() + 50u; /* N_Cr of this tester: 50 ms per frame */
        if (s_uds.got >= s_uds.len) {
            s_uds.seg = false;
            uds_done(s_uds.first, s_uds.msg, s_uds.len, s_uds.frames, NULL);
        }
        return;
    }
    if (s_uds.seg) { /* anything else ends it: the ECU abandoned the response (a new request, N_Bs) */
        s_uds.seg = false;
        uds_done(s_uds.first, s_uds.msg, s_uds.got, s_uds.frames, "segmented response abandoned by the ECU");
    }
    if ((pci == 1u) && (n > 2u)) {
        const uint32_t len = (((uint32_t)f->data[0] & 0x0Fu) << 8) | f->data[1];
        if ((len <= (n - 2u)) || (len > UDS_MSG_MAX)) {
            uds_done(hex, NULL, 0u, 1u, "first frame length invalid for this tester");
            return;
        }
        (void)memcpy(s_uds.msg, &f->data[2], n - 2u);
        s_uds.len = len;
        s_uds.got = n - 2u;
        s_uds.frames = 1u;
        s_uds.sn = 1u;
        s_uds.seg = true;
        (void)snprintf(s_uds.first, sizeof s_uds.first, "%s", hex);
        hal_can_frame_t fc = {.id = UDS_ID_REQ, .len = 8u};
        fc.data[0] = 0x30u; /* FS 0 ContinueToSend, BS 0 (no further flow control), STmin 0 */
        sim_can_inject(HAL_CAN_DIAG, &fc);
        if (s_uds.n > 0u) {
            s_uds.deadline[0] = now_ms_u() + 50u;
        }
        return;
    }
    const uint8_t *m = &f->data[1];
    uint32_t len = 0u;
    if (pci == 0u) {
        len = f->data[0];
        if ((len == 0u) && (n > 2u)) {
            len = f->data[1];
            m = &f->data[2];
        }
        const uint32_t room = n - (uint32_t)(m - f->data);
        len = (len < room) ? len : room;
    }
    uds_done(hex, m, len, 1u, NULL);
}

/* A periodic frame on 0x6E9 (FW-40 0x2A): [pDID, data] — the data as sent, padded to the CAN-FD length. */
static void periodic_line(const hal_can_frame_t *f, uint32_t n)
{
    char hex[(3u * HAL_CAN_MAX_LEN) + 1u];
    hex_of(&f->data[1], (n > 1u) ? (n - 1u) : 0u, hex, true);
    jo_begin();
    jo_s("type", "periodic");
    jo_u("pdid", f->data[0]);
    jo_u("did", 0xF200u | f->data[0]);
    jo_s("hex", hex);
    jo_d("t_ms", session_ms());
    jo_end();
}

/* The bridge's own tester request (dtc_clear): one classic single frame, the simulation run until its response
 * (at most 100 ms of simulated time). */
static bool uds_sync(const uint8_t *req, uint32_t n, uint8_t *rsp, uint32_t *rsp_len)
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u};
    f.data[0] = (uint8_t)n;
    (void)memcpy(&f.data[1], req, n);
    s_uds.sync = true;
    s_uds.sync_got = false;
    sim_can_inject(HAL_CAN_DIAG, &f);
    for (uint32_t k = 0u; (k < 100u) && !s_uds.sync_got; k++) {
        step_ticks(1u);
    }
    s_uds.sync = false;
    *rsp_len = s_uds.sync_got ? s_uds.sync_len : 0u;
    (void)memcpy(rsp, s_uds.sync_msg, *rsp_len);
    return *rsp_len > 0u;
}

static bool uds_step(const jin_t *o, const char *what, const uint8_t *req, uint32_t n, uint8_t want, uint8_t *r)
{
    uint32_t len = 0u;
    if (uds_sync(req, n, r, &len) && (r[0] == want)) {
        return true;
    }
    char e[160];
    if ((len >= 3u) && (r[0] == 0x7Fu)) {
        (void)snprintf(e, sizeof e, "%s: NRC 0x%02X%s", what, r[2],
                       ((r[2] == 0x22u) && (req[0] == 0x27u)) ? " (no key: provision {\"sa_key\":true})"
                       : (r[2] == 0x22u)                      ? " (conditions: the bridge disarmed, HV absent)"
                                                              : "");
    } else {
        (void)snprintf(e, sizeof e, "%s: no response", what);
    }
    ack(o, false, e);
    return false;
}

/* dtc_clear: the tester's sequence through the image's own services — SecurityAccess (the bench key), then 0x14
 * ClearDiagnosticInformation for all groups; the gates and the latches 0x14 keeps are the image's (FW-40). */
static void dtc_clear_uds(const jin_t *o)
{
    if (s_uds.n > 0u) {
        ack(o, false, "UDS requests in flight: wait for their acks");
        return;
    }
    static const uint8_t SEED[] = {0x27u, 0x01u};
    static const uint8_t CLR[] = {0x14u, 0xFFu, 0xFFu, 0xFFu};
    uint8_t r[64];
    if (!uds_step(o, "27 01 requestSeed", SEED, 2u, 0x67u, r)) {
        return;
    }
    if ((r[2] | r[3] | r[4] | r[5]) != 0u) { /* a zero seed: already unlocked */
        uint8_t kq[2u + UDS_SA_LEN] = {0x27u, 0x02u};
        (void)bench_key(&r[2], &kq[2]);
        if (!uds_step(o, "27 02 sendKey", kq, (uint32_t)sizeof kq, 0x67u, r)) {
            return;
        }
    }
    if (uds_step(o, "14 FF FF FF ClearDiagnosticInformation", CLR, 4u, 0x54u, r)) {
        ack(o, true, "UDS 27 01, 27 02, 14 FF FF FF: cleared (the DTCs 0x14 keeps stay)");
    }
}

static void hex_of(const uint8_t *d, uint32_t n, char *out, bool spaced)
{
    out[0] = '\0';
    for (uint32_t i = 0u; i < n; i++) {
        (void)snprintf(&out[(spaced ? 3u : 2u) * i], 4u, spaced ? "%02X " : "%02X", d[i]);
    }
    if (spaced && (n > 0u)) {
        out[(3u * n) - 1u] = '\0';
    }
}

static void can_line(uint8_t bus, const hal_can_frame_t *f)
{
    char hex[3u * HAL_CAN_MAX_LEN + 1u];
    hex_of(f->data, (f->len <= HAL_CAN_MAX_LEN) ? f->len : HAL_CAN_MAX_LEN, hex, false);
    jo_begin();
    jo_s("type", "can");
    jo_u("bus", bus);
    jo_u("id", f->id);
    jo_u("len", f->len);
    jo_s("hex", hex);
    jo_d("t_ms", session_ms());
    jo_end();
}

static void status_rx(void)
{
    hal_can_frame_t f;
    while (sim_can_pop_tx(HAL_CAN_VEHICLE, &f)) {
        if (f.id == CAN_ID_INV_STATUS) {
            hex_of(f.data, (f.len <= HAL_CAN_MAX_LEN) ? f.len : HAL_CAN_MAX_LEN, s_status_hex, false);
        }
        if (s_can_tap) {
            can_line(HAL_CAN_VEHICLE, &f);
        }
    }
    while (sim_can_pop_tx(HAL_CAN_DIAG, &f)) {
        if (s_can_tap) {
            can_line(HAL_CAN_DIAG, &f);
        }
        const uint32_t n = (f.len <= HAL_CAN_MAX_LEN) ? f.len : HAL_CAN_MAX_LEN;
        if (f.id == UDS_ID_PERIODIC) {
            periodic_line(&f, n);
        } else if (f.id == UDS_ID_RSP) {
            uds_frame(&f, n);
        } else {
            /* nothing else is sent on the diagnostic bus */
        }
    }
    while ((s_uds.n > 0u) && (now_ms_u() > s_uds.deadline[0])) {
        jo_begin();
        jo_s("type", "ack");
        if (s_uds.id[0] != 0.0) {
            jo_d("id", s_uds.id[0]);
        }
        jo_s("cmd", "uds");
        jo_b("ok", true);
        jo_s("rsp", "");
        jo_s("info", s_uds.seg ? "segmented response incomplete (no consecutive frame within 50 ms)"
                               : "no response within 50 ms (not a request to 0x7E1, or a frame of a segmented one)");
        jo_d("t_ms", session_ms());
        jo_end();
        s_uds.seg = false;
        uds_pop();
    }
}

static void tick(void)
{
    grids_check();
    run_due();
    if ((s_tick % 10u) == 0u) {
        vcu_tx();
    }
    vcu_step();
    isrs_until(s_task_ns);
    plant_to(s_task_ns);
    s_task_ns += TICK_NS;
    app_task_1ms(&g_app);
    app_idle(&g_app);
    plant_1ms(&s_pl, sim_now_ns());
    resolver_and_temps();
    status_rx();
    s_tick++;
    if ((session_ms() - s_last_tel) >= ((double)s_tel_ms - 0.5)) {
        s_last_tel = session_ms();
        telemetry();
    }
    if (sim_flash_take_reset()) { /* FW-38 ECUReset (hal_sys_reset): the card restarts, as at `reboot` */
        log_line("info", "ECUReset (FW-38): the card restarts");
        power_up();
        hello();
    }
}

static void step_ticks(uint32_t n)
{
    for (uint32_t k = 0u; k < n; k++) {
        tick();
    }
}

/* ---------------- stdin ---------------- */
static char s_in[1u << 16];
static size_t s_in_len;

static void handle_line(char *line)
{
    size_t n = strlen(line);
    while ((n > 0u) && ((line[n - 1u] == '\r') || (line[n - 1u] == ' '))) {
        line[--n] = '\0';
    }
    if (n == 0u) {
        return;
    }
    jin_t o;
    if (!jin_parse(line, &o)) {
        jin_t e = {0};
        ack(&e, false, "not a flat JSON object");
        return;
    }
    double in = 0.0;
    if (jin_num(&o, "in_ms", &in) && (in > 0.0)) {
        if (s_qn >= QMAX) {
            ack(&o, false, "schedule queue full");
            return;
        }
        s_q[s_qn].due = session_ms() + in;
        s_q[s_qn].o = o;
        s_qn++;
        return; /* acknowledged when it runs */
    }
    exec(&o);
}

/* false on EOF */
static bool read_stdin(int timeout_ms)
{
    struct pollfd pfd = {.fd = 0, .events = POLLIN};
    const int r = poll(&pfd, 1, timeout_ms);
    if ((r > 0) && ((pfd.revents & (POLLNVAL | POLLERR)) != 0) && ((pfd.revents & POLLIN) == 0)) {
        return false; /* not a readable stream (macOS reports /dev/null as POLLNVAL): treat as end of input */
    }
    if ((r <= 0) || ((pfd.revents & (POLLIN | POLLHUP)) == 0)) {
        return true;
    }
    const ssize_t got = read(0, &s_in[s_in_len], sizeof s_in - s_in_len - 1u);
    if (got <= 0) {
        return false;
    }
    s_in_len += (size_t)got;
    s_in[s_in_len] = '\0';
    char *start = s_in;
    for (char *nl = strchr(start, '\n'); nl != NULL; nl = strchr(start, '\n')) {
        *nl = '\0';
        if ((size_t)(nl - start) < CMD_LINE_MAX) {
            handle_line(start);
        }
        start = nl + 1;
    }
    s_in_len = (size_t)(&s_in[s_in_len] - start);
    memmove(s_in, start, s_in_len);
    if (s_in_len >= (sizeof s_in - 1u)) {
        s_in_len = 0u; /* an overlong line: dropped */
    }
    return true;
}

/* --script FILE: every line is a command, scheduled like stdin (use "in_ms" for the time after start-up) */
static bool load_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    char line[CMD_LINE_MAX];
    while (fgets(line, sizeof line, f) != NULL) {
        line[strcspn(line, "\n")] = '\0';
        const char *p = line;
        while ((*p == ' ') || (*p == '\t')) {
            p++;
        }
        if ((*p != '\0') && (*p != '#')) {
            handle_line(line);
        }
    }
    (void)fclose(f);
    return true;
}

static void usage(void)
{
    (void)fprintf(stderr, "usage: sim_bridge [--sku 8xx_sic|8xx_igbt|4xx_igbt|4xx_sic] [--rate HZ] [--time FACTOR] [--paused]\n"
                          "                  [--script FILE]   (commands, one JSON object per line; '#' lines are comments)\n"
                          "       sim_bridge --golden   (CAN/UDS codec test vectors from the firmware's encoders, then exit)\n");
}

int main(int argc, char **argv)
{
    const char *script = NULL;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--sku") == 0) && ((i + 1) < argc)) {
            const char *s = argv[++i];
            s_sku = (strcmp(s, "8xx_igbt") == 0) ? TI_SKU_8XX_IGBT : ((strcmp(s, "4xx_igbt") == 0) ? TI_SKU_4XX_IGBT
                  : ((strcmp(s, "4xx_sic") == 0) ? TI_SKU_4XX_SIC : TI_SKU_8XX_SIC));
        } else if ((strcmp(argv[i], "--rate") == 0) && ((i + 1) < argc)) {
            const double hz = atof(argv[++i]);
            s_tel_ms = (hz > 0.0) ? (uint32_t)fmin(fmax(floor((1000.0 / hz) + 0.5), 1.0), 1000.0) : 10u;
        } else if ((strcmp(argv[i], "--time") == 0) && ((i + 1) < argc)) {
            s_factor = fmin(fmax(atof(argv[++i]), 0.0), 100.0);
        } else if (strcmp(argv[i], "--paused") == 0) {
            s_paused = true;
        } else if ((strcmp(argv[i], "--script") == 0) && ((i + 1) < argc)) {
            script = argv[++i];
        } else if (strcmp(argv[i], "--golden") == 0) {
            provision_params();
            s_p = s_p_default;
            golden();
            return 0;
        } else {
            usage();
            return 2;
        }
    }
    (void)fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    provision_params();
    s_p = s_p_default;
    V.send = true;
    V.gear = TI_GEAR_D;
    V.coolant_c = 50.0f;
    V.p_chg_w = 100000.0f;
    V.p_dis_w = 250000.0f;
    V.slew_nm_s = 3000.0f; /* a bench VCU rate-limits torque; 0 = steps (the firmware slews them: cal_torque_slew_nm_s) */
    V.vspeed_valid = true; /* the dyno bench's vehicle is at rest: speed valid and zero (FW-39's precondition) */
    power_up();
    hello();
    if ((script != NULL) && !load_script(script)) {
        (void)fprintf(stderr, "sim_bridge: cannot read the script %s\n", script);
        return 2;
    }
    s_rt_wall = wall_ms();
    s_rt_sim = sim_ms();
    for (;;) {
        int timeout = 1;
        if (!s_paused) {
            if (s_factor <= 0.0) {
                step_ticks(20u);
                timeout = 0;
            } else {
                const double due = s_sim0 + ((wall_ms() - s_wall0) * s_factor);
                uint32_t n = 0u;
                while ((sim_ms() < due) && (n < 200u)) {
                    tick();
                    n++;
                }
                if ((due - sim_ms()) > 250.0) {
                    s_wall0 = wall_ms(); /* the host cannot keep up: re-anchor (rt reports it) */
                    s_sim0 = sim_ms();
                    static double s_last_warn = -1.0e9;
                    if ((s_wall0 - s_last_warn) > 5000.0) {
                        s_last_warn = s_wall0;
                        log_line("warn", "the host cannot keep up with this time factor: running slower than requested");
                    }
                }
                timeout = (n > 0u) ? 0 : 1;
            }
        }
        const double w = wall_ms();
        if ((w - s_rt_wall) >= 1000.0) {
            s_rt = (sim_ms() - s_rt_sim) / (w - s_rt_wall);
            s_rt_wall = w;
            s_rt_sim = sim_ms();
        }
        if (!read_stdin(timeout)) {
            return 0; /* the server went away */
        }
    }
}
