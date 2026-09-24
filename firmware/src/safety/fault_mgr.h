/* fault_mgr.h — active §6 rows, their combined decision, FW-15 bookkeeping and FW-08b.
 *  - FW-15 step 1 (fm_desat): the hardware chain has already forced the safe state (FAULT0/2 ->
 *    PWM low, fault latch -> DRV_EN low, UASCG masks ASC). Firmware then latches which bank in
 *    RETAINED RAM at once and QUEUES the NVM record (never a blocking write in front of the §6
 *    action); an uncommitted retained record is re-queued at the next boot.
 *  - No automatic retry: one VCU-authorised retry per key cycle, >= 1 s after the event, at
 *    reduced torque; a second DESAT latches DESAT_REPEAT. A DESAT record from this or the previous
 *    key cycle blocks automatic arming whatever FLT reads now.
 *  - Combination: a row that forces SPO in hardware wins; otherwise the highest-rank action.
 *  - asc_permitted (read by the fault ISR before any ASC_REQ): false with FLT_LS, V5GD loss, or an
 *    FLT_HS whose FW-15 reset has not completed.
 *  - keep_hv (FW-08b, A12-R08): any active row whose SPO relies on the battery (rule (a) fails,
 *    battery present), at any speed, re-evaluated every update with the present current and speed,
 *    until rule (a) holds or ASC is active. no_safe_state: an active row holds SPO with neither
 *    rule (a) nor (b) (e.g. the battery is gone) — reported on CAN; DTC_SPO_ENERGY records it. */
#ifndef FAULT_MGR_H
#define FAULT_MGR_H

#include "nvlog.h"
#include "safe_state.h"

typedef struct {
    float speed_rpm;
    bool speed_known;
    float id_a;
    float iq_a;
    bool battery_present;
    bool asc_active;
    float vdc_v;
    uint32_t now_ms;
    uint32_t key_cycle;
} fm_ctx_t;

typedef struct {
    uint32_t active;  /* bit per ss_row_t */
    uint32_t latched; /* rows that need a VCU fault reset (or a key cycle) */
    ss_decision_t row_dec[SS_ROW_COUNT];
    ss_decision_t dec;
    ss_row_t dec_row;
    bool asc_permitted;
    bool keep_hv;
    bool no_safe_state;
    bool hs_reset_done;
    uint8_t desat_count;
    bool retry_used;
    uint32_t desat_ms;
    bool desat_blocked;
    uint32_t key_cycle;
} fm_t;

typedef struct {
    uint32_t magic;
    uint32_t key_cycle;
    uint8_t bank; /* 1 HS, 2 LS */
    uint8_t committed;
    uint16_t pad;
    float speed_rpm;
    uint32_t crc;
} fm_retained_t;

extern fm_retained_t g_fm_retained;

void fm_init(fm_t *f, uint32_t key_cycle);
/* Boot: a DESAT record from this or the previous key cycle blocks automatic arming; an
 * uncommitted retained record is re-queued. */
void fm_boot(fm_t *f, const nv_desat_t *rec, bool rec_valid);
void fm_raise(fm_t *f, ss_row_t row, bool latching, const fm_ctx_t *c, const motor_t *m, const ti_params_t *p);
void fm_clear(fm_t *f, ss_row_t row); /* only non-latched rows */
bool fm_reset_latched(fm_t *f, const fm_ctx_t *c, const motor_t *m, const ti_params_t *p);
void fm_update(fm_t *f, const fm_ctx_t *c, const motor_t *m, const ti_params_t *p);
void fm_desat(fm_t *f, bool hs, const fm_ctx_t *c, const motor_t *m, const ti_params_t *p);
void fm_hs_reset_done(fm_t *f);
bool fm_retry_allowed(const fm_t *f, bool vcu_auth, uint32_t now_ms, const ti_params_t *p);
void fm_retry_consumed(fm_t *f, const fm_ctx_t *c);
bool fm_active(const fm_t *f, ss_row_t row);
bool fm_any(const fm_t *f);
bool fm_needs_fault_state(const fm_t *f);
void fm_retained_commit(void);

#endif /* FAULT_MGR_H */
