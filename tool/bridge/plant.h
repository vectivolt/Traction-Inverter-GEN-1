/* plant.h — the physical world behind the firmware's host HAL (src/platform/host), for the Traction Tool bridge.
 *
 *  - PMSM (the calibration record's motor: Ld, Lq, Rs, psi, pole pairs) on a speed-holding dyno: speed follows
 *    the dyno setpoint at a ramp rate, whatever torque the machine makes (the dyno absorbs it).
 *  - Electrical model driven by what the bridge actually does (read from the card model, sim_chain_hs_on /
 *    sim_chain_ls_on): modulating = average phase voltages from the duty the firmware wrote ONE current-loop
 *    period earlier (double-update PWM: the sample-to-actuation delay the FOC compensates, s6_delay_tsw 0.75) less
 *    the dead-time voltage loss; ASC = phases shorted; off = three-phase diode bridge (phase coordinates, the
 *    back-EMF rectifies into the link once it exceeds it). dq model integrated implicitly (stable at 16 000 rpm).
 *  - DC link: capacitance C_nom, pack (open-circuit voltage + internal resistance) behind the contactor or the
 *    precharge resistor, passive bleeder, active discharge (QDIS) resistor string.
 *  - Thermal: first-order module NTCs, board NTCs and motor sensors against a coolant temperature.
 * Everything here is the simulator's truth; the firmware only sees it through the HAL (ADC codes, SDADC blocks). */
#ifndef BRIDGE_PLANT_H
#define BRIDGE_PLANT_H

#include "calib.h"

typedef enum { PL_BR_OFF = 0, PL_BR_MOD, PL_BR_ASC } pl_bridge_t;
typedef enum { PL_CONT_OPEN = 0, PL_CONT_PRECHARGE, PL_CONT_CLOSED } pl_cont_t;

typedef struct {
    /* motor on the dyno */
    motor_t m;
    float speed_rpm;   /* shaft speed (mechanical) */
    float target_rpm;  /* dyno setpoint */
    float ramp_rpm_s;  /* dyno ramp rate */
    float theta0_mech; /* shaft angle at t0_ns (rad) */
    uint64_t t0_ns;
    float id, iq;      /* true dq currents (amplitude-invariant: phase peak A) */
    float duty_now[3]; /* applied during this current-loop period */
    float duty_next[3];/* written by the firmware: applied from the next trigger */
    bool duty_now_ok;  /* duty_now came from a modulating current-loop step (else the outputs are still off) */
    bool duty_next_ok;
    float dt_frac;     /* dead time x f_sw: the duty lost to the dead time */
    pl_bridge_t br;
    /* link */
    float c_f, v_link, v_ocv, r_pack, r_pre, r_bleed, r_active;
    pl_cont_t cont;
    bool qdis;
    /* thermal */
    float fsw_hz;
    float t_cool_c, t_mod_c[3], t_board_c[2], t_motor_c;
    float mod_offset_c, mod_offset_target_c; /* injected heat (coolant flow loss) */
    /* 1 ms averages (telemetry) */
    float i_dc_a, p_dc_w, p_mech_w, p_loss_w, torque_nm;
    double acc_idc, acc_pe, acc_t;
    uint32_t acc_n;
} plant_t;

void plant_init(plant_t *pl, const ti_params_t *p, const calib_t *cal, uint32_t fsw_hz, float v_ocv, uint64_t now_ns);
float plant_theta_mech(const plant_t *pl, uint64_t t_ns);
float plant_theta_e(const plant_t *pl, uint64_t t_ns);
void plant_phase_currents(const plant_t *pl, uint64_t t_ns, float i[3]);
/* Integrates [t0, t1] with the bridge state br (the caller reads it from the card model). */
void plant_elec(plant_t *pl, pl_bridge_t br, uint64_t t0_ns, uint64_t t1_ns);
/* True if the diode bridge conducts or winding current remains: the caller then uses 1 us steps. */
bool plant_off_active(const plant_t *pl, uint64_t t_ns);
void plant_latch_duty(plant_t *pl);
/* Every 1 ms: dyno ramp (angle continuous), thermal step, averages. dt = 1 ms. */
void plant_1ms(plant_t *pl, uint64_t now_ns);
float plant_torque(const plant_t *pl);

#endif /* BRIDGE_PLANT_H */
