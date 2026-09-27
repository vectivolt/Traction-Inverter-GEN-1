/* test_uds_diag.c — FW-40 (round 23): the diagnostic services (comms/uds_diag.c) on the whole firmware and the simulated
 * card. Requests go straight into the dispatcher (uds_handle) between two 1 ms tasks and their frames come from
 * uds_diag_tick(), so a DID or a DTC status is compared with the live state at the same instant; the end-to-end tests
 * put the frames on the diagnostic bus and let the task answer. Layouts are decoded from the contract (§10h), not
 * from the firmware's encoder. */
#include <string.h>

#include "dtc.h"
#include "dtc_table.h"
#include "harness.h"
#include "nvlog.h"
#include "test.h"
#include "uds.h"
#include "uds_diag.h"
#include "sha256.h"
#include "verify.h"
#include "boot.h"
#include "motor.h"

#define MSG_MAX UDS_DIAG_MSG_MAX
#define SEED 0x5EED0001u

typedef struct {
    uint8_t b[MSG_MAX];
    uint32_t n;
} msg_t;

static msg_t s_m; /* the last response */

/* ---------------- frames ---------------- */
static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t be24(const uint8_t *p) { return ((uint32_t)p[0] << 16) | be16(&p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | be24(&p[1]); }
static float bef(const uint8_t *p)
{
    const uint32_t b = be32(p);
    float x = 0.0f;
    memcpy(&x, &b, sizeof x);
    return x;
}
static bool same_f(float a, float b) { return memcmp(&a, &b, sizeof a) == 0; } /* bit-exact */

/* one request: the classic single frame up to 7 bytes, the CAN-FD escape format beyond */
static hal_can_frame_t sf_frame(const uint8_t *req, uint32_t n)
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u};
    memset(f.data, 0xAA, sizeof f.data);
    if (n <= 7u) {
        f.data[0] = (uint8_t)n;
        memcpy(&f.data[1], req, n);
    } else {
        f.len = 64u;
        f.data[0] = 0u;
        f.data[1] = (uint8_t)n;
        memcpy(&f.data[2], req, n);
    }
    return f;
}

static hal_can_frame_t fc_frame(uint8_t fs, uint8_t bs, uint8_t stmin)
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u};
    memset(f.data, 0xAA, sizeof f.data);
    f.data[0] = (uint8_t)(0x30u | fs);
    f.data[1] = bs;
    f.data[2] = stmin;
    return f;
}

/* into the dispatcher, as the 1 ms task's diag() does (the FW-40 frames answer nothing there) */
static bool put(const hal_can_frame_t *f, hal_can_frame_t *rsp) { return uds_handle(&g_app.uds, f, SEED, rsp); }

static void drain(void)
{
    hal_can_frame_t f;
    while (sim_can_pop_tx(HAL_CAN_DIAG, &f)) {
    }
}

static void dtick(void) { uds_diag_tick(&g_app.uds); }

/* the next frame on the response ID within `ticks` more calls of uds_diag_tick (periodic frames skipped) */
static bool next_rsp(hal_can_frame_t *f, uint32_t ticks)
{
    for (uint32_t k = 0u; k <= ticks; k++) {
        while (sim_can_pop_tx(HAL_CAN_DIAG, f)) {
            if (f->id == UDS_ID_RSP) {
                return true;
            }
        }
        if (k < ticks) {
            dtick();
        }
    }
    return false;
}

/* a single frame's payload, or a first frame's message reassembled under a flow control of block size 0, STmin 0 */
static bool take(const hal_can_frame_t *first, msg_t *out)
{
    const uint32_t pci = (uint32_t)first->data[0] >> 4;
    hal_can_frame_t r;
    if (pci == 0u) {
        const bool esc = (first->data[0] == 0u);
        out->n = esc ? first->data[1] : first->data[0];
        memcpy(out->b, &first->data[esc ? 2u : 1u], out->n);
        return (out->n > 0u) && (esc ? ((first->len > 8u) && (out->n > 7u)) : (first->len == 8u));
    }
    if ((pci != 1u) || (first->len != 64u)) {
        return false;
    }
    out->n = (((uint32_t)first->data[0] & 0x0Fu) << 8) | first->data[1];
    memcpy(out->b, &first->data[2], 62u);
    uint32_t got = 62u;
    uint32_t sn = 1u;
    const hal_can_frame_t fc = fc_frame(0u, 0u, 0u);
    (void)put(&fc, &r);
    for (uint32_t k = 0u; (k < 100u) && (got < out->n); k++) {
        hal_can_frame_t cf;
        if (!next_rsp(&cf, 1u) || (cf.data[0] != (0x20u | sn))) {
            return false;
        }
        const uint32_t n = ((out->n - got) < 63u) ? (out->n - got) : 63u;
        memcpy(&out->b[got], &cf.data[1], n);
        got += n;
        sn = (sn + 1u) & 0x0Fu;
    }
    return got == out->n;
}

/* a request between two tasks and its whole response */
static bool ask(const uint8_t *req, uint32_t n)
{
    drain();
    const hal_can_frame_t f = sf_frame(req, n);
    hal_can_frame_t r;
    if (put(&f, &r)) {
        return false; /* answered in the dispatcher's own frame: not a FW-40 request */
    }
    hal_can_frame_t first;
    return next_rsp(&first, 20u) && take(&first, &s_m);
}

/* end to end: the request on the diagnostic bus, the 1 ms task answering (single-frame responses) */
static bool ask_bus(const uint8_t *req, uint32_t n)
{
    drain();
    const hal_can_frame_t f = sf_frame(req, n);
    sim_can_inject(HAL_CAN_DIAG, &f);
    for (uint32_t k = 0u; k < 20u; k++) {
        h_run_ms(1u);
        hal_can_frame_t r;
        while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
            if (r.id == UDS_ID_RSP) {
                return take(&r, &s_m);
            }
        }
    }
    return false;
}

static bool is_nrc(uint8_t sid, uint8_t code)
{
    return (s_m.n == 3u) && (s_m.b[0] == 0x7Fu) && (s_m.b[1] == sid) && (s_m.b[2] == code);
}

static bool test_key(const uint8_t seed[UDS_SA_LEN], uint8_t key[UDS_SA_LEN])
{
    for (uint32_t i = 0u; i < UDS_SA_LEN; i++) {
        key[i] = (uint8_t)(seed[(i + 1u) % UDS_SA_LEN] ^ (0xA5u + i));
    }
    return true;
}

/* FW-32's SecurityAccess with the test key (its responses come back in the dispatcher's frame) */
static bool unlock(void)
{
    g_app.uds.key_fn = test_key;
    const uint8_t sq[2] = {0x27u, 0x01u};
    hal_can_frame_t f = sf_frame(sq, 2u);
    hal_can_frame_t r;
    if (!put(&f, &r) || (r.data[1] != 0x67u)) {
        return false;
    }
    uint8_t kq[2u + UDS_SA_LEN] = {0x27u, 0x02u};
    (void)test_key(&r.data[3], &kq[2]);
    f = sf_frame(kq, sizeof kq);
    return put(&f, &r) && (r.data[1] == 0x67u) && g_app.uds.unlocked;
}

typedef struct {
    uint8_t st[DTC_COUNT];
    uint8_t occ[DTC_COUNT];
    uint32_t first[DTC_COUNT];
    uint32_t last[DTC_COUNT];
} store_t;

static void store_copy(store_t *s)
{
    memset(s, 0, sizeof *s);
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        s->st[i] = dtc_status((dtc_id_t)i);
        s->occ[i] = dtc_occurrences((dtc_id_t)i);
        (void)dtc_times((dtc_id_t)i, &s->first[i], &s->last[i]);
    }
}

static bool store_same(const store_t *a, const store_t *b, dtc_id_t id)
{
    return (a->st[id] == b->st[id]) && (a->occ[id] == b->occ[id]) && (a->first[id] == b->first[id]) &&
           (a->last[id] == b->last[id]);
}

/* the snapshot record's 49 bytes (DID 0xFD2F), written from the contract's table */
static void enc_snapshot(uint8_t o[49], const nv_fault_t *r)
{
    const float f[4] = {r->speed_rpm, r->id_a, r->iq_a, r->vdc_v};
    const float g[5] = {r->op.t_cmd_nm, r->op.t_act_nm, r->op.t_mod_c, r->op.t_mt1_c, r->op.t_mt2_c};
    uint32_t b = 0u;
    o[0] = (uint8_t)(r->key_cycle >> 24);
    o[1] = (uint8_t)(r->key_cycle >> 16);
    o[2] = (uint8_t)(r->key_cycle >> 8);
    o[3] = (uint8_t)r->key_cycle;
    o[4] = (uint8_t)(r->t_ms >> 24);
    o[5] = (uint8_t)(r->t_ms >> 16);
    o[6] = (uint8_t)(r->t_ms >> 8);
    o[7] = (uint8_t)r->t_ms;
    o[8] = r->row;
    o[9] = r->action;
    for (uint32_t k = 0u; k < 4u; k++) {
        memcpy(&b, &f[k], 4u);
        o[10u + (4u * k)] = (uint8_t)(b >> 24);
        o[11u + (4u * k)] = (uint8_t)(b >> 16);
        o[12u + (4u * k)] = (uint8_t)(b >> 8);
        o[13u + (4u * k)] = (uint8_t)b;
    }
    o[26] = r->op.ext;
    o[27] = r->op.state;
    o[28] = r->op.t_valid;
    for (uint32_t k = 0u; k < 5u; k++) {
        memcpy(&b, &g[k], 4u);
        o[29u + (4u * k)] = (uint8_t)(b >> 24);
        o[30u + (4u * k)] = (uint8_t)(b >> 16);
        o[31u + (4u * k)] = (uint8_t)(b >> 8);
        o[32u + (4u * k)] = (uint8_t)b;
    }
}

/* a new key cycle from power-off, as t_run starts every test: retained RAM, NVM and the DTC store empty */
static void fresh(void)
{
    sim_fs26_config(NULL);
    sim_reset();
    sim_nvm_wipe();
    dtc_init();
    memset(&g_fm_retained, 0, sizeof g_fm_retained);
    memset(&g_app_session, 0, sizeof g_app_session);
    h_setup(TI_SKU_8XX_SIC);
}

/* RUN at 1000 rpm and 300 N m, a high-side DESAT, the VCU's one authorised retry, a second DESAT (the FW-15 latch) —
 * the path of scenarios: desat_one_authorised_retry_then_latch; the operating context before each is returned. */
static bool two_desats(float t_cmd[2], uint8_t st[2])
{
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    if (!h_to_run(300.0f)) {
        return false;
    }
    h_ramp_speed(1000.0f, 600u);
    h_run_ms(20u);
    t_cmd[0] = g_app.t_cmd_nm;
    st[0] = (uint8_t)g_app.sm.st;
    sim_chain_desat(true, false);
    h_run_ms(5u);
    H.retry_auth = true;
    h_run_ms(900u);
    if (!h_run_until(SM_RUN, 800u)) {
        return false;
    }
    h_run_ms(20u);
    t_cmd[1] = g_app.t_cmd_nm;
    st[1] = (uint8_t)g_app.sm.st;
    sim_chain_desat(true, false);
    h_run_ms(3000u);
    return dtc_active(DTC_DESAT_REPEAT) && (g_app.sm.st == SM_FAULT) && nv_idle();
}

/* ======================= the DTC table ======================= */
/* Every DTC of dtc.h has its row in the generated table (tools/dtc-table.mjs, the same rows the service tool's JSON
 * carries): its number is dtc_code() — 0xD1 0x00 id, three bytes, distinct — and a description. */
/* round 23: the root of trust is reported (DID 0xFD23): the host build's TEST key, with its key id */
TEST(the_root_of_trust_is_reported_with_the_test_key_id)
{
    h_boot();
    uint8_t key[32];
    uint8_t h[32];
    CHECK(img_pubkey(key));
    sha256(key, 32u, h);
    const uint32_t want = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | (uint32_t)h[3];
    const uint8_t ids[3] = {0x22u, 0xFDu, 0x23u};
    CHECK(ask(ids, 3u) && (s_m.n == (1u + 2u + 5u)));
    const uint8_t *d = &s_m.b[1];
    CHECK((be16(&d[0]) == 0xFD23u) && (d[2] == (uint8_t)IMG_ROOT_TEST) && (be32(&d[3]) == want));
    CHECK(want == 0xD51131ADu); /* the TEST key's id: a changed test key is noticed here and in tools/sign-image.mjs */
}

/* round 23, second pass: DID 0xFD24 is the FW-38 boot record (or state 0xFF without one) */
TEST(the_boot_record_did_mirrors_the_record)
{
    h_boot();
    const uint8_t ids[3] = {0x22u, 0xFDu, 0x24u};
    CHECK(ask(ids, 3u) && (s_m.n == (1u + 2u + 9u)));
    const uint8_t *d = &s_m.b[1];
    boot_rec_t r;
    if (boot_rec_read(&r)) {
        CHECK((be16(&d[0]) == 0xFD24u) && (d[2] == r.state) && (d[3] == r.last) && (d[4] == (uint8_t)r.target) &&
              (be32(&d[5]) == r.sec_counter) && (d[9] == r.lkg_valid) && (d[10] == r.last_err));
    } else {
        CHECK((be16(&d[0]) == 0xFD24u) && (d[2] == 0xFFu) && (be32(&d[5]) == 0u));
    }
}

/* round 23, second pass: DID 0xFD25 carries the record's motor data (the tool's ripple import over CAN needs ψ and pp) */
TEST(the_motor_did_mirrors_the_record)
{
    h_boot();
    const app_t *a = &g_app;
    const uint8_t ids[3] = {0x22u, 0xFDu, 0x25u};
    CHECK(ask(ids, 3u) && (s_m.n == (1u + 2u + 21u)));
    const uint8_t *d = &s_m.b[1];
    const motor_t *m = &a->cal.motor;
    CHECK((be16(&d[0]) == 0xFD25u) && (be32(&d[2]) == (uint32_t)(m->psi_wb * 1.0e6f + 0.5f)) && (d[6] == m->pp) &&
          (be32(&d[7]) == (uint32_t)(m->ld_h * 1.0e9f + 0.5f)) && (be32(&d[11]) == (uint32_t)(m->lq_h * 1.0e9f + 0.5f)) &&
          (be32(&d[15]) == (uint32_t)(m->rs_ohm * 1.0e6f + 0.5f)) && (be16(&d[19]) == (uint32_t)(m->n_max_rpm + 0.5f)) &&
          (be16(&d[21]) == (uint32_t)(m->id_demag_a * 10.0f + 0.5f)));
}

/* round 23, second pass: DID 0xFD26 carries the active record's inductance maps (the tool's read-back after a key cycle) */
TEST(the_map_did_mirrors_the_record)
{
    h_boot();
    const app_t *a = &g_app;
    const uint8_t ids[3] = {0x22u, 0xFDu, 0x26u};
    CHECK(ask(ids, 3u) && (s_m.n == (1u + 2u + 8u * MOTOR_MAP_N + 2u)));
    const uint8_t *d = &s_m.b[1];
    const motor_t *m = &a->cal.motor;
    bool ok = be16(&d[0]) == 0xFD26u;
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
        ok = ok && (be32(&d[2u + 4u * k]) == (uint32_t)(m->ld_map_h[k] * 1.0e9f + 0.5f)) &&
             (be32(&d[2u + 4u * (MOTOR_MAP_N + k)]) == (uint32_t)(m->lq_map_h[k] * 1.0e9f + 0.5f));
    }
    CHECK(ok && (be16(&d[2u + 8u * MOTOR_MAP_N]) == (uint32_t)(m->i_map_a * 10.0f + 0.5f)));
    CHECK(be32(&d[2]) == (uint32_t)(m->ld_h * 1.0e9f + 0.5f)); /* point 0 is the scalar */
}

TEST(every_dtc_has_a_number_and_a_description)
{
    CHECK(DTC_TABLE_N == (uint32_t)DTC_COUNT);
    if (DTC_TABLE_N != (uint32_t)DTC_COUNT) {
        printf("    dtc_table.h has %u rows, dtc.h %u: node tools/dtc-table.mjs --c\n", (unsigned)DTC_TABLE_N - 1u,
               (unsigned)DTC_COUNT - 1u);
    }
    uint32_t bad = 0u;
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        const dtc_table_row_t *r = &DTC_TABLE[i];
        const bool ok = (r->name != NULL) && (strncmp(r->name, "DTC_", 4u) == 0) && (r->text != NULL) &&
                        (strlen(r->text) >= 12u) && (r->number == dtc_code((dtc_id_t)i)) &&
                        (r->number == (0xD10000u | i)) && (r->number <= 0xFFFFFFu);
        if (!ok) {
            bad++;
            printf("    DTC id %u: %s\n", (unsigned)i, (r->name != NULL) ? r->name : "no row");
        }
        for (uint32_t j = 1u; j < i; j++) {
            bad += (dtc_code((dtc_id_t)j) == dtc_code((dtc_id_t)i)) ? 1u : 0u;
        }
    }
    CHECK(bad == 0u);
    CHECK(DTC_TABLE[DTC_NONE].name == NULL); /* DTC_NONE is not a DTC */
}

/* ======================= 0x19 01 / 02 / 0A against injected faults ======================= */
/* RUN, then the HVIL loop opened and the V_DC channels made to disagree (the simulation's own injections), and one DTC
 * that failed and then passed. Counts and lists by status mask equal the store bit for bit; the mask is ANDed with the
 * availability mask 0x7F (bit 7 is never reported); 0A lists every DTC in ascending number with its status. */
TEST(dtc_count_and_lists_by_status_mask_follow_injected_faults)
{
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    CHECK(h_to_run(100.0f));
    h_ramp_speed(1000.0f, 300u);
    sim_hvil_set(SIM_HVIL_OPEN);
    h_run_ms(200u);
    H.ch2_err = 0.2f;
    h_run_ms(50u);
    dtc_set(DTC_NVM, hal_time_ms());
    dtc_pass(DTC_NVM); /* failed once, passing now: 0x2E */
    CHECK(dtc_active(DTC_HVIL_OPEN) && dtc_active(DTC_VDC_DISAGREE) && !dtc_active(DTC_NVM));
    CHECK(dtc_status(DTC_NVM) == 0x2Eu);
    const uint8_t masks[6] = {0x01u, 0x08u, 0x04u, 0x20u, 0xFFu, 0x80u};
    for (uint32_t k = 0u; k < 6u; k++) {
        const uint8_t mk = masks[k];
        uint32_t want = 0u;
        for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
            want += ((dtc_status((dtc_id_t)i) & mk & 0x7Fu) != 0u) ? 1u : 0u;
        }
        const uint8_t q1[3] = {0x19u, 0x01u, mk};
        CHECK(ask(q1, 3u) && (s_m.n == 6u) && (s_m.b[0] == 0x59u) && (s_m.b[1] == 0x01u) && (s_m.b[2] == 0x7Fu) &&
              (s_m.b[3] == 0x01u) && (be16(&s_m.b[4]) == want));
        const uint8_t q2[3] = {0x19u, 0x02u, mk};
        CHECK(ask(q2, 3u) && (s_m.b[0] == 0x59u) && (s_m.b[1] == 0x02u) && (s_m.b[2] == 0x7Fu) &&
              (s_m.n == (3u + (4u * want))));
        uint32_t prev = 0u;
        bool ok = true;
        for (uint32_t e = 0u; (e < want) && ((3u + (4u * e) + 3u) < s_m.n); e++) {
            const uint32_t num = be24(&s_m.b[3u + (4u * e)]);
            const uint32_t id = num & 0xFFFFu;
            ok = ok && (num > prev) && ((num >> 16) == 0xD1u) && (id < (uint32_t)DTC_COUNT) &&
                 (s_m.b[6u + (4u * e)] == dtc_status((dtc_id_t)id)) && ((s_m.b[6u + (4u * e)] & mk) != 0u);
            prev = num;
        }
        CHECK(ok);
        if (mk == 0x01u) {
            CHECK(want >= 2u); /* HVIL open and the V_DC disagreement at least */
        }
        if (mk == 0x80u) {
            CHECK(want == 0u); /* warningIndicatorRequested is not tracked */
        }
    }
    /* testFailed excludes the passing DTC, confirmedDTC includes it */
    const uint8_t tf[3] = {0x19u, 0x02u, 0x01u};
    CHECK(ask(tf, 3u));
    bool nvm = false;
    bool hvil = false;
    for (uint32_t e = 3u; (e + 3u) < s_m.n; e += 4u) {
        nvm = nvm || (be24(&s_m.b[e]) == dtc_code(DTC_NVM));
        hvil = hvil || ((be24(&s_m.b[e]) == dtc_code(DTC_HVIL_OPEN)) && ((s_m.b[e + 3u] & 0x09u) == 0x09u));
    }
    CHECK(!nvm && hvil);
    const uint8_t cd[3] = {0x19u, 0x02u, 0x08u};
    CHECK(ask(cd, 3u));
    nvm = false;
    for (uint32_t e = 3u; (e + 3u) < s_m.n; e += 4u) {
        nvm = nvm || ((be24(&s_m.b[e]) == dtc_code(DTC_NVM)) && (s_m.b[e + 3u] == 0x2Eu));
    }
    CHECK(nvm);
    /* 0A: every DTC, ascending, its status */
    const uint8_t sup[2] = {0x19u, 0x0Au};
    CHECK(ask(sup, 2u) && (s_m.n == (3u + (4u * ((uint32_t)DTC_COUNT - 1u)))) && (s_m.b[1] == 0x0Au));
    bool all = true;
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        const uint32_t e = 3u + (4u * (i - 1u));
        all = all && (be24(&s_m.b[e]) == dtc_code((dtc_id_t)i)) && (s_m.b[e + 3u] == dtc_status((dtc_id_t)i));
    }
    CHECK(all);
    /* end to end through the 1 ms task: the same count */
    const uint8_t q1[3] = {0x19u, 0x01u, 0x01u};
    CHECK(ask(q1, 3u));
    const uint32_t direct = be16(&s_m.b[4]);
    CHECK(ask_bus(q1, 3u) && (s_m.b[0] == 0x59u) && (be16(&s_m.b[4]) == direct));
}

/* ======================= 0x19 04 snapshot records ======================= */
/* Two DESATs write two fault records (FW-15, in the fault ISR). 0x19 04 for DTC_DESAT_HS returns them newest first,
 * record n = the n-th newest, each byte for byte the stored nv_fault_t (the contract's layout) — and the operating
 * context FW-40 added: RUN, the torque command before each event, the extension flag. A record number without data
 * answers the DTC and its status only; 0x00 and 0x11-0xFE are out of range, as is an unknown DTC number. The scan reads
 * at most two ring records per task: the answer takes more than one task. */
TEST(snapshot_records_are_the_stored_fault_records)
{
    float t_cmd[2] = {0.0f, 0.0f};
    uint8_t st[2] = {0u, 0u};
    CHECK(two_desats(t_cmd, st));
    nv_fault_t r0;
    nv_fault_t r1;
    memset(&r0, 0, sizeof r0);
    memset(&r1, 0, sizeof r1);
    CHECK(nv_read_fault(0u, &r0) && nv_read_fault(1u, &r1) && (r0.code == (uint16_t)DTC_DESAT_HS) &&
          (r1.code == (uint16_t)DTC_DESAT_HS));
    CHECK((r1.op.ext == 1u) && (r1.op.state == st[0]) && same_f(r1.op.t_cmd_nm, t_cmd[0]) && (st[0] == SM_RUN));
    CHECK((r0.op.ext == 1u) && (r0.op.state == st[1]) && same_f(r0.op.t_cmd_nm, t_cmd[1]));
    CHECK((r1.row == (uint8_t)SS_ROW_FLT_HS) && (r1.speed_rpm > 900.0f) && (r1.vdc_v > 700.0f) && (r1.op.t_valid & 1u));
    uint8_t e0[49];
    uint8_t e1[49];
    enc_snapshot(e0, &r0);
    enc_snapshot(e1, &r1);
    const uint32_t num = dtc_code(DTC_DESAT_HS);
    const uint8_t all[6] = {0x19u, 0x04u, (uint8_t)(num >> 16), (uint8_t)(num >> 8), (uint8_t)num, 0xFFu};
    drain();
    hal_can_frame_t f = sf_frame(all, 6u);
    hal_can_frame_t r;
    CHECK(!put(&f, &r));
    dtick();
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &r)); /* the ring is scanned over tasks: nothing after the first */
    hal_can_frame_t first;
    CHECK(next_rsp(&first, 12u) && take(&first, &s_m));
    CHECK((s_m.n == (6u + (2u * 53u))) && (s_m.b[0] == 0x59u) && (s_m.b[1] == 0x04u) && (be24(&s_m.b[2]) == num) &&
          (s_m.b[5] == dtc_status(DTC_DESAT_HS)));
    CHECK((s_m.b[6] == 1u) && (s_m.b[7] == 1u) && (be16(&s_m.b[8]) == 0xFD2Fu) && (memcmp(&s_m.b[10], e0, 49u) == 0));
    CHECK((s_m.b[59] == 2u) && (s_m.b[60] == 1u) && (be16(&s_m.b[61]) == 0xFD2Fu) && (memcmp(&s_m.b[63], e1, 49u) == 0));
    const uint8_t rec2[6] = {0x19u, 0x04u, (uint8_t)(num >> 16), (uint8_t)(num >> 8), (uint8_t)num, 0x02u};
    CHECK(ask(rec2, 6u) && (s_m.n == 59u) && (s_m.b[6] == 2u) && (memcmp(&s_m.b[10], e1, 49u) == 0));
    const uint8_t rec1[6] = {0x19u, 0x04u, (uint8_t)(num >> 16), (uint8_t)(num >> 8), (uint8_t)num, 0x01u};
    CHECK(ask(rec1, 6u) && (s_m.n == 59u) && (s_m.b[6] == 1u) && (memcmp(&s_m.b[10], e0, 49u) == 0));
    const uint8_t rec3[6] = {0x19u, 0x04u, (uint8_t)(num >> 16), (uint8_t)(num >> 8), (uint8_t)num, 0x03u};
    CHECK(ask(rec3, 6u) && (s_m.n == 6u) && (s_m.b[0] == 0x59u)); /* no third event: the DTC and its status only */
    const uint32_t rep = dtc_code(DTC_DESAT_REPEAT); /* its events are DTC_DESAT_HS records */
    const uint8_t none[6] = {0x19u, 0x04u, (uint8_t)(rep >> 16), (uint8_t)(rep >> 8), (uint8_t)rep, 0xFFu};
    CHECK(ask(none, 6u) && (s_m.n == 6u) && (s_m.b[5] == dtc_status(DTC_DESAT_REPEAT)));
    const uint8_t r00[6] = {0x19u, 0x04u, (uint8_t)(num >> 16), (uint8_t)(num >> 8), (uint8_t)num, 0x00u};
    CHECK(ask(r00, 6u) && is_nrc(0x19u, UDS_NRC_OUT_OF_RANGE));
    const uint8_t r11[6] = {0x19u, 0x04u, (uint8_t)(num >> 16), (uint8_t)(num >> 8), (uint8_t)num, 0x11u};
    CHECK(ask(r11, 6u) && is_nrc(0x19u, UDS_NRC_OUT_OF_RANGE));
    const uint8_t unk[6] = {0x19u, 0x04u, 0xD1u, 0xFFu, 0xFFu, 0xFFu};
    CHECK(ask(unk, 6u) && is_nrc(0x19u, UDS_NRC_OUT_OF_RANGE));
}

/* A record written while the scan runs moves the ring: the scan starts again once, so the answer is the ring as it
 * is — the new record first, none twice; a second write during the second pass is NRC 0x22 (repeat the request). */
TEST(a_record_written_during_the_scan_is_never_answered_twice)
{
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    for (uint32_t k = 0u; k < (uint32_t)NV_FAULT_RING; k++) {
        const nv_fault_t e = {.key_cycle = 7u, .t_ms = 1000u + k, .code = (uint16_t)DTC_DESAT_LS, .speed_rpm = (float)k};
        (void)nv_queue(NV_REC_FAULT, &e, (uint16_t)sizeof e);
        for (int j = 0; (j < 200) && !nv_idle(); j++) {
            nv_service();
        }
    }
    const uint32_t num = dtc_code(DTC_DESAT_LS);
    const uint8_t all[6] = {0x19u, 0x04u, (uint8_t)(num >> 16), (uint8_t)(num >> 8), (uint8_t)num, 0xFFu};
    drain();
    hal_can_frame_t f = sf_frame(all, 6u);
    hal_can_frame_t r;
    CHECK(!put(&f, &r));
    dtick();
    dtick(); /* four records read */
    const nv_fault_t nw = {.key_cycle = 7u, .t_ms = 5000u, .code = (uint16_t)DTC_DESAT_LS, .speed_rpm = 99.0f};
    (void)nv_queue(NV_REC_FAULT, &nw, (uint16_t)sizeof nw);
    for (int j = 0; (j < 200) && !nv_idle(); j++) {
        nv_service();
    }
    hal_can_frame_t first;
    CHECK(next_rsp(&first, 30u) && take(&first, &s_m));
    CHECK(s_m.n == (6u + (16u * 53u)));
    bool ok = true;
    for (uint32_t k = 0u; (k < 16u) && ok; k++) {
        nv_fault_t e;
        memset(&e, 0, sizeof e);
        uint8_t enc[49];
        ok = nv_read_fault(k, &e);
        enc_snapshot(enc, &e);
        ok = ok && (s_m.b[6u + (53u * k)] == (uint8_t)(k + 1u)) && (memcmp(&s_m.b[10u + (53u * k)], enc, 49u) == 0);
    }
    CHECK(ok && (be32(&s_m.b[14]) == 5000u)); /* record 1 is the new one */
    /* two writes: once more, then NRC 0x22 */
    CHECK(!put(&f, &r));
    dtick();
    for (uint32_t w = 0u; w < 2u; w++) {
        const nv_fault_t more = {.key_cycle = 7u, .t_ms = 6000u + w, .code = (uint16_t)DTC_DESAT_LS};
        (void)nv_queue(NV_REC_FAULT, &more, (uint16_t)sizeof more);
        for (int j = 0; (j < 200) && !nv_idle(); j++) {
            nv_service();
        }
        for (uint32_t k = 0u; k < 9u; k++) {
            dtick(); /* the pass ends: the ring moved */
        }
    }
    CHECK(next_rsp(&first, 20u) && take(&first, &s_m) && is_nrc(0x19u, UDS_NRC_CONDITIONS));
}

/* ======================= 0x22 against the live state ======================= */
/* RUN at 1000 rpm and 120 N m. Every DID, alone and several per request (a classic single frame, and a CAN-FD escape
 * request of eight DIDs answered in a segmented response), decoded from the contract's layouts and compared with
 * the application's state at the same instant: floats bit for bit. */
TEST(dids_read_the_live_state)
{
    h_setup(TI_SKU_8XX_SIC);
    H.coolant_c = 55.0f;
    H.ch2_err = 0.01f; /* the two V_DC channels read 1 % apart (inside the 5 % agreement): each is told apart */
    h_boot();
    CHECK(h_to_run(120.0f));
    h_ramp_speed(1000.0f, 400u);
    h_run_ms(30u);
    const app_t *a = &g_app;
    uint8_t q[3] = {0x22u, 0xF2u, 0x00u};
    /* 0xF200 operating state */
    CHECK(ask(q, 3u) && (s_m.n == 13u) && (s_m.b[0] == 0x62u) && (be16(&s_m.b[1]) == 0xF200u));
    const uint8_t *d = &s_m.b[3];
    const uint32_t fl0 = (a->sm.st == SM_FAULT ? 1u : 0u) | (a->tlim.derate_active ? 2u : 0u) |
                         (a->fm.keep_hv ? 4u : 0u) | (a->fm.no_safe_state ? 8u : 0u) |
                         (a->service_required ? 16u : 0u) | (a->speed_limit_req ? 32u : 0u) |
                         (a->so.self_test_done ? 64u : 0u) | (a->so.torque_enable ? 128u : 0u);
    CHECK((d[0] == SM_RUN) && (d[0] == (uint8_t)a->sm.st) && (d[1] == (uint8_t)a->br.mode) && (d[1] == BR_MOD) &&
          (d[2] == (uint8_t)dis_hv_state(&a->vdc)) && (d[2] == TI_HV_PRESENT) && (d[3] == fl0) && (d[4] == 0u) &&
          (d[5] == 0xFFu) && (be16(&d[6]) == a->fm.active) && (be16(&d[8]) == a->fm.latched));
    /* 0xF201 arming */
    q[2] = 0x01u;
    CHECK(ask(q, 3u) && (s_m.n == 15u));
    d = &s_m.b[3];
    CHECK((d[0] == a->evidence) && (d[0] == ARM_EV_ALL) && (d[1] == 0u) && (d[2] == 1u) && (d[3] == 1u) &&
          (d[4] == 1u) && (d[5] == 1u) && (d[6] == 1u));
    CHECK((d[7] == 0xFDu) && (d[8] == (uint8_t)a->selftest) && (d[9] == (uint8_t)a->st.failed) && (d[10] == 0u) &&
          (d[11] == 0u)); /* arm, init OK, calibration, gains, FS0B, gate power, MCU_GATE_EN; no no-arm latch */
    /* 0xF202 speed and torque */
    q[2] = 0x02u;
    CHECK(ask(q, 3u) && (s_m.n == 23u));
    d = &s_m.b[3];
    CHECK(same_f(bef(&d[0]), a->speed_rpm) && (d[4] == 7u) && same_f(bef(&d[5]), a->can.torque_req_nm) &&
          same_f(bef(&d[9]), a->t_cmd_nm) && same_f(bef(&d[13]), a->t_act_nm) && (d[17] == (uint8_t)a->can.gear) &&
          (d[18] == (uint8_t)a->dir.active));
    CHECK((d[19] == ((a->mod_req ? 1u : 0u) | (a->zero_now ? 2u : 0u) | (a->so.torque_reduced ? 4u : 0u) |
                     (a->can.enable_req ? 8u : 0u))) && (bef(&d[9]) > 100.0f) && (bef(&d[0]) > 900.0f));
    /* 0xF203 currents */
    q[2] = 0x03u;
    CHECK(ask(q, 3u) && (s_m.n == 36u));
    d = &s_m.b[3];
    CHECK((be32(&d[0]) == a->t_isr_us) && same_f(bef(&d[4]), a->isns.i_a[0]) && same_f(bef(&d[8]), a->isns.i_a[1]) &&
          same_f(bef(&d[12]), a->isns.i_a[2]) && same_f(bef(&d[16]), a->foc.id) && same_f(bef(&d[20]), a->foc.iq) &&
          same_f(bef(&d[24]), a->foc.id_ref) && same_f(bef(&d[28]), a->foc.iq_ref) && (d[32] == 0x1Fu));
    /* 0xF204 DC link */
    q[2] = 0x04u;
    CHECK(ask(q, 3u) && (s_m.n == 29u));
    d = &s_m.b[3];
    CHECK(!same_f(a->vdc.v_ch[0], a->vdc.v_ch[1]));
    CHECK(same_f(bef(&d[0]), a->vdc.vdc) && same_f(bef(&d[4]), a->vdc.v_ch[0]) && same_f(bef(&d[8]), a->vdc.v_ch[1]) &&
          same_f(bef(&d[12]), a->vdc.vofs_v) && same_f(bef(&d[16]), a->vdc.v5gd_v) &&
          same_f(bef(&d[20]), a->can.v_pack) && (d[24] == 0x37u) && (d[25] == TI_HV_PRESENT));
    /* 0xF205 temperatures */
    q[2] = 0x05u;
    CHECK(ask(q, 3u) && (s_m.n == 43u));
    d = &s_m.b[3];
    bool temps = true;
    uint32_t valid = 0u;
    uint32_t faults = 0u;
    for (uint32_t k = 0u; k < (uint32_t)TEMP_COUNT; k++) {
        temps = temps && same_f(bef(&d[4u * k]), a->temp.ch[k].t_c);
        valid |= a->temp.ch[k].valid ? (1u << k) : 0u;
        faults |= ((uint32_t)a->temp.ch[k].fault & 3u) << (2u * k);
    }
    bool any = false;
    bool allv = false;
    CHECK(temps && (d[28] == valid) && ((valid & 7u) == 7u) && (be16(&d[29]) == faults) &&
          same_f(bef(&d[31]), temp_module_max(&a->temp, &any, &allv)) && same_f(bef(&d[35]), 55.0f) && (d[39] == 1u));
    /* 0xF206 limits */
    q[2] = 0x06u;
    CHECK(ask(q, 3u) && (s_m.n == 36u));
    d = &s_m.b[3];
    CHECK(same_f(bef(&d[0]), a->tlim.i_limit_rms_a) && same_f(bef(&d[4]), a->tlim.t_lim_motor_nm) &&
          same_f(bef(&d[8]), a->tlim.t_lim_regen_nm) && same_f(bef(&d[12]), a->tlim.derate) &&
          same_f(bef(&d[16]), a->tlim.coolant_factor) && same_f(bef(&d[20]), a->tlim.peak_used_s) &&
          same_f(bef(&d[24]), a->can.p_chg_w) && same_f(bef(&d[28]), a->can.p_dis_w));
    CHECK(d[32] == ((a->tlim.derate_active ? 1u : 0u) | (a->tlim.peak_exhausted ? 2u : 0u) |
                    (can_bms_fresh(&a->can, hal_time_ms(), a->p) ? 4u : 0u) | (a->speed_limit_req ? 8u : 0u) |
                    (a->so.torque_reduced ? 16u : 0u)));
    /* 0xF207 watchdog and heartbeat */
    q[2] = 0x07u;
    CHECK(ask(q, 3u) && (s_m.n == 46u));
    d = &s_m.b[3];
    const uint32_t now_us = hal_time_us();
    const uint32_t now_ms = hal_time_ms();
    CHECK((d[0] == 0x07u) && (d[1] == a->fs.wd_err_cnt) && (be32(&d[2]) == a->fs.n_refresh) &&
          (be32(&d[6]) == (now_us - a->fs.last_refresh_us)) && (be32(&d[10]) == a->fs.n_comm_err) &&
          (be32(&d[14]) == a->n_isr) && (be32(&d[18]) == (now_us - a->t_isr_us)) && (d[22] == 0x0Fu) &&
          (be32(&d[23]) == (now_ms - a->can.last_ms)) && (be32(&d[27]) == (now_ms - a->can.bms_last_ms)) &&
          (be32(&d[31]) == a->can.n_frozen) && (be32(&d[35]) == a->can.n_jump) && (be32(&d[39]) == a->can.n_len));
    CHECK((be32(&d[2]) > 100u) && (be32(&d[6]) < 2500u) && (be32(&d[14]) > 1000u));
    /* 0xF208 uptime and DTCs */
    q[2] = 0x08u;
    CHECK(ask(q, 3u) && (s_m.n == 19u));
    d = &s_m.b[3];
    uint32_t active = 0u;
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        active += dtc_active((dtc_id_t)i) ? 1u : 0u;
    }
    CHECK((be32(&d[0]) == hal_time_ms()) && (be32(&d[4]) == a->key_cycle) && (d[8] == 1u) && (be16(&d[9]) == active) &&
          (be16(&d[11]) == dtc_confirmed_count()) &&
          (be24(&d[13]) == ((dtc_first_active() != DTC_NONE) ? dtc_code(dtc_first_active()) : 0u)));
    /* 0xFD20 identity, 0xFD21 calibration, 0xFD22 the validation record (h_setup's: 14.2 us, this image) */
    const uint8_t ids[7] = {0x22u, 0xFDu, 0x20u, 0xFDu, 0x21u, 0xFDu, 0x22u};
    CHECK(ask(ids, 7u) && (s_m.n == (1u + 19u + 23u + 24u)));
    d = &s_m.b[1];
    CHECK((be16(&d[0]) == 0xFD20u) && (be32(&d[2]) == TI_FW_ID) && (d[6] == TI_SKU_8XX_SIC) &&
          (d[7] == (uint8_t)a->hw_sku) && (d[8] == a->cal.sku) && (memcmp(&d[9], h_serial(), 8u) == 0) &&
          (be16(&d[17]) == ((uint32_t)DTC_COUNT - 1u)));
    d = &s_m.b[20];
    CHECK((be16(&d[0]) == 0xFD21u) && (be16(&d[2]) == CALIB_LAYOUT_VERSION) && (be16(&d[4]) == CALIB_LAYOUT_VERSION) && (be32(&d[6]) == 0u) &&
          (be32(&d[10]) == 0x1001u) && (be32(&d[14]) == a->cal.crc32) && (be32(&d[18]) == a->cal.fsw_hz) &&
          (d[22] == 0u));
    d = &s_m.b[43];
    CHECK((be16(&d[0]) == 0xFD22u) && (d[2] == 1u) && (d[3] == ARM_EV_VALIDATED) && (d[4] == ARM_EV_VALIDATED) &&
          (d[5] == TI_SKU_8XX_SIC) && (be32(&d[6]) == TI_FW_ID) && (be32(&d[10]) == 14200u) &&
          (memcmp(&d[14], h_serial(), 8u) == 0) && (be16(&d[22]) == 1u));
    /* eight DIDs in a CAN-FD escape request: one segmented response, the same bytes as eight single reads */
    const uint8_t eight[17] = {0x22u, 0xF2u, 0x00u, 0xF2u, 0x01u, 0xF2u, 0x02u, 0xF2u, 0x03u,
                               0xF2u, 0x04u, 0xF2u, 0x05u, 0xF2u, 0x06u, 0xFDu, 0x20u};
    CHECK(ask(eight, 17u) && (s_m.n == (1u + 16u + 10u + 12u + 20u + 33u + 26u + 40u + 33u + 17u)));
    msg_t all = s_m;
    uint32_t off = 1u;
    bool same = true;
    for (uint32_t k = 0u; k < 8u; k++) {
        const uint8_t one[3] = {0x22u, eight[1u + (2u * k)], eight[2u + (2u * k)]};
        same = same && ask(one, 3u) && (memcmp(&all.b[off], &s_m.b[1], s_m.n - 1u) == 0);
        off += s_m.n - 1u;
    }
    CHECK(same && (off == all.n));
    /* the supported ones answered, the others left out; none supported: NRC 0x31; the snapshot DID is 0x19 04's */
    const uint8_t mixed[5] = {0x22u, 0xF2u, 0xFFu, 0xF2u, 0x08u};
    CHECK(ask(mixed, 5u) && (s_m.n == 19u) && (be16(&s_m.b[1]) == 0xF208u));
    const uint8_t nope[3] = {0x22u, 0xF2u, 0xFFu};
    CHECK(ask(nope, 3u) && is_nrc(0x22u, UDS_NRC_OUT_OF_RANGE));
    const uint8_t alien[3] = {0x22u, 0x12u, 0x34u}; /* a DID nobody answers: out of range, not "service not supported" */
    CHECK(ask(alien, 3u) && is_nrc(0x22u, UDS_NRC_OUT_OF_RANGE));
    const uint8_t snap[3] = {0x22u, 0xFDu, 0x2Fu};
    CHECK(ask(snap, 3u) && is_nrc(0x22u, UDS_NRC_OUT_OF_RANGE));
    /* end to end: the identity through the 1 ms task */
    const uint8_t id1[3] = {0x22u, 0xFDu, 0x20u};
    CHECK(ask_bus(id1, 3u) && (s_m.n == 20u) && (be32(&s_m.b[3]) == TI_FW_ID));
}

/* ======================= 0x14 gating ======================= */
static void set_clearable(void) { dtc_set(DTC_TAU_MISMATCH, hal_time_ms()); } /* information: no monitor re-sets it */

/* 0x14 is gated exactly like the FW-32 routine: without SecurityAccess NRC 0x33; unlocked, NRC 0x22 while HV is
 * present, while it is unknown (the channels disagree) and while the bridge is armed — nothing cleared, the unlock
 * kept; disarmed with HV absent the clear happens, and consumes the unlock. */
TEST(clear_is_gated_like_the_service_routine)
{
    const uint8_t clr[4] = {0x14u, 0xFFu, 0xFFu, 0xFFu};
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    CHECK(h_run_until(SM_PRECHARGE_WAIT, 3000u));
    set_clearable();
    CHECK((dis_hv_state(&g_app.vdc) == TI_HV_SAFE) && (g_app.br.mode == BR_DISARMED));
    CHECK(ask(clr, 4u) && is_nrc(0x14u, UDS_NRC_SECURITY_DENIED) && dtc_active(DTC_TAU_MISMATCH));
    CHECK(unlock());
    H.contactors = TI_CONT_PRECHARGE; /* HV present, still disarmed */
    h_run_ms(400u);
    CHECK((dis_hv_state(&g_app.vdc) == TI_HV_PRESENT) && (g_app.br.mode == BR_DISARMED));
    CHECK(ask(clr, 4u) && is_nrc(0x14u, UDS_NRC_CONDITIONS) && dtc_active(DTC_TAU_MISMATCH) && g_app.uds.unlocked);
    H.contactors = TI_CONT_CLOSED; /* armed */
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 500u));
    h_run_ms(5u);
    CHECK(g_app.br.mode != BR_DISARMED);
    CHECK(ask(clr, 4u) && is_nrc(0x14u, UDS_NRC_CONDITIONS) && dtc_active(DTC_TAU_MISMATCH) && g_app.uds.unlocked);
    H.link_override = true; /* the link reads 20 V for a moment: HV "absent", the bridge still armed */
    sim_set_link_v(20.0f, 20.0f);
    h_run_ms(3u);
    CHECK((dis_hv_state(&g_app.vdc) == TI_HV_SAFE) && (g_app.br.mode != BR_DISARMED));
    CHECK(ask(clr, 4u) && is_nrc(0x14u, UDS_NRC_CONDITIONS) && dtc_active(DTC_TAU_MISMATCH) && g_app.uds.unlocked);
    /* HV unknown: the channels disagree, disarmed (a fresh key cycle, never armed) */
    fresh();
    h_boot();
    CHECK(h_run_until(SM_PRECHARGE_WAIT, 3000u));
    CHECK(unlock());
    set_clearable();
    H.link_override = true;
    sim_set_link_v(30.0f, 200.0f);
    h_run_ms(5u);
    CHECK((dis_hv_state(&g_app.vdc) == TI_HV_UNKNOWN) && (g_app.br.mode == BR_DISARMED));
    CHECK(ask(clr, 4u) && is_nrc(0x14u, UDS_NRC_CONDITIONS) && dtc_active(DTC_TAU_MISMATCH));
    /* HV absent and disarmed: cleared, the unlock consumed */
    sim_set_link_v(0.0f, 0.0f);
    h_run_ms(5u);
    CHECK((dis_hv_state(&g_app.vdc) == TI_HV_SAFE) && g_app.uds.unlocked);
    CHECK(ask(clr, 4u) && (s_m.n == 1u) && (s_m.b[0] == 0x54u));
    CHECK(!dtc_active(DTC_TAU_MISMATCH) && (dtc_status(DTC_TAU_MISMATCH) == 0x50u) &&
          (dtc_occurrences(DTC_TAU_MISMATCH) == 0u) && !g_app.uds.unlocked);
    set_clearable();
    CHECK(ask(clr, 4u) && is_nrc(0x14u, UDS_NRC_SECURITY_DENIED) && dtc_active(DTC_TAU_MISMATCH)); /* one per unlock */
    /* one DTC by its number */
    CHECK(unlock());
    const uint32_t num = dtc_code(DTC_TAU_MISMATCH);
    const uint8_t one[4] = {0x14u, (uint8_t)(num >> 16), (uint8_t)(num >> 8), (uint8_t)num};
    CHECK(ask(one, 4u) && (s_m.b[0] == 0x54u) && !dtc_active(DTC_TAU_MISMATCH));
}

/* ======================= permanent latches ======================= */
/* After the second DESAT (the FW-15 latch) and with the link made safe and the bridge disarmed, a group clear clears
 * what it may and leaves the DESAT class untouched — status, occurrences, first and last time — and the retry stays
 * refused; a clear of DTC_DESAT_REPEAT alone is NRC 0x22. The stuck-on QDIS service lock, NVM-kept, is kept too. */
TEST(permanent_latches_are_not_cleared)
{
    float t_cmd[2];
    uint8_t st[2];
    CHECK(two_desats(t_cmd, st));
    H.contactors = TI_CONT_OPEN;
    H.link_override = true; /* the link discharged */
    sim_set_link_v(0.0f, 0.0f);
    h_run_ms(20u);
    CHECK((dis_hv_state(&g_app.vdc) == TI_HV_SAFE) && (g_app.br.mode == BR_DISARMED));
    set_clearable();
    store_t before;
    store_copy(&before);
    CHECK(unlock());
    const uint8_t clr[4] = {0x14u, 0xFFu, 0xFFu, 0xFFu};
    CHECK(ask(clr, 4u) && (s_m.b[0] == 0x54u));
    store_t after;
    store_copy(&after);
    CHECK(dtc_active(DTC_DESAT_HS) && dtc_active(DTC_DESAT_REPEAT));
    CHECK(store_same(&before, &after, DTC_DESAT_HS) && store_same(&before, &after, DTC_DESAT_REPEAT));
    CHECK(!dtc_active(DTC_TAU_MISMATCH) && (dtc_occurrences(DTC_TAU_MISMATCH) == 0u));
    CHECK(!fm_retry_allowed(&g_app.fm, true, hal_time_ms() + 10000u, g_app.p) && (g_app.fm.desat_count >= 2u));
    CHECK(unlock());
    const uint32_t num = dtc_code(DTC_DESAT_REPEAT);
    const uint8_t one[4] = {0x14u, (uint8_t)(num >> 16), (uint8_t)(num >> 8), (uint8_t)num};
    CHECK(ask(one, 4u) && is_nrc(0x14u, UDS_NRC_CONDITIONS) && dtc_active(DTC_DESAT_REPEAT) && g_app.uds.unlocked);
    store_copy(&after);
    CHECK(store_same(&before, &after, DTC_DESAT_REPEAT));
    /* the service lock: a stuck-on QDIS latched in an earlier key cycle */
    fresh();
    const nv_service_t lock = {.magic = NV_SERVICE_MAGIC, .dtc = (uint16_t)DTC_QDIS_STUCK_ON, .key_cycle = 1u};
    (void)nv_queue(NV_REC_DTC, &lock, (uint16_t)sizeof lock);
    for (int j = 0; (j < 200) && !nv_idle(); j++) {
        nv_service();
    }
    h_boot();
    h_run_ms(300u);
    CHECK(g_app.service_required && dtc_active(DTC_QDIS_STUCK_ON) && (dis_hv_state(&g_app.vdc) == TI_HV_SAFE));
    CHECK(unlock());
    CHECK(ask(clr, 4u) && (s_m.b[0] == 0x54u) && dtc_active(DTC_QDIS_STUCK_ON) && g_app.service_required);
}

/* ======================= malformed requests ======================= */
typedef struct {
    uint8_t n;
    uint8_t rq[20];
    uint8_t nrc;
} bad_t;

/* Every malformed request is refused with its NRC (length, sub-function, out of range, security) and changes nothing;
 * a frame that is not a valid single frame gets no answer at all. Then 4000 frames from a fixed-seed generator —
 * FW-40's services, flow controls, broken PCIs and lengths: every answer is a well-formed single or first frame with a
 * positive response to the request's service or one of these NRCs; the DTC store is unchanged (0x14 stays locked). */
TEST(malformed_requests_are_refused_and_a_bounded_fuzz_changes_nothing)
{
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    h_run_ms(300u);
    set_clearable();
    const bad_t T[] = {
        {1u, {0x19u}, 0x13u},
        {2u, {0x19u, 0x03u}, 0x12u},
        {3u, {0x19u, 0x81u, 0xFFu}, 0x12u},
        {2u, {0x19u, 0x01u}, 0x13u},
        {4u, {0x19u, 0x01u, 0xFFu, 0x00u}, 0x13u},
        {2u, {0x19u, 0x02u}, 0x13u},
        {3u, {0x19u, 0x0Au, 0x00u}, 0x13u},
        {5u, {0x19u, 0x04u, 0xD1u, 0x00u, 0x11u}, 0x13u},
        {7u, {0x19u, 0x04u, 0xD1u, 0x00u, 0x11u, 0x01u, 0x00u}, 0x13u},
        {6u, {0x19u, 0x04u, 0xD1u, 0xFFu, 0xFFu, 0x01u}, 0x31u},
        {6u, {0x19u, 0x04u, 0x00u, 0x00u, 0x11u, 0x01u}, 0x31u},
        {6u, {0x19u, 0x04u, 0xD1u, 0x00u, 0x11u, 0x00u}, 0x31u},
        {6u, {0x19u, 0x04u, 0xD1u, 0x00u, 0x11u, 0xFEu}, 0x31u},
        {1u, {0x22u}, 0x13u},
        {2u, {0x22u, 0xF2u}, 0x13u},
        {4u, {0x22u, 0xF2u, 0x00u, 0x00u}, 0x13u},
        {19u, {0x22u, 0xF2u, 0x00u, 0xF2u, 0x01u, 0xF2u, 0x02u, 0xF2u, 0x03u, 0xF2u, 0x04u, 0xF2u, 0x05u, 0xF2u, 0x06u,
               0xF2u, 0x07u, 0xF2u, 0x08u}, 0x13u},
        {3u, {0x22u, 0xF2u, 0xFFu}, 0x31u},
        {3u, {0x22u, 0xFDu, 0x2Fu}, 0x31u},
        {1u, {0x2Au}, 0x13u},
        {2u, {0x2Au, 0x03u}, 0x13u},
        {3u, {0x2Au, 0x05u, 0x00u}, 0x31u},
        {3u, {0x2Au, 0x00u, 0x00u}, 0x31u},
        {3u, {0x2Au, 0x03u, 0xFFu}, 0x31u},
        {7u, {0x2Au, 0x03u, 0x00u, 0x01u, 0x02u, 0x03u, 0x04u}, 0x31u},
        {3u, {0x14u, 0xFFu, 0xFFu}, 0x13u},
        {5u, {0x14u, 0xFFu, 0xFFu, 0xFFu, 0x00u}, 0x13u},
        {4u, {0x14u, 0xD1u, 0xFFu, 0xFFu}, 0x31u},
        {4u, {0x14u, 0x00u, 0x00u, 0x01u}, 0x31u},
        {4u, {0x14u, 0xFFu, 0xFFu, 0xFFu}, 0x33u},
    };
    store_t before;
    store_copy(&before);
    for (uint32_t k = 0u; k < TI_ARRAY_LEN(T); k++) {
        const bool ok = ask(T[k].rq, T[k].n) && is_nrc(T[k].rq[0], T[k].nrc);
        CHECK(ok);
        if (!ok) {
            printf("    request %u (SID 0x%02X, %u bytes)\n", (unsigned)k, T[k].rq[0], T[k].n);
        }
    }
    CHECK(g_app.udsd.n_per == 0u); /* the refused 0x2A scheduled nothing */
    /* not a valid single frame: no answer */
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u, .data = {0x00u, 0x19u, 0x0Au}}; /* SF_DL 0 */
    hal_can_frame_t g = {.id = UDS_ID_REQ, .len = 8u, .data = {0x00u, 0x03u, 0x19u, 0x0Au}}; /* escape in 8 bytes */
    hal_can_frame_t h = {.id = UDS_ID_REQ, .len = 64u, .data = {0x00u, 0x3Fu, 0x19u, 0x0Au}}; /* SF_DL > 62 */
    hal_can_frame_t fc = fc_frame(0u, 0u, 0u); /* no segmented response waits */
    hal_can_frame_t o = {.id = 0x7E0u, .len = 8u, .data = {0x02u, 0x19u, 0x0Au}}; /* another ECU's */
    hal_can_frame_t r;
    drain();
    CHECK(!put(&f, &r) && !put(&g, &r) && !put(&h, &r) && !put(&fc, &r) && !put(&o, &r));
    dtick();
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &r));
    /* the fuzz */
    uint32_t seed = 0x2468ACE1u;
    uint32_t answers = 0u;
    uint32_t wrong = 0u;
    static const uint8_t SIDS[4] = {0x14u, 0x19u, 0x22u, 0x2Au};
    static const uint8_t NRCS[5] = {0x12u, 0x13u, 0x22u, 0x31u, 0x33u};
    for (uint32_t it = 0u; it < 4000u; it++) {
        hal_can_frame_t q = {.id = UDS_ID_REQ};
        for (uint32_t k = 0u; k < HAL_CAN_MAX_LEN; k++) {
            seed = (seed * 1664525u) + 1013904223u;
            q.data[k] = (uint8_t)(seed >> 24);
        }
        seed = (seed * 1664525u) + 1013904223u;
        const uint32_t kind = (seed >> 24) % 8u;
        static const uint8_t LENS[8] = {8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u};
        q.len = LENS[(seed >> 8) % 8u];
        const uint8_t sid = SIDS[(seed >> 4) % 4u];
        if (kind < 4u) { /* a classic single frame */
            q.len = 8u;
            q.data[0] = (uint8_t)(1u + ((seed >> 12) % 7u));
            q.data[1] = sid;
        } else if (kind < 6u) { /* an escape single frame */
            q.data[0] = 0u;
            q.data[1] = (uint8_t)((seed >> 12) % 64u);
            q.data[2] = sid;
        } else if (kind == 6u) { /* a flow control */
            q.data[0] = (uint8_t)(0x30u | ((seed >> 12) & 0x0Fu));
        } else {
            /* anything, a random PCI */
        }
        uint8_t req_sid = 0u;
        if (q.data[0] != 0u && (q.data[0] >> 4) == 0u) {
            req_sid = q.data[1];
        } else if (q.data[0] == 0u) {
            req_sid = q.data[2];
        } else {
            /* not a request */
        }
        if ((req_sid == 0x27u) || (req_sid == 0x31u) || (req_sid == 0x2Eu) || (req_sid == 0x10u) ||
            (req_sid == 0x11u) || (req_sid == 0x34u) || (req_sid == 0x36u) || (req_sid == 0x37u)) {
            continue; /* the other services are not FW-40's to fuzz */
        }
        drain();
        (void)put(&q, &r);
        for (uint32_t k = 0u; k < 12u; k++) {
            dtick();
        }
        hal_can_frame_t a;
        while (sim_can_pop_tx(HAL_CAN_DIAG, &a)) {
            if (a.id == UDS_ID_PERIODIC) {
                wrong += ((a.len < 12u) || (a.data[0] > 8u)) ? 1u : 0u;
                continue;
            }
            const uint32_t pci = (uint32_t)a.data[0] >> 4;
            if (pci == 3u) { /* round 23 (item 11): this ECU's flow control for a first frame the fuzz sent */
                wrong += ((a.data[0] != 0x30u) && (a.data[0] != 0x32u)) ? 1u : 0u;
                continue;
            }
            answers++;
            if (req_sid == 0u) { /* round 23 (item 11): a random first frame completed by random consecutive frames: any
                                  * service may answer it — structurally */
                wrong += ((a.id != UDS_ID_RSP) || (pci > 2u)) ? 1u : 0u;
                continue;
            }
            const uint8_t *p = (a.data[0] == 0u) ? &a.data[2] : &a.data[(pci == 1u) ? 2u : 1u];
            const bool pos = (p[0] == (uint8_t)(req_sid + 0x40u));
            bool neg = (p[0] == 0x7Fu) && (p[1] == req_sid);
            bool known = false;
            for (uint32_t k = 0u; k < 5u; k++) {
                known = known || (p[2] == NRCS[k]);
            }
            neg = neg && known;
            wrong += ((a.id != UDS_ID_RSP) || (pci > 2u) || (!pos && !neg && (pci != 2u))) ? 1u : 0u;
        }
    }
    CHECK((answers > 1000u) && (wrong == 0u) && (g_app.udsd.n_per <= UDS_DIAG_PDID_MAX));
    store_t after;
    store_copy(&after);
    bool same = true;
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        same = same && store_same(&before, &after, (dtc_id_t)i);
    }
    CHECK(same && dtc_active(DTC_TAU_MISMATCH)); /* 0x14 never passed its SecurityAccess gate */
}

/* ======================= the transport ======================= */
/* A 0x19 0A response (the longest after a snapshot read) is a first frame and consecutive frames under the tester's
 * flow control: nothing before it, the block size and STmin kept (a busy mailbox only delays a frame, sequence
 * numbers continue), a WAIT holds, an overflow or N_Bs (1 s) abandons it, and a new request aborts it. */
TEST(segmented_responses_follow_the_testers_flow_control)
{
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    h_run_ms(300u);
    const uint32_t total = 3u + (4u * ((uint32_t)DTC_COUNT - 1u));
    const uint8_t sup[2] = {0x19u, 0x0Au};
    hal_can_frame_t f = sf_frame(sup, 2u);
    hal_can_frame_t r;
    hal_can_frame_t c;
    drain();
    CHECK(!put(&f, &r));
    dtick();
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &r) && (r.id == UDS_ID_RSP) && (r.len == 64u) &&
          (r.data[0] == (0x10u | (total >> 8))) && (r.data[1] == (uint8_t)total) && (r.data[2] == 0x59u));
    for (uint32_t k = 0u; k < 5u; k++) {
        sim_advance_us(1000u);
        dtick();
    }
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &r)); /* no consecutive frame without a flow control */
    const hal_can_frame_t cts = fc_frame(0u, 2u, 3u); /* block size 2, STmin 3 ms */
    CHECK(!put(&cts, &r));
    dtick();
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &c) && (c.data[0] == 0x21u) && (c.len == 64u)); /* the first at once */
    sim_advance_us(1000u);
    dtick();
    sim_advance_us(1000u);
    dtick();
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &c)); /* STmin */
    sim_advance_us(1000u);
    sim_can_tx_busy(HAL_CAN_DIAG, 1u); /* the mailbox busy once: the frame waits, it is not lost */
    dtick();
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &c));
    sim_advance_us(1000u);
    dtick();
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &c) && (c.data[0] == 0x22u));
    for (uint32_t k = 0u; k < 5u; k++) { /* the block is done: wait for the next flow control */
        sim_advance_us(1000u);
        dtick();
    }
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &c));
    const hal_can_frame_t wt = fc_frame(1u, 0u, 0u);
    CHECK(!put(&wt, &r));
    sim_advance_us(1000u);
    dtick();
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &c)); /* WAIT */
    const hal_can_frame_t go = fc_frame(0u, 0u, 0u);
    CHECK(!put(&go, &r));
    uint32_t sn = 3u;
    uint32_t got = 62u + (2u * 63u);
    for (uint32_t k = 0u; (k < 10u) && (got < total); k++) {
        dtick();
        CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &c) && (c.data[0] == (0x20u | sn)));
        got += ((total - got) < 63u) ? (total - got) : 63u;
        sn = (sn + 1u) & 0x0Fu;
    }
    static const uint8_t FD[8] = {8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u};
    const uint32_t tail = 1u + (((total - 62u) % 63u == 0u) ? 63u : ((total - 62u) % 63u)); /* PCI + the last bytes */
    uint32_t want = 0u;
    for (uint32_t k = 0u; (k < 8u) && (want == 0u); k++) {
        want = (FD[k] >= tail) ? FD[k] : 0u;
    }
    CHECK((got == total) && (c.len == want)); /* the last frame in the smallest length that holds it (round 23: the
                                               * length follows the DTC count, which grows by appending) */
    dtick();
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &c));
    /* N_Bs: no flow control within 1 s abandons it; a late one is nobody's */
    const uint32_t ab = g_app.udsd.n_aborted;
    CHECK(!put(&f, &r));
    dtick();
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &r) && ((r.data[0] >> 4) == 1u));
    for (uint32_t k = 0u; k < 1001u; k++) {
        sim_advance_us(1000u);
        dtick();
    }
    CHECK(g_app.udsd.n_aborted == (ab + 1u));
    CHECK(!put(&go, &r));
    dtick();
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &c));
    /* overflow abandons it */
    CHECK(!put(&f, &r));
    dtick();
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &r));
    const hal_can_frame_t ovf = fc_frame(2u, 0u, 0u);
    CHECK(!put(&ovf, &r) && (g_app.udsd.n_aborted == (ab + 2u)));
    dtick();
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &c));
    /* a new request aborts the transfer in flight */
    CHECK(!put(&f, &r));
    dtick();
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &r) && !put(&go, &r));
    dtick();
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &c) && (c.data[0] == 0x21u));
    const uint8_t id1[3] = {0x22u, 0xFDu, 0x20u};
    CHECK(ask(id1, 3u) && (s_m.n == 20u) && (g_app.udsd.n_aborted == (ab + 3u)));
    dtick();
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &c));
}

/* ======================= the periodic stream ======================= */
static uint32_t periodic(uint8_t *pdids, uint32_t max) /* this task's periodic frames; their pDIDs */
{
    hal_can_frame_t a;
    uint32_t n = 0u;
    while (sim_can_pop_tx(HAL_CAN_DIAG, &a)) {
        if ((a.id == UDS_ID_PERIODIC) && (n < max)) {
            pdids[n] = a.data[0];
            n++;
        }
    }
    return n;
}

/* 0x2A: at most one periodic frame per task, none in a task that sends a response frame; each DID at its mode's
 * period (fast 1 ms, medium 10 ms, slow 100 ms), round robin among those due; a rate replaced, some or all stopped;
 * four at most (a fifth: NRC 0x31, nothing changed); a busy mailbox drops the frame (counted), never sends it twice; a
 * frame's data are the DID's bytes at that instant. */
TEST(periodic_dids_are_rate_limited_to_one_frame_per_task)
{
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    h_run_ms(300u);
    uint8_t p[8];
    const uint8_t fast0[3] = {0x2Au, 0x03u, 0x00u};
    CHECK(ask(fast0, 3u) && (s_m.n == 1u) && (s_m.b[0] == 0x6Au) && (g_app.udsd.n_per == 1u));
    uint32_t n0 = 0u;
    for (uint32_t k = 0u; k < 100u; k++) {
        sim_advance_us(1000u);
        dtick();
        const uint32_t n = periodic(p, 8u);
        CHECK(n <= 1u);
        n0 += ((n == 1u) && (p[0] == 0x00u)) ? 1u : 0u;
    }
    CHECK(n0 == 100u); /* 1 kHz */
    /* the frame is the DID's bytes now */
    sim_advance_us(1000u);
    dtick();
    hal_can_frame_t a;
    bool got = false;
    while (sim_can_pop_tx(HAL_CAN_DIAG, &a)) {
        got = got || (a.id == UDS_ID_PERIODIC);
        if (a.id == UDS_ID_PERIODIC) {
            break;
        }
    }
    CHECK(got && (a.len == 12u) && (a.data[0] == 0x00u));
    const uint8_t rd[3] = {0x22u, 0xF2u, 0x00u};
    const hal_can_frame_t keep = a;
    CHECK(ask(rd, 3u) && (memcmp(&keep.data[1], &s_m.b[3], 10u) == 0));
    /* four: 0xF200 fast, 0xF203 and 0xF205 fast, 0xF207 slow */
    const uint8_t slow7[3] = {0x2Au, 0x01u, 0x07u};
    CHECK(ask(slow7, 3u) && (s_m.b[0] == 0x6Au));
    const uint8_t fast35[4] = {0x2Au, 0x03u, 0x03u, 0x05u};
    CHECK(ask(fast35, 4u) && (s_m.b[0] == 0x6Au) && (g_app.udsd.n_per == 4u));
    const uint8_t fifth[3] = {0x2Au, 0x03u, 0x08u};
    CHECK(ask(fifth, 3u) && is_nrc(0x2Au, UDS_NRC_OUT_OF_RANGE) && (g_app.udsd.n_per == 4u));
    uint32_t cnt[9] = {0u};
    uint32_t frames = 0u;
    for (uint32_t k = 0u; k < 300u; k++) {
        sim_advance_us(1000u);
        dtick();
        const uint32_t n = periodic(p, 8u);
        CHECK(n <= 1u);
        frames += n;
        if (n == 1u) {
            cnt[p[0]]++;
        }
    }
    CHECK(frames == 300u); /* one per task, never more */
    CHECK((cnt[7] >= 2u) && (cnt[7] <= 4u)); /* slow: 100 ms */
    CHECK((cnt[0] >= 95u) && (cnt[3] >= 95u) && (cnt[5] >= 95u) && ((cnt[0] + cnt[3] + cnt[5] + cnt[7]) == 300u));
    /* 0xF200 to medium (10 ms), 0xF203 and 0xF205 stopped */
    const uint8_t med0[3] = {0x2Au, 0x02u, 0x00u};
    CHECK(ask(med0, 3u) && (s_m.b[0] == 0x6Au) && (g_app.udsd.n_per == 4u));
    const uint8_t stop35[4] = {0x2Au, 0x04u, 0x03u, 0x05u};
    CHECK(ask(stop35, 4u) && (s_m.b[0] == 0x6Au) && (g_app.udsd.n_per == 2u));
    memset(cnt, 0, sizeof cnt);
    frames = 0u;
    for (uint32_t k = 0u; k < 200u; k++) {
        sim_advance_us(1000u);
        dtick();
        const uint32_t n = periodic(p, 8u);
        frames += n;
        if (n == 1u) {
            cnt[p[0]]++;
        }
    }
    CHECK((cnt[0] >= 19u) && (cnt[0] <= 21u) && (cnt[7] >= 1u) && (cnt[7] <= 3u) && (cnt[3] == 0u) && (cnt[5] == 0u));
    /* a busy mailbox: the frames of those tasks are dropped, counted, not sent later */
    const uint8_t fastonly[3] = {0x2Au, 0x03u, 0x00u};
    const uint8_t stopall[2] = {0x2Au, 0x04u};
    CHECK(ask(stopall, 2u) && (g_app.udsd.n_per == 0u) && ask(fastonly, 3u));
    sim_advance_us(1000u);
    dtick();
    (void)periodic(p, 8u);
    const uint32_t dropped = g_app.udsd.n_dropped;
    sim_can_tx_busy(HAL_CAN_DIAG, 3u);
    for (uint32_t k = 0u; k < 3u; k++) {
        sim_advance_us(1000u);
        dtick();
        CHECK(periodic(p, 8u) == 0u);
    }
    CHECK(g_app.udsd.n_dropped == (dropped + 3u));
    sim_advance_us(1000u);
    dtick();
    CHECK(periodic(p, 8u) == 1u); /* one, not four */
    /* stopped: nothing more */
    CHECK(ask(stopall, 2u) && (g_app.udsd.n_per == 0u));
    for (uint32_t k = 0u; k < 20u; k++) {
        sim_advance_us(1000u);
        dtick();
        CHECK(periodic(p, 8u) == 0u);
    }
}

/* The stream at its limit (four DIDs at 1 kHz, subscribed on the bus) beside a run at 3000 rpm and 150 N m: every
 * control output of every task — the torque command and applied torque, the current references, the duty cycles,
 * the state, the bridge, the FS26 answers — and the simulated time each task ends at are bit-identical to the same
 * run without it. The diagnostic layer takes no simulated time (it calls nothing that waits), and one periodic frame
 * goes out per task. */
TEST(the_stream_changes_no_control_output_and_no_task_timing)
{
    enum { N = 400 };
    typedef struct {
        float t_cmd, t_act, id_ref, iq_ref, duty[3];
        uint8_t st, br;
        uint32_t n_refresh;
        uint64_t t_end_ns;
    } rec_t;
    static rec_t run[2][N];
    uint32_t frames = 0u;
    uint32_t per_task_max = 0u;
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        fresh();
        h_boot();
        CHECK(h_to_run(150.0f));
        h_ramp_speed(3000.0f, 600u);
        if (pass == 1u) {
            const uint8_t sub[6] = {0x2Au, 0x03u, 0x00u, 0x03u, 0x05u, 0x07u};
            const hal_can_frame_t f = sf_frame(sub, 6u);
            sim_can_inject(HAL_CAN_DIAG, &f);
        }
        for (uint32_t k = 0u; k < (uint32_t)N; k++) {
            h_run_ms(1u);
            rec_t *r = &run[pass][k];
            memset(r, 0, sizeof *r);
            r->t_cmd = g_app.t_cmd_nm;
            r->t_act = g_app.t_act_nm;
            r->id_ref = g_app.id_ref;
            r->iq_ref = g_app.iq_ref;
            for (uint32_t j = 0u; j < 3u; j++) {
                r->duty[j] = g_app.foc.duty[j];
            }
            r->st = (uint8_t)g_app.sm.st;
            r->br = (uint8_t)g_app.br.mode;
            r->n_refresh = g_app.fs.n_refresh;
            r->t_end_ns = sim_now_ns();
            uint8_t p[8];
            const uint32_t n = periodic(p, 8u);
            if (pass == 1u) {
                frames += n;
                per_task_max = (n > per_task_max) ? n : per_task_max;
            }
        }
    }
    CHECK(memcmp(run[0], run[1], sizeof run[0]) == 0);
    CHECK((frames >= ((uint32_t)N - 2u)) && (per_task_max == 1u)); /* one per task after the subscription's answer */
    CHECK((run[0][N - 1].st == SM_RUN) && (g_app.udsd.n_per == 4u));
    /* the layer alone, the stream due and a segmented response in flight: no simulated time passes */
    const uint8_t sup[2] = {0x19u, 0x0Au};
    const hal_can_frame_t f = sf_frame(sup, 2u);
    hal_can_frame_t r;
    (void)put(&f, &r);
    const hal_can_frame_t cts = fc_frame(0u, 0u, 0u);
    for (uint32_t k = 0u; k < 10u; k++) {
        const uint64_t t0 = sim_now_ns();
        dtick();
        if (k == 1u) {
            (void)put(&cts, &r);
        }
        CHECK(sim_now_ns() == t0);
    }
}

void suite_uds_diag(void)
{
    RUN(every_dtc_has_a_number_and_a_description);
    RUN(the_root_of_trust_is_reported_with_the_test_key_id);
    RUN(the_boot_record_did_mirrors_the_record);
    RUN(the_motor_did_mirrors_the_record);
    RUN(the_map_did_mirrors_the_record);
    RUN(dtc_count_and_lists_by_status_mask_follow_injected_faults);
    RUN(snapshot_records_are_the_stored_fault_records);
    RUN(a_record_written_during_the_scan_is_never_answered_twice);
    RUN(dids_read_the_live_state);
    RUN(clear_is_gated_like_the_service_routine);
    RUN(permanent_latches_are_not_cleared);
    RUN(malformed_requests_are_refused_and_a_bounded_fuzz_changes_nothing);
    RUN(segmented_responses_follow_the_testers_flow_control);
    RUN(periodic_dids_are_rate_limited_to_one_frame_per_task);
    RUN(the_stream_changes_no_control_output_and_no_task_timing);
}
