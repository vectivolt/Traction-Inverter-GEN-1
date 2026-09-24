/* temp.h — temperatures (FW-13): module NTCs (B25/50 3375, R25 5 k, through the power board's
 * 100 R against the card's 5.1 k pull-up to VREF5), board NTCs (10 k B3435 against 10 k), motor
 * sensors (PT1000 or NTC per the motor calibration, 10 k pull-up). Each channel: open / short /
 * rate plausibility and a validity flag. Derating from these lives in control/torque.c (FW-04). */
#ifndef TEMP_H
#define TEMP_H

#include "ti_params.h"

typedef enum { TEMP_TMOD_U = 0, TEMP_TMOD_V, TEMP_TMOD_W, TEMP_NTC_H, TEMP_NTC_A, TEMP_MT1, TEMP_MT2, TEMP_COUNT } temp_ch_t;
typedef enum { TEMP_OK = 0, TEMP_OPEN, TEMP_SHORT, TEMP_RATE } temp_fault_t;
typedef enum { TEMP_MT_PT1000 = 0, TEMP_MT_NTC } temp_mt_type_t;

typedef struct {
    float t_c;
    bool valid;
    temp_fault_t fault;
    bool primed;
    uint8_t rate_cnt;
    uint32_t last_ms;
} temp_ch_state_t;

typedef struct {
    temp_ch_state_t ch[TEMP_COUNT];
} temp_t;

typedef struct {
    temp_mt_type_t type;
    float r25_ohm; /* NTC only */
    float b_k;     /* NTC only */
} temp_mt_cal_t;

float temp_ntc_c(float v_pin, float pullup_ohm, float series_ohm, float r25_ohm, float b_k);
float temp_pt1000_c(float v_pin, float pullup_ohm);
void temp_init(temp_t *t);
void temp_update(temp_t *t, const uint16_t codes[TEMP_COUNT], uint32_t now_ms, const temp_mt_cal_t *mt,
                 const ti_params_t *p);
/* Hottest valid module NTC; *all_valid false if any module NTC is invalid. */
float temp_module_max(const temp_t *t, bool *any_valid, bool *all_valid);

#endif /* TEMP_H */
