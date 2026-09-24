/* ign.h — KL15 sense (H1, ADC0_S8): KL15 through a diode and 47 k/10 k (14 V -> 2.3 V).
 * Hysteresis cal_ign_on_v / cal_ign_off_v, debounce cal_ign_debounce_ms. */
#ifndef IGN_H
#define IGN_H

#include "ti_params.h"

typedef struct {
    bool on;
    bool cand;
    bool pending;
    uint32_t since_ms;
} ign_t;

float ign_kl15_v(uint16_t code);
void ign_init(ign_t *g, bool on);
void ign_update(ign_t *g, uint16_t code, uint32_t now_ms, const ti_params_t *p);

#endif /* IGN_H */
