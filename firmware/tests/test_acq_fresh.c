/* test_acq_fresh.c — round 24 (F241): the acquisition freshness of the slow-list inputs (docs/firmware-contract.md
 * §10n). hal_adc_read() returns the last conversion and its stamp until the next one — forever after a converter stops —
 * and the task took every read as a new sample at its own time: a stopped channel stayed valid at its last value. Now
 * each read is judged against the stamp its consumer last took (ti_acq): NEW is taken, HELD (the same conversion,
 * younger than cal_temp_hold_ms) keeps the consumer's verdict and counts nothing again, EXPIRED (older, or never
 * converted) withdraws the reading. On the host a converter stops with sim_adc_freeze() — its code and stamp stay those
 * of the freeze while the input moves on — and one that never ran is sim_adc_never(). The module-level test of temp.c is
 * in test_temp.c. At the end, two follow-ups: F243 (a latched TEMP_RATE through an open or short sample) and F244 (the V_DC
 * channels' own stamps under the same contract; the wrap of a stopped stamp's age is in test_vdc.c). */
#include <string.h>

#include "dtc.h"
#include "harness.h"
#include "test.h"

#define WRAP_US 4294967296ull /* the 32-bit microsecond counter wraps here */

static const hal_adc_sig_t TSIG[TEMP_COUNT] = {HAL_ADC_TMOD_U, HAL_ADC_TMOD_V, HAL_ADC_TMOD_W, HAL_ADC_NTC_H,
                                               HAL_ADC_NTC_A,  HAL_ADC_MT1,    HAL_ADC_MT2};
static const dtc_id_t TGRP[TEMP_COUNT] = {DTC_TEMP_MODULE, DTC_TEMP_MODULE, DTC_TEMP_MODULE, DTC_TEMP_BOARD,
                                          DTC_TEMP_BOARD,  DTC_TEMP_MOTOR,  DTC_TEMP_MOTOR};
static const float TVAL[TEMP_COUNT] = {70.0f, 50.0f, 45.0f, 35.0f, 30.0f, 60.0f, 55.0f}; /* TMOD_U the hottest module */

static void power_on(void)
{
    sim_reset();
    sim_nvm_wipe();
    dtc_init();
    (void)memset(&g_fm_retained, 0, sizeof g_fm_retained);
    (void)memset(&g_app_session, 0, sizeof g_app_session);
}

static void set_temps(void)
{
    for (uint32_t k = 0u; k < (uint32_t)TEMP_COUNT; k++) {
        sim_set_temp(TSIG[k], TVAL[k]);
    }
}

/* A power-on at the table's temperatures, run to PRECHARGE_WAIT (the fast path running, every window accepted). */
static bool boot_warm(void)
{
    power_on();
    h_setup(TI_SKU_8XX_SIC);
    set_temps();
    h_boot();
    return h_run_until(SM_PRECHARGE_WAIT, 3000u);
}

static uint64_t now_us(void) { return sim_now_ns() / 1000u; }

/* The host's 1 ms task spends simulated time only in its FS26 frames, at its start, so the time h_run_ms(1) returns at is
 * the time sense_slow() judged its reads at: a task input is expired exactly from the tick that ends a hold after the
 * freeze. */

static uint32_t hold_us(void) { return h_p.cal_temp_hold_ms * 1000u; }

static bool same_state(const temp_ch_state_t *a, const temp_ch_state_t *b)
{
    return (a->t_c == b->t_c) && (a->valid == b->valid) && (a->fault == b->fault) && (a->primed == b->primed) &&
           (a->rate_cnt == b->rate_cnt) && (a->last_ms == b->last_ms) && (a->ref_code == b->ref_code) &&
           (a->acc == b->acc) && (a->n == b->n) && (a->win_ms == b->win_ms);
}

/* The hottest valid module NTC the derating uses, with the stopped channel k left out (-273: none). */
static float module_max_without(uint32_t k)
{
    float m = -273.0f;
    for (uint32_t i = 0u; i <= (uint32_t)TEMP_TMOD_W; i++) {
        if (i != k) {
            m = ti_maxf(m, TVAL[i]);
        }
    }
    return m;
}

/* ======================= ti_acq ======================= */

/* The classification itself, at the microsecond: never converted is EXPIRED; a stamp is NEW once, HELD while younger than
 * the hold (hold − 1 µs), EXPIRED from it (hold) — also across the 32-bit wrap — and stays EXPIRED for the same stamp,
 * even where ti_stale reads it as fresh again (2^32 µs later); a stamp ahead of the check by less than the hold is NEW
 * (round 18: stamped after the check time), by the hold or more EXPIRED; a constant code converted on schedule is NEW
 * every time. */
/* F244 hardening: a stamp that is already stale the FIRST time it is read is recorded too, so the same stamp cannot come back
 * as NEW when the 32-bit age wraps (2^32 us later ti_stale would call it fresh again) */
TEST(a_first_read_that_is_already_stale_stays_expired_across_the_wrap)
{
    ti_acq_last_t l = {0u, false, false};
    const uint32_t hold = 10000u;
    const uint32_t t0 = 5000u;
    CHECK(ti_acq(&l, true, t0, t0 + 20000u, hold) == TI_ACQ_EXPIRED); /* 20 ms old at the first read */
    CHECK(ti_acq(&l, true, t0, t0 + 25000u, hold) == TI_ACQ_EXPIRED);
    CHECK(ti_acq(&l, true, t0, t0 + 50u, hold) == TI_ACQ_EXPIRED);     /* now = t0 + 2^32 + 50 us: the age has wrapped */
    CHECK(ti_acq(&l, true, t0 + 30000u, t0 + 30050u, hold) == TI_ACQ_NEW); /* a genuinely new stamp revives it */
}

TEST(an_acquisition_is_new_held_or_expired)
{
    const uint32_t hold = 10000u;
    ti_acq_last_t l = {0};
    CHECK(ti_acq(&l, false, 0u, 5000u, hold) == TI_ACQ_EXPIRED);
    CHECK(ti_acq(&l, false, 0u, 7000u, hold) == TI_ACQ_EXPIRED);
    CHECK(ti_acq(&l, true, 20000u, 20000u, hold) == TI_ACQ_NEW);
    CHECK(ti_acq(&l, true, 20000u, 20000u + (hold - 1u), hold) == TI_ACQ_HELD);
    CHECK(ti_acq(&l, true, 20000u, 20000u + hold, hold) == TI_ACQ_EXPIRED);
    CHECK(ti_acq(&l, true, 20000u, 20000u + (hold / 2u), hold) == TI_ACQ_EXPIRED); /* not revived by the same stamp */
    CHECK(ti_acq(&l, true, 20000u, 20000u, hold) == TI_ACQ_EXPIRED);               /* ... 2^32 us on (age 0) */
    CHECK(ti_acq(&l, true, 21000u, 21000u, hold) == TI_ACQ_NEW);                   /* a new conversion revives it */
    CHECK(ti_acq(&l, true, 22000u, 22000u, hold) == TI_ACQ_NEW);                   /* the same code, a new stamp */
    CHECK(ti_acq(&l, true, 23000u, 22500u, hold) == TI_ACQ_NEW);                   /* 500 us after the check */
    CHECK(ti_acq(&l, true, 23000u + hold, 23000u, hold) == TI_ACQ_EXPIRED);       /* a hold after it: refused */
    /* across the wrap: stamped 4 ms before it */
    const uint32_t t0 = 0xFFFFF060u;
    ti_acq_last_t w = {0};
    CHECK(ti_acq(&w, true, t0, t0, hold) == TI_ACQ_NEW);
    CHECK(ti_acq(&w, true, t0, t0 + 5000u, hold) == TI_ACQ_HELD); /* now past the wrap */
    CHECK(ti_acq(&w, true, t0, t0 + (hold - 1u), hold) == TI_ACQ_HELD);
    CHECK(ti_acq(&w, true, t0, t0 + hold, hold) == TI_ACQ_EXPIRED);
    CHECK(ti_acq(&w, true, t0 + hold, t0 + hold, hold) == TI_ACQ_NEW);
}

/* ======================= the seven temperature channels ======================= */

/* The reviewer's reproduction, end to end: one conversion of TMOD_U at the first task (1 ms), then none for 60 s while
 * the module heats to 120 degC and the current loop and V_DC run on. Before round 24: valid, TEMP_OK, 70 degC, accepted
 * at 60 001 ms, the derating on 70 degC. Now the channel is withdrawn after the hold — its value and its acceptance time
 * as they were — and the derating runs on the valid channels. */
TEST(one_conversion_then_none_for_60_s_is_withdrawn)
{
    power_on();
    h_setup(TI_SKU_8XX_SIC);
    set_temps();
    h_boot();
    sim_adc_freeze(HAL_ADC_TMOD_U, true);
    h_run_ms(1u);
    const temp_ch_state_t first = g_app.temp.ch[TEMP_TMOD_U];
    CHECK(first.valid && (first.fault == TEMP_OK));
    CHECK_NEAR(first.t_c, TVAL[TEMP_TMOD_U], 0.5);
    sim_set_temp(HAL_ADC_TMOD_U, 120.0f);
    bool fast = true;
    for (uint32_t ms = 0u; ms < 60000u; ms++) {
        h_run_ms(1u);
        fast = fast && g_app.isns.fresh && g_app.vdc.valid;
    }
    const temp_ch_state_t *c = &g_app.temp.ch[TEMP_TMOD_U];
    CHECK(fast);
    CHECK(!c->valid && (c->fault == TEMP_OK) && (c->last_ms == first.last_ms) && (c->t_c == first.t_c));
    CHECK(dtc_active(DTC_ADC_SLOW_STALE) && dtc_active(DTC_TEMP_MODULE));
    bool any = false;
    bool all = true;
    CHECK_NEAR(temp_module_max(&g_app.temp, &any, &all), module_max_without(TEMP_TMOD_U), 0.5);
    CHECK(any && !all);
}

/* Each channel on its own: stopped while the fast path runs, it is held — valid, its state bit for bit (nothing
 * accumulated, its acceptance time and window unchanged) though its input moves — until the hold, then withdrawn with its
 * group DTC and DTC_ADC_SLOW_STALE, its open window emptied, no open/short misreported; a module channel leaves the
 * derating's maximum. Resumed, it is valid again once the next window's mean is accepted, not before; the DTCs pass. */
TEST(each_stopped_temperature_channel_is_held_then_withdrawn)
{
    for (uint32_t k = 0u; k < (uint32_t)TEMP_COUNT; k++) {
        CHECK(boot_warm());
        const uint64_t tf = now_us();
        sim_adc_freeze(TSIG[k], true);
        h_run_ms(1u); /* the conversion at the freeze: the last one taken */
        const temp_ch_state_t b = g_app.temp.ch[k];
        CHECK(b.valid && (b.fault == TEMP_OK));
        sim_set_temp(TSIG[k], TVAL[k] + 2.0f); /* the input moves on; the stopped register does not */
        bool held = true;
        bool withdrawn = true;
        bool fast = true;
        for (uint32_t ms = 1u; ms < (h_p.cal_temp_hold_ms + 5u); ms++) {
            h_run_ms(1u);
            const temp_ch_state_t *c = &g_app.temp.ch[k];
            if ((now_us() - tf) < hold_us()) {
                held = held && same_state(c, &b) && !dtc_active(DTC_ADC_SLOW_STALE) && !dtc_active(TGRP[k]);
            } else {
                withdrawn = withdrawn && !c->valid && (c->acc == 0u) && (c->n == 0u) && (c->fault == TEMP_OK) &&
                            dtc_active(DTC_ADC_SLOW_STALE) && dtc_active(TGRP[k]) &&
                            (g_app.acq_expired == (1u << (uint32_t)TSIG[k]));
            }
            fast = fast && g_app.isns.fresh && g_app.vdc.valid;
        }
        CHECK(held && withdrawn && fast);
        h_run_ms(100u);
        bool any = false;
        bool all = false;
        const float m = temp_module_max(&g_app.temp, &any, &all);
        CHECK(!g_app.temp.ch[k].valid && dtc_active(DTC_ADC_SLOW_STALE));
        CHECK_NEAR(m, (k <= (uint32_t)TEMP_TMOD_W) ? module_max_without(k) : TVAL[TEMP_TMOD_U], 0.5);
        CHECK(all == (k > (uint32_t)TEMP_TMOD_W));
        /* resumed: withdrawn until the next window closes, then valid at the moved input */
        sim_adc_freeze(TSIG[k], false);
        h_run_ms(1u);
        CHECK(!dtc_active(DTC_ADC_SLOW_STALE) && (g_app.acq_expired == 0u));
        bool early = false;
        for (uint32_t ms = 1u; ms < (h_p.cal_temp_rate_win_ms - 5u); ms++) {
            h_run_ms(1u);
            early = early || g_app.temp.ch[k].valid;
        }
        h_run_ms(10u);
        CHECK(!early && g_app.temp.ch[k].valid && (g_app.temp.ch[k].fault == TEMP_OK) && !dtc_active(TGRP[k]));
        CHECK_NEAR(g_app.temp.ch[k].t_c, TVAL[k] + 2.0f, 0.5);
        CHECK(dtc_occurrences(DTC_ADC_SLOW_STALE) == 1u);
    }
}

/* All three module NTCs stop at 40 degC while the modules heat to 125 degC: before round 24 the derating stayed at 1
 * (the peak current on 40 degC readings). Now no module reading is valid: FW-04's invalid-reading derating — the
 * continuous rating, never up — and DTC_TEMP_MODULE. */
TEST(three_stopped_module_channels_take_the_invalid_reading_derating)
{
    power_on();
    h_setup(TI_SKU_8XX_SIC);
    for (uint32_t k = 0u; k <= (uint32_t)TEMP_TMOD_W; k++) {
        sim_set_temp(TSIG[k], 40.0f);
    }
    h_boot();
    CHECK(h_run_until(SM_PRECHARGE_WAIT, 3000u));
    for (uint32_t k = 0u; k <= (uint32_t)TEMP_TMOD_W; k++) {
        sim_adc_freeze(TSIG[k], true);
        sim_set_temp(TSIG[k], 125.0f);
    }
    h_run_ms(50u);
    bool any = true;
    bool all = true;
    (void)temp_module_max(&g_app.temp, &any, &all);
    CHECK(!any && !all && dtc_active(DTC_TEMP_MODULE) && dtc_active(DTC_ADC_SLOW_STALE));
    CHECK(g_app.tlim.derate <= ((h_p.i_cont_rms_a / h_p.i_pk_rms_a) + 1e-6f));
    CHECK(g_app.tlim.i_limit_rms_a <= (h_p.i_cont_rms_a + 1e-3f));
}

/* Never converted: invalid from the first task, the stale DTC, no open or short misreported (code 0 read as a short
 * before round 24); its first conversion is accepted at once (the channel was never primed). */
TEST(a_temperature_input_that_never_converts_is_invalid_from_the_start)
{
    for (uint32_t k = 0u; k < (uint32_t)TEMP_COUNT; k++) {
        power_on();
        h_setup(TI_SKU_8XX_SIC);
        set_temps();
        sim_adc_never(TSIG[k], true);
        h_boot();
        bool invalid = true;
        for (uint32_t ms = 0u; ms < 50u; ms++) {
            h_run_ms(1u);
            invalid = invalid && !g_app.temp.ch[k].valid && (g_app.temp.ch[k].fault == TEMP_OK) &&
                      dtc_active(DTC_ADC_SLOW_STALE) && dtc_active(TGRP[k]);
        }
        CHECK(invalid);
        sim_adc_never(TSIG[k], false);
        h_run_ms(1u);
        CHECK(g_app.temp.ch[k].valid && !dtc_active(DTC_ADC_SLOW_STALE));
        CHECK_NEAR(g_app.temp.ch[k].t_c, TVAL[k], 0.5);
    }
}

/* No "must change" rule: a constant temperature converted on schedule is a new sample every task — three minutes, every
 * channel valid at every task, no acquisition or temperature DTC. */
TEST(a_constant_temperature_converted_on_schedule_stays_valid_for_minutes)
{
    CHECK(boot_warm());
    bool valid = true;
    for (uint32_t ms = 0u; ms < 180000u; ms++) {
        h_run_ms(1u);
        for (uint32_t k = 0u; k < (uint32_t)TEMP_COUNT; k++) {
            valid = valid && g_app.temp.ch[k].valid;
        }
    }
    CHECK(valid && (g_app.acq_expired == 0u) && (dtc_occurrences(DTC_ADC_SLOW_STALE) == 0u));
    CHECK((dtc_occurrences(DTC_TEMP_MODULE) == 0u) && (dtc_occurrences(DTC_TEMP_BOARD) == 0u) &&
          (dtc_occurrences(DTC_TEMP_MOTOR) == 0u));
}

/* Across the 32-bit microsecond wrap: every channel converted on schedule stays valid through it; one stopped 4 ms
 * before it is held until its stamp is a hold old — past the wrap — then withdrawn, and valid again after a window. */
TEST(the_hold_is_counted_across_the_microsecond_wrap)
{
    sim_reset_at_us(WRAP_US - 2000000u);
    sim_nvm_wipe();
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    set_temps();
    h_boot();
    CHECK(h_run_until(SM_PRECHARGE_WAIT, 1500u));
    while ((now_us() + 4000u) < WRAP_US) {
        h_run_ms(1u);
    }
    const uint64_t tf = now_us();
    sim_adc_freeze(HAL_ADC_TMOD_V, true);
    h_run_ms(1u);
    bool others = true;
    bool held = true;
    bool withdrawn = true;
    for (uint32_t ms = 1u; ms < (h_p.cal_temp_hold_ms + 5u); ms++) {
        h_run_ms(1u);
        for (uint32_t k = 0u; k < (uint32_t)TEMP_COUNT; k++) {
            others = others && ((k == (uint32_t)TEMP_TMOD_V) || g_app.temp.ch[k].valid);
        }
        if ((now_us() - tf) < hold_us()) {
            held = held && g_app.temp.ch[TEMP_TMOD_V].valid;
        } else {
            withdrawn = withdrawn && !g_app.temp.ch[TEMP_TMOD_V].valid && dtc_active(DTC_ADC_SLOW_STALE);
        }
    }
    CHECK(now_us() > WRAP_US); /* the hold ran across the wrap */
    CHECK(others && held && withdrawn);
    sim_adc_freeze(HAL_ADC_TMOD_V, false);
    h_run_ms(h_p.cal_temp_rate_win_ms + 10u);
    CHECK(g_app.temp.ch[TEMP_TMOD_V].valid && !dtc_active(DTC_ADC_SLOW_STALE));
}

/* ======================= the other slow-list consumers ======================= */

/* V5GD and VOFS (FW-07's references, read at the current-loop rate): stopped while armed, V_DC stays valid for the hold,
 * then both channels are invalid — the §6 V_DC-invalid row (latched), HV unknown, DTC_ADC_SLOW_STALE — with the last
 * verdicts kept: not the V5GD-loss row (its forced SPO and ASC clear) for a reading that only stopped. Resumed, V_DC is
 * valid again and the row stays latched until a fault reset. Before round 24: V_DC valid on the frozen code forever. */
TEST(a_stopped_v5gd_or_vofs_reading_leaves_v_dc_invalid_not_a_v5gd_loss)
{
    const hal_adc_sig_t sig[2] = {HAL_ADC_V5GD, HAL_ADC_VOFS};
    for (uint32_t k = 0u; k < 2u; k++) {
        power_on();
        h_setup(TI_SKU_8XX_SIC);
        h_boot();
        CHECK(h_to_armed());
        const uint64_t tf = now_us();
        sim_adc_freeze(sig[k], true);
        bool held = true;
        bool withdrawn = true;
        for (uint32_t ms = 0u; ms < (h_p.cal_temp_hold_ms + 5u); ms++) {
            const uint64_t lo = now_us();
            h_run_ms(1u);
            const uint64_t hi = now_us();
            if ((hi - tf) < hold_us()) {
                held = held && g_app.vdc.valid && !fm_active(&g_app.fm, SS_ROW_VDC_INVALID);
            }
            if ((lo - tf) >= hold_us()) {
                withdrawn = withdrawn && !g_app.vdc.valid && g_app.vdc.ref_stale && (g_app.vdc.hv == TI_HV_UNKNOWN);
            }
        }
        CHECK(held && withdrawn);
        CHECK(g_app.vdc.v5gd_ok && g_app.vdc.vofs_ok); /* the last verdicts */
        CHECK(fm_active(&g_app.fm, SS_ROW_VDC_INVALID) && !fm_active(&g_app.fm, SS_ROW_V5GD_LOSS));
        CHECK(dtc_active(DTC_ADC_SLOW_STALE) && !dtc_active(DTC_V5GD) && !dtc_active(DTC_VOFS) &&
              !dtc_active(DTC_VDC_STALE)); /* the channels' own samples are fresh */
        CHECK(h_run_until(SM_FAULT, 100u) && (g_app.br.mode != BR_IDLE) && (g_app.br.mode != BR_MOD));
        sim_adc_freeze(sig[k], false);
        h_run_ms(5u);
        CHECK(g_app.vdc.valid && !g_app.vdc.ref_stale && !dtc_active(DTC_ADC_SLOW_STALE));
        CHECK(fm_active(&g_app.fm, SS_ROW_VDC_INVALID) && (g_app.sm.st == SM_FAULT)); /* latched */
    }
    /* never converted: V_DC invalid from the first sample, no arming */
    for (uint32_t k = 0u; k < 2u; k++) {
        power_on();
        h_setup(TI_SKU_8XX_SIC);
        sim_adc_never(sig[k], true);
        h_boot();
        h_run_ms(1u);
        CHECK(!g_app.vdc.valid && g_app.vdc.ref_stale && dtc_active(DTC_ADC_SLOW_STALE));
        CHECK(!h_to_armed());
    }
}

/* KL15: a stopped reading is held (the key state kept), then read as KL15 absent — the power-down after the debounce,
 * as for a converter that never ran. Before round 24: KL15 on forever. */
TEST(a_stopped_kl15_reading_is_held_then_read_as_absent)
{
    CHECK(boot_warm());
    const uint64_t tf = now_us();
    sim_adc_freeze(HAL_ADC_IGN, true);
    bool held = true;
    while ((now_us() - tf) < (hold_us() - 1000u)) {
        h_run_ms(1u);
        held = held && g_app.ign.on && (g_app.sm.st == SM_PRECHARGE_WAIT);
    }
    CHECK(held);
    h_run_ms(h_p.cal_temp_hold_ms + h_p.cal_ign_debounce_ms);
    CHECK(!g_app.ign.on && ((g_app.sm.st == SM_SAFE_POWERDOWN) || (g_app.sm.st == SM_OFF)));
    CHECK(dtc_active(DTC_ADC_SLOW_STALE));
}

/* HVIL (FW-09): the INTRLOK_N conversion stops while armed and the loop then opens. The frozen code alternates CLOSED /
 * implausible against the toggling drive and never debounces — before round 24 the status stayed CLOSED and the open loop
 * went unseen. Now the evaluations wait while the conversion is held, and after the hold it reads as no signal: the
 * signature is lost (DTC_HVIL_SHORT, the §6 command-lost ramp) within FW-09's 100 ms of the stop. */
TEST(a_stopped_interlock_reading_loses_the_hvil_signature_within_100_ms)
{
    power_on();
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    CHECK(h_to_armed());
    CHECK(g_app.hvil.status == HVIL_CLOSED);
    const uint64_t tf = now_us();
    sim_adc_freeze(HAL_ADC_INTRLOK_N, true);
    sim_hvil_set(SIM_HVIL_OPEN);
    uint64_t lost_us = 0u;
    for (uint32_t ms = 0u; (ms < 200u) && (lost_us == 0u); ms++) {
        h_run_ms(1u);
        lost_us = ((g_app.hvil.status != HVIL_CLOSED) && fm_active(&g_app.fm, SS_ROW_CMD_LOST)) ? (now_us() - tf) : 0u;
    }
    CHECK((lost_us > hold_us()) && (lost_us <= 100000u));
    CHECK((g_app.hvil.status == HVIL_SHORT_GND) && dtc_active(DTC_HVIL_SHORT) && dtc_active(DTC_ADC_SLOW_STALE));
}

/* FW-33: a stopped VSUP reading is no reading after the hold (DTC_LV_VSUP_UNKNOWN: no LV supervision, reported) — an
 * overvoltage behind the frozen register cannot pass for 13.5 V. Before round 24: valid at 13.5 V forever. */
TEST(a_stopped_vsup_reading_is_no_reading)
{
    CHECK(boot_warm());
    CHECK(g_app.vsup.valid && !dtc_active(DTC_LV_VSUP_UNKNOWN));
    sim_adc_freeze(HAL_ADC_SBC_AMUX, true);
    sim_fs26_vsup(35.0f);
    h_run_ms(h_p.cal_temp_hold_ms - 2u);
    CHECK(g_app.vsup.valid && !dtc_active(DTC_LV_VSUP_UNKNOWN));
    h_run_ms(5u);
    CHECK(!g_app.vsup.valid && dtc_active(DTC_LV_VSUP_UNKNOWN) && dtc_active(DTC_ADC_SLOW_STALE));
}

/* FW-01's stable reading is eight conversions. HW_ID never converted read code 0 — "shorted to ground"
 * (DTC_HWID_SHORT); a converter that stopped repeated one conversion eight times and passed. Both are now the identity
 * unknown (DTC_HWID_UNKNOWN), no arming; eight real conversions still identify the SKU. */
TEST(the_identity_needs_eight_new_conversions)
{
    for (uint32_t k = 0u; k < 3u; k++) {
        power_on();
        h_setup(TI_SKU_8XX_SIC);
        if (k == 0u) {
            sim_adc_never(HAL_ADC_HW_ID, true);
        } else if (k == 1u) {
            sim_adc_freeze(HAL_ADC_HW_ID, true);
        } else {
            /* converted */
        }
        h_boot();
        if (k < 2u) {
            CHECK((g_app.init == SM_FAIL) && (g_app.hw_sku == TI_SKU_NONE) && dtc_active(DTC_HWID_UNKNOWN) &&
                  !dtc_active(DTC_HWID_SHORT));
        } else {
            CHECK((g_app.init == SM_OK) && (g_app.hw_sku == TI_SKU_8XX_SIC) && !dtc_active(DTC_HWID_UNKNOWN));
        }
    }
}

/* ======================= F243, F244 ======================= */

static bool module_rate_latched(void)
{
    bool r = true;
    for (uint32_t i = 0u; i <= (uint32_t)TEMP_TMOD_W; i++) {
        r = r && !g_app.temp.ch[i].valid && (g_app.temp.ch[i].fault == TEMP_RATE);
    }
    return r;
}

/* FW-04's derating with no valid module reading: the continuous rating, never up */
static bool derated_invalid(void)
{
    return (g_app.tlim.derate <= ((h_p.i_cont_rms_a / h_p.i_pk_rms_a) + 1e-6f)) &&
           (g_app.tlim.i_limit_rms_a <= (h_p.i_cont_rms_a + 1e-3f));
}

/* F243 end to end. The three module NTCs step 40 -> 90 degC: TEMP_RATE latched on each, FW-04's invalid-reading derating.
 * Then TMOD_U reads open (short) for one conversion and all three are back at 40 degC — their last accepted mean, a
 * plausible window. Before F243 the open replaced TMOD_U's latch and the samples back in range cleared it: valid at the
 * next window, the derating released on 40 degC. Now all three stay RATE and invalid for a second, the derating and
 * DTC_TEMP_MODULE stay, and the open (short) has its own record: DTC_TEMP_OPEN_SHORT set at the sample, passed at the next
 * in-range one. */
TEST(a_rate_latched_module_channel_that_reads_open_or_short_stays_latched)
{
    const float rail[2] = {4.99f, 0.0f};
    for (uint32_t k = 0u; k < 2u; k++) {
        power_on();
        h_setup(TI_SKU_8XX_SIC);
        for (uint32_t i = 0u; i <= (uint32_t)TEMP_TMOD_W; i++) {
            sim_set_temp(TSIG[i], 40.0f);
        }
        h_boot();
        CHECK(h_run_until(SM_PRECHARGE_WAIT, 3000u));
        for (uint32_t i = 0u; i <= (uint32_t)TEMP_TMOD_W; i++) {
            sim_set_temp(TSIG[i], 90.0f);
        }
        h_run_ms(5u * h_p.cal_temp_rate_win_ms);
        CHECK(module_rate_latched() && derated_invalid() && dtc_active(DTC_TEMP_MODULE) &&
              !dtc_active(DTC_TEMP_OPEN_SHORT));
        sim_adc_set_v(HAL_ADC_TMOD_U, rail[k]);
        h_run_ms(1u);
        CHECK(module_rate_latched() && (g_app.temp.ch[TEMP_TMOD_U].wire != TEMP_OK) && dtc_active(DTC_TEMP_OPEN_SHORT));
        for (uint32_t i = 0u; i <= (uint32_t)TEMP_TMOD_W; i++) {
            sim_set_temp(TSIG[i], 40.0f);
        }
        bool held = true;
        for (uint32_t ms = 0u; ms < (5u * h_p.cal_temp_rate_win_ms); ms++) {
            h_run_ms(1u);
            held = held && module_rate_latched() && derated_invalid();
        }
        CHECK(held && dtc_active(DTC_TEMP_MODULE) && !dtc_active(DTC_TEMP_OPEN_SHORT) &&
              (dtc_occurrences(DTC_TEMP_OPEN_SHORT) == 1u));
    }
}

/* F244 end to end: a V_DC converter stops while armed (its code and stamp frozen). Judged at every current-loop tick, V_DC
 * stays valid while the stamp is younger than cal_vdc_stale_us, then the channel is stale: V_DC invalid, HV unknown, the
 * section-6 V_DC-invalid row (latched) named DTC_VDC_STALE — neither the slow-list DTC nor the V5GD row — and still stale
 * 20 ms on (across 2^32 us of the stamp's age: test_vdc.c). Resumed, V_DC is valid at the first tick with a new stamp; the
 * row stays latched. Either channel. */
TEST(a_stopped_v_dc_converter_is_stale_until_a_new_conversion)
{
    const hal_adc_sig_t sig[2] = {HAL_ADC_VDC1, HAL_ADC_VDC2};
    const uint32_t hold = h_p.cal_vdc_stale_us;
    for (uint32_t k = 0u; k < 2u; k++) {
        power_on();
        h_setup(TI_SKU_8XX_SIC);
        h_boot();
        CHECK(h_to_armed());
        sim_adc_freeze(sig[k], true);
        const uint32_t tf = hal_time_us(); /* the frozen stamp */
        uint32_t last = g_app.t_isr_us;
        uint32_t ticks = 0u;
        bool held = true;
        bool stale = true;
        for (uint32_t us = 0u; us < (3u * hold); us++) {
            h_isr_only_us(1u);
            if (g_app.t_isr_us != last) {
                last = g_app.t_isr_us;
                ticks++;
                if (ti_age(last, tf) < hold) {
                    held = held && g_app.vdc.valid && !g_app.vdc.ch_stale[k];
                } else {
                    stale = stale && !g_app.vdc.valid && g_app.vdc.ch_stale[k] && !g_app.vdc.ch_stale[1u - k] &&
                            (g_app.vdc.hv == TI_HV_UNKNOWN);
                }
            }
        }
        CHECK(held && stale && (ticks >= 4u));
        h_run_ms(20u);
        CHECK(!g_app.vdc.valid && g_app.vdc.ch_stale[k] && !g_app.vdc.ref_stale);
        CHECK(fm_active(&g_app.fm, SS_ROW_VDC_INVALID) && !fm_active(&g_app.fm, SS_ROW_V5GD_LOSS) &&
              dtc_active(DTC_VDC_STALE) && !dtc_active(DTC_ADC_SLOW_STALE));
        sim_adc_freeze(sig[k], false);
        h_isr_only_us(app_isr_period_us(&g_app));
        CHECK(g_app.vdc.valid && !g_app.vdc.ch_stale[k]);
        h_run_ms(5u);
        CHECK(fm_active(&g_app.fm, SS_ROW_VDC_INVALID) && (g_app.sm.st == SM_FAULT)); /* latched */
    }
}

void suite_acq_fresh(void)
{
    RUN(an_acquisition_is_new_held_or_expired);
    RUN(a_first_read_that_is_already_stale_stays_expired_across_the_wrap);
    RUN(one_conversion_then_none_for_60_s_is_withdrawn);
    RUN(each_stopped_temperature_channel_is_held_then_withdrawn);
    RUN(three_stopped_module_channels_take_the_invalid_reading_derating);
    RUN(a_temperature_input_that_never_converts_is_invalid_from_the_start);
    RUN(a_constant_temperature_converted_on_schedule_stays_valid_for_minutes);
    RUN(the_hold_is_counted_across_the_microsecond_wrap);
    RUN(a_stopped_v5gd_or_vofs_reading_leaves_v_dc_invalid_not_a_v5gd_loss);
    RUN(a_stopped_kl15_reading_is_held_then_read_as_absent);
    RUN(a_stopped_interlock_reading_loses_the_hvil_signature_within_100_ms);
    RUN(a_stopped_vsup_reading_is_no_reading);
    RUN(the_identity_needs_eight_new_conversions);
    RUN(a_rate_latched_module_channel_that_reads_open_or_short_stays_latched);
    RUN(a_stopped_v_dc_converter_is_stale_until_a_new_conversion);
}
