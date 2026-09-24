/* discharge.h — active discharge (FW-17, FW-18), the key-off tau plausibility (FW-02) and the
 * precharge plausibility (FW-19).
 *  FW-17: QDIS fires only while the VCU/BMS reports the main contactors OPEN (released at once if
 *         that report changes), auto-release after 5 s, at most 3 counted discharges per 5 min.
 *         The FW-16 2-tau top-up is not counted (<= 1.6 J, contract).
 *  FW-18: witness on both channels — no decay (>= cal_qdis_decay_min_frac) within 200 ms => stuck-
 *         off DTC and no further active discharge this key cycle (the passive bleeder takes the link
 *         below 60 V in 65 s / 89 s); either witness invalid => HV state UNKNOWN (never SAFE).
 *         Stuck-on / unexpected discharge: QDIS not commanded, the contactors open and the bridge not
 *         modulating (nothing may draw on the link), yet the link decays at the active-discharge
 *         rate (tau below cal_qdis_stuck_on_frac of the bleeder's) => stuck_on, latched. A shorted
 *         QDIS with the battery connected bypasses the 5 s release (4 x 96 W in the resistors) and
 *         only shows at the next opening; app.c then refuses to re-energise (service required,
 *         kept in NVM) and asks the VCU to open the contactors.
 *  FW-02: tau from the first 200 ms, > 20 % off the SKU's expected tau => DTC (plausibility only).
 *  FW-19: during precharge a plateau more than cal_precharge_low_frac below the pack, or a tau
 *         shorter than the vehicle's minimum, refuses arming. */
#ifndef DISCHARGE_H
#define DISCHARGE_H

#include "vdc.h"

typedef enum { DIS_IDLE = 0, DIS_ACTIVE, DIS_DONE, DIS_ABORTED } dis_state_t;
typedef enum { DIS_REQ_OK = 0, DIS_REQ_CONTACTORS, DIS_REQ_RATE, DIS_REQ_BUSY, DIS_REQ_FAILED } dis_req_t;

typedef struct {
    dis_state_t st;
    bool qdis;
    bool counted;
    uint32_t start_ms;
    float v_start[2];
    bool witness_ok_at_start;
    bool witness_done;
    uint32_t fire_ms[3];
    uint8_t n_fire; /* total counted fires (ring index) */
    bool stuck_off, stuck_on, tau_mismatch;
    float tau_meas_s;
    /* passive-decay watch for a stuck-on QDIS */
    bool mon_active;
    uint32_t mon_start_ms;
    float mon_v0;
} dis_t;

void dis_init(dis_t *d);
dis_req_t dis_request(dis_t *d, ti_contactor_t contactors, const vdc_t *v, bool counted, uint32_t now_ms,
                      const ti_params_t *p);
void dis_abort(dis_t *d);
/* bridge_mod: the bridge is modulating (the motor may draw on the link: no stuck-on verdict). */
void dis_step(dis_t *d, ti_contactor_t contactors, const vdc_t *v, bool bridge_mod, uint32_t now_ms,
              const ti_params_t *p);
bool dis_output(const dis_t *d);
/* FW-18: the HV state the vehicle may be told. */
ti_hv_state_t dis_hv_state(const vdc_t *v);

typedef enum { PCH_IDLE = 0, PCH_RUNNING, PCH_OK, PCH_REFUSE_PLATEAU, PCH_REFUSE_TAU, PCH_REFUSE_TIMEOUT } pch_result_t;

typedef struct {
    pch_result_t res;
    uint32_t t0_ms;
    float v0;
    bool t63_done;
    float tau_s;
    float v_max;
    uint32_t v_max_ms;
} pch_t;

void pch_init(pch_t *c);
pch_result_t pch_step(pch_t *c, ti_contactor_t contactors, const vdc_t *v, float v_pack, bool pack_fresh,
                      uint32_t now_ms, const ti_params_t *p);

#endif /* DISCHARGE_H */
