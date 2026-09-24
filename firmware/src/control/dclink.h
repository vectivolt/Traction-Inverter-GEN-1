/* dclink.h — DC-link voltage control for FW-08 (regeneration with the battery path lost, n < n_x):
 * the torque command goes to zero at the current-loop rate and this loop trims it so the motor
 * neither charges nor drains the isolated link: T = sign(w) * PI(V_dc - V_ref), bounded by
 * cal_dcl_tmax_nm. FW-06 (hardware compare -> LS-ASC) remains the fast backstop. */
#ifndef DCLINK_H
#define DCLINK_H

#include "ti_params.h"

typedef struct {
    float integ;
} dcl_t;

void dcl_reset(dcl_t *d);
float dcl_step(dcl_t *d, float v_ref, float v_dc, float omega_mech_rad_s, float dt_s, const ti_params_t *p);

#endif /* DCLINK_H */
