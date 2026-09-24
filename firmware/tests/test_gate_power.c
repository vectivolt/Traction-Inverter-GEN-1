/* test_gate_power.c — FW-14 sequencing: never with FS1B asserted, RDY -> no PWM, recovery. */
#include "dtc.h"
#include "gate_power.h"
#include "gpio.h"
#include "harness.h"
#include "pwm.h"
#include "test.h"

TEST(refused_while_fs1b_asserted)
{
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    fs26_t fs;
    const sim_fs26_cfg_t c = {.prog_id = 0x4A21u};
    sim_fs26_config(&c);
    sim_fs26_reset();
    p.cal_fs26_prog_id = 0x4A21u;
    CHECK(fs26_init(&fs, &p) == FS26_OK);
    gp_t g;
    gp_init(&g);
    CHECK(!gp_request_on(&g, &fs, false, 0u));
    CHECK(!sim_fs26_gpio1() && !hal_gpio_out_state(HAL_DO_EN_FLYBK_HS)); /* FS_GPIO1 stays low from POR */
}

TEST(start_ready_loss_and_recovery)
{
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    fs26_t fs;
    CHECK(h_fs_ready(&fs, &p));
    gp_t g;
    gp_init(&g);
    CHECK(gp_request_on(&g, &fs, true, hal_time_us() / 1000u));
    CHECK(sim_fs26_gpio1() && hal_gpio_out_state(HAL_DO_EN_FLYBK_HS) && hal_gpio_out_state(HAL_DO_EN_FLYBK_LS));
    for (int k = 0; k < 200; k++) {
        h_wait_ms(&fs, 1u);
        (void)gp_step(&g, hal_time_us() / 1000u, &p);
    }
    CHECK(g.st == GP_READY);
    hal_pwm_set_asc(); /* something is driving */
    sim_set_v5gd(0.5f); /* supply collapse: RDY reads low */
    (void)gp_step(&g, hal_time_us() / 1000u, &p);
    CHECK(g.st == GP_LOST && hal_pwm_mode() == HAL_PWM_OFF && dtc_active(DTC_GATE_POWER));
    sim_set_v5gd(5.0f);
    (void)gp_step(&g, hal_time_us() / 1000u, &p);
    CHECK(g.st == GP_READY); /* undervoltage recovers with RDY, no DESAT bookkeeping */
}

TEST(start_timeout)
{
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    fs26_t fs;
    CHECK(h_fs_ready(&fs, &p));
    sim_chain_rdy_timing(900u, 3u); /* rails never make it inside the window */
    gp_t g;
    gp_init(&g);
    CHECK(gp_request_on(&g, &fs, true, hal_time_us() / 1000u));
    for (int k = 0; k < 450; k++) {
        h_wait_ms(&fs, 1u);
        (void)gp_step(&g, hal_time_us() / 1000u, &p);
    }
    CHECK(g.st == GP_LOST && g.timeout_dtc);
}

void suite_gate_power(void)
{
    RUN(refused_while_fs1b_asserted);
    RUN(start_ready_loss_and_recovery);
    RUN(start_timeout);
}
