/* resolver.c — FW-10 demodulation, observer and plausibility. */
#include "resolver.h"

#include "swg.h"
#include "ti_math.h"

#define N HAL_SDADC_BLOCK_N
#define SETTLE_BLOCKS 20u /* acquisition (2 ms) before tracking/acceleration faults count */
#define GAP_MAX_BLOCKS 8u /* a longer gap between consumed blocks re-acquires (invalid ~2.2 ms) */
#define TRIM_LO 0.95f     /* SWG trim dead band around the setpoint: wider than one IOAMPL step (5-7 % */
#define TRIM_HI 1.05f     /* near the top of the range), so the trim never hunts */

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

/* Round 23 (item 7): the instant a block's angle refers to, from its start (us). The envelopes are each channel's
 * projection onto the lagged carrier, (2/N) sum_k x_k sin(ph_k + ref) with x_k = A sin(theta_k) sin(ph_k + ref): the
 * sin/cos of the angle weighted by sin^2(ph_k + ref) = (1 - cos(2 ph_k + 2 ref)) / 2, whose centroid is not the samples'
 * mean instant T (N - 1) / (2N) but T (N - 1) / (2N) - T sin(2 ref - 2 pi / N) / (2 N sin(2 pi / N)) (the sum of k cos(a k
 * + b) over whole periods). At the nominal compensation (ref = -24 deg, N 16, T 100 us) that is 7.70 us later: referred
 * to the mean, the angle led the rotor by w x 7.70 us — 1.85 deg el at 10 000 rpm, 4 pole pairs, which the simulator
 * bridge measured and cal_rslv_latency_us (a delay, >= 0) could not undo. It is the demodulator's own reference time, the
 * same on the target (its signals move within the block too): what remains for cal_rslv_latency_us is the chain's true
 * delay (the input filter's envelope delay, the SDADC's group delay), positive — T-37 measures it with this in place. */
static float centroid_us(float ref, float ts_s)
{
    const float n = (float)N;
    const float t_us = ts_s * 1.0e6f;
    return (t_us * (n - 1.0f) / (2.0f * n)) - (t_us * sinf((2.0f * ref) - (TI_2PI / n)) / (2.0f * n * sinf(TI_2PI / n)));
}

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

/* Round 23 (item 8): FW-10's debounce is cal_rslv_debounce consecutive updates. Once locked, the plausibility is judged
 * on the innovation before the frame is taken: the speed correction a frame makes is wn^2 err ts, so the acceleration
 * check (|dw|/ts above the driveline bound, unchanged) is |err| > thr = bound / wn^2 — the innovation the bound itself
 * leaves in steady state — and the tracking check |err| > the limit. Taken whole, a single frame of innovation k thr makes
 * the critically damped observer ring above thr for about k - 1 frames (host: 3 thr -> 2 counts, 4 thr -> 3): one corrupt
 * frame of a few degrees (40 thr) ran the count past the debounce and latched, and the FW-42 overspeed check could sample
 * the speed jump. So a frame beyond the tracking limit or beyond cal_rslv_debounce x thr is one count and is KEPT OUT —
 * the observer's state is not touched, the angle goes on extrapolating from the last frame taken (FW-28 bounds that);
 * one taken whole (thr < |err| <= debounce x thr) rings for fewer counts than the debounce. Consecutive counts still
 * latch: an offset that stays, or an acceleration beyond the bound (the innovation grows each frame the observer does not
 * take). Returns whether the frame was taken. */
static bool observer(rslv_t *r, float dt_s, float ts_s, const rslv_cal_t *c, const ti_params_t *p)
{
    const float meas = ti_wrap_2pi(atan2f(r->sin_n, r->cos_n));
    if (r->primed && (dt_s > ((float)GAP_MAX_BLOCKS * ts_s))) {
        r->primed = false;
        r->have_first = false;
        r->locked = false;
    }
    if (!r->primed) {
        prime(r, meas, dt_s, ts_s);
        return true;
    }
    const float wn = TI_2PI * p->cal_rslv_bw_hz;
    const float pred = r->theta + (r->omega * dt_s);
    r->err = ((r->sin_n * cosf(pred)) - (r->cos_n * sinf(pred))) / ti_maxf(r->amp, 1.0e-3f);
    if (r->locked) {
        const float thr = (p->cal_rslv_accel_max_rad_s2 * res_per_motor(c)) / (wn * wn);
        const bool trk_bad = ti_absf(r->err) > sinf(p->cal_rslv_track_err_rad);
        count(trk_bad, &r->n_trk, &r->trk_fault, p->cal_rslv_debounce);
        count(ti_absf(r->err) > thr, &r->n_acc, &r->acc_fault, p->cal_rslv_debounce);
        if (trk_bad || (ti_absf(r->err) > ((float)p->cal_rslv_debounce * thr))) {
            return false;
        }
    }
    r->omega += wn * wn * r->err * ts_s;
    r->theta = ti_wrap_2pi(pred + (2.0f * wn * r->err * ts_s));
    if (r->n_blocks < SETTLE_BLOCKS) {
        r->n_blocks++;
    } else {
        r->locked = true;
    }
    r->omega_prev = r->omega;
    return true;
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
    const float ref = atan2f(eq, ei) - ((p->cal_rslv_phase_comp_deg + c->phase_trim_deg) * (TI_PI / 180.0f));
    const float cr = cosf(ref);
    const float sr = sinf(ref);
    const float norm = ti_maxf(a_m * c->ratio_nom, 1.0f);
    r->sin_n = (c->sin_gain * ((si * cr) + (sq * sr)) / norm) - c->sin_offset;
    r->cos_n = (c->cos_gain * ((ci * cr) + (cq * sr)) / norm) - c->cos_offset;
    r->amp = sqrtf((r->sin_n * r->sin_n) + (r->cos_n * r->cos_n));
    const bool amp_bad = (r->amp < p->cal_rslv_amp_min) || (r->amp > p->cal_rslv_amp_max);
    count(amp_bad, &r->n_amp, &r->amp_fault, p->cal_rslv_debounce);
    /* A14-N01: the monitor plane against the trim setpoint, the winding against the resolver's floor */
    r->mon_vpp = a_m / ti_maxf(c->exc_code_per_vpp, 1.0f);
    r->exc_ratio = r->mon_vpp / p->cal_rslv_exc_target_vpp;
    r->wind_vpp = r->mon_vpp * p->cal_rslv_wind_per_mon * r->amp;
    const bool exc_bad = (r->exc_ratio < p->cal_rslv_exc_min) || (r->exc_ratio > p->cal_rslv_exc_max) ||
                         !(r->wind_vpp >= p->rslv_floor_vpp);
    count(r->exc_ready && exc_bad, &r->n_exc, &r->exc_fault, p->cal_rslv_debounce);
    const float dt_s = r->have_first ? ((float)(uint32_t)(t_us - r->t_ref_us) * 1.0e-6f) : ts_s;
    if (!amp_bad && !r->exc_fault && (ts_s > 0.0f) && (dt_s > 0.0f) && observer(r, dt_s, ts_s, c, p)) {
        r->t_ref_us = t_us;
        r->t_mid_us = centroid_us(ref, ts_s); /* round 23 (item 7): the envelopes' own reference instant */
        r->have_frame = true;
        r->t_frame_us = t_us; /* A14-R01: the age of the newest frame taken (round 23: a frame kept out does not
                               * renew the hold — the angle is extrapolated from the last one taken) */
    }
    /* round 23 (item 8): one frame outside the amplitude window no longer withdraws the angle — it is one count of the
     * debounce, kept out of the observer; cal_rslv_debounce in a row latch amp_fault */
    const bool fault = r->amp_fault || r->exc_fault || r->trk_fault || r->acc_fault || r->rate_fault;
    r->valid = r->locked && r->exc_ready && !fault && ti_finite(r->theta) && ti_finite(r->omega);
    if (r->valid) {
        r->stale = false;
    }
}

void rslv_age(rslv_t *r, uint32_t now_us, const ti_params_t *p)
{
    /* round 18 (A16-R01): signed — a frame published after now_us was read (a higher-priority interrupt, or a
     * read inside a long ISR) is fresh, never 2^32 us old */
    if (r->have_frame && ti_stale(now_us, r->t_frame_us, p->cal_rslv_hold_us)) {
        r->stale = true;
        r->valid = false;
        r->have_first = false; /* frames that return are acquired afresh: priming, then SETTLE_BLOCKS */
        r->primed = false;
        r->locked = false;
    }
}

float rslv_theta_e(const rslv_t *r, const rslv_cal_t *c)
{
    return ti_wrap_2pi((r->theta / res_per_motor(c)) - c->zero_rad);
}

/* Round 18 (A16-R03): the block's angle is the rotor's cal_rslv_latency_us BEFORE its mid-block reference (the
 * chain delays it), so the extrapolation to now_us ADDS the latency: theta(now) = theta_block + w * ((now - t_ref)
 * - t_mid + latency). It subtracted it: -12 / -24 deg el at 10 000 rpm, 4 pole pairs, 25 / 50 us. */
float rslv_theta_e_at(const rslv_t *r, const rslv_cal_t *c, uint32_t now_us, const ti_params_t *p)
{
    const float dt_s = ((float)(int32_t)(now_us - r->t_ref_us) - r->t_mid_us + p->cal_rslv_latency_us) * 1.0e-6f;
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

/* One code per call toward the band. exc_ready once the monitor is in the band, or once the trim can go
 * no further toward it (the FW-10 checks then judge what the generator gives). swg_sat: at the top code
 * and still below the band — the part's maximum cannot reach the setpoint (e.g. a lower-impedance
 * resolver loading RSX): the application records a DTC. */
uint8_t rslv_swg_trim(uint8_t code, rslv_t *r)
{
    const bool low = r->exc_ratio < TRIM_LO;
    const bool high = r->exc_ratio > TRIM_HI;
    r->swg_sat = low && (code >= HAL_SWG_CODE_MAX);
    if ((!low && !high) || r->swg_sat || (high && (code == 0u))) {
        r->exc_ready = true;
    }
    if (low && (code < HAL_SWG_CODE_MAX)) {
        return (uint8_t)(code + 1u);
    }
    if (high && (code > 0u)) {
        return (uint8_t)(code - 1u);
    }
    return code;
}
