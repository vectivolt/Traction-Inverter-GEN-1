/* commission.c — FW-39 motor self-commissioning (commission.h; contract §10g). */
#include "commission.h"

#include <stdatomic.h>
#include <string.h>

#include "dtc.h"
#include "nvlog.h"
#include "timer.h"

_Static_assert(sizeof(calib_t) <= NV_PAYLOAD_MAX, "the FW-20 record fits one NVM slot");

#define SID_RC 0x31u
#define RC_START 0x01u
#define RC_STOP 0x02u
#define RC_RESULTS 0x03u

mc_t g_mc;

/* Sized on the host plant (tests/test_commission.c) with the screening motor's record.
 * TODO(HW): the amplitudes, durations, dyno window and bands against the real machine and rig (bring-up FW-39). */
const mc_cal_t MC_CAL_DEFAULT = {
    .hb_timeout_ms = 200u, .vspeed_max_kmh = 0.5f, .lock_rpm = 5.0f, .lock_rad = 0.035f, .dyno_rpm_min = 150.0f,
    .dyno_rpm_max = 450.0f, .dyno_nx_frac = 0.25f, .dyno_steady_frac = 0.05f, .rs_i1_a = 30.0f, .rs_i2_a = 60.0f,
    .rs_settle_ms = 60u, .rs_meas_ms = 200u, .hf_i_a = 20.0f, .hf_bias_a = 50.0f, .hf_n = 40u, .hf_settle_ms = 40u,
    .hf_meas_ms = 200u,
    .psi_settle_ms = 100u, .psi_meas_ms = 400u, .i_margin_a = 20.0f, .u_floor_rel = 0.01f, .u_max_rel = 0.05f,
    .t_unc_us = 20.0f, .zero_u_max_rad = 0.0175f, .band_rs_rel = 0.10f, .band_l_rel = 0.10f, .band_psi_rel = 0.05f,
    .band_zero_rad = 0.035f, .confirm_k = 3.0f, .axes_max_rad = 0.26f, .axes_min_rel = 0.05f};

#define MC_RANGE(f, t, lo, hi) {#f, offsetof(mc_cal_t, f), t, lo, hi}
static const ti_cal_range_t RANGES[] = {
    MC_RANGE(hb_timeout_ms, TI_CAL_U32, 50.0f, 2000.0f),    /* the tool's heartbeat */
    MC_RANGE(vspeed_max_kmh, TI_CAL_F32, 0.0f, 2.0f),       /* "vehicle speed zero" */
    MC_RANGE(lock_rpm, TI_CAL_F32, 1.0f, 30.0f),            /* locked: speed */
    MC_RANGE(lock_rad, TI_CAL_F32, 0.005f, 0.2f),           /* locked: electrical angle excursion */
    MC_RANGE(dyno_rpm_min, TI_CAL_F32, 50.0f, 2000.0f),
    MC_RANGE(dyno_rpm_max, TI_CAL_F32, 100.0f, 3000.0f),
    MC_RANGE(dyno_nx_frac, TI_CAL_F32, 0.05f, 0.5f),        /* the dyno speed against n_x */
    MC_RANGE(dyno_steady_frac, TI_CAL_F32, 0.01f, 0.2f),
    MC_RANGE(rs_i1_a, TI_CAL_F32, 5.0f, 150.0f),
    MC_RANGE(rs_i2_a, TI_CAL_F32, 10.0f, 250.0f),
    MC_RANGE(rs_settle_ms, TI_CAL_U32, 10.0f, 500.0f),
    MC_RANGE(rs_meas_ms, TI_CAL_U32, 40.0f, 2000.0f),
    MC_RANGE(hf_i_a, TI_CAL_F32, 5.0f, 100.0f),
    MC_RANGE(hf_bias_a, TI_CAL_F32, 10.0f, 200.0f),        /* >= 2 x hf_i_a */
    MC_RANGE(hf_n, TI_CAL_U32, 16.0f, 64.0f),               /* samples per injection period */
    MC_RANGE(hf_settle_ms, TI_CAL_U32, 10.0f, 500.0f),
    MC_RANGE(hf_meas_ms, TI_CAL_U32, 40.0f, 2000.0f),
    MC_RANGE(psi_settle_ms, TI_CAL_U32, 20.0f, 1000.0f),
    MC_RANGE(psi_meas_ms, TI_CAL_U32, 80.0f, 4000.0f),
    MC_RANGE(i_margin_a, TI_CAL_F32, 5.0f, 100.0f),
    MC_RANGE(u_floor_rel, TI_CAL_F32, 0.0f, 0.1f),
    MC_RANGE(u_max_rel, TI_CAL_F32, 0.005f, 0.2f),
    MC_RANGE(t_unc_us, TI_CAL_F32, 0.0f, 200.0f),
    MC_RANGE(zero_u_max_rad, TI_CAL_F32, 0.001f, 0.1f),
    MC_RANGE(band_rs_rel, TI_CAL_F32, 0.01f, 0.5f),
    MC_RANGE(band_l_rel, TI_CAL_F32, 0.01f, 0.5f),
    MC_RANGE(band_psi_rel, TI_CAL_F32, 0.01f, 0.3f),
    MC_RANGE(band_zero_rad, TI_CAL_F32, 0.005f, 0.5f),
    MC_RANGE(confirm_k, TI_CAL_F32, 1.0f, 6.0f),
    MC_RANGE(axes_max_rad, TI_CAL_F32, 0.05f, 0.6f),
    MC_RANGE(axes_min_rel, TI_CAL_F32, 0.01f, 0.5f),
};

uint32_t mc_cal_validate(const mc_cal_t *c)
{
    uint32_t bad = 0u;
    for (uint32_t i = 0u; i < TI_ARRAY_LEN(RANGES); i++) {
        const uint8_t *b = (const uint8_t *)c + RANGES[i].offset;
        const float v = (RANGES[i].type == TI_CAL_U32) ? (float)*(const uint32_t *)(const void *)b
                                                       : *(const float *)(const void *)b;
        bad += ((v >= RANGES[i].min) && (v <= RANGES[i].max)) ? 0u : 1u; /* NaN fails */
    }
    bad += (c->rs_i2_a >= (1.5f * c->rs_i1_a)) ? 0u : 1u;
    bad += (c->hf_bias_a >= (2.0f * c->hf_i_a)) ? 0u : 1u;
    bad += (c->dyno_rpm_min < c->dyno_rpm_max) ? 0u : 1u;
    return bad;
}

void mc_init(void)
{
    (void)memset(&g_mc, 0, sizeof g_mc);
    g_mc.cal = &MC_CAL_DEFAULT;
    g_mc.bias_k = MC_BIAS_NONE;
}

bool mc_modulating(void) { return g_mc.run; }
bool mc_torque_barred(void) { return g_mc.bar; }

/* ======================= the record's quantities ======================= */
static float *field(calib_t *c, mc_qty_t q)
{
    switch (q) {
    case MC_Q_RS: return &c->motor.rs_ohm;
    case MC_Q_LD: return &c->motor.ld_h;
    case MC_Q_LQ: return &c->motor.lq_h;
    case MC_Q_PSI: return &c->motor.psi_wb;
    default: return &c->rslv.zero_rad;
    }
}

static float rec_value(const calib_t *c, mc_qty_t q)
{
    switch (q) {
    case MC_Q_RS: return c->motor.rs_ohm;
    case MC_Q_LD: return c->motor.ld_h;
    case MC_Q_LQ: return c->motor.lq_h;
    case MC_Q_PSI: return c->motor.psi_wb;
    default: return c->rslv.zero_rad;
    }
}

/* The FW-20 class limits are calib_check's own ranges: the active record (valid: a precondition) with this one
 * value replaced. */
static bool class_ok(const app_t *a, mc_qty_t q, float v)
{
    calib_t c;
    (void)memcpy(&c, &a->cal, sizeof c);
    *field(&c, q) = v;
    return (calib_check(&c, a->p, a->serial) & CAL_ERR_RANGE) == 0u;
}

static float dist(bool angle, float x, float y)
{
    return angle ? ti_absf(ti_wrap_pi(x - y)) : ti_absf(x - y);
}

/* Compared with the record's value old: inside the band => staged; beyond it => staged only when a second run agrees with
 * the pending first within confirm_k combined standard uncertainties (then their mean), else it becomes the pending one.
 * cls: inside the FW-20 class limits. */
static void judge_r(mc_result_t *r, bool cls, float old, float band, bool angle)
{
    const mc_cal_t *c = g_mc.cal;
    if ((r->verdict == (uint8_t)MC_V_VALID) && !cls) {
        r->verdict = (uint8_t)MC_V_CLASS;
    }
    if (r->verdict != (uint8_t)MC_V_VALID) {
        return;
    }
    r->beyond = dist(angle, r->value, old) > band;
    const bool agrees = r->pending && (dist(angle, r->value, r->pend_value) <=
                                       (c->confirm_k * sqrtf((r->u * r->u) + (r->pend_u * r->pend_u))));
    if (r->beyond && !agrees) {
        r->pending = true;
        r->pend_value = r->value;
        r->pend_u = r->u;
        return;
    }
    r->staged_value = !r->beyond ? r->value
                    : (angle ? ti_wrap_2pi(r->pend_value + (0.5f * ti_wrap_pi(r->value - r->pend_value)))
                             : (0.5f * (r->value + r->pend_value)));
    r->staged = true;
    r->pending = false;
}

static void judge(const app_t *a, mc_qty_t qi)
{
    const mc_cal_t *c = g_mc.cal;
    mc_result_t *r = &g_mc.q[qi];
    const float old = rec_value(&a->cal, qi);
    const float rel = (qi == MC_Q_RS) ? c->band_rs_rel : ((qi == MC_Q_PSI) ? c->band_psi_rel : c->band_l_rel);
    const bool cls = (r->verdict != (uint8_t)MC_V_VALID) || class_ok(a, qi, r->value);
    judge_r(r, cls, old, (qi == MC_Q_ZERO) ? c->band_zero_rad : (rel * ti_absf(old)), qi == MC_Q_ZERO);
}

/* Round 23 (FW-45): a map point — the differential inductance of axis ax (0 d, 1 q) at bias index k against the record's
 * at the same current (its map at its scalar's level), inside the inductance class range. */
static void judge_point(const app_t *a, uint32_t ax, uint32_t k)
{
    const motor_t *mo = &a->cal.motor;
    mc_result_t *r = &g_mc.mp[ax][k];
    float diff = 1.0f;
    (void)motor_sat((ax == 0u) ? mo->ld_map_h : mo->lq_map_h, mo->i_map_a * (float)k / (float)(MOTOR_MAP_N - 1u),
                    mo->i_map_a, &diff);
    const float old = ((ax == 0u) ? mo->ld_h : mo->lq_h) * diff;
    judge_r(r, (r->value >= 20e-6f) && (r->value <= 5e-3f), old, g_mc.cal->band_l_rel * old, false);
}

/* ======================= preconditions and the watch ======================= */
static bool dtc_blocking(void)
{
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        const dtc_id_t d = (dtc_id_t)i;
        const bool record = (d == DTC_SERVICE_LOCK_CLEARED) || (d == DTC_MC_ABORTED) || (d == DTC_MC_CAL_WRITTEN);
        if (!record && dtc_active(d)) {
            return true;
        }
    }
    return false;
}

/* At the start and every 1 ms while a routine runs; the first that fails. */
static mc_reason_t preconditions(const app_t *a, mc_routine_t rt, uint32_t t_ms, bool running)
{
    const mc_cal_t *c = g_mc.cal;
    const ti_params_t *p = a->p;
    if ((a->sm.st != SM_ARMED_ZERO_TORQUE) || ((a->br.mode != BR_IDLE) && (a->br.mode != BR_MOD))) {
        return MC_R_STATE;
    }
    if ((a->evidence != ARM_EV_ALL) || a->no_arm || (a->cal_err != 0u) || !a->gains_ok) {
        return MC_R_EVIDENCE;
    }
    if (fm_any(&a->fm)) {
        return MC_R_FAULT;
    }
    if (dtc_blocking()) {
        return MC_R_DTC;
    }
    if (!can_cmd_fresh(&a->can, t_ms, p) || a->can.enable_req) {
        return MC_R_VCU; /* no torque command in service mode: a request ends it */
    }
    if (!a->can.vspeed_valid || (ti_absf(a->can.vspeed_kmh) > c->vspeed_max_kmh)) {
        return MC_R_VEHICLE_SPEED;
    }
    const bool hv = a->vdc.valid && (a->vdc.vdc >= p->vdc_min_v) && (a->vdc.vdc <= p->vdc_max_v) &&
                    !a->vdc.bms_mismatch && (a->can.contactors == TI_CONT_CLOSED) && can_bms_fresh(&a->can, t_ms, p);
    if (!hv) {
        return MC_R_HV;
    }
    if (!a->rslv.valid || !a->speed_known) {
        return MC_R_SPEED;
    }
    const float n = ti_absf(a->speed_rpm);
    if (rt == MC_RT_PSI_ZERO) {
        const bool in = (n >= c->dyno_rpm_min) && (n <= c->dyno_rpm_max) && (n <= (c->dyno_nx_frac * a->n_x_rpm));
        return in ? MC_R_NONE : MC_R_SPEED;
    }
    return (n <= c->lock_rpm) ? MC_R_NONE : (running ? MC_R_MOVED : MC_R_SPEED);
}

static float i_amp(const app_t *a)
{
    float al;
    float be;
    foc_clarke(a->isns.i_a, &al, &be);
    return sqrtf((al * al) + (be * be));
}

static mc_reason_t watch(const app_t *a, uint32_t t_ms)
{
    const mc_t *m = &g_mc;
    const mc_cal_t *c = m->cal;
    if (ti_elapsed(t_ms, m->t_hb_ms, c->hb_timeout_ms)) {
        return MC_R_HEARTBEAT;
    }
    if (ti_elapsed(t_ms, m->t_start_ms, m->dur_ms)) {
        return MC_R_TIMEOUT;
    }
    if (i_amp(a) > m->i_bound_a) {
        return MC_R_CURRENT;
    }
    if (a->foc.sat && (a->br.mode == BR_MOD)) {
        return MC_R_VOLTAGE;
    }
    if (m->rt == MC_RT_PSI_ZERO) {
        return (ti_absf(a->speed_rpm - m->rpm0) > (c->dyno_steady_frac * ti_absf(m->rpm0))) ? MC_R_SPEED : MC_R_NONE;
    }
    return (ti_absf(ti_wrap_pi(rslv_theta_e(&a->rslv, &a->cal.rslv) - m->th0)) > c->lock_rad) ? MC_R_MOVED : MC_R_NONE;
}

/* The normal safe state: no service modulation — the next current-loop ISR turns the PWM off and the bridge stays
 * armed idle; when a §6 row caused it, its decision owns the bridge and is not touched here. */
static void mc_abort(app_t *a, mc_reason_t r, uint32_t t_ms)
{
    mc_t *m = &g_mc;
    m->run = false;
    m->st = MC_ABORTED;
    m->reason = r;
    if (!fm_any(&a->fm)) {
        a->mod_req = false;
    }
    if (r != MC_R_STOPPED) {
        dtc_set(DTC_MC_ABORTED, t_ms);
    }
}

/* ======================= the routine's plan (task) ======================= */
static uint32_t samples(const app_t *a, uint32_t ms) { return (ms * 2u * a->gains.fsw_hz) / 1000u; }

/* Round 23 (FW-45): breakpoint k's current, at least the routine's bias, at most the SKU's current limit — and for the d run
 * the demagnetisation limit — less the HF amplitude.
 * TODO(HW): the six-bias sweep on the dyno against an LCR / saturation reference, the rig's brake at the q run's torque
 * and the phase currents' zero crossings at low bias (target-bringup T-57). */
static float map_bias(const app_t *a, uint32_t k, bool d_run)
{
    const mc_cal_t *c = g_mc.cal;
    const float full = a->cal.motor.i_map_a * (float)k / (float)(MOTOR_MAP_N - 1u);
    float top = a->p->i_crest_a;
    top = d_run ? ti_minf(top, a->cal.motor.id_demag_a) : top;
    return ti_maxf(ti_minf(ti_maxf(full, c->hf_bias_a), top - c->hf_i_a), 0.0f);
}

static void plan(app_t *a, mc_routine_t rt, int8_t dir, uint8_t bias_k, uint32_t t_ms)
{
    mc_t *m = &g_mc;
    const mc_cal_t *c = m->cal;
    uint32_t settle_ms = c->psi_settle_ms;
    uint32_t meas_ms = c->psi_meas_ms;
    uint32_t per = 1u;
    m->rt = rt;
    m->dir = dir;
    m->bias_k = bias_k;
    m->th0 = rslv_theta_e(&a->rslv, &a->cal.rslv);
    m->c0 = cosf(m->th0);
    m->s0 = sinf(m->th0);
    m->rpm0 = a->speed_rpm;
    m->n_phases = (rt == MC_RT_PSI_ZERO) ? 1u : 2u;
    m->i_bound_a = c->i_margin_a;
    for (uint32_t i = 0u; i < 2u; i++) {
        m->ref_d[i] = 0.0f;
        m->ref_q[i] = 0.0f;
    }
    if (rt == MC_RT_RS) { /* along phase U's axis (alpha): every phase carries at least half the current */
        m->ref_d[0] = c->rs_i1_a * m->c0;
        m->ref_q[0] = -c->rs_i1_a * m->s0;
        m->ref_d[1] = c->rs_i2_a * m->c0;
        m->ref_q[1] = -c->rs_i2_a * m->s0;
        m->i_bound_a = (1.5f * c->rs_i2_a) + c->i_margin_a;
        settle_ms = c->rs_settle_ms;
        meas_ms = c->rs_meas_ms;
    } else if (rt == MC_RT_LDQ) {
        m->hf_n = c->hf_n;
        m->hf_amp = c->hf_i_a;
        for (uint32_t j = 0u; j < m->hf_n; j++) {
            const float ph = TI_2PI * (float)j / (float)m->hf_n;
            m->hf_sin[j] = sinf(ph);
            m->hf_cos[j] = cosf(ph);
        }
        for (uint32_t i = 0u; i < 2u; i++) { /* the operating point: a DC bias along phase U's axis */
            m->ref_d[i] = c->hf_bias_a * m->c0;
            m->ref_q[i] = -c->hf_bias_a * m->s0;
        }
        m->i_bound_a = (1.5f * (c->hf_bias_a + c->hf_i_a)) + c->i_margin_a;
        if (bias_k != MC_BIAS_NONE) {
            /* round 23 (FW-45): the bias of breakpoint k along the TRUE axis each run injects on — the d run at i_d = -b,
             * the q run at i_q = +b; the true d axis is (cos e, -sin e), the true q axis (sin e, cos e) in the controller's
             * frame, e = the back-EMF zero of this key cycle less the record's (0 without one) */
            const float e = m->eps_valid ? m->eps_rad : 0.0f;
            const float bd = map_bias(a, bias_k, true);
            const float bq = map_bias(a, bias_k, false);
            m->ref_d[0] = -bd * cosf(e);
            m->ref_q[0] = bd * sinf(e);
            m->ref_d[1] = bq * sinf(e);
            m->ref_q[1] = bq * cosf(e);
            m->i_bound_a = (1.5f * (ti_maxf(bd, bq) + c->hf_i_a)) + c->i_margin_a;
        }
        per = m->hf_n; /* whole injection periods: exact demodulation */
        settle_ms = c->hf_settle_ms;
        meas_ms = c->hf_meas_ms;
    } else {
        /* i_d = i_q = 0 */
    }
    m->n_settle = ((samples(a, settle_ms) + per - 1u) / per) * per;
    const uint32_t nb = samples(a, meas_ms) / (MC_BLOCKS * per);
    m->n_block = ((nb > 0u) ? nb : 1u) * per;
    m->n_phase = m->n_settle + (MC_BLOCKS * m->n_block);
    m->dur_ms = ((m->n_phases * m->n_phase * 1000u) / (2u * a->gains.fsw_hz)) + 20u;
    (void)memset(m->acc, 0, sizeof m->acc);
    m->cross = 0.0f;
    m->va_prev = 0.0f;
    m->vb_prev = 0.0f;
    m->k = 0u;
    m->done = false;
    const mc_qty_t first = (rt == MC_RT_RS) ? MC_Q_RS : ((rt == MC_RT_LDQ) ? MC_Q_LD : MC_Q_PSI);
    const mc_qty_t last = (rt == MC_RT_RS) ? MC_Q_RS : ((rt == MC_RT_LDQ) ? MC_Q_LQ : MC_Q_ZERO);
    for (uint32_t q = (uint32_t)first; q <= (uint32_t)last; q++) {
        g_mc.q[q].value = 0.0f; /* this run's; pending and staged ones stay */
        g_mc.q[q].u = 0.0f;
        g_mc.q[q].verdict = (uint8_t)MC_V_NONE;
        g_mc.q[q].beyond = false;
    }
    m->st = MC_RUNNING;
    m->bar = true;
    m->reason = MC_R_NONE;
    m->t_start_ms = t_ms;
    m->t_hb_ms = t_ms;
    atomic_signal_fence(memory_order_release); /* the plan before the flag the ISR reads */
    m->run = true;
}

/* ======================= current-loop ISR ======================= */
void mc_isr_refs(app_t *a)
{
    mc_t *m = &g_mc;
    if (!m->run || a->zero_now) {
        return;
    }
    const uint32_t ph = m->k / m->n_phase;
    const uint32_t j = m->k % m->n_phase;
    if (m->rt == MC_RT_LDQ) {
        const float s = m->hf_amp * m->hf_sin[j % m->hf_n];
        a->foc.id_ref = m->ref_d[ph] + ((ph == 0u) ? s : 0.0f);
        a->foc.iq_ref = m->ref_q[ph] + ((ph == 0u) ? 0.0f : s);
    } else {
        a->foc.id_ref = m->ref_d[ph];
        a->foc.iq_ref = m->ref_q[ph];
    }
}

void mc_isr_sample(app_t *a)
{
    mc_t *m = &g_mc;
    if (!m->run || !a->mod_req || a->zero_now) {
        return;
    }
    const uint32_t ph = m->k / m->n_phase;
    const uint32_t j = m->k % m->n_phase;
    const foc_t *f = &a->foc;
    if (j >= m->n_settle) {
        float *x = m->acc[ph][(j - m->n_settle) / m->n_block];
        /* the voltage the bridge is commanded, from the duties (alpha-beta; the common mode cancels) */
        const float va = ((2.0f * f->duty[0]) - f->duty[1] - f->duty[2]) * (a->vdc.vdc * (1.0f / 3.0f));
        const float vb = (f->duty[1] - f->duty[2]) * (a->vdc.vdc * (1.0f / TI_SQRT3));
        if (m->rt == MC_RT_PSI_ZERO) { /* the loop's own dq output: at i = 0 its dead-time compensation is idle */
            x[0] += f->vd;
            x[1] += f->vq;
            x[2] += f->id;
            x[3] += f->iq;
            x[4] += rslv_omega_e(&a->rslv, &a->cal.rslv);
            m->cross += (m->va_prev * vb) - (m->vb_prev * va); /* the voltage vector's turn: not the resolver's */
            m->va_prev = va;
            m->vb_prev = vb;
        } else {
            /* standstill: the duties carry the FOC's dead-time compensation, which acts on a current sampled 1.5
             * periods earlier (at the injection frequency its own vd/vq would read it as reactance); voltage and
             * currents both in the frame of the locked rotor's angle th0 */
            float ia;
            float ib;
            foc_clarke(a->isns.i_a, &ia, &ib);
            if (m->rt == MC_RT_RS) {
                x[0] += va; /* the injection is along alpha (phase U's axis) */
                x[1] += vb;
                x[2] += ia;
                x[3] += ib;
            } else {
                const float s = m->hf_sin[j % m->hf_n];
                const float c = m->hf_cos[j % m->hf_n];
                const float vd = (va * m->c0) + (vb * m->s0);
                const float vq = (vb * m->c0) - (va * m->s0);
                const float id = (ia * m->c0) + (ib * m->s0);
                const float iq = (ib * m->c0) - (ia * m->s0);
                x[0] += vd * s;
                x[1] += vd * c;
                x[2] += vq * s;
                x[3] += vq * c;
                x[4] += id * s;
                x[5] += id * c;
                x[6] += iq * s;
                x[7] += iq * c;
            }
        }
        x[MC_ACC] += 1.0f;
    }
    m->k++;
    if (m->k >= (m->n_phases * m->n_phase)) {
        atomic_signal_fence(memory_order_release); /* the sums before the flag the task reads */
        m->run = false;
        m->done = true;
    }
}

/* ======================= estimates (task) ======================= */
static float std_err(const float x[MC_BLOCKS])
{
    float mu = 0.0f;
    for (uint32_t b = 0u; b < MC_BLOCKS; b++) {
        mu += x[b];
    }
    mu /= (float)MC_BLOCKS;
    float s = 0.0f;
    for (uint32_t b = 0u; b < MC_BLOCKS; b++) {
        s += (x[b] - mu) * (x[b] - mu);
    }
    return sqrtf(s / (float)(MC_BLOCKS * (MC_BLOCKS - 1u)));
}

static float root_sum(float a, float b) { return sqrtf((a * a) + (b * b)); }

static uint8_t noisy(float u, float v, float lim)
{
    return (uint8_t)((ti_finite(u) && (u <= (lim * ti_absf(v)))) ? MC_V_VALID : MC_V_NOISY);
}

static void est_rs(const app_t *a)
{
    mc_t *m = &g_mc;
    const mc_cal_t *c = m->cal;
    float v[2] = {0.0f, 0.0f};
    float i[2] = {0.0f, 0.0f};
    float n[2] = {0.0f, 0.0f};
    float rb[MC_BLOCKS];
    for (uint32_t b = 0u; b < MC_BLOCKS; b++) {
        const float *x1 = m->acc[0][b];
        const float *x2 = m->acc[1][b];
        rb[b] = ((x2[0] / x2[MC_ACC]) - (x1[0] / x1[MC_ACC])) / ((x2[2] / x2[MC_ACC]) - (x1[2] / x1[MC_ACC]));
        for (uint32_t ph = 0u; ph < 2u; ph++) {
            v[ph] += m->acc[ph][b][0];
            i[ph] += m->acc[ph][b][2];
            n[ph] += m->acc[ph][b][MC_ACC];
        }
    }
    const float i1 = i[0] / n[0];
    const float i2 = i[1] / n[1];
    mc_result_t *r = &m->q[MC_Q_RS];
    r->value = ((v[1] / n[1]) - (v[0] / n[0])) / (i2 - i1); /* the constant inverter error cancels between the levels */
    r->u = root_sum(std_err(rb), c->u_floor_rel * r->value);
    const bool reached = (ti_absf(i1 - c->rs_i1_a) <= (0.1f * c->rs_i1_a)) &&
                         (ti_absf(i2 - c->rs_i2_a) <= (0.1f * c->rs_i2_a)); /* the loop regulated both levels */
    r->verdict = (reached && ti_finite(r->value)) ? noisy(r->u, r->value, c->u_max_rel) : (uint8_t)MC_V_NOT_REACHED;
    judge(a, MC_Q_RS);
}

typedef struct {
    float re, im;
} cpx_t;

static cpx_t cmul(cpx_t x, cpx_t y) { return (cpx_t){(x.re * y.re) - (x.im * y.im), (x.re * y.im) + (x.im * y.re)}; }
static cpx_t csub(cpx_t x, cpx_t y) { return (cpx_t){x.re - y.re, x.im - y.im}; }
static cpx_t cdiv(cpx_t x, cpx_t y)
{
    const float d = (y.re * y.re) + (y.im * y.im);
    return (cpx_t){((x.re * y.re) + (x.im * y.im)) / d, ((x.im * y.re) - (x.re * y.im)) / d};
}
/* x = A sin(w t + phi) summed against sin and cos over whole periods => A e^(j phi) */
static cpx_t phasor(const float *x, uint32_t at)
{
    return (cpx_t){2.0f * x[at] / x[MC_ACC], 2.0f * x[at + 1u] / x[MC_ACC]};
}

/* Experiment A injected d, B injected q (their sums xa, xb): the controller-frame impedance Z = [V_A V_B][I_A I_B]^-1
 * (exact whatever cross current the loop let through), the command's actuation undone (k = e^(-j w tau) / sinc(w T/2)),
 * L' = Im(Z)/w (symmetrised); in the frame turned by eps_ref its eigenvalues: Ld = Sigma + Delta, Lq = Sigma - Delta,
 * Delta = (Ld - Lq)/2 signed from the frame's own diagonal (the right axes while the zero used is within 45 deg el),
 * and the residual angle of the saliency axis. */
static void ldq(const float *xa, const float *xb, float w, cpx_t k, float eps_ref, float out[5])
{
    const cpx_t vda = phasor(xa, 0u);
    const cpx_t vqa = phasor(xa, 2u);
    const cpx_t ida = phasor(xa, 4u);
    const cpx_t iqa = phasor(xa, 6u);
    const cpx_t vdb = phasor(xb, 0u);
    const cpx_t vqb = phasor(xb, 2u);
    const cpx_t idb = phasor(xb, 4u);
    const cpx_t iqb = phasor(xb, 6u);
    const cpx_t det = csub(cmul(ida, iqb), cmul(idb, iqa));
    const cpx_t z11 = cmul(cdiv(csub(cmul(vda, iqb), cmul(vdb, iqa)), det), k);
    const cpx_t z12 = cmul(cdiv(csub(cmul(vdb, ida), cmul(vda, idb)), det), k);
    const cpx_t z21 = cmul(cdiv(csub(cmul(vqa, iqb), cmul(vqb, iqa)), det), k);
    const cpx_t z22 = cmul(cdiv(csub(cmul(vqb, ida), cmul(vqa, idb)), det), k);
    const float sig = 0.5f * (z11.im + z22.im) / w;
    const float d = 0.5f * (z11.im - z22.im) / w;
    const float cc = 0.5f * (z12.im + z21.im) / w;
    const float c2 = cosf(2.0f * eps_ref);
    const float s2 = sinf(2.0f * eps_ref);
    const float d2 = (d * c2) - (cc * s2);
    const float cr = (cc * c2) + (d * s2);
    const float mag = sqrtf((d2 * d2) + (cr * cr));
    const float delta = (d2 >= 0.0f) ? mag : -mag;
    out[0] = sig + delta;
    out[1] = sig - delta;
    out[2] = (mag > 0.0f) ? (0.5f * atan2f(-cr / delta, d2 / delta)) : 0.0f;
    out[3] = sqrtf((ida.re * ida.re) + (ida.im * ida.im)); /* the injected amplitudes the loop reached */
    out[4] = sqrtf((iqb.re * iqb.re) + (iqb.im * iqb.im));
}

static void est_ldq(const app_t *a)
{
    mc_t *m = &g_mc;
    const mc_cal_t *c = m->cal;
    const float ts = a->gains.ts_s;
    const float w = TI_2PI / ((float)m->hf_n * ts);
    /* sampled current vs held command, exactly (a pure inductance, the duty held one period from one period after its
     * sample): V/I = j w L sinc(w T/2) e^(+j w 1.5 T) — undone by k = e^(-j w tau) / sinc, tau = delay_s.
     * TODO(HW): tau and the bridge's residual error at the bias point on the real inverter (target-bringup FW-39). */
    const float x = 0.5f * w * ts;
    const float g = x / sinf(x);
    const cpx_t k = {g * cosf(w * a->gains.delay_s), -g * sinf(w * a->gains.delay_s)};
    const float eps_ref = m->eps_valid ? m->eps_rad : 0.0f;
    float sa[MC_ACC + 1u] = {0.0f};
    float sb[MC_ACC + 1u] = {0.0f};
    float ldb[MC_BLOCKS];
    float lqb[MC_BLOCKS];
    float o[5];
    for (uint32_t b = 0u; b < MC_BLOCKS; b++) {
        ldq(m->acc[0][b], m->acc[1][b], w, k, eps_ref, o);
        ldb[b] = o[0];
        lqb[b] = o[1];
        for (uint32_t i = 0u; i <= MC_ACC; i++) {
            sa[i] += m->acc[0][b][i];
            sb[i] += m->acc[1][b][i];
        }
    }
    ldq(sa, sb, w, k, eps_ref, o);
    const bool reached =
        (o[3] >= (0.5f * m->hf_amp)) && (o[4] >= (0.5f * m->hf_amp)) && ti_finite(o[0]) && ti_finite(o[1]);
    const bool salient = ti_absf(o[0] - o[1]) > (c->axes_min_rel * 0.5f * (o[0] + o[1]));
    const bool axes = !salient || (ti_absf(o[2]) <= c->axes_max_rad);
    const float *bl[2] = {ldb, lqb};
    for (uint32_t i = 0u; i < 2u; i++) {
        mc_result_t *r = &m->q[(uint32_t)MC_Q_LD + i];
        r->value = o[i];
        r->u = root_sum(std_err(bl[i]), c->u_floor_rel * o[i]);
        r->verdict = !reached ? (uint8_t)MC_V_NOT_REACHED
                   : (!axes ? (uint8_t)MC_V_AXES : noisy(r->u, r->value, c->u_max_rel));
        if (m->bias_k != MC_BIAS_NONE) { /* round 23 (FW-45): a map point, not the scalar */
            mc_result_t *pt = &m->mp[i][m->bias_k];
            pt->value = r->value;
            pt->u = r->u;
            pt->verdict = r->verdict;
            pt->beyond = false;
            judge_point(a, i, m->bias_k);
        }
    }
    if (m->bias_k == MC_BIAS_NONE) {
        judge(a, MC_Q_LD);
        judge(a, MC_Q_LQ);
    }
}

/* The back-EMF in the controller frame: e_d = w psi sin(eps), e_q = w psi cos(eps), eps = the true zero less the
 * record's; the loop's R and L terms taken off with the record's values (i_d, i_q are regulated to 0). */
static void emf(const float *x, const motor_t *mo, float *psi, float *eps, float *w)
{
    const float n = x[MC_ACC];
    const float id = x[2] / n;
    const float iq = x[3] / n;
    *w = x[4] / n;
    const float ed = (x[0] / n) - (mo->rs_ohm * id) + (*w * mo->lq_h * iq);
    const float eq = (x[1] / n) - (mo->rs_ohm * iq) - (*w * mo->ld_h * id);
    const float sg = (*w >= 0.0f) ? 1.0f : -1.0f;
    *psi = sqrtf((ed * ed) + (eq * eq)) / ti_absf(*w);
    *eps = atan2f(sg * ed, sg * eq);
}

static void est_psi(const app_t *a)
{
    mc_t *m = &g_mc;
    const mc_cal_t *c = m->cal;
    float s[MC_ACC + 1u] = {0.0f};
    float pb[MC_BLOCKS];
    float eb[MC_BLOCKS];
    float wb = 0.0f;
    for (uint32_t b = 0u; b < MC_BLOCKS; b++) {
        emf(m->acc[0][b], &a->cal.motor, &pb[b], &eb[b], &wb);
        for (uint32_t i = 0u; i <= MC_ACC; i++) {
            s[i] += m->acc[0][b][i];
        }
    }
    float psi;
    float eps;
    float w;
    emf(s, &a->cal.motor, &psi, &eps, &w);
    for (uint32_t b = 0u; b < MC_BLOCKS; b++) {
        eb[b] = ti_wrap_pi(eb[b] - eps);
    }
    const bool dir_phases = (m->cross > 0.0f) == (w > 0.0f); /* the voltage vector turns with the resolver */
    const bool dir_dyno = (w > 0.0f) == (m->dir > 0);
    const uint8_t base =
        !dir_phases ? (uint8_t)MC_V_DIR_PHASES : (!dir_dyno ? (uint8_t)MC_V_DIR_DYNO : (uint8_t)MC_V_VALID);
    mc_result_t *rp = &m->q[MC_Q_PSI];
    rp->value = psi;
    rp->u = root_sum(std_err(pb), c->u_floor_rel * psi);
    rp->verdict = (base != (uint8_t)MC_V_VALID) ? base : noisy(rp->u, psi, c->u_max_rel);
    mc_result_t *rz = &m->q[MC_Q_ZERO];
    rz->value = ti_wrap_2pi(a->cal.rslv.zero_rad + eps);
    /* TODO(HW): the zero against a reference encoder on the dyno, both directions (target-bringup FW-39). */
    rz->u = root_sum(std_err(eb), ti_absf(w) * c->t_unc_us * 1.0e-6f);
    rz->verdict = (base != (uint8_t)MC_V_VALID) ? base
                : ((ti_finite(rz->u) && (rz->u <= c->zero_u_max_rad)) ? (uint8_t)MC_V_VALID : (uint8_t)MC_V_NOISY);
    if (rz->verdict == (uint8_t)MC_V_VALID) {
        m->eps_valid = true; /* the LDQ routine's axes from now on */
        m->eps_rad = eps;
    }
    judge(a, MC_Q_PSI);
    judge(a, MC_Q_ZERO);
}

/* ======================= the 1 ms task ======================= */
void mc_task(app_t *a, uint32_t t_ms)
{
    mc_t *m = &g_mc;
    if (m->st != MC_RUNNING) {
        m->bar = m->bar && a->can.enable_req; /* lifted once the VCU has withdrawn its enable */
        return;
    }
    mc_reason_t r = preconditions(a, m->rt, t_ms, true);
    r = (r != MC_R_NONE) ? r : watch(a, t_ms);
    if (r != MC_R_NONE) {
        mc_abort(a, r, t_ms);
        return;
    }
    if (m->done) {
        atomic_signal_fence(memory_order_acquire); /* the flag before the sums */
        if (m->rt == MC_RT_RS) {
            est_rs(a);
        } else if (m->rt == MC_RT_LDQ) {
            est_ldq(a);
        } else {
            est_psi(a);
        }
        m->done = false;
        m->st = MC_DONE;
    }
}

/* ======================= UDS ======================= */
static bool reply(hal_can_frame_t *rsp, const uint8_t *pl, uint8_t n)
{
    *rsp = (hal_can_frame_t){.id = UDS_ID_RSP, .len = 8u};
    (void)memset(rsp->data, 0xAA, 8u);
    rsp->data[0] = n;
    (void)memcpy(&rsp->data[1], pl, n);
    return true;
}

static uint8_t start(app_t *a, const uint8_t *opt, uint8_t n_opt, uint32_t t_ms)
{
    mc_t *m = &g_mc;
    if (m->st == MC_RUNNING) {
        return UDS_NRC_SEQUENCE;
    }
    if ((n_opt != 3u) && (n_opt != 4u)) {
        return UDS_NRC_LENGTH;
    }
    if ((opt[0] == (uint8_t)MC_RT_NONE) || (opt[0] >= (uint8_t)MC_RT_COUNT)) {
        return UDS_NRC_OUT_OF_RANGE;
    }
    const uint8_t bias_k = (n_opt == 4u) ? opt[3] : (uint8_t)MC_BIAS_NONE; /* round 23 (FW-45) */
    if ((n_opt == 4u) && ((opt[0] != (uint8_t)MC_RT_LDQ) || (bias_k >= MOTOR_MAP_N))) {
        return UDS_NRC_OUT_OF_RANGE;
    }
    if (!a->uds.unlocked) {
        return UDS_NRC_SECURITY_DENIED; /* the FW-32 SecurityAccess, nothing else */
    }
    const mc_routine_t rt = (mc_routine_t)opt[0];
    const uint32_t att = ((uint32_t)opt[1] << 8) | opt[2];
    const int8_t dir = (att == MC_ATTEST_DYNO_FWD) ? (int8_t)1 : ((att == MC_ATTEST_DYNO_REV) ? (int8_t)-1 : (int8_t)0);
    mc_reason_t r = ((rt == MC_RT_PSI_ZERO) ? (dir != 0) : (att == MC_ATTEST_LOCKED)) ? MC_R_NONE : MC_R_ATTEST;
    r = (r != MC_R_NONE) ? r : ((mc_cal_validate(m->cal) == 0u) ? MC_R_NONE : MC_R_CAL);
    r = (r != MC_R_NONE) ? r : preconditions(a, rt, t_ms, false);
    if (r != MC_R_NONE) {
        m->reason = r; /* refused: nothing started, the unlock stays */
        return UDS_NRC_CONDITIONS;
    }
    plan(a, rt, dir, bias_k, t_ms);
    a->uds.unlocked = false; /* one start per unlock */
    return 0u;
}

static uint8_t flags(const mc_result_t *r)
{
    return (uint8_t)((r->pending ? 1u : 0u) | (r->staged ? 2u : 0u) | (r->beyond ? 4u : 0u));
}

/* round 23 (FW-45): the staged points of one map (bit k) */
static uint8_t map_mask(uint32_t ax)
{
    uint8_t mask = 0u;
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
        mask |= g_mc.mp[ax][k].staged ? (uint8_t)(1u << k) : 0u;
    }
    return mask;
}

static uint8_t results(const uint8_t *opt, uint8_t n_opt, uint8_t out[3])
{
    static const float UNIT[MC_Q_COUNT] = {MC_UNIT_RS, MC_UNIT_L, MC_UNIT_L, MC_UNIT_PSI, MC_UNIT_ZERO};
    const mc_t *m = &g_mc;
    if (n_opt != 1u) {
        return UDS_NRC_LENGTH;
    }
    const uint8_t idx = opt[0];
    const uint32_t q = idx & 0x0Fu;
    const uint32_t hi = idx & 0xF0u;
    out[0] = idx;
    if (idx <= 1u) {
        uint8_t mask = m->committed ? 0x80u : 0u;
        for (uint32_t i = 0u; i < (uint32_t)MC_Q_COUNT; i++) {
            mask |= m->q[i].staged ? (uint8_t)(1u << i) : 0u;
        }
        mask |= ((map_mask(0u) | map_mask(1u)) != 0u) ? 0x20u : 0u; /* round 23: FW-45 map points, FW-46 the table */
        mask |= m->rip_staged ? 0x40u : 0u;
        out[1] = (idx == 0u) ? (uint8_t)m->st : (uint8_t)m->rt;
        out[2] = (idx == 0u) ? (uint8_t)m->reason : mask;
        return 0u;
    }
    if (idx == 2u) { /* round 23 (FW-45) */
        out[1] = map_mask(0u);
        out[2] = map_mask(1u);
        return 0u;
    }
    const bool point = ((hi == 0x40u) || (hi == 0x50u)) && (q < MOTOR_MAP_N); /* round 23 (FW-45) */
    if (!point && ((q >= (uint32_t)MC_Q_COUNT) || ((hi != 0x10u) && (hi != 0x20u) && (hi != 0x30u)))) {
        return UDS_NRC_OUT_OF_RANGE;
    }
    const mc_result_t *r = point ? &m->mp[(hi == 0x40u) ? 0u : 1u][q] : &m->q[q];
    if (hi == 0x10u) {
        const bool biased = (m->bias_k != MC_BIAS_NONE) && ((q == (uint32_t)MC_Q_LD) || (q == (uint32_t)MC_Q_LQ));
        out[1] = r->verdict;
        out[2] = biased ? (uint8_t)(flags(&m->mp[q - (uint32_t)MC_Q_LD][m->bias_k]) | 0x08u | (uint32_t)(m->bias_k << 4))
                        : flags(r);
        return 0u;
    }
    const float v = ((hi == 0x30u) ? r->u : r->value) / (point ? MC_UNIT_L : UNIT[q]);
    const uint16_t be = (ti_finite(v) && (v > 0.0f)) ? ((v < 65535.0f) ? (uint16_t)(v + 0.5f) : 0xFFFFu) : 0u;
    out[1] = (uint8_t)(be >> 8);
    out[2] = (uint8_t)(be & 0xFFu);
    return 0u;
}

/* RID 0xF021: the staged values into a copy of the active record, sealed and checked by FW-20, queued as a new
 * NV_REC_CALIB version. The running key cycle keeps its record; the next one's init validates this one. Only with the
 * bridge not switching: nv_queue copies the 352-byte record under PRIMASK (the longest critical section, timing.md). */
static uint8_t commit(app_t *a, uint32_t t_ms)
{
    mc_t *m = &g_mc;
    if (!a->uds.unlocked) {
        return UDS_NRC_SECURITY_DENIED;
    }
    if ((m->st == MC_RUNNING) || (a->br.mode == BR_MOD) || (a->br.mode == BR_ASC)) {
        return UDS_NRC_CONDITIONS;
    }
    calib_t c;
    (void)memcpy(&c, &a->cal, sizeof c);
    bool any = false;
    for (uint32_t q = 0u; q < (uint32_t)MC_Q_COUNT; q++) {
        if (m->q[q].staged) {
            *field(&c, (mc_qty_t)q) = m->q[q].staged_value;
            any = true;
        }
    }
    /* round 23 (FW-45): a whole staged axis becomes its map — the apparent inductance at each breakpoint from the
     * differential ones (trapezoids, in steps: L_k = sum over j <= k of (D_(j-1) + D_j) / 2, over k); its scalar the map's
     * point 0 unless the scalar is staged too. Some points only: nothing is written. */
    bool maps = false;
    for (uint32_t ax = 0u; ax < 2u; ax++) {
        const uint8_t mask = map_mask(ax);
        if (mask == 0u) {
            continue;
        }
        if (mask != (uint8_t)((1u << MOTOR_MAP_N) - 1u)) {
            return UDS_NRC_CONDITIONS; /* a map is committed whole */
        }
        float *map = (ax == 0u) ? c.motor.ld_map_h : c.motor.lq_map_h;
        float lam = 0.0f;
        map[0] = m->mp[ax][0].staged_value;
        for (uint32_t k = 1u; k < MOTOR_MAP_N; k++) {
            lam += 0.5f * (m->mp[ax][k - 1u].staged_value + m->mp[ax][k].staged_value);
            map[k] = lam / (float)k;
        }
        if (!m->q[(ax == 0u) ? MC_Q_LD : MC_Q_LQ].staged) {
            *((ax == 0u) ? &c.motor.ld_h : &c.motor.lq_h) = map[0];
        }
        maps = true;
    }
    if (m->rip_staged) { /* round 23 (FW-46) */
        (void)memcpy(c.ripple_ff, m->rip, sizeof c.ripple_ff);
        any = true;
    }
    if (!any && !maps) {
        return UDS_NRC_CONDITIONS;
    }
    if (m->q[MC_Q_LD].staged || m->q[MC_Q_LQ].staged || m->q[MC_Q_PSI].staged || maps) {
        c.mtpa.n = 0u; /* a table solved for the old motor data: the closed form until one is solved again */
    }
    calib_seal(&c);
    if (calib_check(&c, a->p, a->serial) != 0u) {
        return UDS_NRC_CONDITIONS;
    }
    if (!nv_queue(NV_REC_CALIB, &c, (uint16_t)sizeof c)) {
        return UDS_NRC_PROGRAMMING;
    }
    dtc_set(DTC_MC_CAL_WRITTEN, t_ms);
    for (uint32_t q = 0u; q < (uint32_t)MC_Q_COUNT; q++) {
        m->q[q].staged = false;
        m->q[q].pending = false;
    }
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) { /* round 23 (FW-45, FW-46) */
        for (uint32_t ax = 0u; ax < 2u; ax++) {
            m->mp[ax][k].staged = false;
            m->mp[ax][k].pending = false;
        }
    }
    m->rip_staged = false;
    m->committed = true;
    a->uds.unlocked = false;
    return 0u;
}

bool mc_uds_handle(app_t *a, const hal_can_frame_t *rq, hal_can_frame_t *rsp)
{
    if ((rq->id != UDS_ID_REQ) || (rq->len < 5u) || (rq->len > HAL_CAN_MAX_LEN) || ((rq->data[0] & 0xF0u) != 0u)) {
        return false;
    }
    uint8_t n = rq->data[0];
    const uint8_t *m = &rq->data[1];
    if (n == 0u) { /* round 23 (FW-45): the CAN-FD escape single frame (the biased start is 8 bytes) */
        n = rq->data[1];
        m = &rq->data[2];
        if ((rq->len <= 8u) || (n < 4u) || (n > (uint8_t)(rq->len - 2u)) || (m[0] != SID_RC)) {
            return false;
        }
    } else if ((n < 4u) || (n > 7u) || (n >= rq->len) || (m[0] != SID_RC)) {
        return false;
    }
    const uint32_t rid = ((uint32_t)m[2] << 8) | m[3];
    if ((rid != UDS_RID_MC_RUN) && (rid != UDS_RID_MC_COMMIT)) {
        return false; /* the UDS server's */
    }
    const uint32_t t_ms = hal_time_ms();
    uint8_t out[3] = {0u, 0u, 0u};
    uint8_t n_out = 0u;
    uint8_t code = UDS_NRC_SUBFUNCTION_NOT_SUPPORTED;
    if (rid == UDS_RID_MC_COMMIT) {
        code = (m[1] != RC_START) ? code : ((n != 4u) ? UDS_NRC_LENGTH : commit(a, t_ms));
    } else {
        g_mc.t_hb_ms = t_ms; /* the tool's heartbeat: any request of the routine */
        if (m[1] == RC_START) {
            code = start(a, &m[4], (uint8_t)(n - 4u), t_ms);
            out[0] = m[4];
            n_out = 1u;
        } else if (m[1] == RC_STOP) {
            code = (n != 4u) ? UDS_NRC_LENGTH : ((g_mc.st == MC_RUNNING) ? 0u : UDS_NRC_SEQUENCE);
            if (code == 0u) {
                mc_abort(a, MC_R_STOPPED, t_ms);
            }
        } else if (m[1] == RC_RESULTS) {
            code = results(&m[4], (uint8_t)(n - 4u), out);
            n_out = 3u;
        } else {
            /* sub-function not supported */
        }
    }
    if (code != 0u) {
        const uint8_t pl[3] = {0x7Fu, SID_RC, code};
        return reply(rsp, pl, 3u);
    }
    const uint8_t pl[7] = {SID_RC + 0x40u, m[1], m[2], m[3], out[0], out[1], out[2]};
    return reply(rsp, pl, (uint8_t)(4u + n_out));
}

/* ======================= round 23 (FW-46): the ripple table's staging (DID 0xFD46, uds_diag.c) ======================= */
/* 2E FD 46 + 36 big-endian int16 (0.01 A): the FW-39 interlocks — the SecurityAccess unlock (one write per unlock), no
 * routine running, not in torque (RUN/DERATE, the bridge modulating or in ASC) — and every value within
 * cal_ripple_ff_max_a (0: only a zero table). Staged in RAM; the commit (RID 0xF021) seals it into the record.
 * TODO(HW): the table from a dyno torque transducer at <= 100 rpm, and cal_ripple_ff_max_a / cal_ripple_ff_fmax_hz
 * against the measured reduction (target-bringup T-58). */
uint8_t mc_ripple_write(app_t *a, const uint8_t *m, uint32_t n)
{
    if (n != (3u + (2u * TQ_RIPPLE_N))) {
        return UDS_NRC_LENGTH;
    }
    if (!a->uds.unlocked) {
        return UDS_NRC_SECURITY_DENIED;
    }
    if ((g_mc.st == MC_RUNNING) || a->so.torque_enable || (a->br.mode == BR_MOD) || (a->br.mode == BR_ASC)) {
        return UDS_NRC_CONDITIONS;
    }
    int16_t t[TQ_RIPPLE_N];
    const float lim = 100.0f * a->p->cal_ripple_ff_max_a; /* counts */
    for (uint32_t k = 0u; k < TQ_RIPPLE_N; k++) {
        t[k] = (int16_t)(uint16_t)(((uint32_t)m[3u + (2u * k)] << 8) | m[4u + (2u * k)]);
        if (ti_absf((float)t[k]) > lim) {
            return UDS_NRC_OUT_OF_RANGE;
        }
    }
    (void)memcpy(g_mc.rip, t, sizeof g_mc.rip);
    g_mc.rip_staged = true;
    a->uds.unlocked = false;
    return 0u;
}

void mc_ripple_read(const app_t *a, uint8_t out[2u * TQ_RIPPLE_N])
{
    const int16_t *t = g_mc.rip_staged ? g_mc.rip : a->cal.ripple_ff;
    for (uint32_t k = 0u; k < TQ_RIPPLE_N; k++) {
        out[2u * k] = (uint8_t)((uint16_t)t[k] >> 8);
        out[(2u * k) + 1u] = (uint8_t)((uint16_t)t[k] & 0xFFu);
    }
}
