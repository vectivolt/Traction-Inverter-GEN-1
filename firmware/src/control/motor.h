/* motor.h — motor data (a commissioning input, R-F16; carried by the FW-20 calibration record)
 * and the speeds the safety rules derive from it:
 *   n_x  : line-line back-EMF peak sqrt(3)*w_e*psi_f equals the allowed link voltage (the SKU's
 *          OV trip, cold magnets) — §6 column split;
 *   n_ss : E_LL,pk(n_ss) <= 12 V — FW-16 standstill. */
#ifndef MOTOR_H
#define MOTOR_H

#include "ti_math.h"
#include "ti_params.h"

#define MOTOR_MAP_N 6u /* round 23 (FW-45): saturation map breakpoints at k / (N - 1) of i_map_a, k = 0 .. N - 1 */

typedef struct {
    float ld_h; /* the unsaturated (small-signal) inductances: the level of the saturation maps below */
    float lq_h;
    float rs_ohm;
    float psi_wb;     /* cold-magnet flux linkage (peak, per phase) */
    uint8_t pp;       /* pole pairs */
    float id_demag_a; /* demagnetisation limit: id >= -id_demag_a */
    float n_max_rpm;
    bool rule_b_released; /* §6 rule (b) shown on HIL/dyno for this vehicle */
    /* round 23 (FW-45): the apparent (secant) inductance L_d(|i_d|), L_q(|i_q|) at the breakpoints — the flux linkages are
     * psi + L_d(|i_d|) i_d and L_q(|i_q|) i_q. A map is used as its shape (each point over point 0) at the level of the
     * scalar above: point 0 IS the scalar in every record calib_nominal and the FW-39 commit write, and a flat map is the
     * scalar exactly. A map whose point 0 is 0 is none (a motor_t built outside a record): the scalar. */
    float ld_map_h[MOTOR_MAP_N];
    float lq_map_h[MOTOR_MAP_N];
    float i_map_a; /* the breakpoints' full scale: the SKU's current limit i_crest_a (calib_check binds it) */
} motor_t;

/* Round 23 (FW-45): a map's shape at |i| — its apparent inductance over its point 0, linear between the breakpoints and
 * the last point beyond — and in *diff the differential inductance d(L i)/di there over the same point 0 (the segment's
 * slope: L + i dL/di). Exactly 1 for a flat map or none: the scalar model, bit for bit. Callers pass |i|. */
static inline float motor_sat(const float map[MOTOR_MAP_N], float i_abs, float i_map_a, float *diff)
{
    if (map[0] == 0.0f) {
        *diff = 1.0f;
        return 1.0f;
    }
    const float x = i_abs * ((float)(MOTOR_MAP_N - 1u) / i_map_a); /* in breakpoint steps; NaN: the last point */
    if (!(x < (float)(MOTOR_MAP_N - 1u))) {
        *diff = map[MOTOR_MAP_N - 1u] / map[0];
        return *diff;
    }
    const uint32_t k = (uint32_t)x;
    const float s = map[k + 1u] - map[k]; /* per step */
    const float l = map[k] + ((x - (float)k) * s);
    *diff = (l + (x * s)) / map[0];
    return l / map[0];
}

static inline float motor_omega_e(float rpm, const motor_t *m)
{
    return (rpm / TI_RPM_PER_RAD_S) * (float)m->pp;
}

static inline float motor_rpm_at_ell(float v_ll_pk, const motor_t *m)
{
    const float w_e = v_ll_pk / (TI_SQRT3 * m->psi_wb);
    return (w_e / (float)m->pp) * TI_RPM_PER_RAD_S;
}

static inline float motor_n_x_rpm(const motor_t *m, const ti_params_t *p)
{
    return motor_rpm_at_ell(p->ov_trip_v, m);
}

static inline float motor_n_ss_rpm(const motor_t *m, const ti_params_t *p)
{
    return motor_rpm_at_ell(p->fw16_ss_ell_v, m);
}

/* The S6 screening motor (0.35 mH, 25 mOhm) with test-only psi/pp: NOT a real motor. */
static inline motor_t motor_screening(void)
{
    motor_t m = {.ld_h = 0.35e-3f, .lq_h = 0.35e-3f, .rs_ohm = 0.025f, .psi_wb = 0.15f, .pp = 4u,
                 .id_demag_a = 450.0f, .n_max_rpm = 16000.0f, .rule_b_released = false};
    return m;
}

#endif /* MOTOR_H */
