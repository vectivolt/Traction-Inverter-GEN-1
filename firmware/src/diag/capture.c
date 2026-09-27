/* capture.c — FW-41: the sampled-waveform ring (capture.h, contract §10i). */
#include "capture.h"

#include <math.h>
#include <string.h>

#include "dtc.h"
#include "ti_math.h"

#define MASK (CAP_N - 1u)
#define K_A 20.0f                /* LSB per A (0.05 A) */
#define K_V 20.0f                /* LSB per V (0.05 V) */
#define K_TH (32768.0f / TI_2PI) /* LSB per rad: [0, 2 pi) -> 0 .. 32767, wrapped */

_Static_assert((CAP_N >= 64u) && (CAP_N <= 32768u) && ((CAP_N & (CAP_N - 1u)) == 0u),
               "CAP_N: a power of two, 64 .. 32768 (the header counts records in 16 bits)");

typedef struct {
    uint32_t t_us;
    uint32_t flags;
    int16_t ch[CAP_NCH];
} rec_t;
_Static_assert(sizeof(rec_t) == CAP_REC_BYTES, "record layout: 32 bytes, no padding");

/* The image's channel list (contract §10i): name, unit, type (0 u32, 1 i16), offset in the record, unit per LSB. */
typedef struct {
    char name[8];
    char unit[6];
    uint8_t type;
    uint8_t off;
    float scale;
} desc_t;

static const desc_t DESC[2u + CAP_NCH] = {
    {"t_us", "us", 0u, 0u, 1.0f},        {"flags", "bits", 0u, 4u, 1.0f},     {"i_a", "A", 1u, 8u, 1.0f / K_A},
    {"i_b", "A", 1u, 10u, 1.0f / K_A},   {"i_c", "A", 1u, 12u, 1.0f / K_A},   {"i_d", "A", 1u, 14u, 1.0f / K_A},
    {"i_q", "A", 1u, 16u, 1.0f / K_A},   {"i_d_ref", "A", 1u, 18u, 1.0f / K_A}, {"i_q_ref", "A", 1u, 20u, 1.0f / K_A},
    {"v_d", "V", 1u, 22u, 1.0f / K_V},   {"v_q", "V", 1u, 24u, 1.0f / K_V},   {"v_dc", "V", 1u, 26u, 1.0f / K_V},
    {"th_rslv", "rad", 1u, 28u, 1.0f / K_TH}, {"n_rslv", "rpm", 1u, 30u, 1.0f},
};

static _Alignas(32) rec_t s_buf[CAP_N]; /* one record per 32-byte D-cache line */

/* ISR-owned */
static cap_cfg_t s_cfg;   /* the running capture's configuration */
static uint32_t s_w;      /* the next slot */
static uint32_t s_n;      /* records since arming (saturates at CAP_N) */
static uint32_t s_left;   /* post-trigger records still to write */
static uint32_t s_rows0;  /* fm.active at the previous record */
static uint32_t s_dtc0;   /* dtc_events() at the previous record */
static bool s_lvl_ready;  /* the level's hysteresis condition was met since arming */
static float s_k_rpm;     /* LSB per rad/s of the observer (resolver electrical): 1 rpm mechanical */

/* written by the ISR, read by the task */
static volatile struct {
    uint8_t st; /* cap_state_t */
    uint8_t id;
    uint8_t why;
    uint32_t seq; /* freezes since power-up (cap_init leaves it) */
    uint8_t sku;
    uint16_t dtc;  /* the DTC whose occurrence triggered (DTC_NONE: none) */
    uint16_t rows; /* the §6 rows new at the trigger */
    uint32_t first; /* slot of the frozen capture's first record */
    uint32_t trig;  /* index of the trigger record in it */
    uint32_t n;     /* records in it */
    uint32_t rate_hz;
} s_x;

/* task -> ISR requests: the task writes s_cfg_req only while no arm is pending, then counts the request */
static volatile cap_cfg_t s_cfg_req;
static volatile uint32_t s_arm_req;
static volatile uint32_t s_arm_ack;
static volatile uint32_t s_trig_req;
static volatile uint32_t s_trig_ack;

static const cap_cfg_t DEFAULT_CFG = {.pre = (uint16_t)(CAP_N / 2u), .sources = CAP_SRC_DTC | CAP_SRC_ROW};

/* x * k to the nearest int16, saturated at +-32767; not a number -> CAP_RAW_NONE. Branch-free on the M7 (FPv5:
 * VMAXNM, VMINNM, a VRINT in the FPSCR's rounding mode — round to nearest, which nothing changes — and VCVT). */
static int16_t q16(float x, float k)
{
    const float v = x * k;
    return isnan(v) ? CAP_RAW_NONE : (int16_t)rintf(fminf(fmaxf(v, -32767.0f), 32767.0f));
}

/* an angle to 2 pi / 32768 rad per LSB, wrapped: [0, 2 pi) -> 0 .. 32767, a value that rounds to 32768 is 0 */
static int16_t qang(float th)
{
    const float v = th * K_TH;
    return (fabsf(v) < 1.0e9f) ? (int16_t)((uint32_t)(int32_t)rintf(v) & 0x7FFFu) : CAP_RAW_NONE; /* also NaN */
}

static uint32_t flags_of(const app_t *a)
{
    uint32_t f = (a->fm.active & CAP_F_ROWS) | (((uint32_t)a->br.mode & 3u) << CAP_F_BR_SHIFT) |
                 (((uint32_t)a->sm.st & 15u) << CAP_F_SM_SHIFT) | (((uint32_t)a->fm.dec.action & 7u) << CAP_F_ACT_SHIFT);
    f |= a->isns.valid ? CAP_F_ISNS_VALID : 0u;
    f |= a->vdc.valid ? CAP_F_VDC_VALID : 0u;
    f |= a->rslv.valid ? CAP_F_RSLV_VALID : 0u;
    f |= a->mod_req ? CAP_F_MOD_REQ : 0u;
    f |= a->zero_now ? CAP_F_ZERO_NOW : 0u;
    f |= a->foc.sat ? CAP_F_FOC_SAT : 0u;
    f |= a->foc.guard_trip ? CAP_F_FOC_GUARD : 0u;
    return f;
}

static void start(const app_t *a, const cap_cfg_t *c)
{
    dtc_id_t last;
    s_cfg = *c;
    s_w = 0u;
    s_n = 0u;
    s_left = 0u;
    s_rows0 = a->fm.active; /* rows already up, and DTCs already counted, never trigger */
    s_dtc0 = dtc_events(&last);
    s_lvl_ready = false;
    s_trig_ack = s_trig_req; /* a command posted for an earlier capture is dropped */
    s_x.why = (uint8_t)CAP_WHY_NONE;
    s_x.dtc = (uint16_t)DTC_NONE;
    s_x.rows = 0u;
    s_x.n = 0u;
    s_x.st = (uint8_t)CAP_ARMED;
}

void cap_init(const app_t *a)
{
    const uint32_t pp = (a->cal.rslv.resolver_pp > 0u) ? a->cal.rslv.resolver_pp : 1u;
    s_k_rpm = TI_RPM_PER_RAD_S / (float)pp;
    s_cfg_req = DEFAULT_CFG;
    s_arm_req = 0u;
    s_arm_ack = 0u;
    s_trig_req = 0u;
    s_x.id = 0u;
    start(a, &DEFAULT_CFG);
}

/* Rising: fires at v >= lvl once a sample was below lvl - hys since arming; falling: mirrored. Never on a
 * channel that was not a number. */
static bool level(const rec_t *r)
{
    const int32_t v = r->ch[s_cfg.lvl_ch - 2u];
    if (v == CAP_RAW_NONE) {
        return false;
    }
    const int32_t l = s_cfg.lvl;
    const int32_t h = (int32_t)s_cfg.hys;
    const bool fire = s_lvl_ready && (s_cfg.lvl_falling ? (v <= l) : (v >= l));
    s_lvl_ready = s_lvl_ready || (s_cfg.lvl_falling ? (v > (l + h)) : (v < (l - h)));
    return fire;
}

static cap_why_t check(const app_t *a, const rec_t *r)
{
    dtc_id_t last = DTC_NONE;
    const uint32_t rows = a->fm.active & CAP_F_ROWS;
    const uint32_t new_rows = rows & ~s_rows0;
    const uint32_t ev = dtc_events(&last);
    const bool dtc = (ev != s_dtc0) && ((s_cfg.sources & CAP_SRC_DTC) != 0u);
    const bool row = (new_rows != 0u) && ((s_cfg.sources & CAP_SRC_ROW) != 0u);
    s_rows0 = rows;
    s_dtc0 = ev;
    cap_why_t why = CAP_WHY_NONE;
    if (dtc || row) {
        why = CAP_WHY_FAULT;
        s_x.dtc = dtc ? (uint16_t)last : (uint16_t)DTC_NONE;
        s_x.rows = (uint16_t)new_rows;
    } else if (s_trig_req != s_trig_ack) {
        why = CAP_WHY_CMD;
    } else if (((s_cfg.sources & CAP_SRC_LEVEL) != 0u) && level(r)) {
        why = CAP_WHY_LEVEL;
    } else {
        /* nothing */
    }
    s_trig_ack = s_trig_req;
    return why;
}

/* TODO(HW): the cost of this copy on silicon (T-43): cycles from the entry to the return, ARMED and at a trigger. */
void cap_isr(const app_t *a)
{
    if (s_arm_req != s_arm_ack) {
        const cap_cfg_t c = {.pre = s_cfg_req.pre, .sources = s_cfg_req.sources, .lvl_ch = s_cfg_req.lvl_ch,
                             .lvl_falling = s_cfg_req.lvl_falling, .lvl = s_cfg_req.lvl, .hys = s_cfg_req.hys};
        s_arm_ack = s_arm_req;
        start(a, &c);
    }
    const uint8_t st = s_x.st;
    if ((st != (uint8_t)CAP_ARMED) && (st != (uint8_t)CAP_TRIGGERED)) {
        return; /* frozen (or never initialised): nothing is written */
    }
    rec_t *r = &s_buf[s_w];
    r->t_us = a->t_isr_us;
    r->flags = flags_of(a);
    r->ch[0] = q16(a->isns.i_a[0], K_A);
    r->ch[1] = q16(a->isns.i_a[1], K_A);
    r->ch[2] = q16(a->isns.i_a[2], K_A);
    r->ch[3] = q16(a->foc.id, K_A);
    r->ch[4] = q16(a->foc.iq, K_A);
    r->ch[5] = q16(a->foc.id_ref, K_A);
    r->ch[6] = q16(a->foc.iq_ref, K_A);
    r->ch[7] = q16(a->foc.vd, K_V);
    r->ch[8] = q16(a->foc.vq, K_V);
    r->ch[9] = q16(a->vdc.vdc, K_V);
    r->ch[10] = qang(a->rslv.theta);
    r->ch[11] = q16(a->rslv.omega, s_k_rpm);
    if (st == (uint8_t)CAP_ARMED) {
        const cap_why_t why = check(a, r);
        const uint32_t before = s_n;
        s_n = (s_n < CAP_N) ? (s_n + 1u) : CAP_N;
        if (why != CAP_WHY_NONE) {
            const uint32_t pre = (before < s_cfg.pre) ? before : s_cfg.pre;
            r->flags |= CAP_F_TRIGGER;
            s_left = (CAP_N - 1u) - s_cfg.pre;
            s_x.why = (uint8_t)why;
            s_x.first = (s_w - pre) & MASK;
            s_x.trig = pre;
            s_x.n = pre + 1u + s_left;
            s_x.rate_hz = 2u * a->gains.fsw_hz;
            s_x.sku = (uint8_t)a->p->sku;
            s_x.st = (uint8_t)CAP_TRIGGERED;
        }
    } else {
        s_left--; /* TRIGGERED: s_left >= 1 here */
    }
    if ((s_x.st == (uint8_t)CAP_TRIGGERED) && (s_left == 0u)) {
        s_x.id = (uint8_t)(s_x.id + 1u);
        s_x.seq = s_x.seq + 1u;
        s_x.st = (uint8_t)CAP_FROZEN; /* last: the task reads the capture once it sees FROZEN */
    }
    s_w = (s_w + 1u) & MASK;
}

static bool cfg_ok(const cap_cfg_t *c)
{
    const bool lvl = (c->lvl_ch >= 2u) && (c->lvl_ch < (2u + CAP_NCH)) && (c->lvl != CAP_RAW_NONE) && (c->hys <= 32767u);
    return (c->pre < CAP_N) && ((c->sources & (uint8_t)~(CAP_SRC_DTC | CAP_SRC_ROW | CAP_SRC_LEVEL)) == 0u) &&
           (((c->sources & CAP_SRC_LEVEL) == 0u) || lvl);
}

cap_res_t cap_arm(const cap_cfg_t *c)
{
    if (s_arm_req != s_arm_ack) {
        return CAP_EBUSY; /* the ISR has not taken the previous one yet */
    }
    if (c != NULL) {
        if (!cfg_ok(c)) {
            return CAP_ERANGE;
        }
        s_cfg_req = *c;
    }
    s_arm_req = s_arm_req + 1u;
    return CAP_OK;
}

cap_res_t cap_trigger(void)
{
    if (s_arm_req != s_arm_ack) {
        return CAP_EBUSY;
    }
    if (s_x.st != (uint8_t)CAP_ARMED) {
        return CAP_ESTATE;
    }
    s_trig_req = s_trig_req + 1u;
    return CAP_OK;
}

static bool readable(void) { return (s_x.st == (uint8_t)CAP_FROZEN) && (s_arm_req == s_arm_ack); }

void cap_status(cap_status_t *s)
{
    s->st = (cap_state_t)s_x.st;
    s->why = (cap_why_t)s_x.why;
    s->id = s_x.id;
    s->seq = s_x.seq;
    s->arm_pending = (s_arm_req != s_arm_ack);
    s->image_bytes = readable() ? (CAP_HDR_BYTES + (s_x.n * CAP_REC_BYTES)) : 0u;
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(&p[2], v >> 16);
}

/* The frozen capture's header, little-endian (contract §10i). Read only while readable(): the ISR writes none of it. */
static void header(uint8_t h[CAP_HDR_BYTES])
{
    const volatile cap_cfg_t *c = &s_cfg;
    uint32_t bits;
    (void)memset(h, 0, CAP_HDR_BYTES);
    h[0] = (uint8_t)'T';
    h[1] = (uint8_t)'I';
    h[2] = (uint8_t)'C';
    h[3] = (uint8_t)'P';
    h[4] = 1u; /* image format */
    h[5] = 1u; /* flags layout (capture.h) */
    put16(&h[6], CAP_HDR_BYTES);
    put32(&h[8], TI_FW_ID);
    h[12] = s_x.id;
    h[13] = s_x.why;
    put16(&h[14], s_x.dtc);
    put16(&h[16], s_x.rows);
    put16(&h[18], s_x.trig);
    put16(&h[20], s_x.n);
    put16(&h[22], c->pre);
    put32(&h[24], s_x.rate_hz);
    h[28] = c->sources;
    h[29] = (uint8_t)(c->lvl_ch | (c->lvl_falling ? 0x80u : 0u));
    put16(&h[30], (uint16_t)c->lvl);
    put16(&h[32], c->hys);
    h[34] = s_x.sku;
    h[35] = (uint8_t)CAP_REC_BYTES;
    h[36] = (uint8_t)(2u + CAP_NCH);
    h[37] = 20u; /* bytes per channel descriptor */
    for (uint32_t i = 0u; i < (2u + CAP_NCH); i++) {
        uint8_t *d = &h[40u + (20u * i)];
        (void)memcpy(d, DESC[i].name, 8u);
        (void)memcpy(&d[8], DESC[i].unit, 6u);
        d[14] = DESC[i].type;
        d[15] = DESC[i].off;
        (void)memcpy(&bits, &DESC[i].scale, 4u);
        put32(&d[16], bits);
    }
}

static void record(uint8_t out[CAP_REC_BYTES], uint32_t j)
{
    const volatile rec_t *r = &s_buf[(s_x.first + j) & MASK];
    put32(&out[0], r->t_us);
    put32(&out[4], r->flags);
    for (uint32_t i = 0u; i < CAP_NCH; i++) {
        put16(&out[8u + (2u * i)], (uint16_t)r->ch[i]);
    }
}

bool cap_image_read(uint32_t off, uint8_t *dst, uint32_t len)
{
    if (!readable()) {
        return false;
    }
    const uint32_t size = CAP_HDR_BYTES + (s_x.n * CAP_REC_BYTES);
    if ((len > size) || (off > (size - len))) {
        return false;
    }
    uint8_t h[CAP_HDR_BYTES];
    uint8_t rec[CAP_REC_BYTES];
    bool have_h = false;
    uint32_t cur = UINT32_MAX;
    for (uint32_t k = 0u; k < len; k++) { /* bounded by the image size */
        const uint32_t o = off + k;
        if (o < CAP_HDR_BYTES) {
            if (!have_h) {
                header(h);
                have_h = true;
            }
            dst[k] = h[o];
        } else {
            const uint32_t j = (o - CAP_HDR_BYTES) / CAP_REC_BYTES;
            if (j != cur) {
                record(rec, j);
                cur = j;
            }
            dst[k] = rec[(o - CAP_HDR_BYTES) % CAP_REC_BYTES];
        }
    }
    return true;
}
