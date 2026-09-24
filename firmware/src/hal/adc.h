/* hal/adc.h — SAR ADC signals per the frozen ball map (board_pins.h).
 * Groups:
 *   phase currents: ADC3_P1 (U), ADC4_P5 (V), ADC0_S19 (W) — three instances, converted
 *     simultaneously on the eFlexPWM trigger (BCTU), twice per PWM period (double update);
 *   V_DC: ADC6_P4 (ch1) and ADC1_P6 (ch2) — separate instances, free-running >= 200 kS/s with
 *     the analog watchdog at the OV trip (FW-06); ADC1 interleaves INTRLOK_N/TMOD_W as injected
 *     conversions so the ch2 sample gap stays <= 5 us;
 *   slow list (1 kHz, software trigger): everything else.
 * Codes are 12-bit (the platform right-aligns the RTD result). */
#ifndef HAL_ADC_H
#define HAL_ADC_H

#include "ti_types.h"

typedef enum {
    HAL_ADC_ISNS_U = 0,
    HAL_ADC_ISNS_V,
    HAL_ADC_ISNS_W,
    HAL_ADC_VDC1,
    HAL_ADC_VDC2,
    HAL_ADC_VOFS,
    HAL_ADC_V5GD,
    HAL_ADC_HW_ID,
    HAL_ADC_IGN,
    HAL_ADC_INTRLOK_N,
    HAL_ADC_TMOD_U,
    HAL_ADC_TMOD_V,
    HAL_ADC_TMOD_W,
    HAL_ADC_NTC_H,
    HAL_ADC_NTC_A,
    HAL_ADC_MT1,
    HAL_ADC_MT2,
    HAL_ADC_SBC_AMUX,
    HAL_ADC_COUNT
} hal_adc_sig_t;

bool hal_adc_init(void);

/* Latest conversion of a signal and its time stamp. Returns false if never converted. */
bool hal_adc_read(hal_adc_sig_t sig, uint16_t *code, uint32_t *t_us);

/* The simultaneous phase-current group of the last PWM trigger (U, V, W). */
bool hal_adc_read_phase(uint16_t codes[3], uint32_t *t_us);

/* Start the slow list (1 kHz task); results appear via hal_adc_read(). */
void hal_adc_start_slow(void);

/* Analog watchdog: flags (and raises eFlexPWM FAULT1) when code >= hi_trip or code <= lo_trip
 * (lo_trip = 0 disables the low compare). Only the phase currents and V_DC channels are routed to
 * FAULT1. */
bool hal_adc_set_watchdog(hal_adc_sig_t sig, uint16_t lo_trip, uint16_t hi_trip);
uint32_t hal_adc_watchdog_status(void); /* bit (1u << sig) */
void hal_adc_watchdog_clear(uint32_t mask);

#endif /* HAL_ADC_H */
