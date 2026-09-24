/* foc.h — field-oriented current control: amplitude-invariant Clarke/Park (dq = phase peak A),
 * PI per axis with conditional-integration anti-windup and speed-voltage decoupling, voltage circle
 * limit (d priority) at cal_mod_index_max * V_dc / sqrt(3), inverse Park advanced by the
 * sample-to-actuation delay, min-max SVPWM, dead-time compensation, and a finite/range guard on
 * every input and output: a non-finite value never reaches the PWM registers. */
#ifndef FOC_H
#define FOC_H

#include "gains.h"
#include "motor.h"

typedef struct {
    float id_ref;
    float iq_ref;
    float id;
    float iq;
    float xi_d;
    float xi_q;
    float vd;
    float vq;
    float vmax;
    bool sat;
    float duty[3];
    bool guard_trip; /* latched: a non-finite value was caught */
} foc_t;

void foc_reset(foc_t *f);
void foc_clarke(const float iabc[3], float *ia, float *ib);
void foc_park(float a, float b, float th, float *d, float *q);
void foc_ipark(float d, float q, float th, float *a, float *b);
void foc_svpwm(float va, float vb, float vdc, float duty[3]);
void foc_dt_comp(float duty[3], const float iabc[3], float dt_frac, float band_a);
/* One current-loop step. Returns false (duty untouched, guard_trip set) on any non-finite
 * input/intermediate/output or vdc <= 0; the caller then forces the PWM off. */
bool foc_step(foc_t *f, const float iabc[3], float theta_e, float omega_e, float vdc, const motor_t *m,
              const gain_set_t *g, const ti_params_t *p);
/* Back-EMF speed from the voltage model: w = (vq - R iq) / (Ld id + psi). */
float foc_omega_model(const foc_t *f, const motor_t *m, bool *valid);

#endif /* FOC_H */
