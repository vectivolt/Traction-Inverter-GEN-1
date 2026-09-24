/* test_gate_selftest.c — FW-16 on the chain model: a healthy pass, every shutdown term stuck
 * permissive caught at its step, the <= 0.1 J energy precondition and the other conditions. */
#include "bridge.h"
#include "gate_selftest.h"
#include "gpio.h"
#include "harness.h"
#include "pwm.h"
#include "test.h"

static ti_params_t s_p;
static fs26_t s_fs;
static dis_t s_dis;
static bridge_t s_br;

static void ready(void)
{
    sim_reset();
    s_p = *ti_params_get(TI_SKU_8XX_SIC);
    br_init(&s_br, &s_p);
    sim_pwm_fault_route_bind(true); /* step h proves this route: bind it as the board would */
    (void)hal_pwm_init(10000u, s_p.dead_time_ns);
    CHECK(h_fs_ready(&s_fs, &s_p));
    hal_gpio_write(HAL_DO_EN_FLYBK_HS, true);
    hal_gpio_write(HAL_DO_EN_FLYBK_LS, true);
    (void)fs26_set_gpio1(&s_fs, true);
    br_asc_clear_pulse(); /* §9 step 5 at standstill: the POR FS1B preset is cleared before FW-16 */
    h_wait_ms(&s_fs, 200u);
    dis_init(&s_dis);
}

static st_cond_t cond(float v)
{
    const st_cond_t c = {.rdy_both = hal_gpio_read(HAL_DI_RDY_HS) && hal_gpio_read(HAL_DI_RDY_LS),
                         .vdc_both_valid = true, .v_ch = {v, v}, .contactors_open = true, .resolver_valid = true,
                         .speed_rpm = 0.0f, .n_ss_rpm = 110.0f, .pwm_low = (hal_pwm_mode() == HAL_PWM_OFF),
                         .flt_high = hal_gpio_read(HAL_DI_FLT_HS_N) && hal_gpio_read(HAL_DI_FLT_LS_N)};
    return c;
}

static st_res_t run(st_t *t, float v, st_cond_t *override)
{
    vdc_t link;
    vdc_init(&link);
    link.valid = true;
    link.ch_valid[0] = true;
    link.ch_valid[1] = true;
    st_res_t r = ST_RES_BUSY;
    for (int k = 0; k < 3000 && r == ST_RES_BUSY; k++) {
        const st_cond_t c = (override != NULL) ? *override : cond(v);
        r = st_step(t, &c, &s_br, &s_fs, &s_dis, &link, TI_CONT_OPEN, hal_time_ms(), &s_p);
        h_wait_ms(&s_fs, 1u);
        if (t->s == ST_TOPUP) {
            v *= 0.995f; /* the top-up drains the link */
        }
    }
    return r;
}

TEST(healthy_chain_passes_all_steps)
{
    ready();
    st_t t;
    st_init(&t);
    CHECK(run(&t, 1.0f, NULL) == ST_RES_PASS);
    CHECK(t.drop_hs_ms > 0u && t.drop_ls_ms > 0u);
    CHECK(!hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && !hal_gpio_read(HAL_DI_ASC_CMD_RB));
    CHECK(hal_pwm_fault_flags() == 0u && !st_in_step_h(&t));
    CHECK(!sim_fs26_fs0b_asserted() && sim_fs26_gpio1());
    CHECK(hal_gpio_read(HAL_DI_RDY_HS) && hal_gpio_read(HAL_DI_RDY_LS));
}

static st_state_t stuck_case(uint32_t mask)
{
    ready();
    sim_chain_stuck(mask);
    st_t t;
    st_init(&t);
    CHECK(run(&t, 1.0f, NULL) == ST_RES_FAIL);
    CHECK(!hal_gpio_out_state(HAL_DO_MCU_GATE_EN)); /* a failed test leaves the gates disabled */
    return t.failed;
}

TEST(each_term_stuck_permissive_is_detected)
{
    CHECK(stuck_case(SIM_STUCK_FS0B_TERM) == ST_A);
    CHECK(stuck_case(SIM_STUCK_FS1B_PRESET) == ST_A);
    CHECK(stuck_case(SIM_STUCK_CHAIN_LOW) == ST_B);
    CHECK(stuck_case(SIM_STUCK_ASC_CLR_DEAD) == ST_B);
    CHECK(stuck_case(SIM_STUCK_MCU_EN_TERM) == ST_C);
    CHECK(stuck_case(SIM_STUCK_RDY_HS_TERM) == ST_D_DROP);
    CHECK(stuck_case(SIM_STUCK_RDY_LS_TERM) == ST_E_DROP);
    CHECK(stuck_case(SIM_STUCK_ASC_CLK_DEAD) == ST_F);
    CHECK(stuck_case(SIM_STUCK_GPIO1_OR_DEAD) == ST_G);
    CHECK(stuck_case(SIM_STUCK_FLTOK_TERM) == ST_H);
    CHECK(stuck_case(SIM_STUCK_UASCG) == ST_H);
    CHECK(stuck_case(SIM_STUCK_PWM_FAULT_ROUTE) == ST_H);
}

TEST(energy_precondition)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    st_cond_t c = {.vdc_both_valid = true, .v_ch = {2.0f, 2.5f}};
    CHECK(st_energy(&c, p) == ST_ENERGY_OK);
    c.v_ch[1] = 3.5f;
    CHECK(st_energy(&c, p) == ST_ENERGY_TOPUP); /* between 3 V and 60 V: QDIS for 2 tau first */
    c.v_ch[0] = 65.0f;
    CHECK(st_energy(&c, p) == ST_ENERGY_REFUSE); /* the test is refused (skipped) */
    c.v_ch[0] = 1.0f;
    c.v_ch[1] = 1.0f;
    c.vdc_both_valid = false; /* VOFS/V5GD out of window: a blind witness is not zero energy */
    CHECK(st_energy(&c, p) == ST_ENERGY_REFUSE);
}

TEST(topup_then_run_and_not_counted)
{
    ready();
    st_t t;
    st_init(&t);
    const uint64_t t0 = sim_now_ns();
    CHECK(run(&t, 40.0f, NULL) == ST_RES_PASS);
    CHECK(sim_gpio_edge_ns(HAL_DO_QDIS, true, t0) == UINT64_MAX); /* QDIS output is the app's job */
    CHECK(s_dis.n_fire == 0u);                                     /* the top-up is not an FW-17 fire */
    CHECK(sim_now_ns() - t0 >= 1350000000u);                       /* >= 2 tau (8XX) spent first */
}

TEST(skipped_rather_than_run_on_assumption)
{
    ready();
    st_t t;
    st_init(&t);
    CHECK(run(&t, 70.0f, NULL) == ST_RES_SKIP); /* >= 60 V */
    st_cond_t c = cond(1.0f);
    c.contactors_open = false;
    st_init(&t);
    CHECK(run(&t, 1.0f, &c) == ST_RES_SKIP);
    c = cond(1.0f);
    c.speed_rpm = 500.0f; /* not standstill */
    st_init(&t);
    CHECK(run(&t, 1.0f, &c) == ST_RES_SKIP);
    c = cond(1.0f);
    c.rdy_both = false;
    st_init(&t);
    CHECK(run(&t, 1.0f, &c) == ST_RES_SKIP);
    c = cond(1.0f);
    c.resolver_valid = false;
    st_init(&t);
    CHECK(run(&t, 1.0f, &c) == ST_RES_SKIP);
    CHECK(!hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
}

/* A12-R05 inside FW-16: a real DESAT during the test (EN high since step a). Step c's EN drop and the
 * failed test's EN drop both wait for the DESAT hold, like every other software path. */
TEST(failed_test_with_a_desat_drops_en_only_after_the_hold)
{
    ready();
    st_t t;
    st_init(&t);
    vdc_t link;
    vdc_init(&link);
    link.valid = true;
    link.ch_valid[0] = true;
    link.ch_valid[1] = true;
    for (int k = 0; (k < 50) && (t.s != ST_C); k++) {
        const st_cond_t c = cond(1.0f);
        (void)st_step(&t, &c, &s_br, &s_fs, &s_dis, &link, TI_CONT_OPEN, hal_time_ms(), &s_p);
        h_wait_ms(&s_fs, 1u);
    }
    CHECK(t.s == ST_C && hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && hal_gpio_read(HAL_DI_DRV_EN_RB));
    const uint64_t t_flt = sim_now_ns();
    sim_chain_desat(false, false);
    const st_cond_t c = cond(1.0f);
    const st_res_t r = st_step(&t, &c, &s_br, &s_fs, &s_dis, &link, TI_CONT_OPEN, hal_time_ms(), &s_p);
    CHECK(r == ST_RES_FAIL && t.failed == ST_C);
    CHECK(hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && hal_pwm_mode() == HAL_PWM_OFF); /* held */
    sim_advance_us(s_p.cal_desat_en_hold_us);
    br_service(&s_br);
    const uint64_t t_en = sim_gpio_edge_ns(HAL_DO_MCU_GATE_EN, false, t_flt);
    CHECK(!hal_gpio_out_state(HAL_DO_MCU_GATE_EN) && t_en != UINT64_MAX &&
          (t_en - t_flt) >= (uint64_t)s_p.cal_desat_en_hold_us * 1000u);
}

void suite_gate_selftest(void)
{
    RUN(healthy_chain_passes_all_steps);
    RUN(each_term_stuck_permissive_is_detected);
    RUN(energy_precondition);
    RUN(topup_then_run_and_not_counted);
    RUN(skipped_rather_than_run_on_assumption);
    RUN(failed_test_with_a_desat_drops_en_only_after_the_hold);
}
