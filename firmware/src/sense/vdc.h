/* vdc.h — two isolated V_DC channels (FW-07, FW-18). ch1 on ADC6_P4, ch2 on ADC1_P6.
 * Pin = VOFS + V_link / 455.84 (6 x 470 k over 6.2 k into the AMC1311B, receiver offset +0.5 V).
 * A channel is invalid when: its pin < 0.25 V (AMC1311 fail-safe, not a dead bus), its sample is
 * stale, VOFS is outside 0.475–0.525 V (both), V5GD is outside 4.75–5.25 V (both). The pair is
 * invalid when |V1 - V2| > 5 % (floor: cal_vdc_disagree_floor_v). With contactors closed,
 * |V_DC - V_pack| > 3 % flags bms_mismatch (the §6 "V_DC leaving the pack voltage"). HV state:
 * UNKNOWN whenever the pair is invalid, never SAFE (FW-18). Round 18 (A16-R01): now_us is read after the
 * channels were read (the target stamps a conversion when it reads it) and the age is signed (ti_stale). */
#ifndef VDC_H
#define VDC_H

#include "ti_params.h"

typedef struct {
    float gain;     /* V_link per pin volt above VOFS (nominal vdc_div_ratio) */
    float offset_v; /* link volts */
} vdc_cal_t;

typedef struct {
    float v_ch[2];
    bool ch_valid[2];
    bool ch_failsafe[2];
    bool ch_stale[2];
    float vofs_v;
    bool vofs_ok;
    float v5gd_v;
    bool v5gd_ok;
    bool disagree;
    bool bms_mismatch;
    uint32_t bms_bad_since_ms;
    bool bms_bad_pending;
    float vdc;
    bool valid;
    ti_hv_state_t hv;
} vdc_t;

void vdc_init(vdc_t *s);
void vdc_update(vdc_t *s, const uint16_t code_ch[2], const uint32_t t_ch_us[2], uint16_t code_vofs,
                uint16_t code_v5gd, uint32_t now_us, const vdc_cal_t cal[2], const ti_params_t *p);
void vdc_bms_check(vdc_t *s, bool contactors_closed, bool pack_fresh, float v_pack, uint32_t now_ms,
                   const ti_params_t *p);
/* ADC code at which the hardware compare (FW-06) must trip for this channel. */
uint16_t vdc_ov_code(const vdc_cal_t *c, const ti_params_t *p);

#endif /* VDC_H */
