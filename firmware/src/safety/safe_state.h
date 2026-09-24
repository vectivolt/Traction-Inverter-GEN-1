/* safe_state.h — the §6 safe-state decision matrix (review R-F13/F15, rounds 8–13).
 *
 * Actuators: SPO (three-phase open), LS-ASC (entered only as PWM-ASC on the MCU path, §4c) and a
 * torque-controlled ramp. Columns split on speed at n_x (sqrt(3)*w_e*psi_f = OV trip, cold
 * magnets); an unknown speed is treated as n >= n_x. The energy condition applies in BOTH
 * columns (round 13, A11-R01): a cell that ends in SPO is released only if
 *   rule (a): V_pk = E + sqrt((V0 - E)^2 + 1.5*(Ld*id^2 + Lq*iq^2)/C_min) < U_N, with
 *             E = sqrt(3)*w_e*(psi_f + |Lq - Ld|*I/2), valid only while E < V0 (else: fails —
 *             the contract then needs a diode-bridge simulation, which firmware cannot run), or
 *   rule (b): the VCU/BMS integration is proven (motor_t.rule_b_released) AND the battery is
 *             present now — never for the rows whose premise is that the battery path is gone.
 * The current is the one at the decision (dq amplitude-invariant = phase peak amperes).
 * If SPO is refused and LS-ASC is available for the row, the decision becomes LS-ASC (the winding
 * current circulates and decays instead of charging the link, round 13); if LS-ASC is not
 * available (FLT_LS, V5GD loss, the FLT_HS reset interval) SPO is what the hardware does anyway
 * and the decision flags energy_dtc: that motor/vehicle combination was not releasable.
 * keep_hv (FW-08b, A12-R08): the SPO the decision holds relies on the battery as the energy sink —
 * rule (a) fails and the battery is present — at ANY speed, for every row whose premise is not a
 * lost battery. It is re-evaluated with the present current and speed, so it clears once rule (a)
 * holds; ASC (an independent sink) clears it in fault_mgr.c. With the battery absent there is
 * nothing to keep: keep_hv is false and energy_dtc says that no safe state is proven. */
#ifndef SAFE_STATE_H
#define SAFE_STATE_H

#include "motor.h"

typedef enum {
    SS_ROW_CMD_LOST = 0,     /* healthy, command/CAN lost; HVIL open (FW-09) */
    SS_ROW_BATTERY_LOST,     /* contactor open/feedback invalid, V_DC leaving the pack */
    SS_ROW_BMS_LIMIT_ZERO,   /* charge limit 0 with the battery connected (R1-F02) */
    SS_ROW_RESOLVER_INVALID, /* also: phase-current sensing invalid, non-finite control */
    SS_ROW_FLT_HS,
    SS_ROW_FLT_LS,
    SS_ROW_V5GD_LOSS,        /* or the power board's LV feed */
    SS_ROW_OVERVOLTAGE,      /* FW-06 */
    SS_ROW_OVERCURRENT,      /* FW-05 (not a §6 row: treated as "control lost", see .c) */
    SS_ROW_VDC_INVALID,      /* FW-07 with V5GD healthy (VOFS, disagreement, fail-safe) */
    SS_ROW_COUNT
} ss_row_t;

typedef enum {
    SS_ACT_NONE = 0,
    SS_ACT_RAMP_KEEP_CC,     /* ramp torque to zero, keep current control (field weakening) */
    SS_ACT_RAMP_THEN_SPO,    /* ramp torque to zero, then SPO */
    SS_ACT_ZERO_TORQUE_DCL,  /* zero torque at the current-loop rate + DC-link control, FW-06 armed */
    SS_ACT_SPO,
    SS_ACT_LS_ASC,           /* PWM-ASC (§4c MCU path) */
    SS_ACT_SPO_THEN_PWM_ASC  /* FLT_HS at n >= n_x: SPO (hardware), PWM-ASC after the FW-15 reset */
} ss_action_t;

typedef struct {
    ss_row_t row;
    float speed_rpm;
    bool speed_known;
    float id_a;
    float iq_a;
    bool battery_present; /* contactors reported closed and V_DC within 3 % of the pack */
} ss_input_t;

typedef struct {
    ss_action_t action;
    bool high_speed;      /* n >= n_x or unknown */
    bool spo_wanted;      /* the matrix cell ends in SPO */
    bool rule_a;
    bool rule_b;
    bool spo_refused;     /* SPO wanted, neither rule holds */
    bool energy_dtc;      /* SPO held with neither rule (a) nor (b): no safe state proven */
    bool asc_available;   /* LS-ASC may be (re-)entered for this row */
    bool keep_hv;         /* FW-08b report: the SPO relies on the battery (rule (a) fails) */
    bool spo_forced;      /* the hardware holds SPO (fault latch / supply) */
    float v_pk;           /* rule (a) bound, +inf when the screen does not apply */
} ss_decision_t;

float ss_n_x_rpm(const motor_t *m, const ti_params_t *p);
/* Rule (a) screen; returns +INFINITY when E >= V0 (screen invalid). */
float ss_rule_a_vpk(float omega_e, float id_a, float iq_a, const motor_t *m, const ti_params_t *p);
ss_decision_t ss_decide(const ss_input_t *in, const motor_t *m, const ti_params_t *p);
/* Severity used to combine simultaneous rows (higher wins; any spo_forced wins outright). */
uint8_t ss_rank(ss_action_t a);
const char *ss_row_name(ss_row_t r);
const char *ss_action_name(ss_action_t a);

#endif /* SAFE_STATE_H */
