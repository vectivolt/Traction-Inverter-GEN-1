/* sim_pmsm.h — FW-39 (round 23): a virtual PMSM behind the simulated card, for the self-commissioning tests.
 * Until round 23 the scenarios drove the phase currents as an ideal current loop (tests/harness.c): nothing
 * responded to voltage, so nothing could be identified. This model does:
 *   - electrical, in the TRUE rotor dq frame (amplitude-invariant, as the FOC):
 *       v_d = R i_d + L_d di_d/dt - w L_q i_q,   v_q = R i_q + L_q di_q/dt + w (L_d i_d + psi),
 *     integrated in sub-steps of at most 5 us;
 *   - the inverter as the target has it: the duty the current-loop ISR writes at trigger k acts from trigger k + 1
 *     for one period (double-update reload: on average 1.5 ISR periods after its sample, the FOC's s6_delay_tsw);
 *     each leg loses V_DC * t_dead * f_sw * tanh(i / i_knee) (its dead time; the FOC compensates with its own linear
 *     model, cal_dtcomp_band_a and the SKU's dead time — deliberately not the same). PWM off: nothing conducts (the
 *     back-EMF in these tests stays far below V_DC); the low sides on without the high sides (ASC): v = 0;
 *   - the rotor LOCKED (a dyno brake: holds any torque), driven by a DYNO at a constant speed (whatever the torque),
 *     or FREE (its inertia, no load: a rotor that is NOT locked);
 *   - the resolver on the rotor (sim_resolver_set): theta_r = rpp * theta_m, or -rpp * theta_m when it is reversed
 *     (sin/cos swapped), and the TRUE electrical zero: theta_e = pp * theta_m - zero_rad — the calibration record
 *     only carries an estimate of it (rslv_cal_t.zero_rad);
 *   - the phase currents to the ADC (sim_set_phase_currents) with a uniform dither of +/- noise_a;
 *   - round 23 (FW-45): magnetic saturation per axis, the flux L0 Is atan(i / Is) — the differential inductance
 *     L0 / (1 + (i / Is)^2), the apparent one L0 atan(x) / x — so lambda_d = psi + that of i_d, lambda_q that of i_q, the
 *     voltage equations in the flux (v = R i + dlambda/dt -/+ w lambda) and T = 1.5 pp (lambda_d i_q - lambda_q i_d);
 *     Is = 0: the linear machine above, bit for bit;
 *   - round 23 (FW-46): a cogging torque on the shaft, cog6 sin(6 th_e) + cog12 sin(12 th_e + cog12_ph) (0: none).
 * Call sim_pmsm_step(v_dc) at every current-loop trigger, before the ISR (tests/harness.h: H.plant). */
#ifndef SIM_PMSM_H
#define SIM_PMSM_H

#include "ti_types.h"

typedef enum { SIM_ROTOR_LOCKED = 0, SIM_ROTOR_DYNO, SIM_ROTOR_FREE } sim_rotor_t;

typedef struct {
    float rs_ohm;
    float ld_h;
    float lq_h;
    float psi_wb;
    uint8_t pp;         /* motor pole pairs */
    uint8_t rpp;        /* resolver pole pairs */
    float zero_rad;     /* the TRUE electrical zero */
    bool rslv_reversed; /* the resolver turns against the phase sequence */
    float t_dead_s;     /* the inverter's effective dead time */
    float fsw_hz;
    float i_knee_a;     /* where the dead-time error saturates */
    float j_kgm2;       /* FREE rotor inertia */
    float noise_a;      /* ADC-level dither per phase, uniform +/- */
    float isat_d_a;     /* round 23 (FW-45): saturation current per axis (0 = linear) */
    float isat_q_a;
    float cog6_nm;      /* round 23 (FW-46): cogging amplitudes and the 12th's phase */
    float cog12_nm;
    float cog12_ph_rad;
} sim_pmsm_cfg_t;

void sim_pmsm_init(const sim_pmsm_cfg_t *c, float theta_m_rad);
void sim_pmsm_rotor(sim_rotor_t mode, float rpm); /* from its present angle; LOCKED ignores rpm */
void sim_pmsm_step(float vdc);
float sim_pmsm_rpm(void);
float sim_pmsm_theta_e(void);                    /* the TRUE electrical angle, [0, 2 pi) */
void sim_pmsm_idq(float *id_a, float *iq_a);     /* the currents in the TRUE rotor frame */
float sim_pmsm_torque_nm(void);                  /* the shaft's: electromagnetic + cogging */
float sim_pmsm_cogging_nm(float theta_e);        /* round 23 (FW-46): the cogging term alone at an electrical angle */
float sim_pmsm_l_app_h(bool q_axis, float i_a);  /* round 23 (FW-45): the apparent (secant) inductance lambda / i at |i| */

#endif /* SIM_PMSM_H */
