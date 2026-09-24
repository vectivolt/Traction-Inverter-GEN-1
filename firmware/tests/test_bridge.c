/* test_bridge.c — §4c PWM-ASC entry order, FW-06a exit, the guard, FW-15 recovery (§7). */
#include "bridge.h"
#include "gpio.h"
#include "harness.h"
#include "pwm.h"
#include "test.h"

/* gate power up and FS0B released so DRV_EN can follow MCU_GATE_EN */
static void chain_ready(fs26_t *fs, ti_params_t *p)
{
    CHECK(h_fs_ready(fs, p));
    hal_gpio_write(HAL_DO_EN_FLYBK_HS, true);
    hal_gpio_write(HAL_DO_EN_FLYBK_LS, true);
    h_wait_ms(fs, 200u);
    CHECK(hal_gpio_read(HAL_DI_RDY_HS) && hal_gpio_read(HAL_DI_RDY_LS));
}

TEST(pwm_asc_entry_is_break_before_make)
{
    ti_params_t p = *ti_params_get(TI_SKU_8XX_IGBT); /* 2.5 us dead time */
    fs26_t fs;
    chain_ready(&fs, &p);
    bridge_t b;
    br_init(&b);
    CHECK(br_arm_idle(&b));
    (void)hal_pwm_init(5000u, p.dead_time_ns);
    const float d[3] = {0.6f, 0.4f, 0.5f};
    CHECK(br_modulate(&b, d, &p));
    const uint64_t t0 = sim_now_ns();
    br_enter_pwm_asc(&b, hal_time_us(), &p);
    const uint64_t t_req = sim_gpio_edge_ns(HAL_DO_ASC_REQ, true, t0);
    CHECK(t_req != UINT64_MAX);
    CHECK(sim_pwm_hs_off_ns() <= t_req);                              /* 1) high sides off first */
    CHECK(sim_pwm_asc_set_ns() >= sim_pwm_hs_off_ns() + 2500u);      /* 3) LS on >= dead time later */
    CHECK(b.mode == BR_ASC && hal_pwm_mode() == HAL_PWM_ASC && hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    CHECK(hal_gpio_read(HAL_DI_ASC_CMD_RB)); /* latch set */
    CHECK(sim_chain_ls_on() && !sim_chain_hs_on());
}

TEST(asc_exit_only_when_allowed_and_hs_after_1us)
{
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    fs26_t fs;
    chain_ready(&fs, &p);
    bridge_t b;
    br_init(&b);
    br_enter_pwm_asc(&b, hal_time_us(), &p);
    CHECK(!br_exit_asc(&b, false)); /* at n >= n_x without the battery: refused */
    CHECK(b.mode == BR_ASC && hal_gpio_read(HAL_DI_ASC_CMD_RB));
    const uint64_t t0 = sim_now_ns();
    CHECK(br_exit_asc(&b, true));
    const uint64_t t_clr = sim_gpio_edge_ns(HAL_DO_ASC_CLR_N, false, t0);
    CHECK(t_clr != UINT64_MAX);
    CHECK(!hal_gpio_read(HAL_DI_ASC_CMD_RB));
    const float d[3] = {0.5f, 0.5f, 0.5f};
    CHECK(br_modulate(&b, d, &p));
    CHECK(sim_now_ns() >= t_clr + 1000u); /* first HS pulse >= 1 us after the clear */
    CHECK(!br_exit_asc(&b, true));        /* not in ASC any more */
}

TEST(guard_refuses_nonfinite_duty)
{
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    bridge_t b;
    br_init(&b);
    (void)br_arm_idle(&b);
    const float bad[3] = {0.5f, NAN, 0.5f};
    CHECK(!br_modulate(&b, bad, &p));
    CHECK(hal_pwm_mode() == HAL_PWM_OFF && sim_pwm_nan_writes() == 0u);
    const float over[3] = {0.5f, 1.2f, 0.5f};
    CHECK(!br_modulate(&b, over, &p));
    br_enter_pwm_asc(&b, hal_time_us(), &p);
    const float ok[3] = {0.5f, 0.5f, 0.5f};
    CHECK(!br_modulate(&b, ok, &p)); /* never modulate out of ASC without the exit sequence */
}

TEST(fw15_recovery_waits_1p5ms_and_clears)
{
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    fs26_t fs;
    chain_ready(&fs, &p);
    bridge_t b;
    br_init(&b);
    CHECK(br_arm_idle(&b));
    hal_gpio_write(HAL_DO_ASC_REQ, true); /* a latch left set before the fault */
    sim_advance_us(10u);
    const uint64_t t_fault = sim_now_ns();
    sim_chain_desat(true, false);
    sim_advance_us(100u);
    CHECK(!hal_gpio_read(HAL_DI_DRV_EN_RB) && !hal_gpio_read(HAL_DI_ASC_CMD_RB)); /* latch + UASCG */
    CHECK((hal_pwm_fault_flags() & HAL_PWM_FAULT_FLT_HS) != 0u);
    br_rec_start(&b, (uint32_t)(t_fault / 1000u));
    br_rec_t r = BR_REC_WAIT_LOW;
    for (int k = 0; k < 10 && r == BR_REC_WAIT_LOW; k++) {
        h_wait_ms(&fs, 1u);
        r = br_rec_step(&b, hal_time_us(), &p);
    }
    const uint64_t t_pulse = sim_gpio_edge_ns(HAL_DO_FLT_CLR, false, t_fault);
    CHECK(t_pulse != UINT64_MAX && (t_pulse - t_fault) >= 1500000u); /* >= 1.5 ms with DRV_EN low */
    CHECK(sim_gpio_edge_ns(HAL_DO_ASC_CLR_N, false, t_fault) < t_pulse); /* ASC cleared first */
    r = br_rec_step(&b, hal_time_us(), &p);
    CHECK(r == BR_REC_DONE);
    CHECK(hal_gpio_read(HAL_DI_FLT_HS_N) && hal_gpio_read(HAL_DI_DRV_EN_RB) && !hal_gpio_read(HAL_DI_ASC_CMD_RB));
    CHECK((hal_pwm_fault_flags() & HAL_PWM_FAULT_FLT_HS) == 0u && hal_pwm_mode() == HAL_PWM_OFF);
}

TEST(fw15_recovery_fails_safe_on_a_hard_short)
{
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    fs26_t fs;
    chain_ready(&fs, &p);
    bridge_t b;
    br_init(&b);
    (void)br_arm_idle(&b);
    const uint64_t t_fault = sim_now_ns();
    sim_chain_desat(false, true); /* the driver re-trips at once */
    br_rec_start(&b, (uint32_t)(t_fault / 1000u));
    br_rec_t r = BR_REC_WAIT_LOW;
    for (int k = 0; k < 10 && r == BR_REC_WAIT_LOW; k++) {
        h_wait_ms(&fs, 1u);
        r = br_rec_step(&b, hal_time_us(), &p);
    }
    r = br_rec_step(&b, hal_time_us(), &p);
    CHECK(r == BR_REC_FAIL);
    CHECK(!hal_gpio_read(HAL_DI_DRV_EN_RB) && !hal_gpio_out_state(HAL_DO_MCU_GATE_EN));
    CHECK((hal_pwm_fault_flags() & HAL_PWM_FAULT_FLT_LS) != 0u);
}

void suite_bridge(void)
{
    RUN(pwm_asc_entry_is_break_before_make);
    RUN(asc_exit_only_when_allowed_and_hs_after_1us);
    RUN(guard_refuses_nonfinite_duty);
    RUN(fw15_recovery_waits_1p5ms_and_clears);
    RUN(fw15_recovery_fails_safe_on_a_hard_short);
}
