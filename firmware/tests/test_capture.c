/* test_capture.c — FW-41 (round 23): the sampled-waveform capture (src/diag/capture.c, src/diag/uds_capture.c) and
 * its host decoder (tools/capture-decode.mjs).
 * Unit level, on a synthetic application whose fields follow a known waveform: every channel at the scale the contract
 * documents (§10i), the trigger record at the configured split for each source, the level trigger's hysteresis, the
 * sources mask, the ring's bounds over a long random run, the UDS read-out's framing, sequence and re-arm, a decode round
 * trip through the Node decoder, the copy's cost on the host.
 * End to end, on the simulated card: a DESAT, an over-current, an over-voltage and a resolver loss each captured with the
 * fault's record at the split and every record around it equal to what the current-loop ISR computed at that instant;
 * the read-out over the diagnostic bus while the inverter runs, with the ISR grid untouched. */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "capture.h"
#include "dtc.h"
#include "harness.h"
#include "test.h"
#include "uds.h"
#include "uds_capture.h"

#define IMG_MAX (CAP_HDR_BYTES + (CAP_N * CAP_REC_BYTES))
#define T0_US 4294960000u /* 7.3 ms before the 32-bit wrap: a capture's times wrap inside it */
#define PER_US 50u        /* the synthetic ISR period (2 f_sw = 20 kHz) */
#define TWO_PI 6.283185307179586
#define RPM_PER_RAD_S (60.0 / TWO_PI)

static uint8_t s_img[IMG_MAX];
static app_t s_a; /* a synthetic application: the capture reads nothing else */
static uint32_t s_k;
static uds_t s_u;

/* The contract's channel list (§10i), written out independently of capture.c: unit per LSB, name, unit. */
static const double SCALE[CAP_NCH] = {0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, TWO_PI / 32768.0, 1.0};
static const char *const NAME[2u + CAP_NCH] = {"t_us", "flags", "i_a",   "i_b", "i_c", "i_d",     "i_q",
                                               "i_d_ref", "i_q_ref", "v_d", "v_q", "v_dc", "th_rslv", "n_rslv"};
static const char *const UNIT[2u + CAP_NCH] = {"us", "bits", "A", "A", "A", "A", "A", "A", "A", "V", "V", "V", "rad", "rpm"};

/* ---------------- image parsing (from the documented layout) ---------------- */
typedef struct {
    uint32_t fw_id, rate;
    uint16_t hdr_bytes, dtc, rows, trig, n, pre, hys;
    int16_t lvl;
    uint8_t fmt, flags_fmt, id, why, src, lvl_byte, sku, rec_bytes, nch, desc_bytes;
    char magic[5];
} hdr_t;

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static hdr_t hdr(void)
{
    const uint8_t *h = s_img;
    hdr_t r;
    (void)memcpy(r.magic, h, 4u);
    r.magic[4] = '\0';
    r.fmt = h[4];
    r.flags_fmt = h[5];
    r.hdr_bytes = le16(&h[6]);
    r.fw_id = le32(&h[8]);
    r.id = h[12];
    r.why = h[13];
    r.dtc = le16(&h[14]);
    r.rows = le16(&h[16]);
    r.trig = le16(&h[18]);
    r.n = le16(&h[20]);
    r.pre = le16(&h[22]);
    r.rate = le32(&h[24]);
    r.src = h[28];
    r.lvl_byte = h[29];
    r.lvl = (int16_t)le16(&h[30]);
    r.hys = le16(&h[32]);
    r.sku = h[34];
    r.rec_bytes = h[35];
    r.nch = h[36];
    r.desc_bytes = h[37];
    return r;
}

static void rec_at(uint32_t j, uint32_t *t, uint32_t *f, int16_t ch[CAP_NCH])
{
    const uint8_t *p = &s_img[CAP_HDR_BYTES + (j * CAP_REC_BYTES)];
    *t = le32(p);
    *f = le32(&p[4]);
    for (uint32_t c = 0u; c < CAP_NCH; c++) {
        ch[c] = (int16_t)le16(&p[8u + (2u * c)]);
    }
}

/* the frozen image into s_img through the task-side API; its length (0: not readable) */
static uint32_t read_all(void)
{
    cap_status_t s;
    cap_status(&s);
    if ((s.image_bytes == 0u) || (s.image_bytes > IMG_MAX) || !cap_image_read(0u, s_img, s.image_bytes)) {
        return 0u;
    }
    return s.image_bytes;
}

static cap_state_t state(void)
{
    cap_status_t s;
    cap_status(&s);
    return s.st;
}

/* a raw channel against a value: within half an LSB (a little over, for float rounding at the .5 boundary) */
static bool near_raw(int16_t raw, double v, double scale)
{
    if (v != v) {
        return raw == CAP_RAW_NONE;
    }
    return (raw != CAP_RAW_NONE) && (fabs(((double)raw * scale) - v) <= (0.505 * scale));
}

/* channel c (0-based among the int16 ones): the angle (10) compares modulo 2 pi */
static bool near_ch(uint32_t c, int16_t raw, double v)
{
    if ((c != 10u) || (raw == CAP_RAW_NONE) || (v != v)) {
        return near_raw(raw, v, SCALE[c]);
    }
    const double d = remainder(((double)raw * SCALE[c]) - v, TWO_PI);
    return fabs(d) <= (0.505 * SCALE[c]);
}

/* ---------------- the synthetic application ---------------- */
static void wave(uint32_t k, double v[CAP_NCH])
{
    const double w = TWO_PI * (double)k / 400.0; /* 50 Hz electrical at 20 kHz */
    v[0] = 300.0 * sin(w);
    v[1] = 300.0 * sin(w - (TWO_PI / 3.0));
    v[2] = 300.0 * sin(w + (TWO_PI / 3.0));
    v[3] = -40.0 + (0.013 * (double)(k % 1000u));
    v[4] = 250.0 * cos(w / 7.0);
    v[5] = -41.3;
    v[6] = 251.7 + (0.001 * (double)(k % 50000u));
    v[7] = -60.0 + (30.0 * sin(w));
    v[8] = 200.0 + (50.0 * cos(w));
    v[9] = 750.0 + (5.0 * sin(w / 3.0));
    v[10] = fmod(0.0013 * (double)k, TWO_PI);
    v[11] = 3000.0 + (0.5 * (double)(k % 2000u)); /* rpm */
}

static void set_fields(uint32_t k)
{
    double v[CAP_NCH];
    wave(k, v);
    s_a.t_isr_us = T0_US + (PER_US * k);
    s_a.isns.i_a[0] = (float)v[0];
    s_a.isns.i_a[1] = (float)v[1];
    s_a.isns.i_a[2] = (float)v[2];
    s_a.foc.id = (float)v[3];
    s_a.foc.iq = (float)v[4];
    s_a.foc.id_ref = (float)v[5];
    s_a.foc.iq_ref = (float)v[6];
    s_a.foc.vd = (float)v[7];
    s_a.foc.vq = (float)v[8];
    s_a.vdc.vdc = (float)v[9];
    s_a.rslv.theta = (float)v[10];
    s_a.rslv.omega = (float)(v[11] / RPM_PER_RAD_S); /* resolver pole pairs 1 */
}

static void syn_boot(void)
{
    (void)memset(&s_a, 0, sizeof s_a);
    s_a.p = ti_params_get(TI_SKU_8XX_SIC);
    s_a.gains.fsw_hz = 10000u;
    s_a.cal.rslv.resolver_pp = 1u;
    s_k = 0u;
    cap_init(&s_a);
}

static void syn_step(void)
{
    set_fields(s_k);
    cap_isr(&s_a);
    s_k++;
}

static void run_until_frozen(uint32_t max)
{
    for (uint32_t i = 0u; (i < max) && (state() != CAP_FROZEN); i++) {
        syn_step();
    }
}

static cap_res_t arm(uint16_t pre, uint8_t src, uint8_t ch, bool falling, int16_t lvl, uint16_t hys)
{
    const cap_cfg_t c = {.pre = pre, .sources = src, .lvl_ch = ch, .lvl_falling = falling, .lvl = lvl, .hys = hys};
    return cap_arm(&c);
}

/* record j of s_img against sample k of the synthetic waveform */
static bool rec_is_sample(uint32_t j, uint32_t k)
{
    uint32_t t;
    uint32_t f;
    int16_t ch[CAP_NCH];
    double v[CAP_NCH];
    rec_at(j, &t, &f, ch);
    wave(k, v);
    bool ok = (t == (uint32_t)(T0_US + (PER_US * k)));
    for (uint32_t c = 0u; c < CAP_NCH; c++) {
        ok = ok && near_ch(c, ch[c], v[c]);
    }
    return ok;
}

/* ---------------- unit level ---------------- */
TEST(records_every_channel_at_the_documented_scales)
{
    syn_boot();
    CHECK(arm(2u, CAP_SRC_DTC, 0u, false, 0, 0u) == CAP_OK);
    syn_step();
    syn_step();
    /* sample 2: the corners of the quantisation and every flag field */
    s_a.t_isr_us = T0_US + (2u * PER_US);
    s_a.isns.i_a[0] = 2000.0f;  /* beyond +1638.35 A: +32767 */
    s_a.isns.i_a[1] = -2000.0f; /* -32767: INT16_MIN is kept for "not a number" */
    s_a.isns.i_a[2] = NAN;
    s_a.foc.id = 123.456f;      /* 2469.12 -> 2469 */
    s_a.foc.iq = -0.024f;       /* -0.48 -> 0 */
    s_a.foc.id_ref = -0.026f;   /* -0.52 -> -1 */
    s_a.foc.iq_ref = INFINITY;
    s_a.foc.vd = -INFINITY;
    s_a.foc.vq = 0.0f;
    s_a.vdc.vdc = 812.34f;      /* 16246.8 -> 16247 */
    s_a.rslv.theta = 3.0f;      /* 15645.6 -> 15646 */
    s_a.rslv.omega = 1047.1976f; /* 10 000 rpm */
    s_a.fm.active = 1u << SS_ROW_OVERVOLTAGE; /* a new row, but the rows are not a source here */
    s_a.br.mode = BR_MOD;
    s_a.sm.st = SM_RUN;
    s_a.isns.valid = true;
    s_a.rslv.valid = true;
    s_a.mod_req = true;
    s_a.foc.sat = true;
    s_a.fm.dec.action = SS_ACT_SPO;
    cap_isr(&s_a);
    s_k = 3u;
    CHECK(state() == CAP_ARMED);
    dtc_set(DTC_OVERVOLTAGE, 0u); /* the next record is the trigger */
    syn_step();
    CHECK(state() == CAP_TRIGGERED);
    run_until_frozen(CAP_N);
    CHECK(state() == CAP_FROZEN && read_all() == (CAP_HDR_BYTES + (CAP_N * CAP_REC_BYTES)));
    const hdr_t h = hdr();
    CHECK(strcmp(h.magic, "TICP") == 0 && h.fmt == 1u && h.flags_fmt == 1u && h.hdr_bytes == CAP_HDR_BYTES);
    CHECK(h.fw_id == TI_FW_ID && h.id == 1u && h.why == CAP_WHY_FAULT && h.dtc == DTC_OVERVOLTAGE && h.rows == 0u);
    CHECK(h.trig == 2u && h.n == CAP_N && h.pre == 2u && h.rate == 20000u && h.src == CAP_SRC_DTC);
    CHECK(h.sku == (uint8_t)TI_SKU_8XX_SIC && h.rec_bytes == 32u && h.nch == 14u && h.desc_bytes == 20u);
    bool desc_ok = true;
    for (uint32_t i = 0u; i < (2u + CAP_NCH); i++) {
        const uint8_t *d = &s_img[40u + (20u * i)];
        float sc;
        (void)memcpy(&sc, &d[16], 4u); /* little-endian host */
        const double want = (i < 2u) ? 1.0 : SCALE[i - 2u];
        desc_ok = desc_ok && (strncmp((const char *)d, NAME[i], 8u) == 0) && (strncmp((const char *)&d[8], UNIT[i], 6u) == 0);
        desc_ok = desc_ok && (d[14] == ((i < 2u) ? 0u : 1u)) && (d[15] == ((i < 2u) ? (4u * i) : (8u + (2u * (i - 2u)))));
        desc_ok = desc_ok && (fabs((double)sc - want) <= (1e-6 * want));
    }
    CHECK(desc_ok);
    uint32_t t;
    uint32_t f;
    int16_t ch[CAP_NCH];
    CHECK(rec_is_sample(0u, 1u));
    rec_at(1u, &t, &f, ch);
    const int16_t want[CAP_NCH] = {32767, -32767, CAP_RAW_NONE, 2469, 0, -1, 32767, -32767, 0, 16247, 15646, 10000};
    CHECK(t == T0_US + (2u * PER_US) && memcmp(ch, want, sizeof want) == 0);
    const uint32_t wf = (1u << SS_ROW_OVERVOLTAGE) | ((uint32_t)BR_MOD << CAP_F_BR_SHIFT) | ((uint32_t)SM_RUN << CAP_F_SM_SHIFT) |
                        CAP_F_ISNS_VALID | CAP_F_RSLV_VALID | CAP_F_MOD_REQ | CAP_F_FOC_SAT |
                        ((uint32_t)SS_ACT_SPO << CAP_F_ACT_SHIFT);
    CHECK(f == wf);
    rec_at(2u, &t, &f, ch);
    CHECK(rec_is_sample(2u, 3u) && ((f & CAP_F_TRIGGER) != 0u) && ((f & ~CAP_F_TRIGGER) == wf));
    CHECK(rec_is_sample(CAP_N - 1u, CAP_N)); /* the last record: sample 2048, across the 32-bit wrap */
}

/* The fault record lands at index pre for any split; every record is its sample; frozen means frozen. */
TEST(a_fault_trigger_lands_at_the_configured_split)
{
    const uint16_t split[4] = {0u, 1u, 777u, CAP_N - 1u};
    for (uint32_t s = 0u; s < 4u; s++) {
        syn_boot();
        CHECK(arm(split[s], CAP_SRC_ROW, 0u, false, 0, 0u) == CAP_OK);
        for (uint32_t i = 0u; i < (split[s] + 300u); i++) {
            syn_step();
        }
        const uint32_t kt = s_k;
        s_a.fm.active = 1u << SS_ROW_FLT_HS; /* a DESAT row */
        syn_step();
        run_until_frozen(CAP_N + 10u);
        CHECK(read_all() != 0u);
        const hdr_t h = hdr();
        CHECK(h.why == CAP_WHY_FAULT && h.trig == split[s] && h.n == CAP_N && h.rows == (1u << SS_ROW_FLT_HS) && h.dtc == DTC_NONE);
        bool all = true;
        uint32_t trig_flags = 0u;
        for (uint32_t j = 0u; j < h.n; j++) {
            uint32_t t;
            uint32_t f;
            int16_t ch[CAP_NCH];
            rec_at(j, &t, &f, ch);
            all = all && rec_is_sample(j, (kt - h.trig) + j);
            all = all && (((f & CAP_F_TRIGGER) != 0u) == (j == h.trig));
            all = all && (((f & (1u << SS_ROW_FLT_HS)) != 0u) == (j >= h.trig));
            trig_flags |= f & CAP_F_TRIGGER;
        }
        CHECK(all && trig_flags == CAP_F_TRIGGER);
        static uint8_t before[IMG_MAX];
        (void)memcpy(before, s_img, IMG_MAX);
        for (uint32_t i = 0u; i < 3000u; i++) {
            s_a.fm.active = 0u;
            syn_step();
            s_a.fm.active = 1u << SS_ROW_FLT_LS; /* more faults change nothing: frozen */
            syn_step();
        }
        CHECK(state() == CAP_FROZEN && read_all() != 0u && memcmp(before, s_img, IMG_MAX) == 0);
    }
}

TEST(an_early_trigger_keeps_only_the_records_it_has)
{
    syn_boot();
    CHECK(arm(1500u, CAP_SRC_DTC, 0u, false, 0, 0u) == CAP_OK);
    for (uint32_t i = 0u; i < 100u; i++) {
        syn_step();
    }
    dtc_set(DTC_RSLV_STALE, 0u);
    syn_step();
    run_until_frozen(CAP_N);
    CHECK(read_all() == (CAP_HDR_BYTES + (648u * CAP_REC_BYTES)));
    const hdr_t h = hdr();
    CHECK(h.trig == 100u && h.n == 648u && h.pre == 1500u && h.dtc == DTC_RSLV_STALE); /* 100 + 1 + (2047 - 1500) */
    bool all = true;
    for (uint32_t j = 0u; j < h.n; j++) {
        all = all && rec_is_sample(j, j);
    }
    CHECK(all);
}

TEST(the_command_trigger)
{
    syn_boot(); /* armed at init: pre 1024, DTC + rows */
    for (uint32_t i = 0u; i < 50u; i++) {
        syn_step();
    }
    CHECK(cap_trigger() == CAP_OK);
    syn_step();
    CHECK(state() == CAP_TRIGGERED);
    CHECK(cap_trigger() == CAP_ESTATE); /* only while armed */
    run_until_frozen(CAP_N);
    CHECK(read_all() != 0u);
    const hdr_t h = hdr();
    CHECK(h.why == CAP_WHY_CMD && h.trig == 50u && h.n == (50u + 1u + 1023u) && rec_is_sample(50u, 50u));
    CHECK(cap_trigger() == CAP_ESTATE);
    /* an arm not yet taken by the ISR: nothing readable, a second request is busy */
    CHECK(cap_arm(NULL) == CAP_OK);
    CHECK(cap_arm(NULL) == CAP_EBUSY && cap_trigger() == CAP_EBUSY);
    cap_status_t st;
    cap_status(&st);
    CHECK(st.st == CAP_FROZEN && st.arm_pending && st.image_bytes == 0u && !cap_image_read(0u, s_img, 16u));
    syn_step();
    CHECK(state() == CAP_ARMED);
    /* a command posted and then overtaken by a re-arm is dropped: never a trigger of the next capture */
    CHECK(cap_trigger() == CAP_OK && cap_arm(NULL) == CAP_OK);
    for (uint32_t i = 0u; i < 20u; i++) {
        syn_step();
    }
    CHECK(state() == CAP_ARMED);
}

/* one record with i_a (channel 2) or v_dc (channel 11) set, the rest the waveform; the state after it */
static cap_state_t step_with(uint32_t ch, float v)
{
    set_fields(s_k);
    if (ch == 2u) {
        s_a.isns.i_a[0] = v;
    } else {
        s_a.vdc.vdc = v;
    }
    cap_isr(&s_a);
    s_k++;
    return state();
}

TEST(the_level_trigger_needs_its_hysteresis)
{
    /* rising on i_a at 200 A (4000), hysteresis 20 A (400): it fires only at >= 200 A after a sample below 180 A */
    syn_boot();
    CHECK(arm(10u, CAP_SRC_LEVEL, 2u, false, 4000, 400u) == CAP_OK);
    const float up[6] = {250.0f, 190.0f, 210.0f, 180.0f, 179.9f, 199.9f};
    bool quiet = true;
    for (uint32_t i = 0u; i < 6u; i++) {
        quiet = quiet && (step_with(2u, up[i]) == CAP_ARMED);
    }
    CHECK(quiet);
    CHECK(step_with(2u, 200.0f) == CAP_TRIGGERED);
    run_until_frozen(CAP_N);
    CHECK(read_all() != 0u);
    hdr_t h = hdr();
    uint32_t t;
    uint32_t f;
    int16_t ch[CAP_NCH];
    rec_at(h.trig, &t, &f, ch);
    CHECK(h.why == CAP_WHY_LEVEL && h.trig == 6u && ch[0] == 4000 && h.lvl_byte == 2u && h.lvl == 4000 && h.hys == 400u);
    /* falling on V_DC at 700 V (14000), hysteresis 10 V (200); "not a number" never fires */
    CHECK(arm(10u, CAP_SRC_LEVEL, 11u, true, 14000, 200u) == CAP_OK);
    const float down[7] = {650.0f, 705.0f, 710.0f, 710.1f, 700.1f, NAN, NAN};
    quiet = true;
    for (uint32_t i = 0u; i < 7u; i++) {
        quiet = quiet && (step_with(11u, down[i]) == CAP_ARMED);
    }
    CHECK(quiet);
    CHECK(step_with(11u, 700.0f) == CAP_TRIGGERED);
    run_until_frozen(CAP_N);
    CHECK(read_all() != 0u);
    h = hdr();
    rec_at(h.trig, &t, &f, ch);
    CHECK(h.why == CAP_WHY_LEVEL && h.trig == 7u && ch[9] == 14000 && h.lvl_byte == (11u | 0x80u) && h.id == 2u);
    /* zero hysteresis: a crossing from below */
    CHECK(arm(0u, CAP_SRC_LEVEL, 2u, false, 0, 0u) == CAP_OK);
    CHECK(step_with(2u, 0.0f) == CAP_ARMED && step_with(2u, 5.0f) == CAP_ARMED && step_with(2u, -0.1f) == CAP_ARMED);
    CHECK(step_with(2u, 0.0f) == CAP_TRIGGERED);
    /* the level configured but not a source: a crossing does nothing */
    run_until_frozen(CAP_N);
    CHECK(arm(0u, CAP_SRC_DTC, 2u, false, 0, 0u) == CAP_OK);
    CHECK(step_with(2u, -1.0f) == CAP_ARMED && step_with(2u, 1.0f) == CAP_ARMED);
}

TEST(the_sources_mask_selects_the_triggers)
{
    syn_boot();
    /* the level only: a DTC and a new row do nothing */
    CHECK(arm(5u, CAP_SRC_LEVEL, 2u, false, 32000, 0u) == CAP_OK);
    syn_step();
    dtc_set(DTC_HVIL_OPEN, 0u);
    s_a.fm.active = 1u << SS_ROW_CMD_LOST;
    syn_step();
    syn_step();
    CHECK(state() == CAP_ARMED);
    /* the DTCs only: a new row does nothing, a DTC occurrence fires */
    s_a.fm.active = 0u;
    CHECK(arm(5u, CAP_SRC_DTC, 0u, false, 0, 0u) == CAP_OK);
    syn_step();
    s_a.fm.active = 1u << SS_ROW_BATTERY_LOST;
    syn_step();
    CHECK(state() == CAP_ARMED);
    dtc_set(DTC_HVIL_OPEN, 0u); /* still failed: not a new occurrence */
    syn_step();
    CHECK(state() == CAP_ARMED);
    dtc_pass(DTC_HVIL_OPEN);
    dtc_set(DTC_HVIL_OPEN, 0u); /* failed again: a new occurrence */
    syn_step();
    CHECK(state() == CAP_TRIGGERED);
    run_until_frozen(CAP_N);
    CHECK(read_all() != 0u && hdr().dtc == DTC_HVIL_OPEN && hdr().rows == 0u);
    /* the rows only: a DTC does nothing; a row up at the arm does nothing; one that falls and rises again fires */
    s_a.fm.active = 1u << SS_ROW_BATTERY_LOST;
    CHECK(arm(5u, CAP_SRC_ROW, 0u, false, 0, 0u) == CAP_OK);
    syn_step();
    dtc_set(DTC_BMS_TIMEOUT, 0u);
    syn_step();
    CHECK(state() == CAP_ARMED);
    s_a.fm.active = 0u;
    syn_step();
    CHECK(state() == CAP_ARMED);
    s_a.fm.active = 1u << SS_ROW_BATTERY_LOST;
    syn_step();
    CHECK(state() == CAP_TRIGGERED);
    run_until_frozen(CAP_N);
    CHECK(read_all() != 0u && hdr().rows == (1u << SS_ROW_BATTERY_LOST) && hdr().dtc == DTC_NONE);
    /* out of range: refused, nothing changed */
    CHECK(arm(CAP_N, CAP_SRC_DTC, 0u, false, 0, 0u) == CAP_ERANGE);
    CHECK(arm(0u, 0x08u, 0u, false, 0, 0u) == CAP_ERANGE);
    CHECK(arm(0u, CAP_SRC_LEVEL, 1u, false, 0, 0u) == CAP_ERANGE);
    CHECK(arm(0u, CAP_SRC_LEVEL, 14u, false, 0, 0u) == CAP_ERANGE);
    CHECK(arm(0u, CAP_SRC_LEVEL, 2u, false, CAP_RAW_NONE, 0u) == CAP_ERANGE);
    CHECK(arm(0u, CAP_SRC_LEVEL, 2u, false, 0, 32768u) == CAP_ERANGE);
    CHECK(state() == CAP_FROZEN && read_all() != 0u && hdr().rows == (1u << SS_ROW_BATTERY_LOST));
}

static uint32_t rng(uint32_t *x)
{
    *x = (*x * 1664525u) + 1013904223u;
    return *x >> 8;
}

/* Sixty captures at random splits, lengths and sources: every one has exactly its records, in time order, each its
 * own sample, one trigger record at min(pre, recorded); nothing is written once frozen (ASan: nothing outside). */
TEST(the_ring_never_overruns_and_never_blocks)
{
    syn_boot();
    uint32_t x = 12345u;
    bool ok = true;
    uint32_t captures = 0u;
    for (uint32_t cycle = 0u; cycle < 60u; cycle++) {
        const uint16_t pre = (uint16_t)(rng(&x) % CAP_N);
        const uint32_t run = 1u + (rng(&x) % 3000u); /* the first record takes the arm, then the triggers count */
        ok = ok && (arm(pre, CAP_SRC_DTC | CAP_SRC_ROW, 0u, false, 0, 0u) == CAP_OK);
        s_a.fm.active = 0u;
        for (uint32_t i = 0u; i < run; i++) {
            syn_step();
        }
        const uint32_t kt = s_k;
        const uint32_t kind = rng(&x) % 3u;
        if (kind == 0u) {
            dtc_pass(DTC_V5GD);
            dtc_set(DTC_V5GD, 0u);
        } else if (kind == 1u) {
            s_a.fm.active = 1u << SS_ROW_V5GD_LOSS;
        } else {
            ok = ok && (cap_trigger() == CAP_OK);
        }
        run_until_frozen(CAP_N + 1u);
        ok = ok && (state() == CAP_FROZEN) && (read_all() != 0u);
        const hdr_t h = hdr();
        const uint32_t pv = (run < pre) ? run : pre; /* the records before the trigger since the arm */
        ok = ok && (h.trig == pv) && (h.n == (pv + 1u + (CAP_N - 1u - pre))) && (h.n <= CAP_N);
        uint32_t marks = 0u;
        for (uint32_t j = 0u; j < h.n; j++) {
            uint32_t t;
            uint32_t f;
            int16_t ch[CAP_NCH];
            rec_at(j, &t, &f, ch);
            ok = ok && rec_is_sample(j, (kt - pv) + j);
            marks += ((f & CAP_F_TRIGGER) != 0u) ? 1u : 0u;
        }
        ok = ok && (marks == 1u);
        const uint32_t k_end = s_k;
        for (uint32_t i = 0u; i < 50u; i++) {
            syn_step();
        }
        ok = ok && (read_all() != 0u) && rec_is_sample(h.n - 1u, k_end - 1u); /* frozen: the last record stays */
        captures++;
    }
    CHECK(ok && captures == 60u);
}

/* The copy's cost on the host reference (docs/timing.md): a bounded copy, no loop over the ring. TI_CAP_BENCH=1 prints. */
TEST(the_isr_copy_is_a_bounded_copy_on_the_host)
{
    syn_boot();
    CHECK(arm(CAP_N / 2u, CAP_SRC_DTC | CAP_SRC_ROW | CAP_SRC_LEVEL, 2u, false, 32000, 0u) == CAP_OK); /* never fires */
    set_fields(0u);
    double best = 1e9;
    for (uint32_t b = 0u; b < 40u; b++) {
        struct timespec t0;
        struct timespec t1;
        (void)timespec_get(&t0, TIME_UTC);
        for (uint32_t i = 0u; i < 20000u; i++) {
            s_a.t_isr_us += PER_US;
            s_a.isns.i_a[0] = (float)(i % 1000u);
            cap_isr(&s_a);
        }
        (void)timespec_get(&t1, TIME_UTC);
        const double ns = ((double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec)) / 20000.0;
        best = (ns < best) ? ns : best;
    }
    if (getenv("TI_CAP_BENCH") != NULL) {
        printf("    capture: cap_isr %.1f ns per call on the host (best of 40 x 20000, armed, level checked)\n", best);
    }
    CHECK(best < 1000.0 && state() == CAP_ARMED);
}

/* ---------------- the diagnostic path ---------------- */
static const uint8_t FD_LEN[7] = {12u, 16u, 20u, 24u, 32u, 48u, 64u};

/* A request (payload m[0..n)) as a classic or CAN-FD single frame through the whole UDS server -> the response payload
 * and its length; 0 when there is no response or its framing breaks ISO 15765-2 (classic: 8 bytes, 1..7; CAN-FD: the
 * escape, 8..62, in the smallest frame that holds it). */
static uint32_t xfer(const uint8_t *m, uint32_t n, uint8_t out[64])
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u};
    (void)memset(f.data, 0xAA, sizeof f.data);
    if (n <= 7u) {
        f.data[0] = (uint8_t)n;
        (void)memcpy(&f.data[1], m, n);
    } else {
        f.len = 16u;
        f.data[0] = 0u;
        f.data[1] = (uint8_t)n;
        (void)memcpy(&f.data[2], m, n);
    }
    hal_can_frame_t r;
    if (!uds_handle(&s_u, &f, 0x12345678u, &r) || (r.id != UDS_ID_RSP)) {
        return 0u;
    }
    if (r.data[0] != 0u) {
        const uint32_t k = r.data[0];
        if ((r.len != 8u) || (k > 7u)) {
            return 0u;
        }
        (void)memcpy(out, &r.data[1], k);
        return k;
    }
    const uint32_t k = r.data[1];
    uint32_t smallest = 0u;
    for (uint32_t i = 0u; (i < 7u) && (smallest == 0u); i++) {
        smallest = (FD_LEN[i] >= (k + 2u)) ? FD_LEN[i] : 0u;
    }
    if ((k < 8u) || (k > 62u) || (r.len != smallest)) {
        return 0u;
    }
    (void)memcpy(out, &r.data[2], k);
    return k;
}

static bool is_nrc(const uint8_t *r, uint32_t n, uint8_t sid, uint8_t code)
{
    return (n == 3u) && (r[0] == 0x7Fu) && (r[1] == sid) && (r[2] == code);
}

static const uint8_t RD_STATUS[3] = {0x22u, 0xFDu, 0x40u};
static const uint8_t RD_BLOCK[3] = {0x22u, 0xFDu, 0x41u};
static const uint8_t RC_ARM[4] = {0x31u, 0x01u, 0xF0u, 0x41u};
static const uint8_t RC_TRIG[4] = {0x31u, 0x01u, 0xF0u, 0x42u};

/* every block of the frozen capture through the server into img, in order; the block count (0: a gap or an error) */
static uint32_t uds_read_all(uint8_t *img, uint8_t *id)
{
    uint8_t r[64];
    uint32_t n = xfer(RD_STATUS, 3u, r);
    if ((n != 7u) || (r[0] != 0x62u) || ((r[3] & 0x0Fu) != CAP_FROZEN)) {
        return 0u;
    }
    *id = r[4];
    const uint32_t nb = ((uint32_t)r[5] << 8) | r[6];
    uint32_t off = 0u;
    for (uint32_t k = 0u; k < nb; k++) {
        n = xfer(RD_BLOCK, 3u, r);
        if ((n < 7u) || (r[0] != 0x62u) || (r[1] != 0xFDu) || (r[2] != 0x41u) || (r[3] != *id) ||
            ((((uint32_t)r[4] << 8) | r[5]) != k) || ((k + 1u < nb) && (n != (6u + UDS_CAP_BLOCK_BYTES)))) {
            return 0u;
        }
        (void)memcpy(&img[off], &r[6], n - 6u);
        off += n - 6u;
    }
    return nb;
}

TEST(uds_read_out_sequence_integrity_and_rearm)
{
    uds_init(&s_u, NULL, NULL, NULL); /* the default build: no key function */
    syn_boot();
    uint8_t r[64];
    CHECK(xfer(RD_STATUS, 3u, r) == 7u && r[0] == 0x62u && r[3] == (uint8_t)CAP_ARMED && r[5] == 0u && r[6] == 0u);
    CHECK(xfer(RD_BLOCK, 3u, r) == 3u && is_nrc(r, 3u, 0x22u, UDS_NRC_CONDITIONS)); /* armed: nothing to read */
    for (uint32_t i = 0u; i < 1100u; i++) {
        syn_step();
    }
    CHECK(xfer(RC_TRIG, 4u, r) == 4u && r[0] == 0x71u && r[1] == 0x01u && r[2] == 0xF0u && r[3] == 0x42u);
    run_until_frozen(CAP_N);
    const uint32_t bytes = read_all();
    const uint32_t nb = (bytes + 55u) / 56u;
    CHECK(bytes == (CAP_HDR_BYTES + (CAP_N * CAP_REC_BYTES)) && nb == 1176u);
    CHECK(xfer(RD_STATUS, 3u, r) == 7u && r[3] == (uint8_t)(CAP_FROZEN | (CAP_WHY_CMD << 4)) && r[4] == 1u &&
          ((((uint32_t)r[5] << 8) | r[6]) == nb));
    static uint8_t img[IMG_MAX];
    uint8_t id = 0u;
    CHECK(uds_read_all(img, &id) == nb && id == 1u && memcmp(img, s_img, bytes) == 0);
    CHECK(xfer(RD_BLOCK, 3u, r) == 3u && is_nrc(r, 3u, 0x22u, UDS_NRC_OUT_OF_RANGE)); /* past the last block */
    /* seek: any block again, with its number */
    const uint8_t seek5[5] = {0x2Eu, 0xFDu, 0x41u, 0x00u, 0x05u};
    CHECK(xfer(seek5, 5u, r) == 3u && r[0] == 0x6Eu && r[1] == 0xFDu && r[2] == 0x41u);
    CHECK(xfer(RD_BLOCK, 3u, r) == 62u && r[4] == 0u && r[5] == 5u && memcmp(&r[6], &s_img[5u * 56u], 56u) == 0);
    CHECK(xfer(RD_BLOCK, 3u, r) == 62u && r[5] == 6u); /* then on */
    const uint8_t seek_end[5] = {0x2Eu, 0xFDu, 0x41u, (uint8_t)(nb >> 8), (uint8_t)nb};
    CHECK(xfer(seek_end, 5u, r) == 3u && is_nrc(r, 3u, 0x2Eu, UDS_NRC_OUT_OF_RANGE));
    CHECK(xfer(seek5, 4u, r) == 3u && is_nrc(r, 3u, 0x2Eu, UDS_NRC_LENGTH));
    const uint8_t seek_last[5] = {0x2Eu, 0xFDu, 0x41u, (uint8_t)((nb - 1u) >> 8), (uint8_t)(nb - 1u)};
    const uint32_t tail = bytes - ((nb - 1u) * 56u);
    CHECK(xfer(seek_last, 5u, r) == 3u && xfer(RD_BLOCK, 3u, r) == (6u + tail) && memcmp(&r[6], &s_img[bytes - tail], tail) == 0);
    /* the frozen capture survives any number of reads; a re-arm is the only way out */
    CHECK(xfer(RC_ARM, 4u, r) == 4u && r[0] == 0x71u && r[3] == 0x41u);
    CHECK(xfer(RD_BLOCK, 3u, r) == 3u && is_nrc(r, 3u, 0x22u, UDS_NRC_CONDITIONS)); /* the arm is pending: nothing read */
    CHECK(xfer(RC_ARM, 4u, r) == 3u && is_nrc(r, 3u, 0x31u, 0x21u));               /* busy: repeat */
    CHECK(xfer(RD_STATUS, 3u, r) == 7u && r[5] == 0u && r[6] == 0u);
    syn_step();
    CHECK(state() == CAP_ARMED && xfer(RD_BLOCK, 3u, r) == 3u && is_nrc(r, 3u, 0x22u, UDS_NRC_CONDITIONS));
    /* arm with a configuration: a CAN-FD single frame of 12 bytes (pre 256, the level on i_a rising at 0 A, hys 1 A) */
    const uint8_t cfg[12] = {0x31u, 0x01u, 0xF0u, 0x41u, 0x01u, 0x00u, CAP_SRC_LEVEL, 2u, 0x00u, 0x00u, 0x00u, 20u};
    CHECK(xfer(cfg, 12u, r) == 4u && r[0] == 0x71u);
    run_until_frozen(CAP_N + 400u); /* the waveform crosses 0 A upwards within a period */
    CHECK(read_all() != 0u && hdr().why == CAP_WHY_LEVEL && hdr().pre == 256u && hdr().hys == 20u && hdr().id == 2u);
    CHECK(xfer(RD_BLOCK, 3u, r) == 62u && r[3] == 2u && r[4] == 0u && r[5] == 0u); /* a new capture: its id, block 0 */
    /* refusals: the trigger only while armed; configurations out of range; lengths */
    CHECK(xfer(RC_TRIG, 4u, r) == 3u && is_nrc(r, 3u, 0x31u, UDS_NRC_CONDITIONS));
    uint8_t bad[12];
    (void)memcpy(bad, cfg, 12u);
    bad[4] = (uint8_t)(CAP_N >> 8);
    bad[5] = (uint8_t)CAP_N;
    CHECK(xfer(bad, 12u, r) == 3u && is_nrc(r, 3u, 0x31u, UDS_NRC_OUT_OF_RANGE));
    (void)memcpy(bad, cfg, 12u);
    bad[7] = 14u;
    CHECK(xfer(bad, 12u, r) == 3u && is_nrc(r, 3u, 0x31u, UDS_NRC_OUT_OF_RANGE));
    CHECK(xfer(cfg, 11u, r) == 3u && is_nrc(r, 3u, 0x31u, UDS_NRC_LENGTH));
    CHECK(xfer(RC_TRIG, 4u, r) == 3u && xfer(cfg, 5u, r) == 3u && is_nrc(r, 3u, 0x31u, UDS_NRC_LENGTH));
    CHECK(state() == CAP_FROZEN && hdr().id == 2u); /* nothing refused changed anything */
    /* everything else still reaches the rest of the server */
    const uint8_t seed_rq[2] = {0x27u, 0x01u};
    const uint8_t other_did[3] = {0x22u, 0xF1u, 0x90u};
    const uint8_t clear_rq[4] = {0x31u, 0x01u, 0xF0u, 0x10u};
    CHECK(xfer(seed_rq, 2u, r) == 3u && is_nrc(r, 3u, 0x27u, UDS_NRC_CONDITIONS));
    CHECK(xfer(other_did, 3u, r) == 3u && is_nrc(r, 3u, 0x22u, UDS_NRC_SERVICE_NOT_SUPPORTED));
    CHECK(xfer(clear_rq, 4u, r) == 3u && is_nrc(r, 3u, 0x31u, UDS_NRC_SECURITY_DENIED));
}

/* ---------------- decode round trip (tools/capture-decode.mjs) ---------------- */
static bool write_blocks(const char *path, const uint8_t *img, uint32_t bytes, uint8_t id, uint32_t skip, uint32_t id_bad)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        return false;
    }
    const uint32_t nb = (bytes + 55u) / 56u;
    (void)fprintf(fp, "# FW-41 read-out, blocks in reverse order, block 3 repeated\n");
    for (uint32_t i = 0u; i <= nb; i++) {
        const uint32_t k = (i == nb) ? 3u : ((nb - 1u) - i);
        if (k == skip) {
            continue;
        }
        const uint32_t len = ((bytes - (k * 56u)) < 56u) ? (bytes - (k * 56u)) : 56u;
        (void)fprintf(fp, "62 FD 41 %02X %02X %02X", (k == id_bad) ? (unsigned)(id ^ 1u) : (unsigned)id, (unsigned)(k >> 8),
                      (unsigned)(k & 0xFFu));
        for (uint32_t b = 0u; b < len; b++) {
            (void)fprintf(fp, " %02X", img[(k * 56u) + b]);
        }
        (void)fprintf(fp, "\n");
    }
    return fclose(fp) == 0;
}

static int decode(const char *hex, const char *csv)
{
    char cmd[300];
    (void)snprintf(cmd, sizeof cmd, "node tools/capture-decode.mjs %s > %s 2>/dev/null", hex, csv);
    return system(cmd);
}

TEST(the_blocks_decode_to_the_waveform_through_the_node_decoder)
{
    uds_init(&s_u, NULL, NULL, NULL);
    syn_boot();
    CHECK(arm(300u, CAP_SRC_DTC, 0u, false, 0, 0u) == CAP_OK);
    for (uint32_t i = 0u; i < 400u; i++) {
        syn_step();
    }
    const uint32_t kt = s_k;
    set_fields(s_k); /* the trigger record carries the corners: saturation, "not a number" */
    s_a.isns.i_a[0] = 2000.0f;
    s_a.foc.vd = NAN;
    dtc_set(DTC_DESAT_LS, 0u);
    cap_isr(&s_a);
    s_k++;
    run_until_frozen(CAP_N);
    static uint8_t img[IMG_MAX];
    uint8_t id = 0u;
    const uint32_t nb = uds_read_all(img, &id);
    const uint32_t bytes = read_all();
    CHECK(nb == 1176u && memcmp(img, s_img, bytes) == 0);
    CHECK(system(NULL) != 0);
    int local = 0;
    char base[96];
    (void)snprintf(base, sizeof base, "build/cap-rt-%lx", (unsigned long)((uintptr_t)&local ^ (uintptr_t)time(NULL)));
    char hex[128];
    char csv[128];
    (void)snprintf(hex, sizeof hex, "%s.hex", base);
    (void)snprintf(csv, sizeof csv, "%s.csv", base);
    CHECK(write_blocks(hex, img, bytes, id, UINT32_MAX, UINT32_MAX));
    CHECK(decode(hex, csv) == 0);
    FILE *fp = fopen(csv, "r");
    CHECK(fp != NULL);
    uint32_t rows = 0u;
    bool meta = false;
    bool head = false;
    bool vals = true;
    char line[640];
    const char *want_head = "k,t_rel_us,t_us[us],flags[bits],i_a[A],i_b[A],i_c[A],i_d[A],i_q[A],i_d_ref[A],i_q_ref[A],"
                            "v_d[V],v_q[V],v_dc[V],th_rslv[rad],n_rslv[rpm]\n";
    while ((fp != NULL) && (fgets(line, sizeof line, fp) != NULL)) {
        if (line[0] == '#') {
            char want_meta[32];
            (void)snprintf(want_meta, sizeof want_meta, "# fw_id=0x%08lX\n", (unsigned long)TI_FW_ID);
            meta = meta || (strcmp(line, want_meta) == 0);
        } else if (!head) {
            head = (strcmp(line, want_head) == 0);
        } else {
            char *p = line;
            const long k = strtol(p, &p, 10);
            const long trel = strtol(p + 1, &p, 10);
            const unsigned long t = strtoul(p + 1, &p, 10);
            const unsigned long fl = strtoul(p + 1, &p, 16);
            const uint32_t ks = (uint32_t)((long)kt + k);
            double v[CAP_NCH];
            wave(ks, v);
            if (k == 0) {
                v[0] = 1638.35; /* saturated */
                v[7] = NAN;
            }
            vals = vals && (trel == (k * (long)PER_US)) && (t == (unsigned long)(uint32_t)(T0_US + (PER_US * ks)));
            vals = vals && (((fl & CAP_F_TRIGGER) != 0u) == (k == 0));
            for (uint32_t c = 0u; c < CAP_NCH; c++) {
                const double d = strtod(p + 1, &p);
                const double e = (c == 10u) ? remainder(d - v[c], TWO_PI) : (d - v[c]); /* the angle: modulo 2 pi */
                vals = vals && ((v[c] != v[c]) ? (d != d) : (fabs(e) <= ((0.505 * SCALE[c]) + (1e-6 * fabs(v[c])))));
            }
            rows++;
        }
    }
    if (fp != NULL) {
        (void)fclose(fp);
    }
    CHECK(meta && head && vals && rows == CAP_N);
    /* the decoder refuses what is not one complete capture */
    CHECK(write_blocks(hex, img, bytes, id, 700u, UINT32_MAX) && decode(hex, csv) != 0);   /* a block missing */
    CHECK(write_blocks(hex, img, bytes, id, nb - 1u, UINT32_MAX) && decode(hex, csv) != 0); /* the last one missing */
    CHECK(write_blocks(hex, img, bytes, id, UINT32_MAX, 42u) && decode(hex, csv) != 0);    /* two captures mixed */
    (void)remove(hex);
    (void)remove(csv);
}

/* ---------------- end to end on the simulated card ---------------- */
#define REF_N 160u
#define REF_BEFORE 40u
#define PRE_E2E 1500u

typedef struct {
    uint32_t t;
    uint32_t rows;
    uint32_t tf; /* the newest resolver frame */
    double v[CAP_NCH];
} snap_t;

static snap_t s_ref[REF_N];

static void snap(snap_t *s)
{
    s->t = g_app.t_isr_us;
    s->rows = g_app.fm.active;
    s->tf = g_app.rslv.t_frame_us;
    const double v[CAP_NCH] = {g_app.isns.i_a[0], g_app.isns.i_a[1], g_app.isns.i_a[2], g_app.foc.id, g_app.foc.iq,
                               g_app.foc.id_ref, g_app.foc.iq_ref, g_app.foc.vd, g_app.foc.vq, g_app.vdc.vdc,
                               g_app.rslv.theta, g_app.rslv.omega * RPM_PER_RAD_S / (double)g_app.cal.rslv.resolver_pp};
    (void)memcpy(s->v, v, sizeof v);
}

static bool run_to(float rpm, float torque)
{
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    if (!h_to_run(torque)) {
        return false;
    }
    h_ramp_speed(rpm, 600u);
    h_run_ms(20u);
    return g_app.sm.st == SM_RUN;
}

typedef enum { FC_DESAT, FC_OC, FC_OV, FC_RSLV } fc_t;

/* Arm (pre 1500), fill the pre-trigger part, watch 40 current-loop ISRs one at a time, inject, watch 120 more, let the
 * post-trigger part fill, read the image. Returns the index in s_ref of the trigger record (REF_N: not found). */
static uint32_t fault_capture(fc_t fc, hdr_t *h)
{
    const cap_cfg_t c = {.pre = PRE_E2E, .sources = CAP_SRC_DTC | CAP_SRC_ROW};
    CHECK(run_to(1000.0f, 100.0f));
    CHECK(cap_arm(&c) == CAP_OK);
    h_run_ms(100u); /* 2000 records at 20 kHz */
    CHECK(state() == CAP_ARMED); /* nothing else has fired */
    const uint32_t per = app_isr_period_us(&g_app);
    bool grid = true;
    for (uint32_t k = 0u; k < REF_N; k++) {
        if (k == REF_BEFORE) {
            if (fc == FC_DESAT) {
                sim_chain_desat(true, false);
            } else if (fc == FC_OC) {
                H.i_pk_a = 620.0f; /* its crest beyond the 601 A trip */
            } else if (fc == FC_OV) {
                H.link_override = true;
                sim_set_link_v(900.0f, 900.0f); /* beyond the 880 V trip */
            } else {
                sim_sdadc_freeze(HAL_SD_EXC, true);
                sim_sdadc_freeze(HAL_SD_SIN, true);
                sim_sdadc_freeze(HAL_SD_COS, true);
            }
        }
        h_isr_only_us(per);
        snap(&s_ref[k]);
        const uint32_t dt = s_ref[k].t - s_ref[(k > 0u) ? (k - 1u) : 0u].t;
        grid = grid && ((k == 0u) || ((dt + 5u >= per) && (dt <= per + 5u)));
    }
    CHECK(grid); /* one ISR per period; a priority-0 fault ISR may delay one entry by its own run (the PWM-ASC dead time) */
    h_run_ms(100u);
    CHECK(state() == CAP_FROZEN && read_all() != 0u);
    *h = hdr();
    CHECK(h->why == CAP_WHY_FAULT && h->trig == PRE_E2E && h->n == CAP_N);
    uint32_t tt;
    uint32_t f;
    int16_t ch[CAP_NCH];
    rec_at(h->trig, &tt, &f, ch);
    uint32_t jt = REF_N;
    for (uint32_t j = 0u; j < REF_N; j++) {
        jt = ((jt == REF_N) && (s_ref[j].t == tt)) ? j : jt;
    }
    CHECK(jt != REF_N && jt >= REF_BEFORE);
    if (jt == REF_N) {
        return REF_N;
    }
    /* every watched ISR is its record, to half an LSB: the waveform around the fault is the one the ISR computed */
    bool same = true;
    for (uint32_t j = 0u; j < REF_N; j++) {
        const uint32_t idx = (h->trig + j) - jt;
        rec_at(idx, &tt, &f, ch);
        same = same && (tt == s_ref[j].t) && ((f & CAP_F_ROWS) == (s_ref[j].rows & CAP_F_ROWS));
        for (uint32_t cc = 0u; cc < CAP_NCH; cc++) {
            same = same && near_ch(cc, ch[cc], s_ref[j].v[cc]);
        }
    }
    CHECK(same);
    /* and that waveform is the simulated machine's: up to the injection, three phase currents summing to zero on a
     * circle of the reference's amplitude (the harness plant follows the current reference) */
    double amp = 0.0;
    double ref = 0.0;
    bool kcl = true;
    for (uint32_t j = 0u; j < ((h->trig + REF_BEFORE) - jt); j++) {
        rec_at(j, &tt, &f, ch);
        const double ia = ch[0] * 0.05;
        const double ib = ch[1] * 0.05;
        const double ic = ch[2] * 0.05;
        const double al = ia;
        const double be = (ib - ic) / sqrt(3.0);
        amp = fmax(amp, sqrt((al * al) + (be * be)));
        ref = fmax(ref, sqrt(((ch[5] * 0.05) * (ch[5] * 0.05)) + ((ch[6] * 0.05) * (ch[6] * 0.05))));
        kcl = kcl && (fabs(ia + ib + ic) < 3.0);
    }
    CHECK(kcl && ref > 50.0 && fabs(amp - ref) < (0.03 * ref) + 2.0);
    return jt;
}

TEST(a_desat_is_captured_at_the_split_with_the_waveform_around_it)
{
    hdr_t h;
    const uint32_t jt = fault_capture(FC_DESAT, &h);
    CHECK(jt == REF_BEFORE); /* the first current-loop ISR after the fault ISR */
    CHECK(((h.rows & (1u << SS_ROW_FLT_HS)) != 0u) && dtc_active(DTC_DESAT_HS) && dtc_active((dtc_id_t)h.dtc));
}

TEST(an_overcurrent_is_captured_at_the_split_with_the_waveform_around_it)
{
    hdr_t h;
    const uint32_t jt = fault_capture(FC_OC, &h);
    CHECK((jt < REF_N) && ((h.rows & (1u << SS_ROW_OVERCURRENT)) != 0u) && h.dtc == DTC_OVERCURRENT);
    if (jt < REF_N) {
        const double trip = g_app.p->i_oc_trip_a;
        double before = 0.0;
        for (uint32_t k = 0u; k < 3u; k++) {
            before = fmax(before, fabs(s_ref[jt - 1u].v[k]));
        }
        const double at = fmax(fabs(s_ref[jt].v[0]), fmax(fabs(s_ref[jt].v[1]), fabs(s_ref[jt].v[2])));
        CHECK(at >= trip && before < trip); /* the record of the first sample beyond the trip */
    }
}

TEST(an_overvoltage_is_captured_at_the_split_with_the_waveform_around_it)
{
    hdr_t h;
    const uint32_t jt = fault_capture(FC_OV, &h);
    CHECK(jt == REF_BEFORE && ((h.rows & (1u << SS_ROW_OVERVOLTAGE)) != 0u) && h.dtc == DTC_OVERVOLTAGE);
    if (jt < REF_N) {
        CHECK(s_ref[jt].v[9] > g_app.p->ov_trip_v && fabs(s_ref[jt - 1u].v[9] - 750.0) < 5.0);
    }
}

TEST(a_resolver_loss_is_captured_at_the_split_with_the_waveform_around_it)
{
    hdr_t h;
    const uint32_t jt = fault_capture(FC_RSLV, &h);
    CHECK(h.rows == (1u << SS_ROW_RESOLVER_INVALID));
    if (jt < REF_N) {
        /* withdrawn at the first tick at which the newest frame is cal_rslv_hold_us (500 us) old */
        const uint32_t hold = g_app.p->cal_rslv_hold_us;
        CHECK((s_ref[jt].t - s_ref[jt].tf) >= hold && (s_ref[jt - 1u].t - s_ref[jt - 1u].tf) < hold && s_ref[jt].tf == s_ref[jt - 1u].tf);
    }
}

/* The read-out over the diagnostic bus, through the 1 ms task, while the inverter runs under torque: the ISR runs on its
 * grid throughout (arming, trigger, freeze, 1176 blocks), control is untouched, the frozen capture stays as it was. */
TEST(the_capture_reads_out_over_the_diagnostic_bus_while_the_inverter_runs)
{
    CHECK(run_to(1000.0f, 100.0f));
    const uint8_t cfg[12] = {0x31u, 0x01u, 0xF0u, 0x41u, 0x04u, 0x00u, CAP_SRC_DTC | CAP_SRC_ROW, 0u, 0u, 0u, 0u, 0u};
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 16u};
    hal_can_frame_t r;
    (void)memset(f.data, 0xAA, sizeof f.data);
    f.data[0] = 0u;
    f.data[1] = 12u;
    (void)memcpy(&f.data[2], cfg, 12u);
    sim_can_inject(HAL_CAN_DIAG, &f);
    const uint32_t n_isr0 = g_app.n_isr;
    const uint64_t t0 = hal_time_us64();
    h_run_ms(1u);
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &r) && r.len == 8u && r.data[0] == 4u && r.data[1] == 0x71u);
    h_run_ms(80u); /* pre 1024: 51 ms */
    f.len = 8u;
    f.data[0] = 4u;
    (void)memcpy(&f.data[1], RC_TRIG, 4u);
    sim_can_inject(HAL_CAN_DIAG, &f);
    h_run_ms(1u);
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &r) && r.data[0] == 4u && r.data[1] == 0x71u && r.data[4] == 0x42u);
    h_run_ms(80u);
    CHECK(state() == CAP_FROZEN && read_all() != 0u);
    const hdr_t h = hdr();
    CHECK(h.why == CAP_WHY_CMD && h.trig == 1024u && h.n == CAP_N && h.rate == 20000u);
    /* 1176 blocks, four requests per 1 ms task */
    static uint8_t img[IMG_MAX];
    const uint32_t nb = 1176u;
    uint32_t got = 0u;
    bool seq = true;
    bool running = true;
    for (uint32_t tick = 0u; (tick < 400u) && (got < nb); tick++) {
        f.data[0] = 3u;
        (void)memcpy(&f.data[1], RD_BLOCK, 3u);
        for (uint32_t i = 0u; i < 4u; i++) {
            sim_can_inject(HAL_CAN_DIAG, &f);
        }
        h_run_ms(1u);
        while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
            const uint32_t n = r.data[1];
            const uint32_t k = ((uint32_t)r.data[6] << 8) | r.data[7];
            seq = seq && (r.data[0] == 0u) && (r.data[2] == 0x62u) && (r.data[5] == h.id) && (k == got) && (n >= 7u);
            if (seq && (got < nb)) {
                (void)memcpy(&img[got * 56u], &r.data[8], n - 6u);
                got++;
            }
        }
        running = running && (g_app.sm.st == SM_RUN) && (hal_pwm_mode() == HAL_PWM_MOD);
    }
    CHECK(seq && got == nb && running && memcmp(img, s_img, CAP_HDR_BYTES + (CAP_N * CAP_REC_BYTES)) == 0);
    /* the ISR grid: exactly 2 f_sw per second from the arming to the end of the read-out, and every record one
     * period after the one before it */
    const uint32_t per = app_isr_period_us(&g_app);
    const uint32_t isrs = (uint32_t)((hal_time_us64() - t0) / per);
    CHECK((g_app.n_isr - n_isr0) == isrs || (g_app.n_isr - n_isr0) == (isrs + 1u));
    bool spaced = true;
    for (uint32_t j = 1u; j < h.n; j++) {
        uint32_t t1;
        uint32_t t2;
        uint32_t fl;
        int16_t ch[CAP_NCH];
        rec_at(j - 1u, &t1, &fl, ch);
        rec_at(j, &t2, &fl, ch);
        spaced = spaced && ((t2 - t1) == per);
    }
    CHECK(spaced);
    CHECK(state() == CAP_FROZEN && read_all() != 0u && memcmp(img, s_img, CAP_HDR_BYTES + (CAP_N * CAP_REC_BYTES)) == 0);
}

void suite_capture(void)
{
    RUN(records_every_channel_at_the_documented_scales);
    RUN(a_fault_trigger_lands_at_the_configured_split);
    RUN(an_early_trigger_keeps_only_the_records_it_has);
    RUN(the_command_trigger);
    RUN(the_level_trigger_needs_its_hysteresis);
    RUN(the_sources_mask_selects_the_triggers);
    RUN(the_ring_never_overruns_and_never_blocks);
    RUN(the_isr_copy_is_a_bounded_copy_on_the_host);
    RUN(uds_read_out_sequence_integrity_and_rearm);
    RUN(the_blocks_decode_to_the_waveform_through_the_node_decoder);
    RUN(a_desat_is_captured_at_the_split_with_the_waveform_around_it);
    RUN(an_overcurrent_is_captured_at_the_split_with_the_waveform_around_it);
    RUN(an_overvoltage_is_captured_at_the_split_with_the_waveform_around_it);
    RUN(a_resolver_loss_is_captured_at_the_split_with_the_waveform_around_it);
    RUN(the_capture_reads_out_over_the_diagnostic_bus_while_the_inverter_runs);
}
