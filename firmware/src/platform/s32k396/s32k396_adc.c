/* s32k396_adc.c — hal/adc.h on the S32K396 SAR ADCs, BCTU and the watchdog-to-FAULT1 route.
 *   phase currents : ADC3_P1 (U), ADC4_P5 (V), ADC0_S19 (W), one BCTU list fired by eFlexPWM_1
 *                    SM0 VAL0/VAL1 (both zero-vector centres, double update); BCTU end of list ->
 *                    app_isr_current (s32k396_main.c);
 *   V_DC           : ADC6_P4 (ch1) and ADC1_P6 (ch2), continuous (>= 200 kS/s, FW-06); ADC1 adds
 *                    INTRLOK_N and TMOD_W as injected conversions (ch2 gap <= 5 us);
 *   slow list      : the rest, a normal chain started by hal_adc_start_slow() at 1 kHz;
 *   watchdogs      : per-channel thresholds on the five protection channels; the watchdog outputs
 *                    are ORed (LCU) onto eFlexPWM_1 FAULT1 through TRGMUX (§4c, FW-05/FW-06).
 * Codes are right-aligned 12-bit. Stale detection: a result counts as new only when its data
 * register's VALID bit was set (cleared by the read), so a stopped converter ages in the app. */
#include "adc.h"
#include "s32k396.h"
#include "timer.h"

#ifdef TI_RTD_AVAILABLE
#include "Adc_Sar_Ip.h" /* TODO(RTD) */
#include "Bctu_Ip.h"
#include "Lcu_Ip.h"
#include "Trgmux_Ip.h"
extern const Adc_Sar_Ip_ConfigType AdcHwUnit_0, AdcHwUnit_1, AdcHwUnit_3, AdcHwUnit_4, AdcHwUnit_5,
    AdcHwUnit_6;                                   /* TODO(RTD): Config Tools symbols */
extern const Bctu_Ip_ConfigType BctuHwUnit_0;       /* TODO(RTD) */
extern const Trgmux_Ip_InitType Trgmux_Ip_xTrgmuxInitPB; /* TODO(RTD) */
extern const Lcu_Ip_InitType Lcu_Ip_xLcuInitPB;          /* TODO(RTD) */
/* Direct reads of the data and watchdog-status registers (the ISR paths). TODO(RTD): device-header
 * names; ADC_2 is not used on this card. */
static ADC_Type *const BASE[7] = {IP_ADC_0, IP_ADC_1, IP_ADC_2, IP_ADC_3, IP_ADC_4, IP_ADC_5, IP_ADC_6};
static uint32_t cdr(uint32_t inst, uint32_t ch)
{
    return (ch < 32u) ? BASE[inst]->PCDR[ch] : BASE[inst]->ICDR[ch - 32u];
}
#endif

typedef enum { G_PHASE = 0, G_VDC, G_SLOW } grp_t;

typedef struct {
    uint8_t inst;
    uint8_t ch; /* RTD channel index, S32K3_ADC_CH() */
    grp_t grp;
} adc_map_t;

/* hal_adc_sig_t order; instances/channels from board_pins.h (generated from the ball map). */
static const adc_map_t MAP[HAL_ADC_COUNT] = {
    {BP_ISNS_U_INST, S32K3_ADC_CH('P', BP_ISNS_U_CHAN), G_PHASE},
    {BP_ISNS_V_INST, S32K3_ADC_CH('P', BP_ISNS_V_CHAN), G_PHASE},
    {BP_ISNS_W_INST, S32K3_ADC_CH('S', BP_ISNS_W_CHAN), G_PHASE},
    {BP_VDC1_SE_INST, S32K3_ADC_CH('P', BP_VDC1_SE_CHAN), G_VDC},
    {BP_VDC2_SE_INST, S32K3_ADC_CH('P', BP_VDC2_SE_CHAN), G_VDC},
    {BP_VOFS_INST, S32K3_ADC_CH('S', BP_VOFS_CHAN), G_SLOW},
    {BP_V5GD_SNS_INST, S32K3_ADC_CH('P', BP_V5GD_SNS_CHAN), G_SLOW},
    {BP_HW_ID_INST, S32K3_ADC_CH('P', BP_HW_ID_CHAN), G_SLOW},
    {BP_IGN_SNS_INST, S32K3_ADC_CH('S', BP_IGN_SNS_CHAN), G_SLOW},
    {BP_INTRLOK_N_INST, S32K3_ADC_CH('P', BP_INTRLOK_N_CHAN), G_SLOW},
    {BP_TMOD_U_INST, S32K3_ADC_CH('P', BP_TMOD_U_CHAN), G_SLOW},
    {BP_TMOD_V_INST, S32K3_ADC_CH('P', BP_TMOD_V_CHAN), G_SLOW},
    {BP_TMOD_W_INST, S32K3_ADC_CH('S', BP_TMOD_W_CHAN), G_SLOW},
    {BP_NTC_H_INST, S32K3_ADC_CH('P', BP_NTC_H_CHAN), G_SLOW},
    {BP_NTC_A_INST, S32K3_ADC_CH('P', BP_NTC_A_CHAN), G_SLOW},
    {BP_MT1_SIG_INST, S32K3_ADC_CH('P', BP_MT1_SIG_CHAN), G_SLOW},
    {BP_MT2_SIG_INST, S32K3_ADC_CH('P', BP_MT2_SIG_CHAN), G_SLOW},
    {BP_SBC_AMUX_INST, S32K3_ADC_CH('S', BP_SBC_AMUX_CHAN), G_SLOW},
};

/* The five protection channels sit on five different instances: each uses threshold register 0. */
#define WD_REG 0u

static uint16_t s_code[HAL_ADC_COUNT];
static uint32_t s_t_us[HAL_ADC_COUNT];
static bool s_seen[HAL_ADC_COUNT];
static bool s_init;

static bool routed(hal_adc_sig_t s) { return MAP[s].grp != G_SLOW; }

/* Right-aligned 12-bit result, and whether it is new (VALID). */
static bool fetch(hal_adc_sig_t s, uint16_t *code)
{
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD/HW-RM): the channel data register (PCDRn / ICDRn): VALID bit 19, CDATA[14:0] with the
     * 12-bit result left-aligned in 15 bits unless MCR.DATA_ALIGN says otherwise. */
    const uint32_t r = cdr(MAP[s].inst, MAP[s].ch);
    if ((r & (1uL << 19)) == 0u) {
        return false;
    }
    *code = (uint16_t)((r & 0x7FFFu) >> 3);
    return true;
#else
    (void)s;
    (void)code;
    return false; /* no converter: every signal stays "never converted" -> invalid */
#endif
}

static void refresh(hal_adc_sig_t s)
{
    uint16_t c = 0u;
    if (fetch(s, &c)) {
        s_code[s] = c;
        s_t_us[s] = hal_time_us();
        s_seen[s] = true;
    }
}

bool hal_adc_init(void)
{
    s_init = false;
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): per instance Adc_Sar_Ip_Init() then Adc_Sar_Ip_DoCalibration() before use;
     * ADC6/ADC1 in continuous (scan) mode on the V_DC channel (ADC1 + injected INTRLOK_N/TMOD_W);
     * ADC0/3/4 in CTU (BCTU) trigger mode for the phase list + normal chain for the slow list.
     * Bctu_Ip_Init(0, &BctuHwUnit_0): trigger = PWM_1 SM0 OUT_TRIG0|1 via TRGMUX, one list converting
     * ADC3 ch P1, ADC4 ch P5 and ADC0 ch S19 in parallel, end-of-list interrupt enabled.
     * Trgmux_Ip_Init(&Trgmux_Ip_xTrgmuxInitPB): PWM_1 triggers -> BCTU; LCU output -> PWM_1 FAULT1.
     * Lcu_Ip_Init(&Lcu_Ip_xLcuInitPB): OR of the ADC0/1/3/4/6 watchdog outputs (active high).
     * TODO(HW-RM): confirm the ADC watchdog -> TRGMUX/LCU -> eFlexPWM FAULT route exists on the
     * S32K39 (the contract's FW-05 names an "eTPU fault input"; §4c the eFlexPWM channel). */
    const Adc_Sar_Ip_ConfigType *const cfg[7] = {&AdcHwUnit_0, &AdcHwUnit_1, NULL, &AdcHwUnit_3, &AdcHwUnit_4,
                                                 &AdcHwUnit_5, &AdcHwUnit_6};
    for (uint32_t i = 0u; i < 7u; i++) {
        if ((cfg[i] != NULL) && ((Adc_Sar_Ip_Init(i, cfg[i]) != ADC_SAR_IP_STATUS_SUCCESS) ||
                                 (Adc_Sar_Ip_DoCalibration(i) != ADC_SAR_IP_STATUS_SUCCESS))) {
            return false;
        }
    }
    if (Bctu_Ip_Init(0u, &BctuHwUnit_0) != BCTU_IP_STATUS_SUCCESS) {
        return false;
    }
    Trgmux_Ip_Init(&Trgmux_Ip_xTrgmuxInitPB);
    Lcu_Ip_Init(&Lcu_Ip_xLcuInitPB);
    Adc_Sar_Ip_StartConversion(BP_VDC1_SE_INST, ADC_SAR_IP_CONV_CHAIN_NORMAL); /* continuous */
    Adc_Sar_Ip_StartConversion(BP_VDC2_SE_INST, ADC_SAR_IP_CONV_CHAIN_NORMAL);
    s_init = true;
#endif
    return s_init;
}

bool hal_adc_read(hal_adc_sig_t sig, uint16_t *code, uint32_t *t_us)
{
    if ((uint32_t)sig >= (uint32_t)HAL_ADC_COUNT) {
        return false;
    }
    refresh(sig);
    *code = s_code[sig];
    *t_us = s_t_us[sig];
    return s_seen[sig];
}

bool hal_adc_read_phase(uint16_t codes[3], uint32_t *t_us)
{
    bool ok = true;
    for (uint32_t i = 0u; i < 3u; i++) {
        uint16_t c = 0u;
        const bool fresh = fetch((hal_adc_sig_t)i, &c); /* the BCTU list just completed */
        ok = ok && fresh;
        codes[i] = c;
    }
    if (ok) {
        for (uint32_t i = 0u; i < 3u; i++) {
            s_code[i] = codes[i];
            s_seen[i] = true;
        }
        *t_us = hal_time_us();
        s_t_us[0] = *t_us;
        s_t_us[1] = *t_us;
        s_t_us[2] = *t_us;
    }
    return ok;
}

void hal_adc_start_slow(void)
{
    for (uint32_t s = 0u; s < (uint32_t)HAL_ADC_COUNT; s++) {
        if (MAP[s].grp == G_SLOW) {
            refresh((hal_adc_sig_t)s); /* collect the previous chain's results */
        }
    }
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): the slow list shares ADC0/3/4 with the BCTU list, which has priority (a BCTU
     * conversion aborts and resumes the normal chain); ADC5 converts NTC_A only. */
    Adc_Sar_Ip_StartConversion(0u, ADC_SAR_IP_CONV_CHAIN_NORMAL);
    Adc_Sar_Ip_StartConversion(3u, ADC_SAR_IP_CONV_CHAIN_NORMAL);
    Adc_Sar_Ip_StartConversion(4u, ADC_SAR_IP_CONV_CHAIN_NORMAL);
    Adc_Sar_Ip_StartConversion(5u, ADC_SAR_IP_CONV_CHAIN_NORMAL);
#endif
}

bool hal_adc_set_watchdog(hal_adc_sig_t sig, uint16_t lo_trip, uint16_t hi_trip)
{
    if (((uint32_t)sig >= (uint32_t)HAL_ADC_COUNT) || !routed(sig)) {
        return false; /* only the five protection channels reach FAULT1 */
    }
#ifdef TI_RTD_AVAILABLE
    const Adc_Sar_Ip_WdgThresholdType thr = {.LowThreshold = adc_thrl(lo_trip),
                                             .HighThreshold = adc_thrh(hi_trip),
                                             .LowThresholdIntEn = (lo_trip > 0u),
                                             .HighThresholdIntEn = true}; /* TODO(RTD): field names */
    Adc_Sar_Ip_SetWdgThreshold(MAP[sig].inst, WD_REG, &thr);
    /* TODO(RTD): channel -> threshold register (CWSELRn) and enable (CWENRn) are in the generated
     * channel configuration (WdgThreshRegIndex); the watchdog event output is enabled for the LCU. */
    return s_init;
#else
    (void)lo_trip;
    (void)hi_trip;
    return false;
#endif
}

uint32_t hal_adc_watchdog_status(void)
{
    uint32_t st = 0u;
#ifdef TI_RTD_AVAILABLE
    for (uint32_t s = 0u; s < (uint32_t)HAL_ADC_COUNT; s++) {
        if (routed((hal_adc_sig_t)s)) {
            /* WTISR: bit 2k = low threshold k, bit 2k+1 = high threshold k (TODO(HW-RM)) */
            st |= (((BASE[MAP[s].inst]->WTISR >> (2u * WD_REG)) & 0x3u) != 0u) ? (1uL << s) : 0u;
        }
    }
#endif
    return st;
}

void hal_adc_watchdog_clear(uint32_t mask)
{
#ifdef TI_RTD_AVAILABLE
    for (uint32_t s = 0u; s < (uint32_t)HAL_ADC_COUNT; s++) {
        if (((mask & (1uL << s)) != 0u) && routed((hal_adc_sig_t)s)) {
            BASE[MAP[s].inst]->WTISR = 0x3uL << (2u * WD_REG); /* w1c */
        }
    }
#else
    (void)mask;
#endif
}
