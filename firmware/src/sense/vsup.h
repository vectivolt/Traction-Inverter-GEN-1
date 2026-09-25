/* vsup.h — LV supply supervision (round 17): KL30 as the FS26 sees it on VSUP, read through the FS26 AMUX
 * (VSUP / 14: DS Rev.3 Tables 56 and 130; input 4.2–36 V, ratio ±1.5 %, offset ±7 mV, Table 131) on SBC_AMUX
 * (ADC0_S14). The let-through LV entry passes the vehicle's overvoltage profiles to parts rated for them, so above
 * the FS26's VSUP_OV (19.3–20.7 V, Table 9: VSUPOV_I, an interrupt, no fail-safe reaction) is INFORMATION, not a
 * fault, for as long as the vehicle interface allows: above cal_vsup_jump_max_v (IR-03, a test-B pulse, up to 35 V)
 * for cal_vsup_ld_ms, at or below it (IR-02, a 24 V jump start) for cal_vsup_jump_ms in all. Longer is `sustained`
 * — until VSUP is back below the threshold (hysteresis vsup_ov_hyst_v). A reading below the divider's range (the
 * AMUX not configured, or its pin pulled down) is `valid` false: no supervision, reported, never a fault. */
#ifndef VSUP_H
#define VSUP_H

#include "ti_params.h"

typedef struct {
    float v;           /* VSUP, V */
    bool valid;
    bool ov;           /* above vsup_ov_v (with hysteresis): the FS26's VSUPOV */
    bool hi;           /* above cal_vsup_jump_max_v */
    bool sustained;    /* longer than its band allows: latched until the event ends */
    uint32_t t_ov_ms;  /* the event's start */
    uint32_t t_hi_ms;  /* start of the current stretch above the jump-start ceiling */
} vsup_t;

void vsup_init(vsup_t *s);
/* Every 1 ms with the latest SBC_AMUX code. */
void vsup_update(vsup_t *s, uint16_t code, uint32_t now_ms, const ti_params_t *p);

#endif /* VSUP_H */
