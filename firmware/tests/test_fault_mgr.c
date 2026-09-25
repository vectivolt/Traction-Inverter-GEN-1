/* test_fault_mgr.c — row combination, FW-15 retained RAM + queued NVM, the one-retry rule,
 * DESAT records at boot, FW-08b keep-HV, latched resets. */
#include "dtc.h"
#include "fault_mgr.h"
#include "sim.h"
#include "test.h"

static const ti_params_t *P(void) { return ti_params_get(TI_SKU_8XX_SIC); }

static fm_ctx_t ctx(float rpm, float iq, bool battery, uint32_t t)
{
    const fm_ctx_t c = {.speed_rpm = rpm, .speed_known = true, .iq_a = iq, .battery_present = battery, .now_ms = t,
                        .key_cycle = 10u};
    return c;
}

TEST(forced_spo_wins_and_blocks_asc)
{
    const motor_t m = motor_screening();
    fm_t f;
    fm_init(&f, 10u);
    fm_ctx_t c = ctx(1000.0f, 50.0f, true, 0u);
    fm_raise(&f, SS_ROW_OVERVOLTAGE, true, &c, &m, P());
    CHECK(f.dec.action == SS_ACT_LS_ASC && f.asc_permitted);
    fm_raise(&f, SS_ROW_FLT_LS, true, &c, &m, P());
    CHECK(f.dec.action == SS_ACT_SPO && f.dec.spo_forced && !f.asc_permitted); /* no LS-ASC after FLT_LS */
}

TEST(highest_rank_wins)
{
    const motor_t m = motor_screening();
    fm_t f;
    fm_init(&f, 10u);
    fm_ctx_t c = ctx(10000.0f, 50.0f, true, 0u);
    fm_raise(&f, SS_ROW_VDC_INVALID, true, &c, &m, P());      /* SPO */
    fm_raise(&f, SS_ROW_RESOLVER_INVALID, true, &c, &m, P()); /* LS-ASC at speed */
    CHECK(f.dec.action == SS_ACT_LS_ASC && f.dec_row == SS_ROW_RESOLVER_INVALID);
    CHECK(fm_needs_fault_state(&f, false));
    fm_t g;
    fm_init(&g, 10u);
    fm_raise(&g, SS_ROW_CMD_LOST, false, &c, &m, P());
    CHECK(!fm_needs_fault_state(&g, false)); /* command loss ramps, it is not the FAULT state */
    fm_clear(&g, SS_ROW_CMD_LOST);
    CHECK(!fm_any(&g));
}

TEST(fw15_retained_first_nvm_only_queued)
{
    const motor_t m = motor_screening();
    nv_init();
    fm_t f;
    fm_init(&f, 10u);
    const uint32_t writes = sim_nvm_writes_done();
    fm_ctx_t c = ctx(9000.0f, 300.0f, true, 100u);
    fm_desat(&f, true, &c, &m, P());
    CHECK(g_fm_retained.bank == 1u && g_fm_retained.committed == 0u && g_fm_retained.key_cycle == 10u);
    CHECK(nv_pending() >= 1u);
    CHECK(sim_nvm_writes_done() == writes); /* nothing written in front of the §6 action */
    CHECK(fm_active(&f, SS_ROW_FLT_HS) && dtc_active(DTC_DESAT_HS));
    CHECK(f.dec.action == SS_ACT_SPO_THEN_PWM_ASC && f.keep_hv); /* n >= n_x */
}

TEST(one_authorised_retry_after_1s_then_latch)
{
    const motor_t m = motor_screening();
    nv_init();
    dtc_init();
    fm_t f;
    fm_init(&f, 10u);
    fm_ctx_t c = ctx(500.0f, 100.0f, true, 1000u);
    fm_desat(&f, false, &c, &m, P());
    CHECK(!fm_retry_allowed(&f, true, 1999u, P()));  /* < 1 s */
    CHECK(!fm_retry_allowed(&f, false, 2500u, P())); /* not authorised */
    CHECK(fm_retry_allowed(&f, true, 2000u, P()));
    fm_retry_consumed(&f, &c);
    CHECK(!fm_active(&f, SS_ROW_FLT_LS));
    CHECK(!fm_retry_allowed(&f, true, 9000u, P())); /* once per key cycle */
    c.now_ms = 9000u;
    fm_desat(&f, false, &c, &m, P());
    CHECK(dtc_active(DTC_DESAT_REPEAT));
    CHECK(!fm_retry_allowed(&f, true, 20000u, P()));
}

TEST(boot_blocks_on_recent_desat_record)
{
    fm_t f;
    fm_init(&f, 10u);
    const nv_desat_t prev = {.key_cycle = 9u, .bank = 2u};
    fm_boot(&f, &prev, true);
    CHECK(f.desat_blocked && fm_needs_fault_state(&f, false));
    fm_init(&f, 10u);
    const nv_desat_t old = {.key_cycle = 8u, .bank = 2u};
    fm_boot(&f, &old, true);
    CHECK(!f.desat_blocked);
    fm_init(&f, 10u);
    const nv_desat_t same = {.key_cycle = 10u, .bank = 1u, .retry_used = 1u};
    fm_boot(&f, &same, true);
    CHECK(f.desat_blocked && f.retry_used); /* the retry of this key cycle is spent */
}

TEST(uncommitted_retained_record_requeued_at_boot)
{
    const motor_t m = motor_screening();
    nv_init();
    fm_t f;
    fm_init(&f, 10u);
    fm_ctx_t c = ctx(100.0f, 10.0f, true, 5u);
    fm_desat(&f, true, &c, &m, P());
    nv_init(); /* "reboot" before the queue drained (RAM queue lost, retained RAM kept) */
    fm_t g;
    fm_init(&g, 10u);
    fm_boot(&g, NULL, false);
    CHECK(nv_pending() == 1u && g.desat_blocked);
    for (int k = 0; k < 50 && !nv_idle(); k++) {
        nv_service();
    }
    nv_desat_t r = {0};
    CHECK(nv_read(NV_REC_DESAT, &r, (uint16_t)sizeof r) && r.bank == 1u && r.key_cycle == 10u);
    fm_retained_commit();
    CHECK(g_fm_retained.committed == 1u);
}

TEST(keep_hv_until_asc_or_low_speed)
{
    const motor_t m = motor_screening();
    nv_init();
    fm_t f;
    fm_init(&f, 10u);
    fm_ctx_t c = ctx(9000.0f, 100.0f, true, 0u);
    fm_desat(&f, true, &c, &m, P());
    CHECK(f.keep_hv && !f.asc_permitted);
    fm_hs_reset_done(&f);
    fm_update(&f, &c, &m, P());
    CHECK(f.asc_permitted && f.keep_hv); /* PWM-ASC may be re-entered; not yet active */
    c.asc_active = true;
    fm_update(&f, &c, &m, P());
    CHECK(!f.keep_hv);
    c.asc_active = false;
    c.speed_rpm = 1000.0f;
    fm_update(&f, &c, &m, P());
    CHECK(!f.keep_hv);
}

TEST(latched_reset_only_below_n_x_and_never_for_desat)
{
    const motor_t m = motor_screening();
    nv_init();
    fm_t f;
    fm_init(&f, 10u);
    fm_ctx_t c = ctx(10000.0f, 10.0f, true, 0u);
    fm_raise(&f, SS_ROW_OVERVOLTAGE, true, &c, &m, P());
    fm_desat(&f, false, &c, &m, P());
    CHECK(!fm_reset_latched(&f, &c, &m, P())); /* at speed */
    c.speed_rpm = 100.0f;
    CHECK(fm_reset_latched(&f, &c, &m, P()));
    CHECK(!fm_active(&f, SS_ROW_OVERVOLTAGE) && fm_active(&f, SS_ROW_FLT_LS));
    fm_clear(&f, SS_ROW_FLT_LS); /* latched: cannot be cleared by condition */
    CHECK(fm_active(&f, SS_ROW_FLT_LS));
}

/* A12-R08: after a DESAT at standstill and 340 A rms the SPO relies on the battery: keep_hv is
 * asserted, held while rule (a) fails, dropped (with "no safe state proven") when the battery goes,
 * and released once the current has decayed so that rule (a) holds, or while ASC is active. */
TEST(keep_hv_held_until_rule_a_and_no_safe_state_without_battery)
{
    motor_t m = motor_screening();
    m.rule_b_released = true;
    nv_init();
    dtc_init();
    fm_t f;
    fm_init(&f, 10u);
    fm_ctx_t c = ctx(0.0f, 480.8f, true, 100u);
    fm_desat(&f, true, &c, &m, P());
    CHECK(f.keep_hv && !f.no_safe_state && f.dec.action == SS_ACT_SPO);
    c.now_ms = 150u;
    fm_update(&f, &c, &m, P());
    CHECK(f.keep_hv); /* held: the current is still there */
    c.battery_present = false;
    c.now_ms = 160u;
    fm_update(&f, &c, &m, P());
    CHECK(!f.keep_hv && f.no_safe_state && dtc_active(DTC_SPO_ENERGY)); /* nothing to keep, no proof */
    c.battery_present = true;
    fm_update(&f, &c, &m, P());
    CHECK(f.keep_hv && !f.no_safe_state);
    c.asc_active = true;
    fm_update(&f, &c, &m, P());
    CHECK(!f.keep_hv); /* ASC is a sink of its own */
    c.asc_active = false;
    c.iq_a = 20.0f; /* decayed: rule (a) holds */
    fm_update(&f, &c, &m, P());
    CHECK(!f.keep_hv && !f.no_safe_state);
}

/* A13-R02: a battery-path row is a FAULT while its §6 response still has something to manage; the
 * caller's "done" (bridge SPO with nothing left, V_DC at the pack) makes that row alone soft. */
TEST(battery_lost_is_a_fault_until_its_response_is_done)
{
    const motor_t m = motor_screening();
    fm_t f;
    fm_init(&f, 10u);
    fm_ctx_t c = ctx(0.0f, 480.0f, false, 0u);
    fm_raise(&f, SS_ROW_BATTERY_LOST, false, &c, &m, P());
    CHECK(f.dec.action == SS_ACT_ZERO_TORQUE_DCL && !f.dec.keep_hv && !f.no_safe_state);
    CHECK(fm_needs_fault_state(&f, false) && !fm_needs_fault_state(&f, true));
    fm_raise(&f, SS_ROW_VDC_INVALID, true, &c, &m, P());
    CHECK(fm_needs_fault_state(&f, true)); /* any other row still needs it */
}

void suite_fault_mgr(void)
{
    RUN(forced_spo_wins_and_blocks_asc);
    RUN(highest_rank_wins);
    RUN(fw15_retained_first_nvm_only_queued);
    RUN(one_authorised_retry_after_1s_then_latch);
    RUN(boot_blocks_on_recent_desat_record);
    RUN(uncommitted_retained_record_requeued_at_boot);
    RUN(keep_hv_until_asc_or_low_speed);
    RUN(latched_reset_only_below_n_x_and_never_for_desat);
    RUN(keep_hv_held_until_rule_a_and_no_safe_state_without_battery);
    RUN(battery_lost_is_a_fault_until_its_response_is_done);
}
