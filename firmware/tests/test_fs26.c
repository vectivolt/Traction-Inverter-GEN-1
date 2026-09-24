/* test_fs26.c — FW-12 against the FS26 device model: OTP/variant readback, INIT registers and
 * NOT pairs, challenger watchdog (missed and wrong answers -> FS0B), release, FS0B_REQ, GPIO1,
 * LPOFF, SPI integrity. */
#include "fs26.h"
#include "harness.h"
#include "spi_fs26.h"
#include "test.h"

static ti_params_t bound(void)
{
    ti_params_t p = *ti_params_get(TI_SKU_8XX_SIC);
    p.cal_fs26_prog_id = 0x4A21u;
    return p;
}

TEST(protocol_math_datasheet_example)
{
    /* Table 198: token 0xE4F0, release both -> 0xB0D8 */
    CHECK(fs26_release_word(0xE4F0u, FS26_REL_BOTH) == 0xB0D8u);
    CHECK((fs26_release_word(0xE4F0u, FS26_REL_FS0B) >> 13) == 3u);
    /* 32-bit intermediate: NOT(4t + 2) / 4 truncated = ~t */
    CHECK(fs26_wd_answer(0x5AB2u) == (uint16_t)~0x5AB2u);
    CHECK(fs26_wd_cfg_value() == 0xC200u); /* WD_ERR_LIMIT 11 (=2), RFR 6, reaction RSTB+FS0B */
    CHECK((fs26_fssm_value() & FS26_BACKUP_FS0B_BIT) != 0u && (fs26_fssm_value() & FS26_BACKUP_FS1B_BIT) == 0u);
    CHECK((fs26_wdw_value() >> 12) == 3u);  /* 3 ms window */
    CHECK(fs26_ios2_value() == 0x000Bu);    /* TDELAY 0, TDUR 100 ms */
}

TEST(init_readback_ok)
{
    ti_params_t p = bound();
    fs26_t f;
    const sim_fs26_cfg_t c = {.prog_id = 0x4A21u, .device_id = 0x2600u};
    sim_fs26_config(&c);
    sim_fs26_reset();
    CHECK(fs26_init(&f, &p) == FS26_OK);
    CHECK(f.init_done && f.wd_running && !f.fs1b_short_high);
    CHECK(sim_fs26_state() == FS26_STATE_SAFETY_OUT_NOT_RELEASED); /* INIT closed by the first answer */
    uint16_t v = 0u;
    CHECK(fs26_read(&f, FS26_FS_I_WD_CFG, &v) == FS26_OK && ((v >> 14) & 3u) == 3u);
    CHECK(fs26_read(&f, FS26_FS_I_FSSM, &v) == FS26_OK && (v & FS26_BACKUP_FS1B_BIT) == 0u);
    CHECK(fs26_read(&f, FS26_FS_STATES, &v) == FS26_OK && (v & FS26_STATES_REG_CORRUPT) == 0u);
    /* INIT registers are read-only afterwards */
    CHECK(fs26_write(&f, FS26_FS_I_WD_CFG, 0x4200u) == FS26_OK);
    CHECK(fs26_read(&f, FS26_FS_I_WD_CFG, &v) == FS26_OK && ((v >> 14) & 3u) == 3u);
}

TEST(variant_otp_and_debug_refused)
{
    fs26_t f;
    ti_params_t p = bound();
    sim_fs26_cfg_t c = {.prog_id = 0x1111u, .device_id = 0x2600u};
    sim_fs26_config(&c);
    sim_fs26_reset();
    CHECK(fs26_init(&f, &p) == FS26_ERR_PROGID);
    p.cal_fs26_prog_id = 0xFFFFu; /* unbound variant: never arms */
    c.prog_id = 0xFFFFu;
    sim_fs26_config(&c);
    sim_fs26_reset();
    CHECK(fs26_init(&f, &p) == FS26_ERR_PROGID);
    p = bound();
    c = (sim_fs26_cfg_t){.prog_id = 0x4A21u, .otp_corrupt = true};
    sim_fs26_config(&c);
    sim_fs26_reset();
    CHECK(fs26_init(&f, &p) == FS26_ERR_OTP);
    c = (sim_fs26_cfg_t){.prog_id = 0x4A21u, .dbg_mode = true};
    sim_fs26_config(&c);
    sim_fs26_reset();
    CHECK(fs26_init(&f, &p) == FS26_ERR_DEBUG);
}

TEST(refresh_keeps_fs0b_released)
{
    ti_params_t p = bound();
    fs26_t f;
    CHECK(h_fs_ready(&f, &p));
    CHECK(!sim_fs26_fs0b_asserted() && !sim_fs26_fs1b_asserted());
    h_wait_ms(&f, 500u);
    CHECK(!sim_fs26_fs0b_asserted() && sim_fs26_wd_err_cnt() == 0u);
    CHECK(f.n_refresh >= 240u);
}

TEST(watchdog_missed_asserts_fs0b_within_two_windows)
{
    ti_params_t p = bound();
    fs26_t f;
    CHECK(h_fs_ready(&f, &p));
    const uint64_t t0 = sim_now_ns();
    uint64_t t_fs0b = 0u;
    for (uint32_t us = 0u; us < 10000u; us += 100u) { /* runaway code: no refresh */
        sim_advance_us(100u);
        if (sim_fs26_fs0b_asserted() && (t_fs0b == 0u)) {
            t_fs0b = sim_now_ns();
        }
    }
    CHECK(t_fs0b != 0u);
    CHECK((t_fs0b - t0) <= 6000000u); /* WD_ERR_LIMIT 2: the first timeout (+2) asserts FS0B */
    CHECK(sim_fs26_fs1b_asserted());   /* FS1B_TDELAY 0: with FS0B */
    CHECK(sim_fs26_rstb_event());      /* WD_FS_REACTION = RSTB + FS0B */
}

TEST(wrong_answer_counts_and_asserts)
{
    ti_params_t p = bound();
    fs26_t f;
    CHECK(h_fs_ready(&f, &p));
    sim_advance_us(2000u);
    CHECK(fs26_write(&f, FS26_FS_WD_ANSWER, 0x1234u) == FS26_OK);
    CHECK(sim_fs26_wd_err_cnt() == 2u && sim_fs26_fs0b_asserted());
}

TEST(early_answer_in_closed_window_is_an_error)
{
    ti_params_t p = bound();
    fs26_t f;
    CHECK(h_fs_ready(&f, &p));
    sim_advance_us(200u);
    (void)fs26_wd_refresh(&f); /* inside the closed half */
    CHECK(sim_fs26_fs0b_asserted());
}

TEST(release_waits_for_fault_error_counter)
{
    ti_params_t p = bound();
    fs26_t f;
    const sim_fs26_cfg_t c = {.prog_id = 0x4A21u};
    sim_fs26_config(&c);
    sim_fs26_reset();
    CHECK(fs26_init(&f, &p) == FS26_OK);
    CHECK(fs26_release_safety_outputs(&f) == FS26_BUSY); /* FLT_ERR_CNT 1 after POR */
    CHECK(sim_fs26_fs0b_asserted());
    for (int k = 0; k < 5; k++) {
        sim_advance_us(2000u);
        (void)fs26_wd_refresh(&f);
    }
    CHECK(fs26_release_safety_outputs(&f) == FS26_OK);
    CHECK(!sim_fs26_fs0b_asserted() && fs26_outputs_released(&f));
}

TEST(fs0b_req_and_fs1b_with_it)
{
    ti_params_t p = bound();
    fs26_t f;
    CHECK(h_fs_ready(&f, &p));
    CHECK(fs26_request_fs0b(&f) == FS26_OK);
    CHECK(sim_fs26_fs0b_asserted() && sim_fs26_fs1b_asserted());
    CHECK(fs26_release_safety_outputs(&f) == FS26_OK);
    CHECK(!sim_fs26_fs0b_asserted() && !sim_fs26_fs1b_asserted());
}

TEST(fs1b_short_to_high_flagged)
{
    ti_params_t p = bound();
    fs26_t f;
    const sim_fs26_cfg_t c = {.prog_id = 0x4A21u, .fs1b_short_high = true};
    sim_fs26_config(&c);
    sim_fs26_reset();
    CHECK(fs26_init(&f, &p) == FS26_OK);
    CHECK(f.fs1b_short_high);
}

TEST(gpio1_and_lpoff)
{
    ti_params_t p = bound();
    fs26_t f;
    CHECK(h_fs_ready(&f, &p));
    CHECK(!sim_fs26_gpio1());
    CHECK(fs26_set_gpio1(&f, true) == FS26_OK && sim_fs26_gpio1());
    CHECK(fs26_set_gpio1(&f, false) == FS26_OK && !sim_fs26_gpio1());
    CHECK(fs26_goto_lpoff(&f) == FS26_OK && sim_fs26_lpoff() && !f.wd_running);
}

TEST(corrupted_response_never_becomes_a_value)
{
    ti_params_t p = bound();
    fs26_t f;
    CHECK(h_fs_ready(&f, &p));
    uint16_t v = 0xDEADu;
    sim_fs26_corrupt_next_miso();
    CHECK(fs26_read(&f, FS26_M_PROGID, &v) == FS26_ERR_CRC && v == 0xDEADu);
    CHECK(fs26_read(&f, FS26_M_PROGID, &v) == FS26_OK && v == 0x4A21u);
    const uint32_t errs = sim_fs26_crc_errors();
    uint32_t rx;
    CHECK(hal_fs26_xfer(fs26_frame(FS26_M_REG_CTRL1, true, FS26_GPIO1_BIT) ^ 0x1u, &rx)); /* bad MOSI CRC */
    CHECK(sim_fs26_crc_errors() == errs + 1u && !sim_fs26_gpio1()); /* dropped */
}

void suite_fs26(void)
{
    RUN(protocol_math_datasheet_example);
    RUN(init_readback_ok);
    RUN(variant_otp_and_debug_refused);
    RUN(refresh_keeps_fs0b_released);
    RUN(watchdog_missed_asserts_fs0b_within_two_windows);
    RUN(wrong_answer_counts_and_asserts);
    RUN(early_answer_in_closed_window_is_an_error);
    RUN(release_waits_for_fault_error_counter);
    RUN(fs0b_req_and_fs1b_with_it);
    RUN(fs1b_short_to_high_flagged);
    RUN(gpio1_and_lpoff);
    RUN(corrupted_response_never_becomes_a_value);
}
