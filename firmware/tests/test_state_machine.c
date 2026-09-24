/* test_state_machine.c — the pure transition function: the §9 order, every exit to FAULT, the
 * run/derate/zero-torque loop, discharge, power-down and the DESAT retry. */
#include "state_machine.h"
#include "test.h"

static const ti_params_t *P(void) { return ti_params_get(TI_SKU_8XX_SIC); }

static sm_in_t healthy(uint32_t t)
{
    const sm_in_t in = {.now_ms = t, .ign_on = true, .init = SM_OK, .sensors_ok = true, .v5gd_ok = true,
                        .fs0b_released = true, .gate_power_ready = true, .cmd_fresh = true, .enable_req = false,
                        .contactors = TI_CONT_OPEN, .selftest = SM_OK, .precharge = SM_BUSY,
                        .torque_ramped_out = true, .nvm_idle = true, .n_x_rpm = 8000.0f};
    return in;
}

static sm_out_t step(sm_t *s, const sm_in_t *in)
{
    sm_out_t o;
    sm_step(s, in, &o, P());
    return o;
}

TEST(boot_order_follows_section_9)
{
    sm_t s;
    sm_init(&s);
    sm_in_t in = healthy(0u);
    in.fs0b_released = false;
    in.gate_power_ready = false;
    (void)step(&s, &in);
    CHECK(s.st == SM_INIT);
    sm_out_t o = step(&s, &in);
    CHECK(o.req_flt_clear && s.st == SM_INIT); /* §9 step 2 first */
    (void)step(&s, &in);
    CHECK(s.st == SM_SENSOR_SELFTEST);
    o = step(&s, &in);
    CHECK(o.req_fs0b_release && !o.arm && !o.req_gate_power); /* step 4 with MCU_GATE_EN low */
    in.fs0b_released = true;
    o = step(&s, &in);
    CHECK(o.req_asc_decision && o.req_gate_power); /* steps 5 and 6 */
    o = step(&s, &in);
    CHECK(!o.req_asc_decision && s.st == SM_SENSOR_SELFTEST);
    in.gate_power_ready = true;
    (void)step(&s, &in);
    CHECK(s.st == SM_VEHICLE_HANDSHAKE);
    in.selftest = SM_BUSY;
    (void)step(&s, &in);
    CHECK(s.st == SM_GATE_SELFTEST);
    o = step(&s, &in);
    CHECK(o.req_selftest && !o.self_test_done && !o.arm);
    in.selftest = SM_OK;
    o = step(&s, &in);
    CHECK(o.self_test_done && s.st == SM_PRECHARGE_WAIT); /* the VCU precharges only after this */
    in.contactors = TI_CONT_CLOSED;
    in.link_at_pack = true;
    (void)step(&s, &in);
    CHECK(s.st == SM_ARMED_ZERO_TORQUE);
    o = step(&s, &in);
    CHECK(o.arm && !o.torque_enable);
    in.enable_req = true;
    in.torque_req_nm = 50.0f;
    (void)step(&s, &in);
    CHECK(s.st == SM_RUN);
    o = step(&s, &in);
    CHECK(o.arm && o.torque_enable && !o.torque_reduced);
}

static sm_t at(sm_state_t st)
{
    sm_t s;
    sm_init(&s);
    s.st = st;
    s.step6_done = true;
    s.self_test_done = true;
    return s;
}

TEST(init_and_sensor_failures_never_arm)
{
    sm_t s;
    sm_init(&s);
    sm_in_t in = healthy(0u);
    in.init = SM_FAIL;
    (void)step(&s, &in);
    (void)step(&s, &in);
    CHECK(s.st == SM_FAULT);
    for (int k = 0; k < 10; k++) {
        CHECK(!step(&s, &in).arm);
    }
    CHECK(s.st == SM_FAULT); /* no step 6: never leaves FAULT this key cycle */
    const bool cases[4][4] = {{false, false, false, false}, {true, true, false, false},
                              {true, false, true, false}, {true, false, false, true}};
    for (int c = 0; c < 4; c++) {
        s = at(SM_SENSOR_SELFTEST);
        in = healthy(0u);
        in.v5gd_ok = cases[c][0];
        in.flt_low_at_boot = cases[c][1];
        in.rdy_before_gate_power = cases[c][2];
        in.gate_power_failed = cases[c][3];
        (void)step(&s, &in);
        CHECK(s.st == SM_FAULT);
    }
    s = at(SM_SENSOR_SELFTEST);
    in = healthy(0u);
    in.sensors_ok = false;
    (void)step(&s, &in);
    CHECK(s.st == SM_SENSOR_SELFTEST);
    in.now_ms = P()->cal_sensor_selftest_ms;
    (void)step(&s, &in);
    CHECK(s.st == SM_FAULT);
}

TEST(selftest_and_precharge_refusals)
{
    sm_t s = at(SM_GATE_SELFTEST);
    s.self_test_done = false;
    sm_in_t in = healthy(0u);
    in.selftest = SM_FAIL;
    (void)step(&s, &in);
    CHECK(s.st == SM_FAULT);
    s = at(SM_PRECHARGE_WAIT);
    in = healthy(0u);
    in.precharge = SM_FAIL;
    (void)step(&s, &in);
    CHECK(s.st == SM_FAULT);
    s = at(SM_PRECHARGE_WAIT);
    in = healthy(0u);
    in.contactors = TI_CONT_CLOSED;
    in.link_at_pack = false; /* closed but not at the pack voltage */
    (void)step(&s, &in);
    CHECK(s.st == SM_PRECHARGE_WAIT);
}

TEST(stale_command_ramps_then_zero_torque)
{
    sm_t s = at(SM_RUN);
    sm_in_t in = healthy(0u);
    in.contactors = TI_CONT_CLOSED;
    in.enable_req = true;
    in.cmd_fresh = false;
    in.torque_ramped_out = false; /* FW-11: ramp, do not hold */
    (void)step(&s, &in);
    CHECK(s.st == SM_RUN);
    in.torque_ramped_out = true;
    (void)step(&s, &in);
    CHECK(s.st == SM_ARMED_ZERO_TORQUE);
}

TEST(derate_hysteresis_states)
{
    sm_t s = at(SM_RUN);
    sm_in_t in = healthy(0u);
    in.contactors = TI_CONT_CLOSED;
    in.enable_req = true;
    in.torque_ramped_out = false;
    in.derate_active = true;
    (void)step(&s, &in);
    CHECK(s.st == SM_DERATE);
    sm_out_t o = step(&s, &in);
    CHECK(o.torque_enable && s.st == SM_DERATE);
    in.derate_active = false;
    (void)step(&s, &in);
    CHECK(s.st == SM_RUN);
}

TEST(faults_and_contactor_opening)
{
    sm_t s = at(SM_RUN);
    sm_in_t in = healthy(0u);
    in.contactors = TI_CONT_CLOSED;
    in.fault_needed = true;
    (void)step(&s, &in);
    CHECK(s.st == SM_FAULT);
    in.fault_needed = false;
    (void)step(&s, &in);
    CHECK(s.st == SM_VEHICLE_HANDSHAKE); /* the fault cleared (or the VCU reset it) */
    s = at(SM_ARMED_ZERO_TORQUE);
    in = healthy(0u);
    in.contactors = TI_CONT_OPEN;
    in.speed_rpm = 100.0f;
    (void)step(&s, &in);
    CHECK(s.st == SM_PRECHARGE_WAIT); /* normal opening at zero torque */
}

TEST(discharge_only_with_contactors_open)
{
    sm_t s = at(SM_ARMED_ZERO_TORQUE);
    sm_in_t in = healthy(0u);
    in.contactors = TI_CONT_CLOSED;
    in.discharge_req = true;
    (void)step(&s, &in);
    CHECK(s.st != SM_DISCHARGE);
    s = at(SM_FAULT);
    in.contactors = TI_CONT_OPEN;
    in.fault_needed = true;
    (void)step(&s, &in);
    CHECK(s.st == SM_DISCHARGE); /* e.g. crash: from FAULT too */
    sm_out_t o = step(&s, &in);
    CHECK(o.req_discharge);
    in.discharge_done = true;
    (void)step(&s, &in);
    CHECK(s.st == SM_FAULT);
}

TEST(key_off_powerdown_to_lpoff)
{
    sm_t s = at(SM_ARMED_ZERO_TORQUE);
    sm_in_t in = healthy(0u);
    in.ign_on = false;
    in.contactors = TI_CONT_OPEN;
    in.nvm_idle = false;
    (void)step(&s, &in);
    CHECK(s.st == SM_SAFE_POWERDOWN);
    sm_out_t o = step(&s, &in);
    CHECK(o.req_discharge && !o.arm && !o.req_lpoff);
    in.discharge_done = true;
    o = step(&s, &in);
    CHECK(!o.req_lpoff); /* NVM queue still busy */
    in.nvm_idle = true;
    o = step(&s, &in);
    CHECK(o.req_lpoff && s.st == SM_OFF);
}

TEST(desat_retry_runs_at_reduced_torque)
{
    sm_t s = at(SM_FAULT);
    sm_in_t in = healthy(0u);
    in.fault_needed = true;
    in.retry_allowed = true;
    sm_out_t o = step(&s, &in);
    CHECK(o.req_recovery && s.st == SM_FAULT);
    in.retry_allowed = false;
    in.recovery_done = true;
    in.fault_needed = false;
    (void)step(&s, &in);
    CHECK(s.st == SM_VEHICLE_HANDSHAKE);
    in.recovery_done = false;
    (void)step(&s, &in);
    CHECK(s.st == SM_PRECHARGE_WAIT); /* stored self-test of this key cycle */
    in.contactors = TI_CONT_CLOSED;
    in.link_at_pack = true;
    (void)step(&s, &in);
    in.enable_req = true;
    in.torque_req_nm = 100.0f;
    (void)step(&s, &in);
    CHECK(s.st == SM_RUN);
    o = step(&s, &in);
    CHECK(o.torque_reduced);
}

void suite_state_machine(void)
{
    RUN(boot_order_follows_section_9);
    RUN(init_and_sensor_failures_never_arm);
    RUN(selftest_and_precharge_refusals);
    RUN(stale_command_ramps_then_zero_torque);
    RUN(derate_hysteresis_states);
    RUN(faults_and_contactor_opening);
    RUN(discharge_only_with_contactors_open);
    RUN(key_off_powerdown_to_lpoff);
    RUN(desat_retry_runs_at_reduced_torque);
}
