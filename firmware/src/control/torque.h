/* torque.h — torque path: limits (FW-03 power envelope, FW-04 thermal derating and the 30 s peak
 * budget, BMS regen/discharge power relayed by the VCU, FW-11 zero regen on BMS timeout),
 * torque -> current (MTPA LUT with a closed-form fallback, field weakening on the voltage
 * ellipse, a negative-Id demagnetisation clamp, the current circle) and the guards. */
#ifndef TORQUE_H
#define TORQUE_H

#include "motor.h"

#define MTPA_LUT_MAX 16u

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
/* Torque -> (id, iq). Returns false on a non-finite input (outputs zeroed). */
bool torque_to_current(float t_nm, float omega_e, float vdc, float i_max_a, const motor_t *m, const mtpa_lut_t *lut,
                       const ti_params_t *p, float *id, float *iq);
/* MTPA d-current for a q-current (closed form). */
float torque_mtpa_id(float iq, const motor_t *m);

#endif /* TORQUE_H */
