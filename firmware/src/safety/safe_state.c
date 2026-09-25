/* safe_state.c — §6 matrix and the SPO energy release rules (a)/(b). Pure functions. */
#include "safe_state.h"

float ss_n_x_rpm(const motor_t *m, const ti_params_t *p) { return motor_n_x_rpm(m, p); }

float ss_rule_a_vpk(float omega_e, float id_a, float iq_a, const motor_t *m, const ti_params_t *p)
{
    const float i_mag = sqrtf((id_a * id_a) + (iq_a * iq_a));
    const float e = TI_SQRT3 * ti_absf(omega_e) * (m->psi_wb + (ti_absf(m->lq_h - m->ld_h) * i_mag * 0.5f));
    const float v0 = p->ov_trip_v;
    if (e >= v0) {
        return INFINITY; /* the current does not decay: only a simulation could release it */
    }
    const float w = 1.5f * ((m->ld_h * id_a * id_a) + (m->lq_h * iq_a * iq_a)) / p->c_min_f;
    return e + sqrtf(((v0 - e) * (v0 - e)) + w);
}

/* The matrix cell before the energy rule. */
static ss_action_t cell(ss_row_t row, bool high, bool battery)
{
    switch (row) {
    case SS_ROW_CMD_LOST:
        return high ? (battery ? SS_ACT_RAMP_KEEP_CC : SS_ACT_LS_ASC) : SS_ACT_RAMP_THEN_SPO;
    case SS_ROW_BATTERY_LOST:
        return high ? SS_ACT_LS_ASC : SS_ACT_ZERO_CURRENT;
    case SS_ROW_BMS_LIMIT_ZERO: /* a connected pack is a voltage source: never an ASC case */
        return high ? SS_ACT_RAMP_KEEP_CC : SS_ACT_RAMP_THEN_SPO;
    case SS_ROW_RESOLVER_INVALID:
    case SS_ROW_OVERCURRENT: /* control lost: LS-ASC needs no angle */
        return high ? SS_ACT_LS_ASC : SS_ACT_SPO;
    case SS_ROW_FLT_HS:
        return high ? SS_ACT_SPO_THEN_PWM_ASC : SS_ACT_SPO;
    case SS_ROW_FLT_LS:
    case SS_ROW_V5GD_LOSS:
    case SS_ROW_VDC_INVALID:
        return SS_ACT_SPO;
    case SS_ROW_OVERVOLTAGE:
        return SS_ACT_LS_ASC; /* FW-06 at any speed */
    default:
        return SS_ACT_SPO;
    }
}

static bool asc_available(ss_row_t row)
{
    /* FLT_LS: a shorted HS would short the link through the motor. V5GD: ASC masked / no bias.
     * FLT_HS: not with EN low; only as PWM-ASC after the FW-15 reset (SPO_THEN_PWM_ASC). */
    return (row != SS_ROW_FLT_LS) && (row != SS_ROW_V5GD_LOSS) && (row != SS_ROW_FLT_HS);
}

static bool rule_b_applies(ss_row_t row)
{
    return (row != SS_ROW_BATTERY_LOST) && (row != SS_ROW_OVERVOLTAGE); /* premise: battery gone */
}

ss_decision_t ss_decide(const ss_input_t *in, const motor_t *m, const ti_params_t *p)
{
    ss_decision_t d = {0};
    d.high_speed = !in->speed_known || (ti_absf(in->speed_rpm) >= ss_n_x_rpm(m, p));
    d.action = cell(in->row, d.high_speed, in->battery_present);
    d.asc_available = asc_available(in->row);
    d.spo_forced = (in->row == SS_ROW_FLT_LS) || (in->row == SS_ROW_V5GD_LOSS) || (in->row == SS_ROW_FLT_HS);
    /* an unknown speed cannot prove E < V0: evaluate rule (a) at n_max */
    const float rpm = in->speed_known ? in->speed_rpm : m->n_max_rpm;
    d.v_pk = ss_rule_a_vpk(motor_omega_e(rpm, m), in->id_a, in->iq_a, m, p);
    d.rule_a = d.v_pk < p->cap_un_v;
    d.rule_b = m->rule_b_released && in->battery_present && rule_b_applies(in->row);
    d.spo_wanted = (d.action == SS_ACT_SPO) || (d.action == SS_ACT_SPO_THEN_PWM_ASC);
    d.spo_refused = d.spo_wanted && !d.rule_a && !d.rule_b;
    if (d.spo_refused) {
        if ((d.action == SS_ACT_SPO) && d.asc_available) {
            d.action = SS_ACT_LS_ASC;
        } else {
            d.energy_dtc = true; /* SPO is what the hardware holds; the release rule is violated */
        }
    }
    if ((d.action == SS_ACT_LS_ASC) && !d.asc_available) {
        d.action = SS_ACT_SPO;
    }
    /* FW-08b / A12-R08: the final action holds SPO, rule (a) does not cover it, so the battery is
     * the sink — whatever the speed; a lost-battery row never borrows it */
    const bool spo_held = (d.action == SS_ACT_SPO) || (d.action == SS_ACT_SPO_THEN_PWM_ASC);
    d.keep_hv = spo_held && !d.rule_a && in->battery_present && rule_b_applies(in->row);
    return d;
}

uint8_t ss_rank(ss_action_t a)
{
    static const uint8_t R[7] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
    return ((uint32_t)a < 7u) ? R[a] : 0u;
}

const char *ss_row_name(ss_row_t r)
{
    static const char *const N[SS_ROW_COUNT] = {"CMD_LOST", "BATTERY_LOST", "BMS_LIMIT_ZERO", "RESOLVER_INVALID",
                                                "FLT_HS", "FLT_LS", "V5GD_LOSS", "OVERVOLTAGE", "OVERCURRENT",
                                                "VDC_INVALID"};
    return (r < SS_ROW_COUNT) ? N[r] : "?";
}

const char *ss_action_name(ss_action_t a)
{
    static const char *const N[7] = {"NONE", "RAMP_KEEP_CC", "RAMP_THEN_SPO", "ZERO_CURRENT", "SPO", "LS_ASC",
                                     "SPO_THEN_PWM_ASC"};
    return ((uint32_t)a < 7u) ? N[a] : "?";
}
