/* plant.c — see plant.h. Single precision like the firmware; -Wdouble-promotion is on. */
#include "plant.h"

#include <math.h>
#include <string.h>

#include "ti_math.h"

#define TWO_PI_3 2.0943951f
#define I_EPS_A 0.01f         /* a leg below this carries no current (diode model: numerical zero) */
#define DT_BAND_A 2.0f        /* dead-time voltage loss: linear band around zero current */
#define R_PACK_OHM 0.03f      /* pack internal resistance */
#define TAU_PRE_S 0.1f        /* precharge time constant (vehicle R_pre x C) */
/* thermal: module NTC to coolant, tuned so the NTC reaches the 90 degC derating start after ~30 s of the rated
 * peak at 65 degC coolant (FW-04: the 30 s peak assumes 65 degC) and the continuous rating stays well below it */
#define K_COND_OHM 0.006f     /* conduction loss per phase = k I_rms^2 */
#define K_SW_V 0.9f           /* switching loss per phase = k I_rms (V/800) (f_sw/10 kHz) */
#define RTH_MOD_K_W 0.033f
#define TAU_MOD_S 20.0f
#define RTH_MOTOR_K_W 0.004f
#define TAU_MOTOR_S 300.0f
#define TAU_BOARD_S 60.0f
#define OFFSET_RATE_K_S 2.0f  /* injected heat ramps at this rate */

void plant_init(plant_t *pl, const ti_params_t *p, const calib_t *cal, uint32_t fsw_hz, float v_ocv, uint64_t now_ns)
{
    (void)memset(pl, 0, sizeof *pl);
    pl->m = cal->motor;
    pl->ramp_rpm_s = 2000.0f;
    pl->theta0_mech = 0.3f;
    pl->t0_ns = now_ns;
    for (uint32_t k = 0u; k < 3u; k++) {
        pl->duty_now[k] = 0.5f;
        pl->duty_next[k] = 0.5f;
    }
    pl->fsw_hz = (float)fsw_hz;
    pl->dt_frac = (float)p->dead_time_ns * 1.0e-9f * (float)fsw_hz;
    pl->c_f = p->c_nom_f;
    pl->v_ocv = v_ocv;
    pl->r_pack = R_PACK_OHM;
    pl->r_pre = TAU_PRE_S / p->c_nom_f;
    pl->r_bleed = p->r_bleed_ohm;
    pl->r_active = p->r_active_ohm;
    pl->t_cool_c = 50.0f;
    for (uint32_t k = 0u; k < 3u; k++) {
        pl->t_mod_c[k] = pl->t_cool_c;
    }
    pl->t_board_c[0] = pl->t_cool_c + 5.0f;
    pl->t_board_c[1] = 40.0f;
    pl->t_motor_c = pl->t_cool_c;
}

float plant_theta_mech(const plant_t *pl, uint64_t t_ns)
{
    const float w = pl->speed_rpm / TI_RPM_PER_RAD_S;
    const float dt = (float)(int64_t)(t_ns - pl->t0_ns) * 1.0e-9f;
    return ti_wrap_2pi(pl->theta0_mech + (w * dt));
}

/* motor electrical angle: resolver pole pairs 1, electrical zero 0 (the nominal calibration record) */
float plant_theta_e(const plant_t *pl, uint64_t t_ns)
{
    return ti_wrap_2pi(plant_theta_mech(pl, t_ns) * (float)pl->m.pp);
}

static float omega_e(const plant_t *pl) { return (pl->speed_rpm / TI_RPM_PER_RAD_S) * (float)pl->m.pp; }

static void dq_to_abc(float d, float q, float th, float i[3])
{
    const float c = cosf(th);
    const float s = sinf(th);
    const float a = (d * c) - (q * s);
    const float b = (d * s) + (q * c);
    i[0] = a;
    i[1] = (-0.5f * a) + ((0.5f * TI_SQRT3) * b);
    i[2] = (-0.5f * a) - ((0.5f * TI_SQRT3) * b);
}

static void abc_to_dq(const float x[3], float th, float *d, float *q)
{
    const float a = ((2.0f * x[0]) - x[1] - x[2]) * (1.0f / 3.0f);
    const float b = (x[1] - x[2]) * (1.0f / TI_SQRT3);
    const float c = cosf(th);
    const float s = sinf(th);
    *d = (a * c) + (b * s);
    *q = (-a * s) + (b * c);
}

void plant_phase_currents(const plant_t *pl, uint64_t t_ns, float i[3])
{
    dq_to_abc(pl->id, pl->iq, plant_theta_e(pl, t_ns), i);
}

float plant_torque(const plant_t *pl)
{
    return 1.5f * (float)pl->m.pp * ((pl->m.psi_wb * pl->iq) + ((pl->m.ld_h - pl->m.lq_h) * pl->id * pl->iq));
}

/* the link node over h, implicit: C dV/dt = g_src (V_ocv - V) - i_dc - V (1/R_bleed + qdis/R_active) */
static void link_step(plant_t *pl, float i_dc, float h)
{
    const float g_src = (pl->cont == PL_CONT_CLOSED) ? (1.0f / pl->r_pack)
                      : ((pl->cont == PL_CONT_PRECHARGE) ? (1.0f / pl->r_pre) : 0.0f);
    const float g = g_src + (1.0f / pl->r_bleed) + (pl->qdis ? (1.0f / pl->r_active) : 0.0f);
    const float ch = pl->c_f / h;
    pl->v_link = ((ch * pl->v_link) + (g_src * pl->v_ocv) - i_dc) / (ch + g);
    if (pl->v_link < 0.0f) {
        pl->v_link = 0.0f;
    }
}

/* phase voltages (volts above DC-) as the switches apply them, dead-time loss included */
static void mod_voltages(const plant_t *pl, const float i[3], float v[3])
{
    for (uint32_t k = 0u; k < 3u; k++) {
        const float loss = pl->dt_frac * ti_clampf(i[k] / DT_BAND_A, -1.0f, 1.0f);
        v[k] = (pl->duty_now[k] - loss) * pl->v_link;
    }
}

/* One implicit dq step with applied (vd, vq): stable for any speed and step. */
static void dq_step(plant_t *pl, float vd, float vq, float h)
{
    const float w = omega_e(pl);
    const float a11 = (pl->m.ld_h / h) + pl->m.rs_ohm;
    const float a12 = -w * pl->m.lq_h;
    const float a21 = w * pl->m.ld_h;
    const float a22 = (pl->m.lq_h / h) + pl->m.rs_ohm;
    const float b1 = ((pl->m.ld_h / h) * pl->id) + vd;
    const float b2 = ((pl->m.lq_h / h) * pl->iq) + vq - (w * pl->m.psi_wb);
    const float det = (a11 * a22) - (a12 * a21);
    pl->id = ((b1 * a22) - (a12 * b2)) / det;
    pl->iq = ((a11 * b2) - (a21 * b1)) / det;
}

/* All six switches off: each leg conducts through its diodes by the sign of its current (lower diode: phase at
 * DC-, i > 0; upper diode: phase at V_link, i < 0); a leg at zero current floats until its terminal voltage
 * leaves [0, V_link]. Explicit step h (1 us); returns the current drawn from the link. */
static float diode_step(plant_t *pl, uint64_t t_ns, float h)
{
    const float th = plant_theta_e(pl, t_ns);
    const float w = omega_e(pl);
    const float l = 0.5f * (pl->m.ld_h + pl->m.lq_h);
    float i[3];
    dq_to_abc(pl->id, pl->iq, th, i);
    float e[3];
    bool on[3];
    uint32_t n_on = 0u;
    for (uint32_t k = 0u; k < 3u; k++) {
        e[k] = -w * pl->m.psi_wb * sinf(th - (TWO_PI_3 * (float)k));
        on[k] = ti_absf(i[k]) > I_EPS_A;
        n_on += on[k] ? 1u : 0u;
    }
    const float vl = pl->v_link;
    float v[3] = {0.0f, 0.0f, 0.0f};
    if (n_on < 2u) {
        /* no conduction path: start one only if the line-line back-EMF exceeds the link */
        uint32_t kmax = 0u;
        uint32_t kmin = 0u;
        for (uint32_t k = 1u; k < 3u; k++) {
            kmax = (e[k] > e[kmax]) ? k : kmax;
            kmin = (e[k] < e[kmin]) ? k : kmin;
        }
        if ((e[kmax] - e[kmin]) <= vl) {
            pl->id = 0.0f;
            pl->iq = 0.0f;
            return 0.0f;
        }
        i[0] = 0.0f;
        i[1] = 0.0f;
        i[2] = 0.0f;
        const float di = ((e[kmax] - e[kmin] - vl) / (2.0f * l)) * h; /* grows from zero */
        i[kmin] = di;
        i[kmax] = -di;
    } else {
        for (uint32_t k = 0u; k < 3u; k++) {
            v[k] = (i[k] > 0.0f) ? 0.0f : vl;
        }
        float vn;
        if (n_on == 3u) {
            vn = (v[0] + v[1] + v[2]) * (1.0f / 3.0f);
        } else {
            uint32_t m = 0u;
            for (uint32_t k = 0u; k < 3u; k++) {
                m = on[k] ? m : k;
            }
            const uint32_t j = (m + 1u) % 3u;
            const uint32_t k2 = (m + 2u) % 3u;
            vn = 0.5f * (v[j] + v[k2] - e[j] - e[k2]);
            const float vm = vn + e[m]; /* the floating leg's terminal */
            if ((vm > vl) || (vm < 0.0f)) {
                v[m] = (vm > vl) ? vl : 0.0f; /* its diode now conducts too */
                vn = (v[0] + v[1] + v[2]) * (1.0f / 3.0f);
                on[m] = true;
            } else {
                v[m] = vm;
            }
        }
        float ni[3];
        for (uint32_t k = 0u; k < 3u; k++) {
            ni[k] = on[k] ? (i[k] + (((v[k] - vn - (pl->m.rs_ohm * i[k]) - e[k]) / l) * h)) : 0.0f;
            if (on[k] && (ti_absf(i[k]) > I_EPS_A) && ((ni[k] * i[k]) < 0.0f)) {
                ni[k] = 0.0f; /* a diode never conducts backwards: the current stops at zero */
            }
        }
        const float mean = (ni[0] + ni[1] + ni[2]) * (1.0f / 3.0f);
        for (uint32_t k = 0u; k < 3u; k++) {
            i[k] = ni[k] - mean;
        }
    }
    float i_dc = 0.0f;
    for (uint32_t k = 0u; k < 3u; k++) {
        i_dc += (i[k] < 0.0f) ? i[k] : 0.0f; /* upper diodes feed the link */
    }
    abc_to_dq(i, plant_theta_e(pl, t_ns + (uint64_t)(h * 1.0e9f)), &pl->id, &pl->iq);
    return i_dc;
}

bool plant_off_active(const plant_t *pl, uint64_t t_ns)
{
    (void)t_ns;
    const float e_ll = TI_SQRT3 * ti_absf(omega_e(pl)) * pl->m.psi_wb;
    return (ti_absf(pl->id) > I_EPS_A) || (ti_absf(pl->iq) > I_EPS_A) || (e_ll > pl->v_link);
}

void plant_elec(plant_t *pl, pl_bridge_t br, uint64_t t0_ns, uint64_t t1_ns)
{
    if (t1_ns <= t0_ns) {
        return;
    }
    if ((br == PL_BR_MOD) && !pl->duty_now_ok) {
        br = PL_BR_OFF; /* modulation starts at the first reload of a written duty: until then the outputs stay off */
    }
    pl->br = br;
    const float h = (float)(t1_ns - t0_ns) * 1.0e-9f;
    const float th_mid = plant_theta_e(pl, t0_ns + ((t1_ns - t0_ns) / 2u));
    float i_dc = 0.0f;
    if (br == PL_BR_MOD) {
        float i[3];
        dq_to_abc(pl->id, pl->iq, th_mid, i);
        float v[3];
        mod_voltages(pl, i, v);
        float vd;
        float vq;
        abc_to_dq(v, th_mid, &vd, &vq); /* the common mode (neutral) drops out */
        dq_step(pl, vd, vq, h);
        for (uint32_t k = 0u; k < 3u; k++) {
            i_dc += (v[k] / ti_maxf(pl->v_link, 1.0f)) * i[k];
        }
    } else if (br == PL_BR_ASC) {
        dq_step(pl, 0.0f, 0.0f, h);
    } else {
        i_dc = diode_step(pl, t0_ns, h);
    }
    /* while modulating, the inverter's losses are drawn from the link (the thermal model uses the same law); in ASC
     * and through the diodes the winding current is driven by the back-EMF, so the motor supplies them */
    float p_inv = 0.0f;
    if (br == PL_BR_MOD) {
        const float i_rms = sqrtf(0.5f * ((pl->id * pl->id) + (pl->iq * pl->iq)));
        p_inv = 3.0f * i_rms * ((K_COND_OHM * i_rms) + (K_SW_V * (pl->v_link / 800.0f) * (pl->fsw_hz / 10000.0f)));
    }
    const float i_total = i_dc + ((pl->v_link > 10.0f) ? (p_inv / pl->v_link) : 0.0f);
    link_step(pl, i_total, h);
    pl->acc_idc += (double)(i_total * h);
    pl->acc_pe += (double)(i_total * pl->v_link * h);
    pl->acc_t += (double)h;
}

void plant_latch_duty(plant_t *pl)
{
    for (uint32_t k = 0u; k < 3u; k++) {
        pl->duty_now[k] = pl->duty_next[k];
    }
    pl->duty_now_ok = pl->duty_next_ok;
}

void plant_1ms(plant_t *pl, uint64_t now_ns)
{
    const float dt = 1.0e-3f;
    /* dyno: re-anchor the angle, then move the speed toward the setpoint */
    pl->theta0_mech = plant_theta_mech(pl, now_ns);
    pl->t0_ns = now_ns;
    const float step = pl->ramp_rpm_s * dt;
    pl->speed_rpm = ti_ramp(pl->speed_rpm, pl->target_rpm, step);
    /* averages over the last ms */
    const double t = (pl->acc_t > 0.0) ? pl->acc_t : 1.0e-3;
    pl->i_dc_a = (float)(pl->acc_idc / t);
    pl->p_dc_w = (float)(pl->acc_pe / t);
    pl->acc_idc = 0.0;
    pl->acc_pe = 0.0;
    pl->acc_t = 0.0;
    pl->torque_nm = plant_torque(pl);
    pl->p_mech_w = pl->torque_nm * (pl->speed_rpm / TI_RPM_PER_RAD_S);
    /* thermal */
    const float i_rms = sqrtf(0.5f * ((pl->id * pl->id) + (pl->iq * pl->iq)));
    const float p_sw = (pl->br == PL_BR_MOD) ? (K_SW_V * i_rms * (pl->v_link / 800.0f) * (pl->fsw_hz / 10000.0f)) : 0.0f;
    const float p_ph = (K_COND_OHM * i_rms * i_rms) + p_sw;
    const float p_cu = 1.5f * pl->m.rs_ohm * ((pl->id * pl->id) + (pl->iq * pl->iq));
    pl->p_loss_w = (3.0f * p_ph) + p_cu;
    pl->mod_offset_c = ti_ramp(pl->mod_offset_c, pl->mod_offset_target_c, OFFSET_RATE_K_S * dt);
    /* the three modules sit on the coldplate a little differently (thermal path and NTC placement) */
    static const float RTH_SCALE[3] = {1.0f, 1.04f, 0.97f};
    static const float NTC_OFFSET_C[3] = {0.0f, 0.35f, -0.25f};
    for (uint32_t k = 0u; k < 3u; k++) {
        const float target = pl->t_cool_c + NTC_OFFSET_C[k] + (RTH_MOD_K_W * RTH_SCALE[k] * p_ph) + pl->mod_offset_c;
        pl->t_mod_c[k] += (target - pl->t_mod_c[k]) * (dt / TAU_MOD_S);
    }
    const float tb0 = pl->t_cool_c + 5.0f + (0.003f * 3.0f * p_ph);
    const float tb1 = 40.0f + (0.001f * 3.0f * p_ph);
    pl->t_board_c[0] += (tb0 - pl->t_board_c[0]) * (dt / TAU_BOARD_S);
    pl->t_board_c[1] += (tb1 - pl->t_board_c[1]) * (dt / TAU_BOARD_S);
    pl->t_motor_c += ((pl->t_cool_c + (RTH_MOTOR_K_W * p_cu)) - pl->t_motor_c) * (dt / TAU_MOTOR_S);
}
