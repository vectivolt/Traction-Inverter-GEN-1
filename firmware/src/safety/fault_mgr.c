/* fault_mgr.c — §6 rows, FW-15, FW-08b. */
#include "fault_mgr.h"

#include <stddef.h>

#include "dtc.h"
#include "ti_crc.h"

#define FM_RETAINED_MAGIC 0x46573135u /* "FW15" */

TI_RETAINED fm_retained_t g_fm_retained;

static uint32_t bit(ss_row_t r) { return 1u << (uint32_t)r; }

void fm_init(fm_t *f, uint32_t key_cycle)
{
    *f = (fm_t){0};
    f->key_cycle = key_cycle;
    f->asc_permitted = true;
}

static uint32_t retained_crc(void) { return ti_crc32(&g_fm_retained, offsetof(fm_retained_t, crc)); }

void fm_boot(fm_t *f, const nv_desat_t *rec, bool rec_valid)
{
    const bool ret_ok = (g_fm_retained.magic == FM_RETAINED_MAGIC) && (g_fm_retained.crc == retained_crc());
    if (ret_ok && (g_fm_retained.committed == 0u)) {
        const nv_desat_t r = {.key_cycle = g_fm_retained.key_cycle, .bank = g_fm_retained.bank,
                              .speed_rpm = g_fm_retained.speed_rpm};
        (void)nv_queue(NV_REC_DESAT, &r, (uint16_t)sizeof r); /* the brown-out ate the NVM write */
    }
    const bool recent_nvm = rec_valid && ((rec->key_cycle + 1u) >= f->key_cycle);
    const bool recent_ret = ret_ok && ((g_fm_retained.key_cycle + 1u) >= f->key_cycle);
    if (recent_nvm || recent_ret) {
        f->desat_blocked = true;
        f->desat_count = 1u;
        f->retry_used = recent_nvm && (rec->key_cycle == f->key_cycle) && (rec->retry_used != 0u);
    }
}

static ss_decision_t decide(ss_row_t row, const fm_ctx_t *c, const motor_t *m, const ti_params_t *p)
{
    const ss_input_t in = {.row = row, .speed_rpm = c->speed_rpm, .speed_known = c->speed_known, .id_a = c->id_a,
                           .iq_a = c->iq_a, .battery_present = c->battery_present};
    return ss_decide(&in, m, p);
}

static void combine(fm_t *f, const fm_ctx_t *c)
{
    ss_decision_t best = {0};
    ss_row_t best_row = SS_ROW_COUNT;
    bool forced = false;
    bool keep = false;
    bool unproven = false;
    for (uint32_t r = 0u; r < (uint32_t)SS_ROW_COUNT; r++) {
        if ((f->active & bit((ss_row_t)r)) == 0u) {
            continue;
        }
        const ss_decision_t *d = &f->row_dec[r];
        keep = keep || d->keep_hv;
        unproven = unproven || d->energy_dtc;
        if (forced) {
            continue;
        }
        if (d->spo_forced || (best_row == SS_ROW_COUNT) || (ss_rank(d->action) > ss_rank(best.action))) {
            best = *d;
            best_row = (ss_row_t)r;
            forced = d->spo_forced;
        }
    }
    f->dec = best;
    f->dec_row = best_row;
    f->asc_permitted = ((f->active & (bit(SS_ROW_FLT_LS) | bit(SS_ROW_V5GD_LOSS))) == 0u) &&
                       (((f->active & bit(SS_ROW_FLT_HS)) == 0u) || f->hs_reset_done);
    /* FW-08b: while an SPO relies on the battery, until rule (a) holds or ASC (a sink of its own) */
    f->keep_hv = keep && !c->asc_active;
    f->no_safe_state = unproven;
}

void fm_raise(fm_t *f, ss_row_t row, bool latching, const fm_ctx_t *c, const motor_t *m, const ti_params_t *p)
{
    if (row >= SS_ROW_COUNT) {
        return;
    }
    const bool fresh = (f->active & bit(row)) == 0u;
    f->active |= bit(row);
    if (latching) {
        f->latched |= bit(row);
    }
    if (fresh) {
        f->row_dec[row] = decide(row, c, m, p); /* the energy rule at the current AT the fault */
        if (f->row_dec[row].energy_dtc) {
            dtc_set(DTC_SPO_ENERGY, c->now_ms);
        }
    }
    combine(f, c);
}

void fm_clear(fm_t *f, ss_row_t row)
{
    if ((row < SS_ROW_COUNT) && ((f->latched & bit(row)) == 0u)) {
        f->active &= ~bit(row);
    }
}

bool fm_reset_latched(fm_t *f, const fm_ctx_t *c, const motor_t *m, const ti_params_t *p)
{
    /* never at speed, never for a DESAT (its own one-retry rule) */
    const uint32_t desat = bit(SS_ROW_FLT_HS) | bit(SS_ROW_FLT_LS);
    if (!c->speed_known || (ti_absf(c->speed_rpm) >= ss_n_x_rpm(m, p))) {
        return false;
    }
    f->active &= ~(f->latched & ~desat);
    f->latched &= desat;
    combine(f, c);
    return true;
}

void fm_update(fm_t *f, const fm_ctx_t *c, const motor_t *m, const ti_params_t *p)
{
    for (uint32_t r = 0u; r < (uint32_t)SS_ROW_COUNT; r++) {
        if ((f->active & bit((ss_row_t)r)) != 0u) {
            /* live: keep_hv and "no safe state proven" follow the present current, speed and
             * battery; the DTC keeps the history */
            f->row_dec[r] = decide((ss_row_t)r, c, m, p);
            if (f->row_dec[r].energy_dtc) {
                dtc_set(DTC_SPO_ENERGY, c->now_ms);
            }
        }
    }
    combine(f, c);
}

void fm_retained_commit(void)
{
    if (g_fm_retained.magic == FM_RETAINED_MAGIC) {
        g_fm_retained.committed = 1u;
        g_fm_retained.crc = retained_crc();
    }
}

void fm_desat(fm_t *f, bool hs, const fm_ctx_t *c, const motor_t *m, const ti_params_t *p)
{
    /* 1) retained RAM first: survives an MCU reset (not an FS26 restart — the NVM record does) */
    g_fm_retained.magic = FM_RETAINED_MAGIC;
    g_fm_retained.key_cycle = c->key_cycle;
    g_fm_retained.bank = hs ? 1u : 2u;
    g_fm_retained.committed = 0u;
    g_fm_retained.speed_rpm = c->speed_rpm;
    g_fm_retained.crc = retained_crc();
    /* 2) queue, never write */
    const nv_desat_t r = {.key_cycle = c->key_cycle, .bank = hs ? 1u : 2u, .retry_used = f->retry_used ? 1u : 0u,
                          .speed_rpm = c->speed_rpm};
    (void)nv_queue(NV_REC_DESAT, &r, (uint16_t)sizeof r);
    const nv_fault_t ev = {.key_cycle = c->key_cycle, .t_ms = c->now_ms,
                           .code = (uint16_t)(hs ? DTC_DESAT_HS : DTC_DESAT_LS),
                           .row = (uint8_t)(hs ? SS_ROW_FLT_HS : SS_ROW_FLT_LS), .speed_rpm = c->speed_rpm,
                           .id_a = c->id_a, .iq_a = c->iq_a, .vdc_v = c->vdc_v};
    (void)nv_queue(NV_REC_FAULT, &ev, (uint16_t)sizeof ev);
    /* 3) bookkeeping */
    f->desat_count = (f->desat_count < 255u) ? (uint8_t)(f->desat_count + 1u) : 255u;
    f->desat_ms = c->now_ms;
    f->hs_reset_done = false;
    dtc_set(hs ? DTC_DESAT_HS : DTC_DESAT_LS, c->now_ms);
    if (f->desat_count >= 2u) {
        dtc_set(DTC_DESAT_REPEAT, c->now_ms);
    }
    fm_raise(f, hs ? SS_ROW_FLT_HS : SS_ROW_FLT_LS, true, c, m, p);
}

void fm_hs_reset_done(fm_t *f) { f->hs_reset_done = true; }

bool fm_retry_allowed(const fm_t *f, bool vcu_auth, uint32_t now_ms, const ti_params_t *p)
{
    /* ms stamps are floored: 1000 counts can be 999.001 ms of real time, so one more count keeps FW-15's
     * "no sooner than 1 s after the event" (round 16: a sub-ms phase shift exposed it) */
    return vcu_auth && !f->retry_used && (f->desat_count == 1u) && !dtc_active(DTC_DESAT_REPEAT) &&
           ti_elapsed(now_ms, f->desat_ms, p->desat_retry_min_ms + 1u);
}

void fm_retry_consumed(fm_t *f, const fm_ctx_t *c)
{
    f->retry_used = true;
    f->desat_blocked = false;
    f->active &= ~(bit(SS_ROW_FLT_HS) | bit(SS_ROW_FLT_LS));
    f->latched &= ~(bit(SS_ROW_FLT_HS) | bit(SS_ROW_FLT_LS));
    const nv_desat_t r = {.key_cycle = c->key_cycle, .bank = g_fm_retained.bank, .retry_used = 1u,
                          .speed_rpm = c->speed_rpm};
    (void)nv_queue(NV_REC_DESAT, &r, (uint16_t)sizeof r);
    combine(f, c);
}

bool fm_active(const fm_t *f, ss_row_t row) { return (row < SS_ROW_COUNT) && ((f->active & bit(row)) != 0u); }
bool fm_any(const fm_t *f) { return f->active != 0u; }

bool fm_needs_fault_state(const fm_t *f, bool battery_lost_done)
{
    const uint32_t soft = bit(SS_ROW_CMD_LOST) | bit(SS_ROW_BMS_LIMIT_ZERO) |
                          (battery_lost_done ? bit(SS_ROW_BATTERY_LOST) : 0u);
    return ((f->active & ~soft) != 0u) || f->desat_blocked;
}
