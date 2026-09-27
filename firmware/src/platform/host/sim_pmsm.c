/* sim_pmsm.c — the virtual PMSM of sim_pmsm.h (FW-39, round 23). Host tests only. */
#include "sim_pmsm.h"

#include <math.h>

#include "sim.h"
#include "ti_math.h"

#define SUB_S 5.0e-6f   /* integration sub-step bound */
#define SUB_MAX 400u    /* a gap of up to 2 ms between triggers (a long task) */

static struct {
    sim_pmsm_cfg_t c;
    float id, iq;          /* true rotor frame */
    float th_m, w_m;       /* mechanical rad, rad/s */
    sim_rotor_t mode;
    float th_a;            /* DYNO: the angle at the anchor, which the resolver model shares */
    uint64_t t_a, t_ns;
    bool mod_latched;      /* the bridge modulated at the previous trigger: its duty acts now */
    float duty[3];
    uint32_t noise;
} S;

static void resolver_follow(void)
{
    const float s = S.c.rslv_reversed ? -1.0f : 1.0f;
    const sim_resolver_t r = {.theta0_rad = ti_wrap_2pi(s * (float)S.c.rpp * S.th_m),
                              .omega_rad_s = s * (float)S.c.rpp * S.w_m, .lag_deg = 24.0f, .sin_gain = 1.0f,
                              .cos_gain = 1.0f, .out_gain = 1.0f};
    sim_resolver_set(&r);
}

void sim_pmsm_init(const sim_pmsm_cfg_t *c, float theta_m_rad)
{
    S.c = *c;
    S.id = 0.0f;
    S.iq = 0.0f;
    S.th_m = ti_wrap_2pi(theta_m_rad);
    S.w_m = 0.0f;
    S.mode = SIM_ROTOR_LOCKED;
    S.t_ns = sim_now_ns();
    S.t_a = S.t_ns;
    S.th_a = S.th_m;
    S.mod_latched = false;
    S.noise = 424242u;
    resolver_follow();
}

void sim_pmsm_rotor(sim_rotor_t mode, float rpm)
{
    if (S.mode == SIM_ROTOR_DYNO) {
        /* round 23: the dyno's angle NOW — the model had advanced it only to the last current-loop trigger, and anchoring
         * there stepped the rotor (and the resolver it drives) back by w (now - trigger) at every call: up to 2.4 mrad at
         * 450 rpm, 50 us after a trigger, once per dyno_ramp millisecond — a persistent angle step FW-10 counts (§10k) */
        S.th_m = S.th_a + (S.w_m * ((float)(int64_t)(sim_now_ns() - S.t_a) * 1e-9f));
    }
    S.mode = mode;
    S.w_m = (mode == SIM_ROTOR_LOCKED) ? 0.0f : (rpm / TI_RPM_PER_RAD_S);
    S.th_a = S.th_m;
    S.t_a = sim_now_ns();
    resolver_follow();
}

static float theta_e(void) { return ti_wrap_2pi(((float)S.c.pp * S.th_m) - S.c.zero_rad); }
static float theta_e_now(void) { return theta_e(); }

float sim_pmsm_rpm(void) { return S.w_m * TI_RPM_PER_RAD_S; }
float sim_pmsm_theta_e(void) { return theta_e(); }

void sim_pmsm_idq(float *id_a, float *iq_a)
{
    *id_a = S.id;
    *iq_a = S.iq;
}

/* Round 23 (FW-45): the saturated axis — the flux, the differential inductance, the apparent one (Is = 0: L0). */
static float phi(float l0, float is, float i) { return (is > 0.0f) ? (l0 * is * atanf(i / is)) : (l0 * i); }
static float l_diff(float l0, float is, float i) { return (is > 0.0f) ? (l0 / (1.0f + ((i / is) * (i / is)))) : l0; }
static bool saturating(void) { return (S.c.isat_d_a > 0.0f) || (S.c.isat_q_a > 0.0f); }

float sim_pmsm_l_app_h(bool q_axis, float i_a)
{
    const float l0 = q_axis ? S.c.lq_h : S.c.ld_h;
    const float is = q_axis ? S.c.isat_q_a : S.c.isat_d_a;
    const float i = fabsf(i_a);
    return (i > 1e-3f) ? (phi(l0, is, i) / i) : l0;
}

float sim_pmsm_cogging_nm(float theta_e)
{
    return (S.c.cog6_nm * sinf(6.0f * theta_e)) + (S.c.cog12_nm * sinf((12.0f * theta_e) + S.c.cog12_ph_rad));
}

float sim_pmsm_torque_nm(void)
{
    float t;
    if (saturating()) {
        const float lam_d = S.c.psi_wb + phi(S.c.ld_h, S.c.isat_d_a, S.id);
        const float lam_q = phi(S.c.lq_h, S.c.isat_q_a, S.iq);
        t = 1.5f * (float)S.c.pp * ((lam_d * S.iq) - (lam_q * S.id));
    } else {
        t = 1.5f * (float)S.c.pp * ((S.c.psi_wb + ((S.c.ld_h - S.c.lq_h) * S.id)) * S.iq);
    }
    if ((S.c.cog6_nm != 0.0f) || (S.c.cog12_nm != 0.0f)) {
        t += sim_pmsm_cogging_nm(theta_e_now());
    }
    return t;
}

static void abc(float th, float i[3])
{
    const float a = (S.id * cosf(th)) - (S.iq * sinf(th));
    const float b = (S.id * sinf(th)) + (S.iq * cosf(th));
    i[0] = a;
    i[1] = (-0.5f * a) + ((0.5f * TI_SQRT3) * b);
    i[2] = (-0.5f * a) - ((0.5f * TI_SQRT3) * b);
}

static float dither(void)
{
    S.noise = (S.noise * 1664525u) + 1013904223u;
    return (((float)(S.noise >> 8) / 16777216.0f) - 0.5f) * 2.0f * S.c.noise_a;
}

static void rotor_advance(float h, uint64_t t_end)
{
    if (S.mode == SIM_ROTOR_DYNO) {
        S.th_m = S.th_a + (S.w_m * ((float)(int64_t)(t_end - S.t_a) * 1e-9f)); /* the resolver model's own law */
    } else if (S.mode == SIM_ROTOR_FREE) {
        S.w_m += (sim_pmsm_torque_nm() / S.c.j_kgm2) * h;
        S.th_m += S.w_m * h;
    } else {
        /* locked */
    }
}

/* At a current-loop trigger, before the ISR: the interval since the previous trigger, then the samples. */
void sim_pmsm_step(float vdc)
{
    const uint64_t now = sim_now_ns();
    const uint64_t t0 = S.t_ns;
    const float span = (now > t0) ? ((float)(now - t0) * 1e-9f) : 0.0f;
    S.t_ns = now;
    const bool hs = sim_chain_hs_on();
    const bool ls = sim_chain_ls_on();
    const bool mod = hs && ls && S.mod_latched; /* the duty written last trigger; an off or ASC acts at once */
    const bool asc = ls && !hs;
    uint32_t n = (uint32_t)(span / SUB_S) + 1u;
    n = (n > SUB_MAX) ? SUB_MAX : n;
    const float h = span / (float)n;
    const float vdt = vdc * S.c.t_dead_s * S.c.fsw_hz;
    for (uint32_t k = 0u; k < n; k++) {
        rotor_advance(h, ((k + 1u) == n) ? now : (t0 + (uint64_t)((float)(k + 1u) * h * 1e9f)));
        if (!mod && !asc) {
            S.id = 0.0f; /* nothing conducts */
            S.iq = 0.0f;
            continue;
        }
        const float th = theta_e();
        float vd = 0.0f;
        float vq = 0.0f;
        if (mod) {
            float i[3];
            float v[3];
            abc(th, i);
            for (uint32_t j = 0u; j < 3u; j++) {
                v[j] = (S.duty[j] * vdc) - (vdt * tanhf(i[j] / S.c.i_knee_a));
            }
            const float va = ((2.0f * v[0]) - v[1] - v[2]) * (1.0f / 3.0f);
            const float vb = (v[1] - v[2]) * (1.0f / TI_SQRT3);
            vd = (va * cosf(th)) + (vb * sinf(th));
            vq = (-va * sinf(th)) + (vb * cosf(th));
        }
        const float we = (float)S.c.pp * S.w_m;
        float did;
        float diq;
        if (saturating()) { /* dlambda/dt = v - R i -/+ w lambda, di = dlambda / L_diff */
            const float lam_d = S.c.psi_wb + phi(S.c.ld_h, S.c.isat_d_a, S.id);
            const float lam_q = phi(S.c.lq_h, S.c.isat_q_a, S.iq);
            did = (vd - (S.c.rs_ohm * S.id) + (we * lam_q)) / l_diff(S.c.ld_h, S.c.isat_d_a, S.id);
            diq = (vq - (S.c.rs_ohm * S.iq) - (we * lam_d)) / l_diff(S.c.lq_h, S.c.isat_q_a, S.iq);
        } else {
            did = (vd - (S.c.rs_ohm * S.id) + (we * S.c.lq_h * S.iq)) / S.c.ld_h;
            diq = (vq - (S.c.rs_ohm * S.iq) - (we * ((S.c.ld_h * S.id) + S.c.psi_wb))) / S.c.lq_h;
        }
        S.id += h * did;
        S.iq += h * diq;
    }
    S.th_m = (S.mode == SIM_ROTOR_DYNO) ? S.th_m : ti_wrap_2pi(S.th_m);
    if (S.mode == SIM_ROTOR_FREE) {
        resolver_follow(); /* piecewise: the angle and the speed now */
    }
    S.mod_latched = hs && ls;
    for (uint32_t j = 0u; j < 3u; j++) {
        S.duty[j] = sim_pwm_duty(j);
    }
    float i[3];
    abc(theta_e(), i);
    sim_set_phase_currents(i[0] + dither(), i[1] + dither(), i[2] + dither());
}
