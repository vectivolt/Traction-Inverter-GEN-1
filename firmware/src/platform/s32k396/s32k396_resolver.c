/* s32k396_resolver.c — hal/sdadc.h and hal/swg.h: resolver excitation and demodulation inputs.
 *   SWG1 (SGEN_1 / D_IP_SWG_SYN): 10 kHz excitation, amplitude code 0..15 (CTRL.IOFREQ, CTRL.IOAMPL);
 *   SDADC1 = excitation monitor (VREXM), SDADC2 = SIN, SDADC3 = COS, all three started by one
 *   trigger so their blocks are aligned, 16 samples per carrier period (ODR 160 kS/s), each moved
 *   by eDMA into a two-block ring. The SIN channel's DMA major-loop interrupt stamps each block
 *   (start = completion - one carrier period; the SDADC group delay is cal_rslv_latency_us).
 * hal_sdadc_read_block() returns the newest complete block once; a block the current ISR missed is
 * skipped and the observer bridges the gap from the time stamps (resolver.c). */
#include <string.h>

#include "s32k396.h"
#include "sdadc.h"
#include "swg.h"
#include "timer.h"

#ifdef TI_RTD_AVAILABLE
#include "Dma_Ip.h"   /* TODO(RTD) */
#include "Sdadc_Ip.h" /* TODO(RTD) */
extern const Sdadc_Ip_ConfigType SdadcHwUnit_1, SdadcHwUnit_2, SdadcHwUnit_3; /* TODO(RTD): Config Tools */
extern const Dma_Ip_InitType DmaIpInit;                                      /* TODO(RTD) */
#define TI_SWG_IOFREQ_10K 0u /* TODO(HW-RM): SGEN CTRL.IOFREQ for 10 kHz at the SWG clock (RM formula) */
#define TI_NOCACHE __attribute__((section(".ti_nocache"))) /* linker: non-cacheable SRAM for eDMA */
static bool s_swg_err_latched;
#else
#define TI_NOCACHE
#endif

#define NBUF 2u

TI_NOCACHE static int16_t s_buf[HAL_SD_COUNT][NBUF][HAL_SDADC_BLOCK_N]; /* eDMA destinations */
static volatile uint32_t s_seq;   /* blocks completed (all three channels together) */
static volatile uint32_t s_t_blk; /* start time of the newest complete block */
static uint32_t s_taken[HAL_SD_COUNT];
static uint32_t s_period_us = 100u;
static bool s_ok;

bool hal_sdadc_init(uint32_t carrier_hz)
{
    s_ok = false;
    if ((carrier_hz == 0u) || (carrier_hz > 50000u)) {
        return false;
    }
    s_period_us = 1000000u / carrier_hz;
    (void)memset(s_buf, 0, sizeof s_buf);
    s_seq = 0u;
    (void)memset(s_taken, 0, sizeof s_taken);
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): Sdadc_Ip_Init(1/2/3, &SdadcHwUnit_n): differential inputs AN0/AN1 (board_pins),
     * decimation for ODR = 16 x carrier_hz, trigger = the SWG1 period start (via TRGMUX) so sample
     * 0 of every block sits at carrier phase 0, FIFO watermark 16, DMA request enabled.
     * Dma_Ip_Init(&DmaIpInit): three channels SDADCn CDR -> s_buf[n], 16 x 16 bit per major loop,
     * two major loops per ring, major-loop interrupt on the SIN channel only
     * (-> s32k_sdadc_block_irq). TODO(HW): SDADC input range / gain for the 3 V p-p windings. */
    if ((Sdadc_Ip_Init(1u, &SdadcHwUnit_1) != SDADC_IP_STATUS_SUCCESS) ||
        (Sdadc_Ip_Init(2u, &SdadcHwUnit_2) != SDADC_IP_STATUS_SUCCESS) ||
        (Sdadc_Ip_Init(3u, &SdadcHwUnit_3) != SDADC_IP_STATUS_SUCCESS)) {
        return false;
    }
    Dma_Ip_Init(&DmaIpInit);
    s_ok = true;
#endif
    return s_ok;
}

/* eDMA major loop of the SIN channel: one block of all three channels is complete. */
void s32k_sdadc_block_irq(void)
{
    s_t_blk = hal_time_us() - s_period_us;
    s_seq = s_seq + 1u;
}

bool hal_sdadc_read_block(hal_sd_ch_t ch, int16_t out[HAL_SDADC_BLOCK_N], uint32_t *t_us)
{
    if (!s_ok || ((uint32_t)ch >= (uint32_t)HAL_SD_COUNT)) {
        return false;
    }
    for (uint32_t attempt = 0u; attempt < 2u; attempt++) { /* a block completing during the copy: once more */
        const uint32_t seq = s_seq;
        if (seq == s_taken[ch]) {
            return false;
        }
        const uint32_t t = s_t_blk;
        (void)memcpy(out, s_buf[ch][(seq - 1u) % NBUF], sizeof s_buf[ch][0]);
        if (seq == s_seq) {
            s_taken[ch] = seq;
            *t_us = t;
            return true;
        }
    }
    return false;
}

/* ---------------- SWG1 ---------------- */
bool hal_swg_start(uint32_t freq_hz, uint8_t amplitude_code)
{
    if ((freq_hz != 10000u) || (amplitude_code > 15u)) {
        return false; /* the resolver chain (card filter, -24 deg) is characterised at 10 kHz only */
    }
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD/HW-RM): SGEN_1 CTRL: IOFREQ from freq_hz and the SWG clock per the RM formula,
     * IOAMPL = amplitude_code, LDOS = 1 (load on the next period), then enable; confirm that
     * STAT.PHERR/FERR/SERR are clear after one period. The excitation amplitude is trimmed at run
     * time by rslv_swg_trim() through this call. */
    IP_SGEN_1->CTRL = SGEN_CTRL_IOAMPL(amplitude_code) | SGEN_CTRL_IOFREQ(TI_SWG_IOFREQ_10K) | SGEN_CTRL_LDOS(1u);
    s_swg_err_latched = false;
    return true;
#else
    return false;
#endif
}

void hal_swg_stop(void)
{
#ifdef TI_RTD_AVAILABLE
    IP_SGEN_1->CTRL = 0u; /* TODO(RTD) */
#endif
}

bool hal_swg_error(void)
{
#ifdef TI_RTD_AVAILABLE
    const uint32_t st = IP_SGEN_1->STAT; /* TODO(RTD): PHERR | FERR | SERR */
    s_swg_err_latched = s_swg_err_latched || ((st & (SGEN_STAT_PHERR_MASK | SGEN_STAT_FERR_MASK | SGEN_STAT_SERR_MASK)) != 0u);
    return s_swg_err_latched;
#else
    return true; /* no generator: report the error so the resolver is never trusted */
#endif
}
