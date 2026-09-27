/* calib.c — FW-20 record construction and validation. */
#include "calib.h"

#include <stddef.h>
#include <string.h>

#include "ti_crc.h"

void calib_nominal(calib_t *c, const ti_params_t *p, const uint8_t serial[8])
{
    (void)memset(c, 0, sizeof *c);
    c->layout_version = CALIB_LAYOUT_VERSION;
    c->sku = (uint8_t)p->sku;
    (void)memcpy(c->hw_serial, serial, 8u);
    c->motor_id = 0u;
    c->fsw_hz = p->fsw_hz[0];
    for (uint32_t i = 0u; i < 3u; i++) {
        c->isns[i] = (isns_cal_t){.offset_v = p->isns_zero_v, .gain_v_per_a = p->isns_sens_v_per_a, .sign = 1};
    }
    for (uint32_t i = 0u; i < 2u; i++) {
        c->vdc[i] = (vdc_cal_t){.gain = p->vdc_div_ratio, .offset_v = 0.0f};
    }
    /* monitor chain: REXM 0.70 / 0.82 (mean 0.76) into SDADC1, +/-32767 = +/-5 V: 0.76 x 6553 / 2 = 2500
     * codes of carrier amplitude per V pp at the protected node (EOL measures it) */
    c->rslv = (rslv_cal_t){.ratio_nom = 0.8f, .exc_code_per_vpp = 2500.0f, .sin_gain = 1.0f, .cos_gain = 1.0f,
                           .motor_pp = 4u, .resolver_pp = 1u};
    c->motor = motor_screening();
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) { /* round 23 (FW-45): flat maps — the scalars (no saturation) */
        c->motor.ld_map_h[k] = c->motor.ld_h;
        c->motor.lq_map_h[k] = c->motor.lq_h;
    }
    c->motor.i_map_a = p->i_crest_a;
    c->mt = (temp_mt_cal_t){.type = TEMP_MT_PT1000, .r25_ohm = 10000.0f, .b_k = 3435.0f};
    calib_seal(c);
}

void calib_seal(calib_t *c)
{
    c->crc32 = ti_crc32(c, offsetof(calib_t, crc32));
}

static bool in(float x, float lo, float hi) { return (x >= lo) && (x <= hi); }

/* Round 23 (FW-45): each point in the inductance class range, non-increasing with the current (saturation only lowers it)
 * beyond MOTOR_MAP_RISE_TOL — the FW-39 routine's points scatter by about 0.1 % on a machine that does not saturate, and
 * a strict rule refused every such commit (found by the service tool) — and the last >= 0.3 x the first (a sanity bound,
 * the current loop's gain floor).
 * Round 24: and the model's flux lambda = L(i) i rises with the current, with a margin — the apparent inductance alone does
 * not make it (L 1.75 mH to 0.56 mH over one segment passed: dlambda/di -1.8 mH there). motor_sat's L is linear on segment
 * k (x in [k, k + 1] breakpoint steps), so dlambda/di = L_k + (2x - k) s, s = L_(k+1) - L_k, is affine there and its ends
 * bound it: on a falling segment the right end, L_(k+1) + (k + 1) s — the one-sided slope just below breakpoint k + 1,
 * checked here; on a rising one the left end, >= L_k, which the segment before it (or k = 0) already holds above the margin;
 * beyond the last point L_(N-1) (the rule above). A rise inside MOTOR_MAP_RISE_TOL only raises the slope. */
#define MOTOR_MAP_RISE_TOL 0.02f /* a rise between neighbouring points inside this is measurement scatter, not physics */
#define MOTOR_MAP_SLOPE_MIN 0.25f /* x L_0 */
static bool map_ok(const float map[MOTOR_MAP_N])
{
    bool ok = map[MOTOR_MAP_N - 1u] >= (0.3f * map[0]);
    for (uint32_t k = 0u; k < MOTOR_MAP_N; k++) {
        ok = ok && in(map[k], 20e-6f, 5e-3f) && ((k == 0u) || (map[k] <= ((1.0f + MOTOR_MAP_RISE_TOL) * map[k - 1u])));
        ok = ok && ((k == 0u) || ((map[k] + ((float)k * (map[k] - map[k - 1u]))) >= (MOTOR_MAP_SLOPE_MIN * map[0])));
    }
    return ok;
}

/* Round 23 (FW-46): every entry within the CAL's ceiling (30 A); the build's cal_ripple_ff_max_a bounds what is applied */
static bool ripple_ok(const int16_t tab[TQ_RIPPLE_N])
{
    bool ok = true;
    for (uint32_t k = 0u; k < TQ_RIPPLE_N; k++) {
        ok = ok && (tab[k] >= -3000) && (tab[k] <= 3000);
    }
    return ok;
}

static bool ranges_ok(const calib_t *c, const ti_params_t *p)
{
    bool ok = true;
    for (uint32_t i = 0u; i < 3u; i++) {
        ok = ok && in(c->isns[i].offset_v, 2.2f, 2.8f) && in(c->isns[i].gain_v_per_a, 1.9e-3f, 2.6e-3f) &&
             ((c->isns[i].sign == 1) || (c->isns[i].sign == -1));
    }
    for (uint32_t i = 0u; i < 2u; i++) {
        ok = ok && in(c->vdc[i].gain, 0.85f * p->vdc_div_ratio, 1.15f * p->vdc_div_ratio) &&
             in(c->vdc[i].offset_v, -20.0f, 20.0f);
    }
    const rslv_cal_t *r = &c->rslv;
    ok = ok && in(r->ratio_nom, 0.2f, 2.0f) && in(r->exc_code_per_vpp, 1500.0f, 3500.0f) && in(r->sin_gain, 0.8f, 1.25f) &&
         in(r->cos_gain, 0.8f, 1.25f) && in(r->sin_offset, -0.2f, 0.2f) && in(r->cos_offset, -0.2f, 0.2f) &&
         in(r->phase_trim_deg, -30.0f, 30.0f) && in(r->zero_rad, 0.0f, 6.2832f) && (r->resolver_pp >= 1u) &&
         (r->motor_pp >= r->resolver_pp) && (r->motor_pp <= 12u) && ((r->motor_pp % r->resolver_pp) == 0u);
    const motor_t *m = &c->motor;
    ok = ok && in(m->ld_h, 20e-6f, 5e-3f) && in(m->lq_h, 20e-6f, 5e-3f) && in(m->rs_ohm, 1e-3f, 0.5f) &&
         in(m->psi_wb, 0.01f, 0.5f) && (m->pp >= 1u) && (m->pp <= 12u) && (m->pp == r->motor_pp) &&
         in(m->id_demag_a, 10.0f, 3000.0f) && in(m->n_max_rpm, 1000.0f, 30000.0f);
    ok = ok && ((c->mt.type == TEMP_MT_PT1000) ||
                ((c->mt.type == TEMP_MT_NTC) && in(c->mt.r25_ohm, 1000.0f, 100000.0f) && in(c->mt.b_k, 2000.0f, 5000.0f)));
    ok = ok && (c->mtpa.n <= MTPA_LUT_MAX);
    for (uint32_t i = 1u; ok && (i < c->mtpa.n); i++) {
        ok = c->mtpa.t_nm[i] > c->mtpa.t_nm[i - 1u];
    }
    return ok && map_ok(m->ld_map_h) && map_ok(m->lq_map_h) && (m->i_map_a == p->i_crest_a) && ripple_ok(c->ripple_ff);
}

uint32_t calib_check(const calib_t *c, const ti_params_t *p, const uint8_t serial[8])
{
    uint32_t err = 0u;
    if (c == NULL) {
        return CAL_ERR_MISSING;
    }
    err |= (c->layout_version == CALIB_LAYOUT_VERSION) ? 0u : CAL_ERR_VERSION;
    err |= (c->crc32 == ti_crc32(c, offsetof(calib_t, crc32))) ? 0u : CAL_ERR_CRC;
    err |= ranges_ok(c, p) ? 0u : CAL_ERR_RANGE;
    err |= (c->sku == (uint8_t)p->sku) ? 0u : CAL_ERR_SKU;
    err |= (memcmp(c->hw_serial, serial, 8u) == 0) ? 0u : CAL_ERR_SERIAL;
    err |= (c->motor_id != 0u) ? 0u : CAL_ERR_MOTOR_ID;
    bool fsw_ok = false;
    for (uint32_t i = 0u; (i < p->n_fsw) && (i < 2u); i++) {
        fsw_ok = fsw_ok || (p->fsw_hz[i] == c->fsw_hz);
    }
    err |= fsw_ok ? 0u : CAL_ERR_FSW;
    return err;
}
