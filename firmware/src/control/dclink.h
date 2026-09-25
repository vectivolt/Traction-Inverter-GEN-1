/* dclink.h — the FW-08 DC-link trim (round 17, README item 26). It runs only in normal RUN with the battery
 * path proven (app.c torque_path): while V_DC is above the SKU's normal-range maximum (vdc_max_v, the
 * reference by construction, never a higher value) it takes back regenerative torque,
 *   cut = PI(V_dc - vdc_max_v) in [0, cal_dcl_tmax_nm],
 * and never adds motoring torque or flips the sign of the command. With the battery path lost the §6
 * row applies zero current instead (id = iq = 0) and this trim is reset, never engaged. FW-06 (hardware
 * compare -> LS-ASC) stays the fast backstop. */
#ifndef DCLINK_H
#define DCLINK_H

#include "ti_params.h"

typedef struct {
    float integ; /* Nm of regen taken back, [0, cal_dcl_tmax_nm] */
} dcl_t;

void dcl_reset(dcl_t *d);
/* The command t_nm (T > 0 accelerates in +speed) with its regenerative part reduced; a motoring command
 * passes unchanged. A non-finite link reading takes all regen back; a non-finite command or speed gives 0. */
float dcl_trim(dcl_t *d, float t_nm, float v_dc, float omega_mech_rad_s, float dt_s, const ti_params_t *p);

#endif /* DCLINK_H */
