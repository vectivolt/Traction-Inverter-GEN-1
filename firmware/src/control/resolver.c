/* resolver.c — FW-10 demodulation, observer and plausibility. */
#include "resolver.h"

#include "ti_math.h"

#define N HAL_SDADC_BLOCK_N
#define SETTLE_BLOCKS 20u /* acquisition (2 ms) before tracking/acceleration faults count */
#define GAP_MAX_BLOCKS 8u /* a longer gap between consumed blocks re-acquires (invalid ~2.2 ms) */

static float s_sin[N], s_cos[N];
static bool s_tab;

static void table(void)
{
    if (!s_tab) {
        for (uint32_t k = 0u; k < N; k++) {
            const float ph = TI_2PI * (float)k / (float)N;
            s_sin[k] = sinf(ph);
            s_cos[k] = cosf(ph);
        }
        s_tab = true;
    }
}

void rslv_init(rslv_t *r)
{
    *r = (rslv_t){0};
    table();
}

/* x = A sin(wt + phi)  =>  i = A cos(phi), q = A sin(phi) */
static void iq(const int16_t x[N], float *i, float *q)
{
    float si = 0.0f;
    float sq = 0.0f;
    for (uint32_t k = 0u; k < N; k++) {
        si += (float)x[k] * s_sin[k];
        sq += (float)x[k] * s_cos[k];
    }
    *i = si * (2.0f / (float)N);
    *q = sq * (2.0f / (float)N);
}

static void count(bool bad, uint8_t *n, bool *fault, uint8_t limit)
{
    if (bad) {
        *n = (*n < 255u) ? (uint8_t)(*n + 1u) : 255u;
        if (*n >= limit) {
            *fault = true; /* latched for the key cycle */
        }
    } else {
        *n = 0u;
    }
}

static float res_per_motor(const rslv_cal_t *c) { return (float)c->resolver_pp / (float)c->motor_pp; }

/* Type-II tracking observer. Prediction uses the true time between the blocks consumed (a block
 * the ISR missed is bridged, not mistaken for a speed step); the correction uses the gains designed
 * for the nominal block period ts_s. Priming needs two consecutive blocks (angle and speed without
 * aliasing up to n_max); a gap longer than GAP_MAX_BLOCKS re-acquires. */
static void prime(rslv_t *r, float meas, float dt_s, float ts_s)
{
    if (r->have_first && (dt_s < (1.5f * ts_s))) {
        r->omega = ti_wrap_pi(meas - r->theta) / dt_s;
        r->omega_prev = r->omega;
        r->primed = true;
        r->n_blocks = 0u;
    }
    r->have_first = true;
    r->theta = meas;
}

static void observer(rslv_t *r, float dt_s, float ts_s, const rslv_cal_t *c, const ti_params_t *p)
{
    const float meas = ti_wrap_2pi(atan2f(r->sin_n, r->cos_n));
    if (r->primed && (dt_s > ((float)GAP_MAX_BLOCKS * ts_s))) {
        r->primed = false;
        r->have_first = false;
        r->locked = false;
    }
    if (!r->primed) {
        prime(r, meas, dt_s, ts_s);
        return;
    }
    const float wn = TI_2PI * p->cal_rslv_bw_hz;
    const float pred = r->theta + (r->omega * dt_s);
    r->err = ((r->sin_n * cosf(pred)) - (r->cos_n * sinf(pred))) / ti_maxf(r->amp, 1.0e-3f);
    r->omega += wn * wn * r->err * ts_s;
    r->theta = ti_wrap_2pi(pred + (2.0f * wn * r->err * ts_s));
    if (r->n_blocks < SETTLE_BLOCKS) {
        r->n_blocks++;
    } else {
        r->locked = true;
        count(ti_absf(r->err) > sinf(p->cal_rslv_track_err_rad), &r->n_trk, &r->trk_fault, p->cal_rslv_debounce);
        const float acc_lim = p->cal_rslv_accel_max_rad_s2 * res_per_motor(c);
        count((ti_absf(r->omega - r->omega_prev) / ts_s) > acc_lim, &r->n_acc, &r->acc_fault, p->cal_rslv_debounce);
    }
    r->omega_prev = r->omega;
}

void rslv_update(rslv_t *r, const int16_t exc[N], const int16_t sn[N], const int16_t cs[N], float ts_s, uint32_t t_us,
                 const rslv_cal_t *c, const ti_params_t *p)
{
    float ei;
    float eq;
    float si;
    float sq;
    float ci;
    float cq;
    table();
    iq(exc, &ei, &eq);
    iq(sn, &si, &sq);
    iq(cs, &ci, &cq);
    const float a_m = sqrtf((ei * ei) + (eq * eq));
    r->exc_ratio = a_m / ti_maxf(c->exc_nom_code, 1.0f);
    count((r->exc_ratio < p->cal_rslv_exc_min) || (r->exc_ratio > p->cal_rslv_exc_max), &r->n_exc, &r->exc_fault,
          p->cal_rslv_debounce);
    const float ref = atan2f(eq, ei) - ((p->cal_rslv_phase_comp_deg + c->phase_trim_deg) * (TI_PI / 180.0f));
    const float cr = cosf(ref);
    const float sr = sinf(ref);
    const float norm = ti_maxf(a_m * c->ratio_nom, 1.0f);
    r->sin_n = (c->sin_gain * ((si * cr) + (sq * sr)) / norm) - c->sin_offset;
    r->cos_n = (c->cos_gain * ((ci * cr) + (cq * sr)) / norm) - c->cos_offset;
    r->amp = sqrtf((r->sin_n * r->sin_n) + (r->cos_n * r->cos_n));
    const bool amp_bad = (r->amp < p->cal_rslv_amp_min) || (r->amp > p->cal_rslv_amp_max);
    count(amp_bad, &r->n_amp, &r->amp_fault, p->cal_rslv_debounce);
    const float dt_s = r->have_first ? ((float)(uint32_t)(t_us - r->t_ref_us) * 1.0e-6f) : ts_s;
    if (!amp_bad && !r->exc_fault && (ts_s > 0.0f) && (dt_s > 0.0f)) {
        observer(r, dt_s, ts_s, c, p);
        r->t_ref_us = t_us;
        r->t_mid_us = 0.5f * ts_s * 1.0e6f * (float)(N - 1u) / (float)N; /* mean sampling instant */
    }
    const bool fault = r->amp_fault || r->exc_fault || r->trk_fault || r->acc_fault || r->rate_fault;
    r->valid = r->locked && !amp_bad && !fault && ti_finite(r->theta) && ti_finite(r->omega);
}

float rslv_theta_e(const rslv_t *r, const rslv_cal_t *c)
{
    return ti_wrap_2pi((r->theta / res_per_motor(c)) - c->zero_rad);
}

float rslv_theta_e_at(const rslv_t *r, const rslv_cal_t *c, uint32_t now_us, const ti_params_t *p)
{
    const float dt_s = ((float)(int32_t)(now_us - r->t_ref_us) - r->t_mid_us - p->cal_rslv_latency_us) * 1.0e-6f;
    return ti_wrap_2pi(((r->theta + (r->omega * dt_s)) / res_per_motor(c)) - c->zero_rad);
}

float rslv_omega_e(const rslv_t *r, const rslv_cal_t *c) { return r->omega / res_per_motor(c); }

float rslv_speed_rpm(const rslv_t *r, const rslv_cal_t *c)
{
    return (r->omega / (float)c->resolver_pp) * TI_RPM_PER_RAD_S;
}

void rslv_rate_check(rslv_t *r, float omega_e_model, bool model_valid, const rslv_cal_t *c, const ti_params_t *p)
{
    const float w = rslv_omega_e(r, c);
    if (!model_valid || !r->valid || (ti_absf(w) < p->cal_rslv_rate_min_rad_s)) {
        r->n_rate = 0u;
        return;
    }
    const float tol = (p->cal_rslv_rate_tol_frac * ti_absf(w)) + p->cal_rslv_rate_tol_rad_s;
    count(ti_absf(omega_e_model - w) > tol, &r->n_rate, &r->rate_fault, (uint8_t)(p->cal_rslv_debounce * 10u));
    if (r->rate_fault) {
        r->valid = false;
    }
}

uint8_t rslv_swg_trim(uint8_t code, const rslv_t *r)
{
    if ((r->exc_ratio < 0.95f) && (code < 15u)) {
        return (uint8_t)(code + 1u);
    }
    if ((r->exc_ratio > 1.05f) && (code > 0u)) {
        return (uint8_t)(code - 1u);
    }
    return code;
}
