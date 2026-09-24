/* test_can_cmd.c — FW-11: E2E CRC, alive counter (frozen / jump), 20 ms staleness, BMS timeout,
 * direction interlock, status frame. */
#include "can_cmd.h"
#include "test.h"

static const ti_params_t *P(void) { return ti_params_get(TI_SKU_8XX_SIC); }

static hal_can_frame_t cmd(uint8_t ctr, float t)
{
    hal_can_frame_t f;
    can_encode_vcu_cmd(&f, ctr, TI_GEAR_D, true, false, t, TI_CONT_CLOSED, false, false, false, 60.0f);
    return f;
}

TEST(valid_frame_decoded)
{
    can_cmd_t c;
    can_cmd_init(&c);
    hal_can_frame_t f = cmd(3u, 123.4f);
    CHECK(can_cmd_rx(&c, &f, 100u, P()));
    CHECK_NEAR(c.torque_req_nm, 123.4, 0.05);
    CHECK(c.gear == TI_GEAR_D && c.enable_req && c.contactors == TI_CONT_CLOSED);
    CHECK_NEAR(c.coolant_c, 60.0, 0.5);
    CHECK(can_cmd_fresh(&c, 100u, P()));
}

TEST(crc_bad_rejected)
{
    can_cmd_t c;
    can_cmd_init(&c);
    hal_can_frame_t f = cmd(1u, 50.0f);
    f.data[3] ^= 0x01u;
    CHECK(!can_cmd_rx(&c, &f, 100u, P()));
    CHECK(c.n_crc == 1u && !can_cmd_fresh(&c, 100u, P()));
    f = cmd(1u, 50.0f);
    f.len = 4u;
    CHECK(!can_cmd_rx(&c, &f, 100u, P()));
}

TEST(frozen_counter_goes_stale_after_20ms)
{
    can_cmd_t c;
    can_cmd_init(&c);
    hal_can_frame_t f = cmd(7u, 80.0f);
    CHECK(can_cmd_rx(&c, &f, 1000u, P()));
    for (uint32_t t = 1010u; t <= 1100u; t += 10u) { /* the sender keeps repeating counter 7 */
        CHECK(!can_cmd_rx(&c, &f, t, P()));
    }
    CHECK(c.n_frozen >= 9u);
    CHECK(can_cmd_fresh(&c, 1020u, P()));  /* 20 ms: still fresh */
    CHECK(!can_cmd_fresh(&c, 1021u, P())); /* > 20 ms: stale */
}

TEST(counter_jump_rejected_then_resynced)
{
    can_cmd_t c;
    can_cmd_init(&c);
    hal_can_frame_t f = cmd(1u, 10.0f);
    CHECK(can_cmd_rx(&c, &f, 0u, P()));
    f = cmd(3u, 10.0f); /* one lost frame: accepted */
    CHECK(can_cmd_rx(&c, &f, 10u, P()));
    f = cmd(7u, 10.0f); /* jump of 4: rejected */
    CHECK(!can_cmd_rx(&c, &f, 20u, P()));
    f = cmd(8u, 10.0f);
    CHECK(can_cmd_rx(&c, &f, 30u, P()));
    f = cmd(9u, 10.0f); /* wrap-safe (counter 15 -> 0 covered by the mask) */
    CHECK(can_cmd_rx(&c, &f, 40u, P()));
}

TEST(bms_has_its_own_timeout)
{
    can_cmd_t c;
    can_cmd_init(&c);
    hal_can_frame_t f;
    can_encode_vcu_bms(&f, 0u, 750.0f, 50e3f, 200e3f);
    CHECK(can_cmd_rx(&c, &f, 0u, P()));
    CHECK_NEAR(c.v_pack, 750.0, 0.1);
    CHECK_NEAR(c.p_chg_w, 50e3, 100.0);
    for (uint32_t t = 10u; t <= 200u; t += 10u) {
        f = cmd((uint8_t)(t / 10u), 10.0f);
        (void)can_cmd_rx(&c, &f, t, P());
    }
    CHECK(can_cmd_fresh(&c, 200u, P()));
    CHECK(can_bms_fresh(&c, P()->cal_bms_timeout_ms, P()));
    CHECK(!can_bms_fresh(&c, P()->cal_bms_timeout_ms + 1u, P()));
}

TEST(direction_interlock)
{
    can_dir_t d = {0};
    const ti_params_t *p = P();
    CHECK(can_dir_interlock(&d, TI_GEAR_N, 100.0f, 0.0f, p) == 0.0f);
    CHECK(can_dir_interlock(&d, TI_GEAR_D, 100.0f, 0.0f, p) == 100.0f);
    CHECK(can_dir_interlock(&d, TI_GEAR_D, -50.0f, 0.0f, p) == 0.0f);    /* would drive backwards */
    CHECK(can_dir_interlock(&d, TI_GEAR_D, -50.0f, 500.0f, p) == -50.0f); /* regen while rolling forward */
    CHECK(can_dir_interlock(&d, TI_GEAR_R, -50.0f, 500.0f, p) == -50.0f); /* change refused at speed: still D */
    CHECK(d.change_refused && d.active == TI_GEAR_D);
    CHECK(can_dir_interlock(&d, TI_GEAR_R, -50.0f, 0.0f, p) == -50.0f);
    CHECK(d.active == TI_GEAR_R);
    CHECK(can_dir_interlock(&d, TI_GEAR_R, 50.0f, 0.0f, p) == 0.0f);      /* R never forwards */
    CHECK(can_dir_interlock(&d, TI_GEAR_R, 50.0f, -300.0f, p) == 50.0f);  /* braking while reversing */
    CHECK(can_dir_interlock(&d, TI_GEAR_R, NAN, 0.0f, p) == 0.0f);
}

TEST(status_frame_round_trip)
{
    const can_status_t s = {.state = 7u, .bridge = 3u, .hv = TI_HV_UNKNOWN, .keep_hv = true, .self_test_done = true,
                            .torque_nm = -12.3f, .speed_rpm = 4500.0f, .vdc_v = 712.5f, .vdc_valid = true};
    hal_can_frame_t f;
    can_status_encode(&s, 5u, &f);
    CHECK(f.id == CAN_ID_INV_STATUS && f.len == 16u);
    CHECK(can_e2e_crc(f.id, f.data, f.len) == f.data[0]);
    CHECK((f.data[1] & 0x0Fu) == 5u && (f.data[1] & 0x20u) != 0u && (f.data[1] & 0x10u) != 0u);
    CHECK((f.data[3] & 0x03u) == 3u && ((f.data[3] >> 2) & 0x03u) == (uint8_t)TI_HV_UNKNOWN);
    CHECK((uint16_t)(f.data[8] | (f.data[9] << 8)) == 7125u);
}

void suite_can_cmd(void)
{
    RUN(valid_frame_decoded);
    RUN(crc_bad_rejected);
    RUN(frozen_counter_goes_stale_after_20ms);
    RUN(counter_jump_rejected_then_resynced);
    RUN(bms_has_its_own_timeout);
    RUN(direction_interlock);
    RUN(status_frame_round_trip);
}
