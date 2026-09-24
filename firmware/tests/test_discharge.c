/* test_discharge.c — FW-17/18/19 and the FW-02 tau plausibility. */
#include "discharge.h"
#include "dtc.h"
#include "test.h"

static const ti_params_t *P(void) { return ti_params_get(TI_SKU_8XX_SIC); }

static vdc_t link(float v1, float v2, bool valid)
{
    vdc_t v;
    vdc_init(&v);
    v.v_ch[0] = v1;
    v.v_ch[1] = v2;
    v.ch_valid[0] = valid;
    v.ch_valid[1] = valid;
    v.valid = valid;
    v.vdc = 0.5f * (v1 + v2);
    v.hv = !valid ? TI_HV_UNKNOWN : ((v.vdc < 60.0f) ? TI_HV_SAFE : TI_HV_PRESENT);
    return v;
}

TEST(fires_only_with_contactors_reported_open)
{
    dis_t d;
    dis_init(&d);
    const vdc_t v = link(700.0f, 700.0f, true);
    CHECK(dis_request(&d, TI_CONT_CLOSED, &v, true, 0u, P()) == DIS_REQ_CONTACTORS);
    CHECK(dis_request(&d, TI_CONT_PRECHARGE, &v, true, 0u, P()) == DIS_REQ_CONTACTORS);
    CHECK(dis_request(&d, TI_CONT_INVALID, &v, true, 0u, P()) == DIS_REQ_CONTACTORS);
    CHECK(!dis_output(&d));
    CHECK(dis_request(&d, TI_CONT_OPEN, &v, true, 0u, P()) == DIS_REQ_OK);
    CHECK(dis_output(&d));
    dis_step(&d, TI_CONT_CLOSED, &v, 10u, P()); /* the report changes: release at once */
    CHECK(!dis_output(&d) && d.st == DIS_ABORTED);
}

static void run_decay(dis_t *d, float v0, float tau, uint32_t ms, uint32_t t0)
{
    for (uint32_t t = 1u; t <= ms; t++) {
        const float v = v0 * expf(-(float)t * 1e-3f / tau);
        const vdc_t l = link(v, v, true);
        dis_step(d, TI_CONT_OPEN, &l, t0 + t, P());
    }
}

TEST(normal_discharge_auto_release_and_tau_ok)
{
    dis_t d;
    dis_init(&d);
    const vdc_t v = link(800.0f, 800.0f, true);
    CHECK(dis_request(&d, TI_CONT_OPEN, &v, true, 0u, P()) == DIS_REQ_OK);
    run_decay(&d, 800.0f, 0.60f, 300u, 0u);
    CHECK(!d.stuck_off && !d.tau_mismatch && dis_output(&d));
    CHECK_NEAR(d.tau_meas_s, 0.60, 0.02);
    run_decay(&d, 800.0f * expf(-0.5f), 0.60f, 2000u, 300u); /* reaches < 60 V: done */
    CHECK(d.st == DIS_DONE && !dis_output(&d));
}

TEST(auto_release_after_5s)
{
    dis_t d;
    dis_init(&d);
    const vdc_t v = link(800.0f, 800.0f, true);
    (void)dis_request(&d, TI_CONT_OPEN, &v, true, 0u, P());
    run_decay(&d, 800.0f, 30.0f, 199u, 0u); /* slow decay (still > 10 % drop? no) */
    d.witness_done = true;                  /* isolate the 5 s rule from the witness */
    for (uint32_t t = 200u; t < 5000u; t++) {
        dis_step(&d, TI_CONT_OPEN, &v, t, P());
    }
    CHECK(dis_output(&d));
    dis_step(&d, TI_CONT_OPEN, &v, 5000u, P());
    CHECK(!dis_output(&d) && d.st == DIS_DONE);
}

TEST(stuck_off_detected_in_200ms)
{
    dis_t d;
    dis_init(&d);
    dtc_init();
    const vdc_t v = link(800.0f, 800.0f, true);
    (void)dis_request(&d, TI_CONT_OPEN, &v, true, 0u, P());
    for (uint32_t t = 1u; t <= 200u; t++) {
        dis_step(&d, TI_CONT_OPEN, &v, t, P()); /* no decay */
    }
    CHECK(d.stuck_off && dtc_active(DTC_QDIS_STUCK_OFF) && !dis_output(&d));
}

TEST(wrong_bank_tau_is_a_dtc)
{
    dis_t d;
    dis_init(&d);
    dtc_init();
    const vdc_t v = link(800.0f, 800.0f, true);
    (void)dis_request(&d, TI_CONT_OPEN, &v, true, 0u, P());
    run_decay(&d, 800.0f, 1.5f, 250u, 0u); /* a 1.5 s bank behind the 8XX string */
    CHECK(d.tau_mismatch && dtc_active(DTC_TAU_MISMATCH) && !d.stuck_off);
}

TEST(invalid_witness_means_unknown_never_safe)
{
    dis_t d;
    dis_init(&d);
    dtc_init();
    vdc_t v = link(0.0f, 0.0f, false); /* e.g. VOFS out of window */
    CHECK(dis_hv_state(&v) == TI_HV_UNKNOWN);
    (void)dis_request(&d, TI_CONT_OPEN, &v, true, 0u, P());
    for (uint32_t t = 1u; t <= 300u; t++) {
        dis_step(&d, TI_CONT_OPEN, &v, t, P());
    }
    CHECK(!d.stuck_off && (dis_hv_state(&v) == TI_HV_UNKNOWN)); /* no verdict on a blind witness */
    v = link(10.0f, 10.0f, true);
    CHECK(dis_hv_state(&v) == TI_HV_SAFE);
}

TEST(three_per_five_minutes_and_uncounted_topup)
{
    dis_t d;
    dis_init(&d);
    dtc_init();
    const vdc_t v = link(100.0f, 100.0f, true);
    for (uint32_t k = 0u; k < 3u; k++) {
        CHECK(dis_request(&d, TI_CONT_OPEN, &v, true, k * 10000u, P()) == DIS_REQ_OK);
        dis_abort(&d);
    }
    CHECK(dis_request(&d, TI_CONT_OPEN, &v, true, 40000u, P()) == DIS_REQ_RATE);
    CHECK(dtc_active(DTC_QDIS_RATE_LIMIT));
    CHECK(dis_request(&d, TI_CONT_OPEN, &v, false, 41000u, P()) == DIS_REQ_OK); /* FW-16 top-up */
    dis_abort(&d);
    CHECK(dis_request(&d, TI_CONT_OPEN, &v, true, 299999u, P()) == DIS_REQ_RATE);
    CHECK(dis_request(&d, TI_CONT_OPEN, &v, true, 300000u, P()) == DIS_REQ_OK); /* first fire aged out */
}

TEST(stuck_on_seen_as_fast_decay_with_qdis_off)
{
    dis_t d;
    dis_init(&d);
    dtc_init();
    for (uint32_t t = 0u; t <= 1001u; t++) { /* QDIS never commanded, contactors open, tau 0.6 s */
        const float vv = 800.0f * expf(-(float)t * 1e-3f / 0.6f);
        const vdc_t l = link(vv, vv, true);
        dis_step(&d, TI_CONT_OPEN, &l, t, P());
    }
    CHECK(d.stuck_on && dtc_active(DTC_QDIS_STUCK_ON));
    dis_init(&d);
    dtc_init();
    for (uint32_t t = 0u; t <= 2001u; t++) { /* the passive bleeder alone: 66 k x 323 uF */
        const float vv = 800.0f * expf(-(float)t * 1e-3f / 21.3f);
        const vdc_t l = link(vv, vv, true);
        dis_step(&d, TI_CONT_OPEN, &l, t, P());
    }
    CHECK(!d.stuck_on);
}

static pch_result_t precharge(float plateau, float tau, float v_pack)
{
    pch_t c;
    pch_init(&c);
    pch_result_t r = PCH_IDLE;
    for (uint32_t t = 0u; t < 3000u; t++) {
        const float vv = v_pack * plateau * (1.0f - expf(-(float)t * 1e-3f / tau));
        const vdc_t l = link(vv, vv, true);
        r = pch_step(&c, TI_CONT_PRECHARGE, &l, v_pack, true, t, P());
        if ((r != PCH_RUNNING) && (r != PCH_IDLE)) {
            break;
        }
    }
    return r;
}

TEST(fw19_precharge_plateau_and_tau)
{
    dtc_init();
    CHECK(precharge(0.997f, 0.10f, 750.0f) == PCH_OK);
    CHECK(precharge(0.95f, 0.10f, 750.0f) == PCH_REFUSE_PLATEAU); /* shorted QDIS: ~5 % low */
    CHECK(dtc_active(DTC_PRECHARGE_PLATEAU));
    CHECK(precharge(0.997f, 0.005f, 750.0f) == PCH_REFUSE_TAU);    /* charges far too fast */
    pch_t c;
    pch_init(&c);
    const vdc_t l = link(0.0f, 0.0f, true);
    pch_result_t r = PCH_IDLE;
    for (uint32_t t = 0u; t <= P()->cal_precharge_timeout_ms; t++) {
        r = pch_step(&c, TI_CONT_PRECHARGE, &l, 750.0f, true, t, P());
    }
    CHECK(r == PCH_REFUSE_TIMEOUT);
}

void suite_discharge(void)
{
    RUN(fires_only_with_contactors_reported_open);
    RUN(normal_discharge_auto_release_and_tau_ok);
    RUN(auto_release_after_5s);
    RUN(stuck_off_detected_in_200ms);
    RUN(wrong_bank_tau_is_a_dtc);
    RUN(invalid_witness_means_unknown_never_safe);
    RUN(three_per_five_minutes_and_uncounted_topup);
    RUN(stuck_on_seen_as_fast_decay_with_qdis_off);
    RUN(fw19_precharge_plateau_and_tau);
}
