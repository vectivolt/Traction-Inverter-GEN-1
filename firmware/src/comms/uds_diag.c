/* uds_diag.c — FW-40: ReadDTCInformation, ReadDataByIdentifier, ReadDataByPeriodicIdentifier and
 * ClearDiagnosticInformation with their ISO 15765-2 transport (uds_diag.h; contract §10h). Runs in the 1 ms task
 * (app.c: diag): uds_diag_rx() from uds_handle() for each request, uds_diag_tick() once after them. Every read is a
 * bounded copy of state the application already holds; nothing here is read back by control. */
#include "uds_diag.h"

#include <string.h>

#include "app.h"
#include "commission.h"
#include "dtc.h"
#include "gpio.h"
#include "timer.h"
#include "uds_update.h"
#include "update.h"
#include "verify.h"
#include "boot.h"

#define SID_CLEAR 0x14u
#define SID_RDTCI 0x19u
#define SID_RDBI 0x22u
#define SID_RDBPI 0x2Au
#define SID_WDBI 0x2Eu /* round 23 (FW-46): only DID 0xFD46 here */
#define POS 0x40u
#define NEG 0x7Fu

#define RDTCI_COUNT 0x01u
#define RDTCI_BY_MASK 0x02u
#define RDTCI_SNAPSHOT 0x04u
#define RDTCI_SUPPORTED 0x0Au
#define STATUS_AVAIL 0x7Fu /* dtc.h bits 0-6; warningIndicatorRequested (bit 7) is not tracked */
#define DTC_FORMAT 0x01u   /* ISO_14229-1_DTCFormat */
#define GROUP_ALL 0xFFFFFFu
#define REC_ALL 0xFFu

#define DID_SNAPSHOT 0xFD2Fu /* the 0x19 04 record's one identifier: nv_fault_t (not readable by 0x22) */
#define SNAP_LEN 49u
#define SNAP_REC (4u + SNAP_LEN) /* record number, number of identifiers, the DID, the data */
#define DID_PER_RQ_MAX 8u
#define DID_LEN_MAX (2u * TQ_RIPPLE_N) /* the longest DID: 0xFD46 (72 bytes; the telemetry DIDs are <= 43) */

#define SF_CLASSIC_MAX 7u
#define SF_FD_MAX 62u
#define FF_DATA 62u
#define CF_DATA 63u
#define N_AS_MS 1000u
#define N_BS_MS 1000u
#define N_CR_MS 1000u /* round 23 (item 11): a segmented request's next consecutive frame */
#define FC_CTS 0x30u
#define FC_OVERFLOW 0x32u
#define N_WFT_MAX 16u
#define SCAN_PER_TICK 2u /* ring records read per task: each is a 512-byte slot copy and a CRC-32 */
#define PAD 0xAAu

enum { TP_IDLE = 0, TP_FIRST, TP_WAIT_FC, TP_CF };

_Static_assert((3u + (4u * ((uint32_t)DTC_COUNT - 1u))) <= UDS_DIAG_MSG_MAX, "0x19 0A outgrows the response buffer");
_Static_assert((6u + (NV_FAULT_RING * SNAP_REC)) <= UDS_DIAG_MSG_MAX, "0x19 04 outgrows the response buffer");
_Static_assert(UDS_DIAG_MSG_MAX <= 4095u, "the ISO 15765-2 first frame carries 12 bits of length");
_Static_assert((UPD_BLOCK_MAX <= UDS_DIAG_RX_MAX) && (UDS_DIAG_RX_MAX <= 4095u),
               "a TransferData block must fit the segmented request's buffer and the classic first frame");
_Static_assert((uint32_t)SS_ROW_COUNT <= 16u, "0xF200 carries the rows in 16 bits");
_Static_assert((uint32_t)TEMP_COUNT == 7u, "0xF205 carries seven temperature channels");
_Static_assert(NV_FAULT_RING < REC_ALL, "record numbers");

/* the application instance: app_init() hands it to uds_init() as the context */
static app_t *app_of(const uds_t *u) { return (app_t *)u->ctx; }

/* ---------------- encoding: big-endian, f32 = IEEE 754 binary32 ---------------- */
static uint8_t *put8(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    return &p[1];
}

static uint8_t *put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return &p[2];
}

static uint8_t *put24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 16);
    return put16(&p[1], v);
}

static uint8_t *put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    return put24(&p[1], v);
}

static uint8_t *putf(uint8_t *p, float x)
{
    uint32_t b = 0u;
    (void)memcpy(&b, &x, sizeof b);
    return put32(p, b);
}

static uint32_t bit(bool b, uint32_t k) { return b ? (1u << k) : 0u; }

/* ---------------- the DIDs (layouts: contract §10h) ---------------- */
typedef struct {
    const app_t *a;
    uint32_t now_ms;
    uint32_t now_us;
} did_in_t;

typedef uint8_t *(*did_fn_t)(const did_in_t *c, uint8_t *p);

typedef struct {
    uint16_t did;
    uint8_t len;
    did_fn_t fn;
} did_row_t;

static uint8_t *did_state(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    const bool any = fm_any(&a->fm);
    p = put8(p, (uint32_t)a->sm.st);
    p = put8(p, (uint32_t)a->br.mode);
    p = put8(p, (uint32_t)dis_hv_state(&a->vdc));
    p = put8(p, bit(a->sm.st == SM_FAULT, 0u) | bit(a->tlim.derate_active, 1u) | bit(a->fm.keep_hv, 2u) |
                    bit(a->fm.no_safe_state, 3u) | bit(a->service_required, 4u) | bit(a->speed_limit_req, 5u) |
                    bit(a->so.self_test_done, 6u) | bit(a->so.torque_enable, 7u));
    p = put8(p, any ? (uint32_t)a->fm.dec.action : 0u);
    p = put8(p, (any && (a->fm.dec_row < SS_ROW_COUNT)) ? (uint32_t)a->fm.dec_row : 0xFFu);
    p = put16(p, a->fm.active);
    return put16(p, a->fm.latched);
}

static uint8_t *did_arming(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    const uint32_t ev = a->evidence;
    p = put8(p, ev);
    p = put8(p, ARM_EV_ALL & ~ev);
    for (uint32_t k = 0u; k < 5u; k++) { /* FW-24 item by item, in the ARM_EV_* bit order */
        p = put8(p, (ev >> k) & 1u);
    }
    p = put8(p, bit(a->so.arm, 0u) | bit(a->no_arm, 1u) | bit(a->init == SM_OK, 2u) | bit(a->cal_err == 0u, 3u) |
                    bit(a->gains_ok, 4u) | bit(a->fs0b_released, 5u) | bit(a->gp.st == GP_READY, 6u) |
                    bit(hal_gpio_out_state(HAL_DO_MCU_GATE_EN), 7u));
    p = put8(p, (uint32_t)a->selftest);
    p = put8(p, (uint32_t)a->st.failed);
    p = put8(p, a->fm.desat_count);
    return put8(p, bit(a->fm.retry_used, 0u) | bit(a->fm.desat_blocked, 1u) | bit(a->rec_active, 2u));
}

static uint8_t *did_speed_torque(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    p = putf(p, a->speed_rpm);
    p = put8(p, bit(a->rslv.valid, 0u) | bit(a->speed_known, 1u) | bit(a->rslv_seen, 2u));
    p = putf(p, a->can.torque_req_nm);
    p = putf(p, a->t_cmd_nm);
    p = putf(p, a->t_act_nm);
    p = put8(p, (uint32_t)a->can.gear);
    p = put8(p, (uint32_t)a->dir.active);
    return put8(p, bit(a->mod_req, 0u) | bit(a->zero_now, 1u) | bit(a->so.torque_reduced, 2u) |
                       bit(a->can.enable_req, 3u));
}

static uint8_t *did_currents(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    const isns_t *s = &a->isns;
    p = put32(p, a->t_isr_us);
    for (uint32_t k = 0u; k < 3u; k++) {
        p = putf(p, s->i_a[k]);
    }
    p = putf(p, a->foc.id);
    p = putf(p, a->foc.iq);
    p = putf(p, a->foc.id_ref);
    p = putf(p, a->foc.iq_ref);
    return put8(p, bit(s->valid, 0u) | bit(s->fresh, 1u) | bit(s->ch_valid[0], 2u) | bit(s->ch_valid[1], 3u) |
                       bit(s->ch_valid[2], 4u) | bit(s->sum_fault, 5u) | bit(s->stuck_fault, 6u) |
                       bit(s->open_wire[0] || s->open_wire[1] || s->open_wire[2], 7u));
}

static uint8_t *did_dc_link(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    const vdc_t *v = &a->vdc;
    p = putf(p, v->vdc);
    p = putf(p, v->v_ch[0]);
    p = putf(p, v->v_ch[1]);
    p = putf(p, v->vofs_v);
    p = putf(p, v->v5gd_v);
    p = putf(p, a->can.v_pack);
    p = put8(p, bit(v->valid, 0u) | bit(v->ch_valid[0], 1u) | bit(v->ch_valid[1], 2u) | bit(v->disagree, 3u) |
                    bit(v->vofs_ok, 4u) | bit(v->v5gd_ok, 5u) | bit(v->bms_mismatch, 6u) |
                    bit(v->ch_failsafe[0] || v->ch_failsafe[1], 7u));
    return put8(p, (uint32_t)dis_hv_state(v));
}

static uint8_t *did_temperatures(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    uint32_t valid = 0u;
    uint32_t faults = 0u;
    for (uint32_t k = 0u; k < (uint32_t)TEMP_COUNT; k++) {
        const temp_ch_state_t *ch = &a->temp.ch[k];
        p = putf(p, ch->t_c);
        valid |= bit(ch->valid, k);
        faults |= ((uint32_t)ch->fault & 3u) << (2u * k);
    }
    p = put8(p, valid);
    p = put16(p, faults);
    bool any = false;
    bool all = false;
    p = putf(p, temp_module_max(&a->temp, &any, &all));
    p = putf(p, a->can.coolant_c);
    return put8(p, bit(a->can.coolant_valid, 0u));
}

static uint8_t *did_limits(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    const torque_lim_t *l = &a->tlim;
    p = putf(p, l->i_limit_rms_a);
    p = putf(p, l->t_lim_motor_nm);
    p = putf(p, l->t_lim_regen_nm);
    p = putf(p, l->derate);
    p = putf(p, l->coolant_factor);
    p = putf(p, l->peak_used_s);
    p = putf(p, a->can.p_chg_w);
    p = putf(p, a->can.p_dis_w);
    return put8(p, bit(l->derate_active, 0u) | bit(l->peak_exhausted, 1u) |
                       bit(can_bms_fresh(&a->can, c->now_ms, a->p), 2u) | bit(a->speed_limit_req, 3u) |
                       bit(a->so.torque_reduced, 4u));
}

static uint32_t age_or_never(bool ever, uint32_t now, uint32_t since) { return ever ? (now - since) : 0xFFFFFFFFu; }

static uint8_t *did_heartbeat(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    const fs26_t *f = &a->fs;
    const can_cmd_t *can = &a->can;
    p = put8(p, bit(f->init_done, 0u) | bit(f->wd_running, 1u) | bit(a->fs0b_released, 2u) |
                    bit(f->fs1b_short_high, 3u));
    p = put8(p, f->wd_err_cnt);
    p = put32(p, f->n_refresh);
    p = put32(p, age_or_never(f->n_refresh > 0u, c->now_us, f->last_refresh_us));
    p = put32(p, f->n_comm_err);
    p = put32(p, a->n_isr);
    const uint32_t isr_age = c->now_us - a->t_isr_us; /* an entry after now (the ISR preempted this copy) reads 0 */
    p = put32(p, (isr_age > 0x7FFFFFFFu) ? 0u : isr_age);
    p = put8(p, bit(can_cmd_fresh(can, c->now_ms, a->p), 0u) | bit(can->ever, 1u) |
                    bit(can_bms_fresh(can, c->now_ms, a->p), 2u) | bit(can->bms_ever, 3u));
    p = put32(p, age_or_never(can->ever, c->now_ms, can->last_ms));
    p = put32(p, age_or_never(can->bms_ever, c->now_ms, can->bms_last_ms));
    p = put32(p, can->n_frozen);
    p = put32(p, can->n_jump);
    return put32(p, can->n_len);
}

static uint8_t *did_uptime(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    uint32_t active = 0u;
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        active += dtc_active((dtc_id_t)i) ? 1u : 0u;
    }
    const dtc_id_t first = dtc_first_active();
    p = put32(p, c->now_ms);
    p = put32(p, a->key_cycle);
    p = put8(p, a->cold_start ? 1u : 0u);
    p = put16(p, active);
    p = put16(p, dtc_confirmed_count());
    return put24(p, (first != DTC_NONE) ? dtc_code(first) : 0u);
}

static uint8_t *did_identity(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    p = put32(p, TI_FW_ID);
    p = put8(p, (uint32_t)a->p->sku);
    p = put8(p, (uint32_t)a->hw_sku);
    p = put8(p, a->cal.sku);
    for (uint32_t k = 0u; k < 8u; k++) {
        p = put8(p, a->serial[k]);
    }
    return put16(p, (uint32_t)DTC_COUNT - 1u);
}

static uint8_t *did_calibration(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    const uint32_t bad = ti_params_validate(a->p);
    p = put16(p, a->cal.layout_version);
    p = put16(p, CALIB_LAYOUT_VERSION);
    p = put32(p, a->cal_err);
    p = put32(p, a->cal.motor_id);
    p = put32(p, a->cal.crc32);
    p = put32(p, a->cal.fsw_hz);
    return put8(p, (bad > 255u) ? 255u : bad);
}

static uint8_t *did_validation(const did_in_t *c, uint8_t *p)
{
    const app_t *a = c->a;
    arm_validation_t v;
    (void)memset(&v, 0, sizeof v);
    const bool present = nv_read(NV_REC_VALIDATION, &v, (uint16_t)sizeof v); /* the RAM mirror of the data flash */
    p = put8(p, present ? 1u : 0u);
    p = put8(p, arm_validation_flags(&v, present, a->serial, a->p->sku, TI_FW_ID, a->p));
    p = put8(p, v.flags);
    p = put8(p, v.sku);
    p = put32(p, v.fw_id);
    p = put32(p, v.ovp_chain_ns);
    for (uint32_t k = 0u; k < 8u; k++) {
        p = put8(p, v.hw_serial[k]);
    }
    return put16(p, v.layout);
}

/* round 23: the root of trust of the image verifier (verify.h): kind, then the key id */
static uint8_t *did_root(const did_in_t *c, uint8_t *p)
{
    (void)c;
    uint32_t key_id = 0u;
    const img_root_t kind = img_root(&key_id);
    p = put8(p, (uint32_t)kind);
    return put32(p, key_id);
}

/* round 23, second pass: the FW-38 boot record (boot.h) — the tool's rollback pre-check over CAN (the anti-rollback
 * counter), the state after an activation, the last refusal's reason; state 0xFF = no record */
static uint8_t *did_boot(const did_in_t *c, uint8_t *p)
{
    (void)c;
    boot_rec_t r;
    if (!boot_rec_read(&r)) {
        (void)memset(&r, 0, sizeof r);
        r.state = 0xFFu;
    }
    p = put8(p, r.state);
    p = put8(p, r.last);
    p = put8(p, r.target & 0xFFu);
    p = put32(p, r.sec_counter);
    p = put8(p, r.lkg_valid);
    return put8(p, r.last_err);
}

/* round 23, second pass: the record's motor data for the service tool over CAN (the ripple-map import needs ψ and the
 * pole pairs; nothing else carried them): ψ µWb, pp, L_d nH, L_q nH, R_s µΩ, n_max rpm, i_d,demag 0.1 A */
static uint8_t *did_motor(const did_in_t *c, uint8_t *p)
{
    const motor_t *m = &c->a->cal.motor;
    p = put32(p, (uint32_t)(m->psi_wb * 1.0e6f + 0.5f));
    p = put8(p, m->pp);
    p = put32(p, (uint32_t)(m->ld_h * 1.0e9f + 0.5f));
    p = put32(p, (uint32_t)(m->lq_h * 1.0e9f + 0.5f));
    p = put32(p, (uint32_t)(m->rs_ohm * 1.0e6f + 0.5f));
    p = put16(p, (uint32_t)(m->n_max_rpm + 0.5f));
    return put16(p, (uint32_t)(m->id_demag_a * 10.0f + 0.5f));
}

/* round 23, second pass: the ACTIVE record's inductance maps (FW-45) — the tool's read-back after a commit and a key cycle
 * (the routine's per-point results belong to the running key cycle only): 6 x L_d nH BE32, 6 x L_q nH BE32, i_map 0.1 A */
static uint8_t *did_maps(const did_in_t *c, uint8_t *p)
{
    const motor_t *m = &c->a->cal.motor;
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
        p = put32(p, (uint32_t)(m->ld_map_h[k] * 1.0e9f + 0.5f));
    }
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
        p = put32(p, (uint32_t)(m->lq_map_h[k] * 1.0e9f + 0.5f));
    }
    return put16(p, (uint32_t)(m->i_map_a * 10.0f + 0.5f));
}

/* round 23 (FW-46): the ripple table the next commit writes (commission.h) */
static uint8_t *did_ripple(const did_in_t *c, uint8_t *p)
{
    mc_ripple_read(c->a, p);
    return &p[2u * TQ_RIPPLE_N];
}

static const did_row_t DIDS[] = {
    {0xF200u, 10u, did_state},        {0xF201u, 12u, did_arming},      {0xF202u, 20u, did_speed_torque},
    {0xF203u, 33u, did_currents},     {0xF204u, 26u, did_dc_link},     {0xF205u, 40u, did_temperatures},
    {0xF206u, 33u, did_limits},       {0xF207u, 43u, did_heartbeat},   {0xF208u, 16u, did_uptime},
    {0xFD20u, 17u, did_identity},     {0xFD21u, 21u, did_calibration}, {0xFD22u, 22u, did_validation},
    {0xFD23u, 5u, did_root},          {0xFD24u, 9u, did_boot},         {0xFD25u, 21u, did_motor},
    {0xFD26u, (uint8_t)(8u * MOTOR_MAP_N + 2u), did_maps},
    {MC_DID_RIPPLE, (uint8_t)DID_LEN_MAX, did_ripple},
};

static const did_row_t *did_find(uint32_t did)
{
    for (uint32_t k = 0u; k < TI_ARRAY_LEN(DIDS); k++) {
        if (DIDS[k].did == did) {
            return &DIDS[k];
        }
    }
    return NULL;
}

/* One DID's bytes (exactly r->len of them, through a bounded scratch buffer). The current-loop ISR (priority 2)
 * preempts this task: a copy it interrupted is taken again, so the fields come from one ISR's results (three attempts;
 * the copy is a few hundred cycles against a 50–100 us period). */
static void did_copy(const app_t *a, const did_row_t *r, uint8_t *out, uint32_t now_ms, uint32_t now_us)
{
    const did_in_t c = {.a = a, .now_ms = now_ms, .now_us = now_us};
    uint8_t tmp[DID_LEN_MAX];
    for (uint32_t k = 0u; k < 3u; k++) {
        const uint32_t t0 = a->t_isr_us;
        (void)r->fn(&c, tmp);
        if (a->t_isr_us == t0) {
            break;
        }
    }
    (void)memcpy(out, tmp, r->len);
}

/* ---------------- responses and the transport ---------------- */
static void respond(uds_diag_t *d, uint32_t len, uint32_t now_ms)
{
    d->len = (uint16_t)len;
    d->off = 0u;
    d->tp = TP_FIRST;
    d->t_tp_ms = now_ms;
}

static void refuse(uds_diag_t *d, uint8_t sid, uint8_t code, uint32_t now_ms)
{
    d->msg[0] = NEG;
    d->msg[1] = sid;
    d->msg[2] = code;
    respond(d, 3u, now_ms);
}

static void abandon(uds_diag_t *d)
{
    if ((d->tp != TP_IDLE) || d->scan) {
        d->n_aborted++;
    }
    d->tp = TP_IDLE;
    d->scan = false;
}

/* the smallest CAN-FD data length (8, 12, 16, 20, 24, 32, 48, 64) holding n bytes; 8 at least, as FW-32 pads */
static uint8_t fd_len(uint32_t n)
{
    static const uint8_t L[] = {8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u};
    uint32_t k = 0u;
    while ((k < (TI_ARRAY_LEN(L) - 1u)) && (L[k] < n)) { /* at most 7 steps */
        k++;
    }
    return L[k];
}

static void frame(hal_can_frame_t *f, uint32_t id, uint32_t n)
{
    *f = (hal_can_frame_t){.id = id, .len = fd_len(n)};
    (void)memset(f->data, PAD, sizeof f->data);
}

/* A single frame to UDS_ID_REQ, classic or escape format (ISO 15765-2:2016 9.6.2): its payload and length. */
static bool sf_parse(const hal_can_frame_t *rq, const uint8_t **m, uint32_t *n)
{
    if ((rq->len < 2u) || (rq->len > HAL_CAN_MAX_LEN) || ((rq->data[0] & 0xF0u) != 0u)) {
        return false;
    }
    if (rq->data[0] != 0u) {
        *n = rq->data[0];
        *m = &rq->data[1];
        return (*n <= SF_CLASSIC_MAX) && (*n <= (uint32_t)(rq->len - 1u));
    }
    *n = rq->data[1];
    *m = &rq->data[2];
    return (rq->len > 8u) && (*n >= 1u) && (*n <= (uint32_t)(rq->len - 2u));
}

/* STmin (ISO 15765-2:2016 Table 21): 0-127 ms, F1-F9 100-900 us, reserved values as 127 ms */
static uint32_t stmin_us(uint8_t x)
{
    if (x <= 0x7Fu) {
        return (uint32_t)x * 1000u;
    }
    return ((x >= 0xF1u) && (x <= 0xF9u)) ? ((uint32_t)(x - 0xF0u) * 100u) : 127000u;
}

static bool flow_control(uds_diag_t *d, const hal_can_frame_t *fc, uint32_t now_ms, uint32_t now_us)
{
    if ((d->tp != TP_WAIT_FC) || (fc->len < 3u)) {
        return false; /* no segmented response waits for it: not FW-40's */
    }
    const uint32_t fs = fc->data[0] & 0x0Fu;
    if (fs == 0u) { /* ContinueToSend */
        d->bs = fc->data[1];
        d->bs_left = d->bs;
        d->stmin_us = stmin_us(fc->data[2]);
        d->t_cf_us = now_us - d->stmin_us; /* the first consecutive frame at once */
        d->t_tp_ms = now_ms;
        d->tp = TP_CF;
    } else if ((fs == 1u) && (d->n_wait < N_WFT_MAX)) { /* Wait: N_Bs again */
        d->n_wait++;
        d->t_tp_ms = now_ms;
    } else {
        abandon(d); /* Overflow, an invalid flow status, or waited too often */
    }
    return true;
}

/* One response frame (a single, first or consecutive frame). true: the mailbox was used or is busy — nothing else
 * this task. A busy mailbox keeps the frame for the next task, for N_As at most. */
static bool send_usdt(uds_diag_t *d, const hal_can_frame_t *f, uint32_t take, uint32_t now_ms, uint32_t now_us)
{
    if (!hal_can_tx(HAL_CAN_DIAG, f)) {
        if (ti_elapsed(now_ms, d->t_tp_ms, N_AS_MS)) {
            abandon(d);
        }
        return true;
    }
    const bool first = (d->tp == TP_FIRST);
    d->off = (uint16_t)(d->off + take);
    d->t_tp_ms = now_ms;
    d->t_cf_us = now_us;
    if (d->off >= d->len) {
        d->tp = TP_IDLE;
    } else if (first) {
        d->tp = TP_WAIT_FC;
        d->sn = 1u;
        d->n_wait = 0u;
    } else {
        d->sn = (uint8_t)((d->sn + 1u) & 0x0Fu);
        if ((d->bs != 0u) && (--d->bs_left == 0u)) {
            d->tp = TP_WAIT_FC;
            d->n_wait = 0u;
        }
    }
    return true;
}

static bool tp_step(uds_diag_t *d, uint32_t now_ms, uint32_t now_us)
{
    hal_can_frame_t f;
    if (d->tp == TP_FIRST) {
        uint32_t take = d->len;
        if (d->len <= SF_CLASSIC_MAX) {
            frame(&f, UDS_ID_RSP, 1u + d->len);
            f.data[0] = (uint8_t)d->len;
            (void)memcpy(&f.data[1], d->msg, d->len);
        } else if (d->len <= SF_FD_MAX) {
            frame(&f, UDS_ID_RSP, 2u + d->len);
            f.data[0] = 0u;
            f.data[1] = (uint8_t)d->len;
            (void)memcpy(&f.data[2], d->msg, d->len);
        } else {
            take = FF_DATA;
            frame(&f, UDS_ID_RSP, HAL_CAN_MAX_LEN);
            f.data[0] = (uint8_t)(0x10u | ((uint32_t)d->len >> 8));
            f.data[1] = (uint8_t)d->len;
            (void)memcpy(&f.data[2], d->msg, FF_DATA);
        }
        return send_usdt(d, &f, take, now_ms, now_us);
    }
    if (d->tp == TP_CF) {
        if (!ti_elapsed(now_us, d->t_cf_us, d->stmin_us)) {
            return false;
        }
        const uint32_t left = (uint32_t)d->len - d->off;
        const uint32_t take = (left < CF_DATA) ? left : CF_DATA;
        frame(&f, UDS_ID_RSP, 1u + take);
        f.data[0] = (uint8_t)(0x20u | d->sn);
        (void)memcpy(&f.data[1], &d->msg[d->off], take);
        return send_usdt(d, &f, take, now_ms, now_us);
    }
    if ((d->tp == TP_WAIT_FC) && ti_elapsed(now_ms, d->t_tp_ms, N_BS_MS)) {
        abandon(d); /* N_Bs: no flow control came */
    }
    return false;
}

/* ---------------- 0x19 ReadDTCInformation ---------------- */
static uint32_t dtc_of(uint32_t number) /* the DTC with this number; 0 = none */
{
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        if (dtc_code((dtc_id_t)i) == number) {
            return i;
        }
    }
    return 0u;
}

static uint8_t *snapshot(uint8_t *p, const nv_fault_t *r)
{
    p = put32(p, r->key_cycle);
    p = put32(p, r->t_ms);
    p = put8(p, r->row);
    p = put8(p, r->action);
    p = putf(p, r->speed_rpm);
    p = putf(p, r->id_a);
    p = putf(p, r->iq_a);
    p = putf(p, r->vdc_v);
    p = put8(p, r->op.ext);
    p = put8(p, r->op.state);
    p = put8(p, r->op.t_valid);
    p = putf(p, r->op.t_cmd_nm);
    p = putf(p, r->op.t_act_nm);
    p = putf(p, r->op.t_mod_c);
    p = putf(p, r->op.t_mt1_c);
    return putf(p, r->op.t_mt2_c);
}

static void scan_restart(uds_diag_t *d)
{
    d->scan_age = 0u;
    d->scan_hits = 0u;
    d->len = 6u; /* the header stays: 59 04, the DTC, its status */
    (void)memset(&d->scan_top, 0, sizeof d->scan_top);
    d->scan_top_ok = nv_read_fault(0u, &d->scan_top);
}

/* The ring newest first, SCAN_PER_TICK records per task; record n = the n-th newest event of this DTC. A record
 * written meanwhile moves the ring under the scan: the newest record differs at the end, and the scan starts again
 * (once; then NRC 0x22). */
static void scan_step(uds_diag_t *d, uint32_t now_ms)
{
    if (!d->scan) {
        return;
    }
    bool done = false;
    for (uint32_t k = 0u; (k < SCAN_PER_TICK) && !done; k++) {
        nv_fault_t r;
        (void)memset(&r, 0, sizeof r);
        if ((d->scan_age >= NV_FAULT_RING) || !nv_read_fault(d->scan_age, &r)) {
            done = true;
        } else {
            d->scan_age++;
            if (r.code == d->scan_id) {
                d->scan_hits++;
                if ((d->scan_rec == REC_ALL) || (d->scan_rec == d->scan_hits)) {
                    uint8_t *p = &d->msg[d->len];
                    p = put8(p, d->scan_hits);
                    p = put8(p, 1u);
                    p = put16(p, DID_SNAPSHOT);
                    (void)snapshot(p, &r);
                    d->len = (uint16_t)(d->len + SNAP_REC);
                    done = (d->scan_rec == d->scan_hits);
                }
            }
        }
    }
    if (!done) {
        return;
    }
    nv_fault_t top;
    (void)memset(&top, 0, sizeof top);
    const bool ok = nv_read_fault(0u, &top);
    if ((ok != d->scan_top_ok) || (ok && (memcmp(&top, &d->scan_top, sizeof top) != 0))) {
        if (d->scan_again > 0u) {
            d->scan_again--;
            scan_restart(d);
            return;
        }
        d->scan = false;
        refuse(d, SID_RDTCI, UDS_NRC_CONDITIONS, now_ms);
        return;
    }
    d->scan = false;
    respond(d, d->len, now_ms);
}

static void read_dtc_info(uds_diag_t *d, const uint8_t *m, uint32_t n, uint32_t now_ms)
{
    if (n < 2u) {
        refuse(d, SID_RDTCI, UDS_NRC_LENGTH, now_ms);
        return;
    }
    const uint8_t sub = m[1];
    if ((sub != RDTCI_COUNT) && (sub != RDTCI_BY_MASK) && (sub != RDTCI_SNAPSHOT) && (sub != RDTCI_SUPPORTED)) {
        refuse(d, SID_RDTCI, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, now_ms);
        return;
    }
    const uint32_t want = (sub == RDTCI_SUPPORTED) ? 2u : ((sub == RDTCI_SNAPSHOT) ? 6u : 3u);
    if (n != want) {
        refuse(d, SID_RDTCI, UDS_NRC_LENGTH, now_ms);
        return;
    }
    d->msg[0] = SID_RDTCI + POS;
    d->msg[1] = sub;
    if (sub == RDTCI_SNAPSHOT) {
        const uint32_t number = ((uint32_t)m[2] << 16) | ((uint32_t)m[3] << 8) | m[4];
        const uint32_t id = dtc_of(number);
        const uint8_t rec = m[5];
        if ((id == 0u) || (rec == 0u) || ((rec > NV_FAULT_RING) && (rec != REC_ALL))) {
            refuse(d, SID_RDTCI, UDS_NRC_OUT_OF_RANGE, now_ms);
            return;
        }
        (void)put24(&d->msg[2], number);
        d->msg[5] = dtc_status((dtc_id_t)id);
        d->scan = true;
        d->scan_id = (uint16_t)id;
        d->scan_rec = rec;
        d->scan_again = 1u;
        scan_restart(d);
        return; /* the response comes from the scan (uds_diag_tick) */
    }
    const uint32_t mask = (sub == RDTCI_SUPPORTED) ? 0u : ((uint32_t)m[2] & STATUS_AVAIL);
    uint32_t count = 0u;
    uint8_t *p = put8(&d->msg[2], STATUS_AVAIL);
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        const uint8_t st = dtc_status((dtc_id_t)i);
        if ((sub == RDTCI_SUPPORTED) || ((st & mask) != 0u)) {
            count++;
            if (sub != RDTCI_COUNT) {
                p = put24(p, dtc_code((dtc_id_t)i));
                p = put8(p, st);
            }
        }
    }
    if (sub == RDTCI_COUNT) {
        p = put8(p, DTC_FORMAT);
        p = put16(p, count);
    }
    respond(d, (uint32_t)(p - d->msg), now_ms);
}

/* ---------------- 0x22 ReadDataByIdentifier ---------------- */
static void read_dids(const app_t *a, uds_diag_t *d, const uint8_t *m, uint32_t n, uint32_t now_ms, uint32_t now_us)
{
    if ((n < 3u) || (((n - 1u) & 1u) != 0u) || (((n - 1u) / 2u) > DID_PER_RQ_MAX)) {
        refuse(d, SID_RDBI, UDS_NRC_LENGTH, now_ms);
        return;
    }
    uint32_t len = 1u;
    d->msg[0] = SID_RDBI + POS;
    for (uint32_t k = 1u; k < n; k += 2u) { /* at most DID_PER_RQ_MAX */
        const uint32_t did = ((uint32_t)m[k] << 8) | m[k + 1u];
        const did_row_t *r = did_find(did);
        if (r == NULL) {
            continue; /* ISO 14229-1: the supported ones are answered, the others left out */
        }
        (void)put16(&d->msg[len], did);
        did_copy(a, r, &d->msg[len + 2u], now_ms, now_us);
        len += 2u + r->len;
    }
    if (len == 1u) {
        refuse(d, SID_RDBI, UDS_NRC_OUT_OF_RANGE, now_ms);
        return;
    }
    respond(d, len, now_ms);
}

/* ---------------- 0x2A ReadDataByPeriodicIdentifier ---------------- */
static const uint32_t RATE_MS[4] = {0u, 100u, 10u, 1u}; /* by transmission mode: slow, medium, fast */

static bool pdid_ok(uint32_t pdid) { return did_find(0xF200u | pdid) != NULL; }

static uint32_t pdid_slot(const uds_diag_t *d, uint32_t pdid)
{
    for (uint32_t i = 0u; i < d->n_per; i++) {
        if (d->per[i].pdid == pdid) {
            return i;
        }
    }
    return UDS_DIAG_PDID_MAX;
}

static void pdid_remove(uds_diag_t *d, uint32_t i)
{
    for (uint32_t j = i; (j + 1u) < d->n_per; j++) {
        d->per[j] = d->per[j + 1u];
    }
    d->n_per--;
}

static void periodic_rq(uds_diag_t *d, const uint8_t *m, uint32_t n, uint32_t now_ms)
{
    if (n < 2u) {
        refuse(d, SID_RDBPI, UDS_NRC_LENGTH, now_ms);
        return;
    }
    const uint8_t mode = m[1];
    if (mode == 4u) { /* stopSending: the listed ones, or all */
        if (n == 2u) {
            d->n_per = 0u;
        }
        for (uint32_t k = 2u; k < n; k++) {
            const uint32_t i = pdid_slot(d, m[k]);
            if (i < d->n_per) {
                pdid_remove(d, i);
            }
        }
        d->msg[0] = SID_RDBPI + POS;
        respond(d, 1u, now_ms);
        return;
    }
    if ((mode < 1u) || (mode > 3u)) {
        refuse(d, SID_RDBPI, UDS_NRC_OUT_OF_RANGE, now_ms);
        return;
    }
    if (n < 3u) {
        refuse(d, SID_RDBPI, UDS_NRC_LENGTH, now_ms);
        return;
    }
    uint32_t seen[8] = {0u}; /* 256 bits: each pDID counted once */
    uint32_t ok = 0u;
    uint32_t add = 0u;
    for (uint32_t k = 2u; k < n; k++) {
        const uint32_t x = m[k];
        if (!pdid_ok(x) || ((seen[x >> 5] & (1u << (x & 31u))) != 0u)) {
            continue;
        }
        seen[x >> 5] |= 1u << (x & 31u);
        ok++;
        add += (pdid_slot(d, x) < d->n_per) ? 0u : 1u;
    }
    if ((ok == 0u) || ((d->n_per + add) > UDS_DIAG_PDID_MAX)) {
        refuse(d, SID_RDBPI, UDS_NRC_OUT_OF_RANGE, now_ms); /* none supported, or the scheduler is full: nothing done */
        return;
    }
    for (uint32_t x = 0u; x < 256u; x++) { /* each supported pDID of the request: scheduled, or its rate replaced */
        if ((seen[x >> 5] & (1u << (x & 31u))) == 0u) {
            continue;
        }
        uint32_t i = pdid_slot(d, x);
        if (i >= d->n_per) {
            i = d->n_per;
            d->n_per++;
        }
        d->per[i] = (uds_pdid_t){.pdid = (uint8_t)x, .mode = mode, .t_ms = now_ms - RATE_MS[mode]}; /* due now */
    }
    d->msg[0] = SID_RDBPI + POS;
    respond(d, 1u, now_ms);
}

/* One due periodic DID per task (round robin), rate-limited by its mode; a busy mailbox drops it. */
static void periodic_step(const app_t *a, uds_diag_t *d, uint32_t now_ms, uint32_t now_us)
{
    for (uint32_t k = 0u; k < d->n_per; k++) { /* at most UDS_DIAG_PDID_MAX */
        const uint32_t i = (d->rr + k) % d->n_per;
        uds_pdid_t *s = &d->per[i];
        const did_row_t *r = did_find(0xF200u | s->pdid);
        if ((r == NULL) || !ti_elapsed(now_ms, s->t_ms, RATE_MS[s->mode & 3u])) {
            continue;
        }
        hal_can_frame_t f;
        frame(&f, UDS_ID_PERIODIC, 1u + r->len);
        f.data[0] = s->pdid;
        did_copy(a, r, &f.data[1], now_ms, now_us);
        s->t_ms = now_ms;
        d->rr = (uint8_t)((i + 1u) % d->n_per);
        if (hal_can_tx(HAL_CAN_DIAG, &f)) {
            d->n_sent++;
        } else {
            d->n_dropped++;
        }
        return;
    }
}

/* ---------------- 0x14 ClearDiagnosticInformation ---------------- */
/* A clear releases no latch, so it must not hide one. Kept: the DESAT class (FW-15: the second DESAT latches for the
 * key cycle, a retry is the VCU's one authorisation; the fault ring keeps their records), the service lock (FW-26:
 * NVM-kept, only the FW-32 routine clears it), and every failure that forbids arming for the key cycle (FW-38's
 * programming session included) — cleared, the inverter would still refuse to arm with nothing saying why. Each is
 * judged again at the next power-up (the store is RAM). Every other DTC's monitor runs on and sets it again while its
 * fault is present. */
static bool kept(uint32_t id)
{
    switch ((dtc_id_t)id) {
    case DTC_DESAT_HS:
    case DTC_DESAT_LS:
    case DTC_DESAT_REPEAT:
    case DTC_DESAT_PENDING_BOOT:
    case DTC_FLT_RECOVERY_FAIL:
    case DTC_QDIS_STUCK_ON:
    case DTC_FS26_PROGID:
    case DTC_FS26_OTP_CORRUPT:
    case DTC_FS26_DEBUG_MODE:
    case DTC_FS26_INIT_READBACK:
    case DTC_FS26_SPI:
    case DTC_FS26_RELEASE:
    case DTC_FS1B_SHORT_HIGH:
    case DTC_FS26_GPIO1_OTP:
    case DTC_HWID_OPEN:
    case DTC_HWID_SHORT:
    case DTC_HWID_UNKNOWN:
    case DTC_SKU_MISMATCH:
    case DTC_CALIB_INVALID:
    case DTC_PARAMS_INVALID:
    case DTC_GAINS:
    case DTC_PWM_LOCK:
    case DTC_ARM_EVIDENCE:
    case DTC_SELFTEST_FAIL:
    case DTC_SELFTEST_NO_PASS:
    case DTC_GATE_POWER:
    case DTC_PRECHARGE_PLATEAU:
    case DTC_PRECHARGE_TAU:
    case DTC_PRECHARGE_TIMEOUT:
    case DTC_FW_UPDATE:
        return true;
    default:
        return false;
    }
}

static void clear_dtcs(app_t *a, uds_t *u, uds_diag_t *d, const uint8_t *m, uint32_t n, uint32_t now_ms)
{
    if (n != 4u) {
        refuse(d, SID_CLEAR, UDS_NRC_LENGTH, now_ms);
        return;
    }
    const uint32_t group = ((uint32_t)m[1] << 16) | ((uint32_t)m[2] << 8) | m[3];
    const uint32_t id = (group == GROUP_ALL) ? 0u : dtc_of(group);
    if ((group != GROUP_ALL) && (id == 0u)) {
        refuse(d, SID_CLEAR, UDS_NRC_OUT_OF_RANGE, now_ms);
        return;
    }
    if (!u->unlocked) {
        refuse(d, SID_CLEAR, UDS_NRC_SECURITY_DENIED, now_ms);
        return;
    }
    /* the FW-32 conditions: HV absent (unknown counts as present) and the bridge disarmed */
    if ((dis_hv_state(&a->vdc) != TI_HV_SAFE) || (a->br.mode != BR_DISARMED) || ((id != 0u) && kept(id))) {
        refuse(d, SID_CLEAR, UDS_NRC_CONDITIONS, now_ms);
        return;
    }
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        if (((id == 0u) || (i == id)) && !kept(i)) {
            dtc_clear((dtc_id_t)i);
        }
    }
    u->unlocked = false; /* one clear per unlock, as the FW-32 routine */
    d->msg[0] = SID_CLEAR + POS;
    respond(d, 1u, now_ms);
}

/* ---------------- round 23 (item 11): a segmented request ---------------- */
/* The receiving side of ISO 15765-2 for FW-38's TransferData blocks (round 23: and FW-46's 2E FD 46). A first frame (a 12-bit length; the 32-bit escape
 * form only to refuse what UDS_DIAG_RX_MAX cannot hold — an overflow flow control) starts a reception and drops a pending
 * response, as any request does; this ECU answers ContinueToSend with block size UDS_DIAG_RX_BS and STmin 0 — the frames
 * one 1 ms task reads — from uds_diag_tick; consecutive frames in sequence fill the buffer, a wrong sequence number or
 * N_Cr (1 s) without one abandons it (ISO 15765-2 9.6.4.4, 9.8.2). The complete request goes to the programming services
 * or — round 23 (FW-46) — to the ripple table's write, the only ones that take one (another service's segmented request is
 * NRC 0x13): their single-frame response is queued like FW-40's. Bounded: one frame is one copy of at most 63 bytes. */
/* round 23 (FW-46): 2E FD 46 — its only form is segmented (75 bytes); a single frame of it is the wrong length */
static bool ripple_rq(const uint8_t *m, uint32_t n)
{
    return (n >= 3u) && (m[0] == SID_WDBI) && ((((uint32_t)m[1] << 8) | m[2]) == MC_DID_RIPPLE);
}

static void ripple_write(app_t *a, uds_diag_t *d, const uint8_t *m, uint32_t n, uint32_t now_ms)
{
    const uint8_t code = mc_ripple_write(a, m, n);
    if (code != 0u) {
        refuse(d, SID_WDBI, code, now_ms);
        return;
    }
    d->msg[0] = SID_WDBI + POS;
    (void)put16(&d->msg[1], MC_DID_RIPPLE);
    respond(d, 3u, now_ms);
}

static void rx_dispatch(uds_t *u, uds_diag_t *d, uint32_t now_ms)
{
    if (ripple_rq(d->rx, d->rx_len)) {
        ripple_write(app_of(u), d, d->rx, d->rx_len, now_ms); /* round 23 (FW-46) */
        return;
    }
    if (!upd_uds_claims(d->rx, d->rx_len)) {
        refuse(d, d->rx[0], UDS_NRC_LENGTH, now_ms);
        return;
    }
    hal_can_frame_t r;
    if (upd_uds(u, d->rx, d->rx_len, &r) && (r.data[0] >= 1u) && (r.data[0] <= SF_CLASSIC_MAX)) {
        (void)memcpy(d->msg, &r.data[1], r.data[0]);
        respond(d, r.data[0], now_ms);
    }
}

static bool rx_first(uds_diag_t *d, const hal_can_frame_t *f, uint32_t now_ms)
{
    if (f->len < 8u) {
        return true; /* not a first frame of this bus */
    }
    uint32_t len = ((uint32_t)(f->data[0] & 0x0Fu) << 8) | f->data[1];
    uint32_t hdr = 2u;
    if (len == 0u) { /* the escape form: a 32-bit length */
        len = ((uint32_t)f->data[2] << 24) | ((uint32_t)f->data[3] << 16) | ((uint32_t)f->data[4] << 8) | f->data[5];
        hdr = 6u;
    }
    abandon(d); /* a new request: a pending response or scan is dropped */
    d->rx_on = false;
    if (len > UDS_DIAG_RX_MAX) {
        d->fc_owed = FC_OVERFLOW;
        d->n_aborted++;
        return true;
    }
    const uint32_t take = (uint32_t)f->len - hdr;
    if (len <= take) {
        return true; /* what a single frame carries: not a first frame's (ignored, ISO 15765-2 9.6.3.2) */
    }
    (void)memcpy(d->rx, &f->data[hdr], take);
    d->rx_len = (uint16_t)len;
    d->rx_off = (uint16_t)take;
    d->rx_sn = 1u;
    d->rx_bs_left = UDS_DIAG_RX_BS;
    d->rx_on = true;
    d->fc_owed = FC_CTS;
    d->t_rx_ms = now_ms;
    return true;
}

static bool rx_consecutive(uds_t *u, uds_diag_t *d, const hal_can_frame_t *f, uint32_t now_ms)
{
    if (!d->rx_on) {
        return false; /* no reception: nobody's */
    }
    if (((f->data[0] & 0x0Fu) != d->rx_sn) || (f->len < 2u)) {
        d->rx_on = false; /* out of sequence: the reception is abandoned */
        d->n_aborted++;
        return true;
    }
    const uint32_t left = (uint32_t)d->rx_len - d->rx_off;
    const uint32_t take = (((uint32_t)f->len - 1u) < left) ? ((uint32_t)f->len - 1u) : left;
    (void)memcpy(&d->rx[d->rx_off], &f->data[1], take);
    d->rx_off = (uint16_t)(d->rx_off + take);
    d->rx_sn = (uint8_t)((d->rx_sn + 1u) & 0x0Fu);
    d->t_rx_ms = now_ms;
    if (d->rx_off >= d->rx_len) {
        d->rx_on = false;
        rx_dispatch(u, d, now_ms);
    } else if (--d->rx_bs_left == 0u) {
        d->rx_bs_left = UDS_DIAG_RX_BS;
        d->fc_owed = FC_CTS; /* the next block */
    } else {
        /* within the block */
    }
    return true;
}

/* One flow control frame of this ECU's (a response frame's slot in the task). */
static bool rx_flow_control(uds_diag_t *d)
{
    if (d->fc_owed == 0u) {
        return false;
    }
    hal_can_frame_t f;
    frame(&f, UDS_ID_RSP, 3u);
    f.data[0] = d->fc_owed;
    f.data[1] = (d->fc_owed == FC_CTS) ? UDS_DIAG_RX_BS : 0u;
    f.data[2] = 0u; /* STmin 0 */
    if (hal_can_tx(HAL_CAN_DIAG, &f)) {
        d->fc_owed = 0u; /* a busy mailbox: again next task, inside N_Br */
    }
    return true;
}

/* ---------------- entry points ---------------- */
/* FW-40's services. It owns 0x22 from its place in uds_handle(): the DIDs other items answer are taken before it —
 * FW-41's single reads of 0xFD40/0xFD41 in uds_handle(), FW-39's and FW-43's (0xFE43) in app.c: diag — so a DID that
 * reaches it unknown is left out, and none known is NRC 0x31 (not the dispatcher's 0x11). */
static bool claimed(uint8_t sid)
{
    return (sid == SID_CLEAR) || (sid == SID_RDTCI) || (sid == SID_RDBI) || (sid == SID_RDBPI);
}

bool uds_diag_rx(uds_t *u, const hal_can_frame_t *rq)
{
    app_t *a = app_of(u);
    if ((a == NULL) || (rq->id != UDS_ID_REQ) || (rq->len < 1u)) {
        return false;
    }
    uds_diag_t *d = &a->udsd;
    const uint32_t now_ms = hal_time_ms();
    const uint32_t now_us = hal_time_us();
    if ((rq->data[0] & 0xF0u) == 0x30u) {
        return flow_control(d, rq, now_ms, now_us);
    }
    if ((rq->data[0] & 0xF0u) == 0x10u) {
        return rx_first(d, rq, now_ms); /* round 23 (item 11) */
    }
    if ((rq->data[0] & 0xF0u) == 0x20u) {
        return rx_consecutive(u, d, rq, now_ms);
    }
    const uint8_t *m = NULL;
    uint32_t n = 0u;
    if (!sf_parse(rq, &m, &n) || (!claimed(m[0]) && !ripple_rq(m, n))) {
        return false;
    }
    abandon(d); /* a new request: a pending response or scan is dropped */
    if (ripple_rq(m, n)) {
        ripple_write(a, d, m, n, now_ms); /* round 23 (FW-46): a single frame is never its 75 bytes: NRC 0x13 */
    } else if (m[0] == SID_RDTCI) {
        read_dtc_info(d, m, n, now_ms);
    } else if (m[0] == SID_RDBI) {
        read_dids(a, d, m, n, now_ms, now_us);
    } else if (m[0] == SID_RDBPI) {
        periodic_rq(d, m, n, now_ms);
    } else {
        clear_dtcs(a, u, d, m, n, now_ms);
    }
    return true;
}

void uds_diag_tick(uds_t *u)
{
    app_t *a = app_of(u);
    if (a == NULL) {
        return;
    }
    uds_diag_t *d = &a->udsd;
    const uint32_t now_ms = hal_time_ms();
    const uint32_t now_us = hal_time_us();
    scan_step(d, now_ms);
    if (d->rx_on && ti_elapsed(now_ms, d->t_rx_ms, N_CR_MS)) {
        d->rx_on = false; /* N_Cr: the next consecutive frame never came */
        d->n_aborted++;
    }
    if (!rx_flow_control(d) && !tp_step(d, now_ms, now_us)) {
        periodic_step(a, d, now_ms, now_us);
    }
}
