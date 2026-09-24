/* discharge.c — FW-17/18/19 and the FW-02 tau check. */
#include "discharge.h"

#include <math.h>

#include "dtc.h"
#include "ti_math.h"

#define WITNESS_MIN_V 60.0f /* decay checks need a voltage worth measuring */
#define STUCK_ON_WINDOW_MS 1000u

void dis_init(dis_t *d)
{
    *d = (dis_t){0};
}

static uint8_t fires_in_window(const dis_t *d, uint32_t now_ms, const ti_params_t *p)
{
    uint8_t n = 0u;
    const uint8_t stored = (d->n_fire < 3u) ? d->n_fire : 3u;
    for (uint8_t i = 0u; i < stored; i++) {
        n += ti_elapsed(now_ms, d->fire_ms[i], p->qdis_window_ms) ? 0u : 1u;
    }
    return n;
}

dis_req_t dis_request(dis_t *d, ti_contactor_t contactors, const vdc_t *v, bool counted, uint32_t now_ms,
                      const ti_params_t *p)
{
    if (contactors != TI_CONT_OPEN) {
        return DIS_REQ_CONTACTORS;
    }
    if (d->st == DIS_ACTIVE) {
        return DIS_REQ_BUSY;
    }
    if (d->stuck_off) {
        return DIS_REQ_FAILED; /* the active path is presumed open: the passive bleeder takes over */
    }
    if (counted && (fires_in_window(d, now_ms, p) >= p->qdis_max_per_window)) {
        dtc_set(DTC_QDIS_RATE_LIMIT, now_ms);
        return DIS_REQ_RATE;
    }
    if (counted) {
        d->fire_ms[d->n_fire % 3u] = now_ms;
        d->n_fire++;
    }
    d->st = DIS_ACTIVE;
    d->qdis = true;
    d->counted = counted;
    d->start_ms = now_ms;
    d->witness_ok_at_start = v->ch_valid[0] && v->ch_valid[1] && v->valid;
    d->v_start[0] = v->v_ch[0];
    d->v_start[1] = v->v_ch[1];
    d->witness_done = false;
    d->mon_active = false;
    return DIS_REQ_OK;
}

void dis_abort(dis_t *d)
{
    if (d->st == DIS_ACTIVE) {
        d->st = DIS_ABORTED;
    }
    d->qdis = false;
}

static void witness(dis_t *d, const vdc_t *v, uint32_t now_ms, const ti_params_t *p)
{
    d->witness_done = true;
    const bool usable = d->witness_ok_at_start && v->valid && (d->v_start[0] >= WITNESS_MIN_V) &&
                        (d->v_start[1] >= WITNESS_MIN_V);
    if (!usable) {
        return; /* HV state stays UNKNOWN through dis_hv_state() */
    }
    const float drop0 = (d->v_start[0] - v->v_ch[0]) / d->v_start[0];
    const float drop1 = (d->v_start[1] - v->v_ch[1]) / d->v_start[1];
    if ((drop0 < p->cal_qdis_decay_min_frac) || (drop1 < p->cal_qdis_decay_min_frac)) {
        d->stuck_off = true;
        dtc_set(DTC_QDIS_STUCK_OFF, now_ms);
        dis_abort(d);
        return;
    }
    /* FW-02 plausibility: tau from the first window */
    const float t_s = (float)ti_age(now_ms, d->start_ms) * 1.0e-3f;
    const float v0 = 0.5f * (d->v_start[0] + d->v_start[1]);
    d->tau_meas_s = t_s / logf(v0 / ti_maxf(v->vdc, 1.0f));
    if (ti_absf(d->tau_meas_s - p->tau_dis_s) > (p->tau_band_frac * p->tau_dis_s)) {
        d->tau_mismatch = true;
        dtc_set(DTC_TAU_MISMATCH, now_ms);
    }
}

static void stuck_on_watch(dis_t *d, ti_contactor_t contactors, const vdc_t *v, bool bridge_mod, uint32_t now_ms,
                           const ti_params_t *p)
{
    const bool eligible = !d->qdis && (contactors == TI_CONT_OPEN) && !bridge_mod && v->valid &&
                          (v->vdc >= WITNESS_MIN_V);
    if (!eligible) {
        d->mon_active = false;
        return;
    }
    if (!d->mon_active) {
        d->mon_active = true;
        d->mon_start_ms = now_ms;
        d->mon_v0 = v->vdc;
        return;
    }
    if (!ti_elapsed(now_ms, d->mon_start_ms, STUCK_ON_WINDOW_MS)) {
        return;
    }
    const float t_s = (float)ti_age(now_ms, d->mon_start_ms) * 1.0e-3f;
    const float ratio = d->mon_v0 / ti_maxf(v->vdc, 1.0f);
    const float tau_passive = p->r_bleed_ohm * p->c_nom_f;
    if ((ratio > 1.0f) && ((t_s / logf(ratio)) < (p->cal_qdis_stuck_on_frac * tau_passive))) {
        d->stuck_on = true;
        dtc_set(DTC_QDIS_STUCK_ON, now_ms);
    }
    d->mon_start_ms = now_ms;
    d->mon_v0 = v->vdc;
}

void dis_step(dis_t *d, ti_contactor_t contactors, const vdc_t *v, bool bridge_mod, uint32_t now_ms,
              const ti_params_t *p)
{
    if (d->st == DIS_ACTIVE) {
        if (contactors != TI_CONT_OPEN) {
            dis_abort(d); /* FW-17: never with the contactors reported anything but open */
        } else if (ti_elapsed(now_ms, d->start_ms, p->qdis_on_max_ms)) {
            d->st = DIS_DONE; /* auto-release after 5 s; the passive bleeder continues */
            d->qdis = false;
        } else if (!d->witness_done && ti_elapsed(now_ms, d->start_ms, p->qdis_nodecay_ms)) {
            witness(d, v, now_ms, p);
        } else if (d->witness_done && v->valid && (v->v_ch[0] < p->hv_safe_v) && (v->v_ch[1] < p->hv_safe_v)) {
            d->st = DIS_DONE;
            d->qdis = false;
        } else {
            /* discharging */
        }
    }
    stuck_on_watch(d, contactors, v, bridge_mod, now_ms, p);
}

bool dis_output(const dis_t *d) { return d->qdis; }

ti_hv_state_t dis_hv_state(const vdc_t *v) { return v->valid ? v->hv : TI_HV_UNKNOWN; }

/* ---------------- FW-19 precharge ---------------- */
void pch_init(pch_t *c)
{
    *c = (pch_t){0};
}

static pch_result_t verdict(pch_t *c, float v, float v_pack, uint32_t now_ms, const ti_params_t *p, dtc_id_t *dtc)
{
    (void)now_ms;
    if ((v / v_pack) < (1.0f - p->cal_precharge_low_frac)) {
        *dtc = DTC_PRECHARGE_PLATEAU;
        return PCH_REFUSE_PLATEAU;
    }
    if (c->t63_done && (c->tau_s < p->cal_precharge_tau_min_s)) {
        *dtc = DTC_PRECHARGE_TAU;
        return PCH_REFUSE_TAU;
    }
    return PCH_OK;
}

pch_result_t pch_step(pch_t *c, ti_contactor_t contactors, const vdc_t *v, float v_pack, bool pack_fresh,
                      uint32_t now_ms, const ti_params_t *p)
{
    if ((c->res != PCH_IDLE) && (c->res != PCH_RUNNING)) {
        return c->res; /* verdict latched until pch_init() */
    }
    if (c->res == PCH_IDLE) {
        if (contactors != TI_CONT_PRECHARGE) {
            return PCH_IDLE;
        }
        c->res = PCH_RUNNING;
        c->t0_ms = now_ms;
        c->v0 = v->valid ? v->vdc : 0.0f;
        c->v_max = c->v0;
        c->v_max_ms = now_ms;
    }
    dtc_id_t dtc = DTC_NONE;
    if (ti_elapsed(now_ms, c->t0_ms, p->cal_precharge_timeout_ms)) {
        c->res = PCH_REFUSE_TIMEOUT;
        dtc = DTC_PRECHARGE_TIMEOUT;
    } else if (v->valid && pack_fresh && (v_pack > p->hv_safe_v)) {
        const float vv = v->vdc;
        if (!c->t63_done && ((vv - c->v0) >= (0.632f * (v_pack - c->v0)))) {
            c->t63_done = true;
            c->tau_s = (float)ti_age(now_ms, c->t0_ms) * 1.0e-3f;
        }
        if (vv > (c->v_max + (0.002f * v_pack))) {
            c->v_max = vv;
            c->v_max_ms = now_ms;
        }
        const bool plateau = c->t63_done && ti_elapsed(now_ms, c->v_max_ms, p->cal_precharge_plateau_ms);
        if (plateau || (contactors == TI_CONT_CLOSED)) {
            c->res = verdict(c, vv, v_pack, now_ms, p, &dtc);
        }
    } else {
        /* waiting for valid readings */
    }
    if (dtc != DTC_NONE) {
        dtc_set(dtc, now_ms);
    }
    return c->res;
}
