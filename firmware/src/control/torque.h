/* torque.h — torque path: limits (FW-03 power envelope, FW-04 thermal derating and the 30 s peak
 * budget, BMS regen/discharge power relayed by the VCU, FW-11 zero regen on BMS timeout),
 * torque -> current (round 23, FW-37: the MTPA point — a validated LUT or the solved minimum — and field
 * weakening along the torque hyperbola, solved together with the voltage ellipse, the current circle and the
 * demagnetisation limit; the torque reduced only when no point of the hyperbola fits), the F23 refusal and
 * a postcondition on every returned vector; round 23 (FW-45) the saturation maps of motor_t in all of it; (FW-46) the
 * torque-ripple feed-forward table and its scale. */
#ifndef TORQUE_H
#define TORQUE_H

#include "motor.h"

#define MTPA_LUT_MAX 16u
#define TQ_RIPPLE_N 36u /* round 23 (FW-46): the ripple table's points over one electrical period (10 deg el) */

typedef struct {
    uint8_t n; /* 0 or 1 => closed form */
    float t_nm[MTPA_LUT_MAX];
    float id_a[MTPA_LUT_MAX];
    float iq_a[MTPA_LUT_MAX];
} mtpa_lut_t;

typedef struct {
    float derate;        /* 0..1 from the module NTCs (FW-04) */
    bool derate_active;
    float coolant_factor; /* 0..1: peak allowance above the 65 degC coolant assumption */
    float peak_used_s;
    bool peak_exhausted;  /* re-allowed only after full recovery */
    float i_limit_rms_a;  /* the current allowance now */
    float t_lim_motor_nm; /* magnitudes */
    float t_lim_regen_nm;
} torque_lim_t;

/* FW-03: P_max(V) = min(P_rated, sqrt(3/2) * 0.95 * V * I * 0.85). */
float torque_p_max_w(float vdc, float i_rms_a, float p_rated_w, const ti_params_t *p);

void torque_lim_init(torque_lim_t *l, const ti_params_t *p);
/* 1 kHz: thermal derate (with hysteresis) and the peak budget. */
void torque_derate_update(torque_lim_t *l, float t_mod_c, bool t_mod_valid, float t_cool_c, bool t_cool_valid,
                          float i_rms_now_a, float dt_s, const ti_params_t *p);
/* 1 kHz: torque magnitudes allowed now. bms_fresh false => zero regen (FW-11). */
void torque_limits(torque_lim_t *l, float vdc, float omega_mech_rad_s, float p_chg_w, float p_dis_w, bool bms_fresh,
                   const ti_params_t *p);
/* Clamp a request to the limits (sign convention: T > 0 accelerates in +speed). */
float torque_clamp(const torque_lim_t *l, float t_req_nm, float omega_mech_rad_s);
typedef enum {
    TQ_NONFINITE = 0, /* a non-finite input or result: outputs zeroed */
    TQ_OK,            /* the requested torque (within 0.1 %): the MTPA point, or in field weakening the
                         least-current point of its torque hyperbola that fits */
    TQ_LIMITED,       /* no point of the hyperbola fits: the largest torque of the same sign that does
                         (|T| < |T_req|, zero at worst) */
    TQ_INFEASIBLE,    /* not even iq = 0 is feasible inside the demagnetisation and current limits:
                         outputs = iq 0 at the least-voltage id; the caller commands zero torque,
                         requests a speed limit and sets a DTC */
    TQ_POSTCOND       /* round 23: the vector failed its own postcondition — a software fault: outputs
                         zeroed; the caller issues no current reference */
} tq_res_t;

/* Torque -> (id, iq). Postcondition, checked on the returned vector before every return (FW-37): its torque
 * torque_from_current(id, iq) has the sign of t_nm (or is zero) and |T| <= |t_nm| (1 + 1e-3) + 1e-3 N m —
 * never more torque than requested — and within 0.1 % of it for TQ_OK; sqrt(id^2 + iq^2) <= i_max_a,
 * id >= -id_demag_a and, except for TQ_INFEASIBLE (iq = 0 there), torque_v_required(id, iq) <=
 * torque_v_available(vdc) — each within 1e-6. A failure is TQ_POSTCOND. */
tq_res_t torque_to_current(float t_nm, float omega_e, float vdc, float i_max_a, const motor_t *m, const mtpa_lut_t *lut,
                           const ti_params_t *p, float *id, float *iq);
/* Round 23: the torque a dq pair represents, T = 1.5 pp (psi + (Ld - Lq) id) iq (amplitude-invariant dq). */
float torque_from_current(float id, float iq, const motor_t *m);
/* Steady-state phase-voltage magnitude (peak) a dq pair needs at omega_e: Rs, Ld, Lq and psi. */
float torque_v_required(float id, float iq, float omega_e, const motor_t *m);
/* What a steady-state reference may use: the FOC limit cal_mod_index_max * V_dc / sqrt(3) less the
 * dynamic reserve cal_vdyn_reserve_frac kept for the current loop. */
float torque_v_available(float vdc, const ti_params_t *p);
/* MTPA d-current for a q-current (closed form). */
float torque_mtpa_id(float iq, const motor_t *m);

/* Round 23 (FW-46): the torque-ripple (cogging) feed-forward. The table's i_q at theta_e less mean_cnt (A; 0.01 A per
 * count, 10 deg el steps, linear, periodic; mean_cnt in counts); 0 for a non-finite angle. */
float torque_ripple_at(const int16_t tab[TQ_RIPPLE_N], float mean_cnt, float theta_e);
/* A k in [0, 1] with (id, iq + k ff) inside the current circle i_max_a and the voltage ellipse torque_v_available(vdc) at
 * omega_e for EVERY ff between ff_lo and ff_hi (the table's extremes; id unchanged, so the demagnetisation limit holds) —
 * the feed-forward is scaled down, never the solved vector; 0 when (id, iq) itself does not fit, or an extreme is NaN.
 * Round 24: the two extremes alone do not cover the values between them on a saturation map (|v| can peak at a breakpoint
 * of the L_q map or inside a steep segment): the interval is cut at the breakpoints and each piece bounded (torque.c,
 * ff_span_fits) — the largest k that bound allows, to the bisection's 1/4096. */
float torque_ripple_scale(float id, float iq, float ff_lo, float ff_hi, float omega_e, float vdc, float i_max_a,
                          const motor_t *m, const ti_params_t *p);

#endif /* TORQUE_H */
