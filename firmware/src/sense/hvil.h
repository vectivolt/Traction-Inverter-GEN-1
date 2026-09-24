/* hvil.h — HVIL signature (FW-09). The card drives INTRLOK_P (B4) through the connector loop and
 * reads INTRLOK_N (D8, ADC1_P7) against a 10 k/10 k divider from V5A:
 *   loop closed: 3.0 V with the drive high, 2.0 V with it low;  loop open: 2.5 V either way;
 *   < 1 V short to ground; > 4 V short to a supply; anything else implausible.
 * The drive toggles every cal_hvil_period_ms; each sample is judged against the drive level that
 * produced it. There is no hardware comparator (R-F12): the reaction (<= 100 ms) is this code. */
#ifndef HVIL_H
#define HVIL_H

#include "ti_params.h"

typedef enum {
    HVIL_UNKNOWN = 0,
    HVIL_CLOSED,
    HVIL_OPEN,
    HVIL_SHORT_GND,
    HVIL_SHORT_SUPPLY,
    HVIL_IMPLAUSIBLE
} hvil_status_t;

typedef struct {
    hvil_status_t status;
    hvil_status_t cand;
    uint8_t cnt;
    bool drive;
    uint32_t last_ms;
    bool started;
} hvil_t;

void hvil_init(hvil_t *h);
hvil_status_t hvil_classify(float v, bool drive, const ti_params_t *p);
/* Call every 1 ms with the latest INTRLOK_N code; returns the INTRLOK_P level to drive now. */
bool hvil_step(hvil_t *h, uint16_t code, uint32_t now_ms, const ti_params_t *p);

#endif /* HVIL_H */
