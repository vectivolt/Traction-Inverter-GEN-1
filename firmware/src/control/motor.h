/* motor.h — motor data (a commissioning input, R-F16; carried by the FW-20 calibration record)
 * and the speeds the safety rules derive from it:
 *   n_x  : line-line back-EMF peak sqrt(3)*w_e*psi_f equals the allowed link voltage (the SKU's
 *          OV trip, cold magnets) — §6 column split;
 *   n_ss : E_LL,pk(n_ss) <= 12 V — FW-16 standstill. */
#ifndef MOTOR_H
#define MOTOR_H

#include "ti_math.h"
#include "ti_params.h"

typedef struct {
    float ld_h;
    float lq_h;
    float rs_ohm;
    float psi_wb;     /* cold-magnet flux linkage (peak, per phase) */
    uint8_t pp;       /* pole pairs */
    float id_demag_a; /* demagnetisation limit: id >= -id_demag_a */
    float n_max_rpm;
    bool rule_b_released; /* §6 rule (b) shown on HIL/dyno for this vehicle */
} motor_t;

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
