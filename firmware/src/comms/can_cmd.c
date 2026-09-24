/* can_cmd.c — FW-11 receive checks, direction interlock, status frame. */
#include "can_cmd.h"

#include "ti_crc.h"
#include "ti_math.h"

void can_cmd_init(can_cmd_t *c)
{
    *c = (can_cmd_t){0};
    c->contactors = TI_CONT_INVALID;
}

uint8_t can_e2e_crc(uint32_t id, const uint8_t *data, uint8_t len)
{
    uint8_t buf[HAL_CAN_MAX_LEN + 1u];
    buf[0] = (uint8_t)(id & 0xFFu); /* DataID */
    for (uint8_t i = 1u; (i < len) && (i < HAL_CAN_MAX_LEN); i++) {
        buf[i] = data[i];
    }
    return ti_crc8_1d(buf, len, 0xFFu, 0xFFu);
}

static int16_t get_i16(const uint8_t *d) { return (int16_t)(uint16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8)); }
static uint16_t get_u16(const uint8_t *d) { return (uint16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8)); }
static void put_u16(uint8_t *d, uint16_t v)
{
    d[0] = (uint8_t)(v & 0xFFu);
    d[1] = (uint8_t)(v >> 8);
}

/* alive counter: 0 = repeated (frozen sender), 1..jump = accepted, else rejected and resynced */
static bool counter_ok(bool *have, uint8_t *last, uint8_t now_ctr, uint32_t *n_frozen, uint32_t *n_jump,
                       const ti_params_t *p)
{
    if (!*have) {
        *have = true;
        *last = now_ctr;
        return true;
    }
    const uint8_t delta = (uint8_t)((now_ctr - *last) & 0x0Fu);
    if (delta == 0u) {
        (*n_frozen)++;
        return false;
    }
    *last = now_ctr;
    if (delta > p->cal_can_ctr_max_jump) {
        (*n_jump)++;
        return false;
    }
    return true;
}

static bool rx_cmd(can_cmd_t *c, const hal_can_frame_t *f, uint32_t now_ms, const ti_params_t *p)
{
    if (!counter_ok(&c->have_ctr, &c->ctr, (uint8_t)(f->data[1] & 0x0Fu), &c->n_frozen, &c->n_jump, p)) {
        return false;
    }
    c->gear = (ti_gear_t)((f->data[1] >> 4) & 0x03u);
    c->enable_req = (f->data[1] & 0x40u) != 0u;
    c->fault_reset_req = (f->data[1] & 0x80u) != 0u;
    c->torque_req_nm = 0.1f * (float)get_i16(&f->data[2]);
    const uint8_t cs = (uint8_t)(f->data[4] & 0x03u);
    c->contactors = (cs == 1u) ? TI_CONT_OPEN : ((cs == 2u) ? TI_CONT_PRECHARGE : ((cs == 3u) ? TI_CONT_CLOSED : TI_CONT_INVALID));
    c->desat_retry_auth = (f->data[4] & 0x04u) != 0u;
    c->discharge_req = (f->data[4] & 0x08u) != 0u;
    c->shutdown_req = (f->data[4] & 0x10u) != 0u;
    c->coolant_valid = f->data[5] != 0xFFu;
    c->coolant_c = (float)f->data[5] - 40.0f;
    c->last_ms = now_ms;
    c->ever = true;
    return true;
}

static bool rx_bms(can_cmd_t *c, const hal_can_frame_t *f, uint32_t now_ms, const ti_params_t *p)
{
    if (!counter_ok(&c->bms_have_ctr, &c->bms_ctr, (uint8_t)(f->data[1] & 0x0Fu), &c->n_frozen, &c->n_jump, p)) {
        return false;
    }
    c->v_pack = 0.1f * (float)get_u16(&f->data[2]);
    c->p_chg_w = 100.0f * (float)get_u16(&f->data[4]);
    c->p_dis_w = 100.0f * (float)get_u16(&f->data[6]);
    c->bms_last_ms = now_ms;
    c->bms_ever = true;
    return true;
}

bool can_cmd_rx(can_cmd_t *c, const hal_can_frame_t *f, uint32_t now_ms, const ti_params_t *p)
{
    if ((f->id != CAN_ID_VCU_CMD) && (f->id != CAN_ID_VCU_BMS)) {
        return false;
    }
    if (f->len < 8u) {
        c->n_len++;
        return false;
    }
    if (can_e2e_crc(f->id, f->data, 8u) != f->data[0]) {
        c->n_crc++;
        return false;
    }
    return (f->id == CAN_ID_VCU_CMD) ? rx_cmd(c, f, now_ms, p) : rx_bms(c, f, now_ms, p);
}

bool can_cmd_fresh(const can_cmd_t *c, uint32_t now_ms, const ti_params_t *p)
{
    return c->ever && !ti_elapsed(now_ms, c->last_ms, p->can_stale_ms + 1u);
}

bool can_bms_fresh(const can_cmd_t *c, uint32_t now_ms, const ti_params_t *p)
{
    return c->bms_ever && !ti_elapsed(now_ms, c->bms_last_ms, p->cal_bms_timeout_ms + 1u);
}

float can_dir_interlock(can_dir_t *d, ti_gear_t req, float t_req_nm, float speed_rpm, const ti_params_t *p)
{
    const bool standstill = ti_absf(speed_rpm) < p->cal_dir_change_rpm;
    d->change_refused = (req != d->active) && !standstill;
    if ((req != d->active) && standstill) {
        d->active = req;
    }
    if (!ti_finite(t_req_nm)) {
        return 0.0f;
    }
    switch (d->active) {
    case TI_GEAR_D: /* forward: drive +, brake only while rolling forward */
        return (t_req_nm >= 0.0f) ? t_req_nm : ((speed_rpm > p->cal_dir_change_rpm) ? t_req_nm : 0.0f);
    case TI_GEAR_R: /* mirror */
        return (t_req_nm <= 0.0f) ? t_req_nm : ((speed_rpm < -p->cal_dir_change_rpm) ? t_req_nm : 0.0f);
    default:
        return 0.0f;
    }
}

static void finish(hal_can_frame_t *f, uint32_t id, uint8_t len)
{
    f->id = id;
    f->len = len;
    f->data[0] = can_e2e_crc(id, f->data, len);
}

void can_status_encode(const can_status_t *s, uint8_t ctr, hal_can_frame_t *f)
{
    *f = (hal_can_frame_t){0};
    f->data[1] = (uint8_t)((ctr & 0x0Fu) | (s->self_test_done ? 0x10u : 0u) | (s->keep_hv ? 0x20u : 0u) |
                           (s->derate ? 0x40u : 0u) | (s->fault ? 0x80u : 0u));
    f->data[2] = s->state;
    f->data[3] = (uint8_t)((s->bridge & 0x03u) | ((uint8_t)((uint8_t)s->hv & 0x03u) << 2) |
                           (s->zero_torque ? 0x10u : 0u) | (s->discharging ? 0x20u : 0u) |
                           (s->precharge_refused ? 0x40u : 0u) | (s->speed_valid ? 0x80u : 0u));
    put_u16(&f->data[4], (uint16_t)(int16_t)ti_clampf(s->torque_nm * 10.0f, -32767.0f, 32767.0f));
    put_u16(&f->data[6], (uint16_t)(int16_t)ti_clampf(s->speed_rpm, -32767.0f, 32767.0f));
    put_u16(&f->data[8], s->vdc_valid ? (uint16_t)ti_clampf(s->vdc_v * 10.0f, 0.0f, 65534.0f) : 0xFFFFu);
    f->data[10] = (uint8_t)ti_clampf(s->t_module_c + 40.0f, 0.0f, 254.0f);
    f->data[11] = (uint8_t)((s->n_dtc > 255u) ? 255u : s->n_dtc);
    put_u16(&f->data[12], s->first_dtc);
    f->data[14] = (uint8_t)((s->no_safe_state ? 0x01u : 0u) | (s->service_required ? 0x02u : 0u) |
                            (s->open_contactors_req ? 0x04u : 0u) | (s->speed_limit_req ? 0x08u : 0u));
    f->data[15] = (uint8_t)(s->evidence_missing & 0x1Fu);
    finish(f, CAN_ID_INV_STATUS, 16u);
}

void can_encode_vcu_cmd(hal_can_frame_t *f, uint8_t ctr, ti_gear_t gear, bool enable, bool fault_reset,
                        float torque_nm, ti_contactor_t cont, bool retry_auth, bool discharge, bool shutdown,
                        float coolant_c)
{
    *f = (hal_can_frame_t){0};
    f->data[1] = (uint8_t)((ctr & 0x0Fu) | (((uint8_t)gear & 0x03u) << 4) | (enable ? 0x40u : 0u) |
                           (fault_reset ? 0x80u : 0u));
    put_u16(&f->data[2], (uint16_t)(int16_t)ti_clampf(torque_nm * 10.0f, -32767.0f, 32767.0f));
    const uint8_t cs = (cont == TI_CONT_OPEN) ? 1u : ((cont == TI_CONT_PRECHARGE) ? 2u : ((cont == TI_CONT_CLOSED) ? 3u : 0u));
    f->data[4] = (uint8_t)(cs | (retry_auth ? 0x04u : 0u) | (discharge ? 0x08u : 0u) | (shutdown ? 0x10u : 0u));
    f->data[5] = (uint8_t)ti_clampf(coolant_c + 40.0f, 0.0f, 254.0f);
    finish(f, CAN_ID_VCU_CMD, 8u);
}

void can_encode_vcu_bms(hal_can_frame_t *f, uint8_t ctr, float v_pack, float p_chg_w, float p_dis_w)
{
    *f = (hal_can_frame_t){0};
    f->data[1] = (uint8_t)(ctr & 0x0Fu);
    put_u16(&f->data[2], (uint16_t)ti_clampf(v_pack * 10.0f, 0.0f, 65535.0f));
    put_u16(&f->data[4], (uint16_t)ti_clampf(p_chg_w / 100.0f, 0.0f, 65535.0f));
    put_u16(&f->data[6], (uint16_t)ti_clampf(p_dis_w / 100.0f, 0.0f, 65535.0f));
    finish(f, CAN_ID_VCU_BMS, 8u);
}
