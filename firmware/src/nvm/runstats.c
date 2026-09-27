/* runstats.c — FW-43 accumulators, the run-time record and its DIDs (see runstats.h). */
#include "runstats.h"

#include <string.h>

#include "nvlog.h"
#include "ti_math.h"
#include "uds.h"

_Static_assert(sizeof(nv_runtime_t) == 96u, "nv_runtime_t has implicit padding");
_Static_assert(sizeof(nv_runtime_t) <= NV_PAYLOAD_MAX, "nv_runtime_t exceeds an NVM slot");
_Static_assert((3u + RS_DID_LEN) <= 62u, "the DID response exceeds one CAN-FD single frame");

#define SID_RDBI 0x22u

/* DTC -> class. Designated entries; every DTC not listed (DTC_NONE, the information DTCs, and any DTC added later
 * until someone classifies it) is RS_CLS_INFO (0). */
static const uint8_t CLS[DTC_COUNT] = {
    [DTC_DESAT_HS] = RS_CLS_POWER_STAGE, [DTC_DESAT_LS] = RS_CLS_POWER_STAGE, [DTC_DESAT_REPEAT] = RS_CLS_POWER_STAGE,
    [DTC_DESAT_PENDING_BOOT] = RS_CLS_POWER_STAGE, [DTC_FLT_RECOVERY_FAIL] = RS_CLS_POWER_STAGE,
    [DTC_OVERCURRENT] = RS_CLS_POWER_STAGE, [DTC_OVERVOLTAGE] = RS_CLS_POWER_STAGE, [DTC_GATE_POWER] = RS_CLS_POWER_STAGE,
    [DTC_SPO_ENERGY] = RS_CLS_POWER_STAGE,
    [DTC_ISNS_OPEN] = RS_CLS_SENSOR, [DTC_ISNS_RANGE] = RS_CLS_SENSOR, [DTC_ISNS_SUM] = RS_CLS_SENSOR,
    [DTC_ISNS_OFFSET] = RS_CLS_SENSOR, [DTC_ISNS_STALE] = RS_CLS_SENSOR, [DTC_ISNS_STUCK] = RS_CLS_SENSOR,
    [DTC_VDC_DISAGREE] = RS_CLS_SENSOR, [DTC_VOFS] = RS_CLS_SENSOR, [DTC_VDC_FAILSAFE] = RS_CLS_SENSOR,
    [DTC_VDC_STALE] = RS_CLS_SENSOR, [DTC_VDC_BMS] = RS_CLS_SENSOR, [DTC_RSLV_AMPLITUDE] = RS_CLS_SENSOR,
    [DTC_RSLV_EXCITATION] = RS_CLS_SENSOR, [DTC_RSLV_TRACKING] = RS_CLS_SENSOR, [DTC_RSLV_ACCEL] = RS_CLS_SENSOR,
    [DTC_RSLV_RATE] = RS_CLS_SENSOR, [DTC_RSLV_STALE] = RS_CLS_SENSOR, [DTC_RSLV_SWG_SAT] = RS_CLS_SENSOR,
    [DTC_TEMP_MODULE] = RS_CLS_SENSOR, [DTC_TEMP_BOARD] = RS_CLS_SENSOR, [DTC_TEMP_MOTOR] = RS_CLS_SENSOR,
    [DTC_TEMP_OPEN_SHORT] = RS_CLS_SENSOR,
    [DTC_FS26_PROGID] = RS_CLS_SUPPLY, [DTC_FS26_OTP_CORRUPT] = RS_CLS_SUPPLY, [DTC_FS26_DEBUG_MODE] = RS_CLS_SUPPLY,
    [DTC_FS26_INIT_READBACK] = RS_CLS_SUPPLY, [DTC_FS26_SPI] = RS_CLS_SUPPLY, [DTC_FS26_WD] = RS_CLS_SUPPLY,
    [DTC_FS26_RELEASE] = RS_CLS_SUPPLY, [DTC_FS1B_SHORT_HIGH] = RS_CLS_SUPPLY, [DTC_FS26_GPIO1_OTP] = RS_CLS_SUPPLY,
    [DTC_V5GD] = RS_CLS_SUPPLY, [DTC_LV_OV_SUSTAINED] = RS_CLS_SUPPLY,
    [DTC_CAN_TIMEOUT] = RS_CLS_VEHICLE, [DTC_BMS_TIMEOUT] = RS_CLS_VEHICLE, [DTC_CAN_E2E] = RS_CLS_VEHICLE,
    [DTC_HVIL_OPEN] = RS_CLS_VEHICLE, [DTC_HVIL_SHORT] = RS_CLS_VEHICLE,
    [DTC_OVERTEMP] = RS_CLS_LIMIT, [DTC_TORQUE_INFEASIBLE] = RS_CLS_LIMIT, [DTC_OVERSPEED] = RS_CLS_LIMIT,
    [DTC_QDIS_STUCK_OFF] = RS_CLS_HV_PATH, [DTC_QDIS_STUCK_ON] = RS_CLS_HV_PATH, [DTC_QDIS_RATE_LIMIT] = RS_CLS_HV_PATH,
    [DTC_TAU_MISMATCH] = RS_CLS_HV_PATH, [DTC_PRECHARGE_PLATEAU] = RS_CLS_HV_PATH, [DTC_PRECHARGE_TAU] = RS_CLS_HV_PATH,
    [DTC_PRECHARGE_TIMEOUT] = RS_CLS_HV_PATH,
    [DTC_HWID_OPEN] = RS_CLS_INTEGRITY, [DTC_HWID_SHORT] = RS_CLS_INTEGRITY, [DTC_HWID_UNKNOWN] = RS_CLS_INTEGRITY,
    [DTC_SKU_MISMATCH] = RS_CLS_INTEGRITY, [DTC_CALIB_INVALID] = RS_CLS_INTEGRITY, [DTC_PARAMS_INVALID] = RS_CLS_INTEGRITY,
    [DTC_PWM_LOCK] = RS_CLS_INTEGRITY, [DTC_ARM_EVIDENCE] = RS_CLS_INTEGRITY, [DTC_SELFTEST_FAIL] = RS_CLS_INTEGRITY,
    [DTC_SELFTEST_NO_PASS] = RS_CLS_INTEGRITY, [DTC_SENSOR_SELFTEST] = RS_CLS_INTEGRITY, [DTC_GAINS] = RS_CLS_INTEGRITY,
    [DTC_NVM] = RS_CLS_INTEGRITY, [DTC_CTRL_NONFINITE] = RS_CLS_INTEGRITY, [DTC_TORQUE_POSTCOND] = RS_CLS_INTEGRITY,
};

rs_cls_t rs_dtc_class(dtc_id_t id) { return ((uint32_t)id < (uint32_t)DTC_COUNT) ? (rs_cls_t)CLS[id] : RS_CLS_INFO; }

uint32_t rs_wh(uint64_t j) { return (uint32_t)(j / 3600u); }

void rs_init(rs_t *s, const nv_runtime_t *rec, uint32_t now_ms)
{
    (void)memset(s, 0, sizeof *s);
    if (rec != NULL) {
        s->rec = *rec;
    } else {
        s->rec.version = RS_LAYOUT_VERSION;
        s->rec.t_mod_max_c = RS_T_NONE;
        s->rec.t_cool_max_c = RS_T_NONE;
        s->rec.t_mot_max_c = RS_T_NONE;
    }
    s->t_save_ms = now_ms;
}

/* Whole joules into the 64-bit counter, the fraction kept (a float total would stop counting 1 ms steps). */
static void add_j(uint64_t *acc, float *res, float j)
{
    *res += j;
    if (*res >= 1.0f) {
        const uint32_t whole = (uint32_t)*res;
        *acc += whole;
        *res -= (float)whole;
    }
}

static void tick_s(uint32_t *ms, uint32_t *s)
{
    if (++(*ms) >= 1000u) {
        *ms = 0u;
        (*s)++;
    }
}

static void max_c(float *m, float t, bool ok)
{
    if (ok && ti_finite(t) && (t > *m)) {
        *m = t;
    }
}

void rs_step(rs_t *s, const rs_in_t *in)
{
    if (!in->on) {
        return; /* OFF, or SAFE_POWERDOWN after its record: nothing more is kept */
    }
    tick_s(&s->on_ms, &s->rec.t_on_s);
    if (in->run) {
        tick_s(&s->run_ms, &s->rec.t_run_s);
    }
    if (in->mod && ti_finite(in->p_w)) {
        const float j = in->p_w * 1.0e-3f; /* 1 ms */
        if (j >= 0.0f) {
            add_j(&s->rec.e_mot_j, &s->r_mot_j, j);
        } else {
            add_j(&s->rec.e_reg_j, &s->r_reg_j, -j);
        }
    }
    max_c(&s->rec.t_mod_max_c, in->t_mod_c, in->t_mod_ok);
    max_c(&s->rec.t_cool_max_c, in->t_cool_c, in->t_cool_ok);
    max_c(&s->rec.t_mot_max_c, in->t_mot_c, in->t_mot_ok);
}

void rs_count_dtcs(rs_t *s)
{
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        const uint8_t occ = dtc_occurrences((dtc_id_t)i);
        if (occ < s->occ_seen[i]) {
            s->occ_seen[i] = 0u; /* the store was cleared (UDS 0x14): count from zero */
        }
        s->rec.n_dtc[CLS[i]] += (uint32_t)occ - (uint32_t)s->occ_seen[i];
        s->occ_seen[i] = occ;
    }
}

void rs_persist(rs_t *s, bool shutdown, bool force, uint32_t now_ms, const ti_params_t *p)
{
    if (!shutdown) {
        s->down_saved = false;
    }
    const bool due = force || s->pending || (shutdown && !s->down_saved) ||
                     ti_elapsed(now_ms, s->t_save_ms, p->cal_rs_save_s * 1000u);
    if (!due) {
        return;
    }
    s->pending = !nv_queue(NV_REC_RUNTIME, &s->rec, (uint16_t)sizeof s->rec); /* a full queue: next tick */
    if (!s->pending) {
        s->t_save_ms = now_ms;
        s->down_saved = shutdown;
    }
}

static uint8_t *put_be(uint8_t *b, uint32_t v, uint32_t n)
{
    for (uint32_t i = 0u; i < n; i++) {
        b[i] = (uint8_t)(v >> (8u * (n - 1u - i)));
    }
    return &b[n];
}

static uint32_t deci_c(float c)
{
    if (!(c > RS_T_NONE)) {
        return 0x8000u; /* INT16_MIN: none yet */
    }
    const float d = ti_clampf((c * 10.0f) + ((c >= 0.0f) ? 0.5f : -0.5f), -32767.0f, 32767.0f);
    return (uint32_t)(uint16_t)(int16_t)d;
}

void rs_did_read(const nv_runtime_t *r, uint32_t key_cycle, uint8_t out[RS_DID_LEN])
{
    uint8_t *b = put_be(out, rs_wh(r->e_mot_j), 4u);
    b = put_be(b, rs_wh(r->e_reg_j), 4u);
    b = put_be(b, r->t_on_s, 4u);
    b = put_be(b, r->t_run_s, 4u);
    b = put_be(b, key_cycle, 4u);
    b = put_be(b, deci_c(r->t_mod_max_c), 2u);
    b = put_be(b, deci_c(r->t_cool_max_c), 2u);
    b = put_be(b, deci_c(r->t_mot_max_c), 2u);
    for (uint32_t k = 0u; k < (uint32_t)RS_CLS_COUNT; k++) {
        b = put_be(b, r->n_dtc[k], 4u);
    }
}

bool rs_uds_handle(const nv_runtime_t *r, uint32_t key_cycle, const hal_can_frame_t *rq, hal_can_frame_t *rsp)
{
    /* SID + this DID alone, as a classic single frame (PCI 0x03) or a CAN-FD escape single frame (0x00 0x03) */
    if ((rq->id != UDS_ID_REQ) || (rq->len < 4u) || (rq->len > HAL_CAN_MAX_LEN)) {
        return false;
    }
    const uint8_t *m = (rq->data[0] == 3u) ? &rq->data[1]
                     : (((rq->data[0] == 0u) && (rq->data[1] == 3u) && (rq->len > 8u)) ? &rq->data[2] : NULL);
    if ((m == NULL) || (m[0] != SID_RDBI) || ((((uint32_t)m[1] << 8) | m[2]) != RS_DID)) {
        return false;
    }
    /* SID + DID + the record: 61 bytes, one CAN-FD single frame (escape PCI 0x00, SF_DL) of 64 bytes */
    *rsp = (hal_can_frame_t){.id = UDS_ID_RSP, .len = 64u};
    (void)memset(rsp->data, 0xAA, HAL_CAN_MAX_LEN);
    rsp->data[0] = 0u;
    rsp->data[1] = (uint8_t)(3u + RS_DID_LEN);
    rsp->data[2] = SID_RDBI + 0x40u;
    rsp->data[3] = (uint8_t)(RS_DID >> 8);
    rsp->data[4] = (uint8_t)RS_DID;
    rs_did_read(r, key_cycle, &rsp->data[5]);
    return true;
}
