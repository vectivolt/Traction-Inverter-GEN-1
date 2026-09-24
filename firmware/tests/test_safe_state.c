/* test_safe_state.c — every §6 row at n < n_x and n >= n_x, with the round-13 energy rule
 * evaluated for the S6 screening motor (0.35 mH, 25 mOhm; psi 0.15 Wb, 4 pole pairs for n_x). */
#include "safe_state.h"
#include "test.h"

#define LOW_RPM 1000.0f
#define HIGH_RPM 10000.0f
#define I_RATED 480.8f /* 340 A rms crest */
#define I_SMALL 100.0f

static const ti_params_t *P(void) { return ti_params_get(TI_SKU_8XX_SIC); }

static ss_decision_t dec(ss_row_t row, float rpm, float i_pk, bool battery, bool rule_b_released)
{
    motor_t m = motor_screening();
    m.rule_b_released = rule_b_released;
    const ss_input_t in = {.row = row, .speed_rpm = rpm, .speed_known = true, .id_a = 0.0f, .iq_a = i_pk,
                           .battery_present = battery};
    return ss_decide(&in, &m, P());
}

TEST(n_x_of_the_screening_motor)
{
    const motor_t m = motor_screening();
    CHECK_NEAR(ss_n_x_rpm(&m, P()), 880.0 / (sqrt(3.0) * 0.15) / 4.0 * 60.0 / (2.0 * 3.14159265), 1.0);
    CHECK(ss_n_x_rpm(&m, P()) > LOW_RPM && ss_n_x_rpm(&m, P()) < HIGH_RPM);
    CHECK(ss_n_x_rpm(&m, ti_params_get(TI_SKU_4XX_IGBT)) < ss_n_x_rpm(&m, P())); /* 530 V class */
}

TEST(rule_a_winding_energy_at_standstill)
{
    const motor_t m = motor_screening();
    /* contract: 340 A rms on the isolated 8XX link ends at ~1065–1089 V from 850–880 V */
    CHECK_NEAR(ss_rule_a_vpk(0.0f, 0.0f, I_RATED, &m, P()), 1091.5, 2.0);
    CHECK_NEAR(ss_rule_a_vpk(0.0f, 0.0f, 282.8f, &m, P()), 958.5, 2.0); /* 200 A rms passes */
    CHECK(ss_rule_a_vpk(0.0f, 0.0f, 353.6f, &m, P()) < 1000.5f);       /* 250 A rms: the contract's limit */
    CHECK(ss_rule_a_vpk(0.0f, 0.0f, 360.0f, &m, P()) > 1000.0f);
    CHECK(ss_rule_a_vpk(0.0f, 0.0f, 565.7f, &m, ti_params_get(TI_SKU_4XX_IGBT)) > 600.0f); /* 4XX 400 A rms */
    /* E >= V0: the screen does not apply (current does not decay) */
    CHECK(isinf(ss_rule_a_vpk(motor_omega_e(HIGH_RPM, &m), 0.0f, 10.0f, &m, P())));
    /* below n_x with no current: V_pk = V0 */
    CHECK_NEAR(ss_rule_a_vpk(motor_omega_e(LOW_RPM, &m), 0.0f, 0.0f, &m, P()), 880.0, 0.01);
}

TEST(row_cmd_lost)
{
    CHECK(dec(SS_ROW_CMD_LOST, LOW_RPM, I_SMALL, true, false).action == SS_ACT_RAMP_THEN_SPO);
    CHECK(dec(SS_ROW_CMD_LOST, HIGH_RPM, I_SMALL, true, false).action == SS_ACT_RAMP_KEEP_CC);
    CHECK(dec(SS_ROW_CMD_LOST, HIGH_RPM, I_SMALL, false, false).action == SS_ACT_LS_ASC); /* no battery */
}

TEST(row_battery_lost_never_released_by_rule_b)
{
    const ss_decision_t lo = dec(SS_ROW_BATTERY_LOST, LOW_RPM, I_RATED, true, true);
    CHECK(lo.action == SS_ACT_ZERO_TORQUE_DCL && !lo.rule_b);
    const ss_decision_t hi = dec(SS_ROW_BATTERY_LOST, HIGH_RPM, I_RATED, false, true);
    CHECK(hi.action == SS_ACT_LS_ASC && !hi.rule_b);
}

TEST(row_bms_limit_zero_with_battery_is_never_asc)
{
    /* R1-F02/R2-F35: a connected pack is a voltage source — zero regen, keep current control */
    const ss_decision_t hi = dec(SS_ROW_BMS_LIMIT_ZERO, HIGH_RPM, I_RATED, true, false);
    CHECK(hi.action == SS_ACT_RAMP_KEEP_CC);
    CHECK(hi.action != SS_ACT_LS_ASC);
    CHECK(dec(SS_ROW_BMS_LIMIT_ZERO, LOW_RPM, I_SMALL, true, false).action == SS_ACT_RAMP_THEN_SPO);
    /* the contactor OPENING instead is the battery-lost row: LS-ASC at speed */
    CHECK(dec(SS_ROW_BATTERY_LOST, HIGH_RPM, I_RATED, false, false).action == SS_ACT_LS_ASC);
}

TEST(row_resolver_invalid_with_energy_rule)
{
    const ss_decision_t a = dec(SS_ROW_RESOLVER_INVALID, LOW_RPM, I_SMALL, false, false);
    CHECK(a.action == SS_ACT_SPO && a.rule_a && !a.spo_refused); /* SPO allowed by rule (a) */
    const ss_decision_t b = dec(SS_ROW_RESOLVER_INVALID, LOW_RPM, I_RATED, false, false);
    CHECK(b.spo_refused && !b.rule_a && !b.rule_b);
    CHECK(b.action == SS_ACT_LS_ASC); /* SPO refused: the winding current circulates instead */
    const ss_decision_t c = dec(SS_ROW_RESOLVER_INVALID, LOW_RPM, I_RATED, true, true);
    CHECK(c.action == SS_ACT_SPO && c.rule_b); /* released under rule (b) with the pack connected */
    const ss_decision_t d = dec(SS_ROW_RESOLVER_INVALID, LOW_RPM, I_RATED, false, true);
    CHECK(d.action == SS_ACT_LS_ASC); /* (b) needs the battery present NOW */
    CHECK(dec(SS_ROW_RESOLVER_INVALID, HIGH_RPM, I_SMALL, true, true).action == SS_ACT_LS_ASC);
}

TEST(row_flt_hs)
{
    const ss_decision_t lo = dec(SS_ROW_FLT_HS, LOW_RPM, I_SMALL, true, false);
    CHECK(lo.action == SS_ACT_SPO && lo.spo_forced && !lo.asc_available && !lo.keep_hv);
    const ss_decision_t hi = dec(SS_ROW_FLT_HS, HIGH_RPM, I_SMALL, true, false);
    CHECK(hi.action == SS_ACT_SPO_THEN_PWM_ASC && hi.keep_hv); /* FW-08b */
    const ss_decision_t e = dec(SS_ROW_FLT_HS, LOW_RPM, I_RATED, false, false);
    CHECK(e.action == SS_ACT_SPO && e.energy_dtc); /* the hardware holds SPO: release rule violated */
}

TEST(row_flt_ls_is_spo_only)
{
    const ss_decision_t lo = dec(SS_ROW_FLT_LS, LOW_RPM, I_SMALL, true, false);
    const ss_decision_t hi = dec(SS_ROW_FLT_LS, HIGH_RPM, I_RATED, false, false);
    CHECK(lo.action == SS_ACT_SPO && hi.action == SS_ACT_SPO);
    /* battery absent: nothing to keep (A12-R08), and no safe state is proven */
    CHECK(!hi.asc_available && hi.spo_forced && !hi.keep_hv && hi.energy_dtc);
}

TEST(row_v5gd_and_vdc_invalid)
{
    const ss_decision_t v = dec(SS_ROW_V5GD_LOSS, HIGH_RPM, I_SMALL, true, false);
    CHECK(v.action == SS_ACT_SPO && v.spo_forced && !v.asc_available);
    CHECK(dec(SS_ROW_V5GD_LOSS, LOW_RPM, I_RATED, false, false).energy_dtc);
    CHECK(dec(SS_ROW_V5GD_LOSS, LOW_RPM, I_RATED, true, true).action == SS_ACT_SPO);
    CHECK(dec(SS_ROW_VDC_INVALID, LOW_RPM, I_SMALL, true, false).action == SS_ACT_SPO);
    CHECK(dec(SS_ROW_VDC_INVALID, LOW_RPM, I_RATED, false, false).action == SS_ACT_LS_ASC);
}

TEST(row_overvoltage_and_overcurrent)
{
    CHECK(dec(SS_ROW_OVERVOLTAGE, LOW_RPM, I_SMALL, true, true).action == SS_ACT_LS_ASC); /* FW-06 at any speed */
    CHECK(dec(SS_ROW_OVERVOLTAGE, HIGH_RPM, I_SMALL, false, false).action == SS_ACT_LS_ASC);
    CHECK(dec(SS_ROW_OVERCURRENT, LOW_RPM, I_SMALL, false, false).action == SS_ACT_SPO);
    CHECK(dec(SS_ROW_OVERCURRENT, HIGH_RPM, I_SMALL, false, false).action == SS_ACT_LS_ASC);
}

TEST(unknown_speed_takes_high_column)
{
    motor_t m = motor_screening();
    const ss_input_t in = {.row = SS_ROW_RESOLVER_INVALID, .speed_rpm = 0.0f, .speed_known = false, .iq_a = I_SMALL};
    const ss_decision_t d = ss_decide(&in, &m, P());
    CHECK(d.high_speed && d.action == SS_ACT_LS_ASC);
}

TEST(ranking)
{
    CHECK(ss_rank(SS_ACT_SPO_THEN_PWM_ASC) > ss_rank(SS_ACT_LS_ASC));
    CHECK(ss_rank(SS_ACT_LS_ASC) > ss_rank(SS_ACT_SPO));
    CHECK(ss_rank(SS_ACT_SPO) > ss_rank(SS_ACT_ZERO_TORQUE_DCL));
    CHECK(ss_rank(SS_ACT_RAMP_THEN_SPO) > ss_rank(SS_ACT_RAMP_KEEP_CC));
}

/* A12-R08: keep_hv = the SPO relies on the battery (rule (a) fails, battery present) at ANY speed.
 * Cross product speed {0, low, high} x current {small, rated} x {FLT_HS, FLT_LS} x rule (b)
 * {not released, released} x battery {absent, present}. */
TEST(keep_hv_follows_the_battery_as_the_sink_at_every_speed)
{
    /* the reviewer's reproduction: 0 rpm, 0.35 mH, 340 A rms, 8XX: rule_a 0, rule_b 1, SPO */
    const ss_decision_t r = dec(SS_ROW_FLT_HS, 0.0f, I_RATED, true, true);
    CHECK(!r.rule_a && r.rule_b && r.action == SS_ACT_SPO && r.keep_hv && !r.energy_dtc);
    const float rpm[3] = {0.0f, LOW_RPM, HIGH_RPM};
    const float amp[2] = {I_SMALL, I_RATED};
    const ss_row_t rows[2] = {SS_ROW_FLT_HS, SS_ROW_FLT_LS};
    unsigned n = 0u;
    for (unsigned a = 0u; a < 3u; a++) {
        for (unsigned b = 0u; b < 2u; b++) {
            for (unsigned c = 0u; c < 2u; c++) {
                for (unsigned rel = 0u; rel < 2u; rel++) {
                    for (unsigned bat = 0u; bat < 2u; bat++) {
                        const ss_decision_t d = dec(rows[c], rpm[a], amp[b], bat != 0u, rel != 0u);
                        const bool high = (a == 2u);
                        const bool rule_a = !high && (b == 0u); /* E >= V0 above n_x; 340 A rms fails */
                        CHECK(d.rule_a == rule_a);
                        CHECK(d.keep_hv == (!rule_a && (bat != 0u)));
                        CHECK(d.energy_dtc == (!rule_a && !((rel != 0u) && (bat != 0u))));
                        CHECK(d.action == (((c == 0u) && high) ? SS_ACT_SPO_THEN_PWM_ASC : SS_ACT_SPO));
                        n++;
                    }
                }
            }
        }
    }
    CHECK(n == 48u);
    /* the same for any row that ends in SPO; lost-battery rows never keep (nor borrow) a battery */
    CHECK(dec(SS_ROW_V5GD_LOSS, 0.0f, I_RATED, true, true).keep_hv);
    CHECK(!dec(SS_ROW_BATTERY_LOST, 0.0f, I_RATED, true, true).keep_hv);
    CHECK(!dec(SS_ROW_OVERVOLTAGE, 0.0f, I_RATED, true, true).keep_hv);
    /* where LS-ASC takes the energy nothing relies on the battery */
    const ss_decision_t asc = dec(SS_ROW_RESOLVER_INVALID, 0.0f, I_RATED, true, false);
    CHECK(asc.action == SS_ACT_LS_ASC && !asc.keep_hv && !asc.energy_dtc);
}

void suite_safe_state(void)
{
    RUN(n_x_of_the_screening_motor);
    RUN(rule_a_winding_energy_at_standstill);
    RUN(row_cmd_lost);
    RUN(row_battery_lost_never_released_by_rule_b);
    RUN(row_bms_limit_zero_with_battery_is_never_asc);
    RUN(row_resolver_invalid_with_energy_rule);
    RUN(row_flt_hs);
    RUN(row_flt_ls_is_spo_only);
    RUN(row_v5gd_and_vdc_invalid);
    RUN(row_overvoltage_and_overcurrent);
    RUN(unknown_speed_takes_high_column);
    RUN(ranking);
    RUN(keep_hv_follows_the_battery_as_the_sink_at_every_speed);
}
