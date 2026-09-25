/* s32k396_resolver.c — hal/sdadc.h and hal/swg.h: resolver excitation and demodulation inputs.
 *   SWG1 (SGEN_1 / D_IP_SWG_SYN): 10 kHz excitation, amplitude code 0..15 (CTRL.IOFREQ, CTRL.IOAMPL);
 *   SDADC1 = excitation monitor (VREXM), SDADC2 = SIN, SDADC3 = COS, all three started by one
 *   trigger so their blocks are aligned, 16 samples per carrier period (ODR 160 kS/s).
 * Round 16 (A14-R02): each SDADC has its own eDMA channel into a 4-slot ring — four scatter-gather
 * TCDs of one block each, the last linking back to the first, a major-loop interrupt on every TCD —
 * and its own completion interrupt (s32k_sdadc_dma_irq). The frame protocol is sdadc_ring.c, the same
 * code the host tests run: a frame is published only once all three channels have completed the same
 * epoch, and hal_sdadc_read_frame() copies it under a seqlock with the three DMA write positions
 * (their TCD destination addresses) checked before and after the copy. One channel's heartbeat no
 * longer vouches for the other two, and a completion between channel reads can no longer mix epochs. */
#include <string.h>

#include "s32k396.h"
#include "sdadc.h"
#include "swg.h"
#include "timer.h"

#ifdef TI_RTD_AVAILABLE
#include "Dma_Ip.h"   /* TODO(RTD): the IP drivers and the Config Tools symbols below */
#include "Sdadc_Ip.h"
extern const Sdadc_Ip_ConfigType SdadcHwUnit_1, SdadcHwUnit_2, SdadcHwUnit_3;
extern const Dma_Ip_InitType DmaIpInit;
#define TI_SWG_IOFREQ_10K 0u /* TODO(HW-RM): SGEN CTRL.IOFREQ for 10 kHz at the SWG clock (RM formula) */
#define TI_NOCACHE __attribute__((section(".ti_nocache"))) /* linker: non-cacheable SRAM for eDMA */
/* TODO(RTD): the eDMA channel of each SDADC (Config Tools: EXC, SIN, COS consecutive from TI_SD_DMA_CH0),
 * its TCD destination address and error flag; an SDADC FIFO overrun (samples lost) counts as lost too. */
#define TI_SD_DMA_CH0 0u
#define TI_SD_DMA_CH(ch) (TI_SD_DMA_CH0 + (uint32_t)(ch))
#define TI_SD_DADDR(ch) ((uintptr_t)IP_EDMA->TCD[TI_SD_DMA_CH(ch)].DADDR)
#define TI_SD_LOST(ch) ((IP_EDMA->TCD[TI_SD_DMA_CH(ch)].CH_ES & DMA_TCD_CH_ES_ERR_MASK) != 0u)
static bool s_swg_err_latched;
#else
#define TI_NOCACHE
#endif

TI_NOCACHE static int16_t s_buf[HAL_SD_COUNT][HAL_SD_NBUF][HAL_SDADC_BLOCK_N]; /* eDMA destinations */
static hal_sd_ring_t s_ring;
static bool s_ok;

bool hal_sdadc_init(uint32_t carrier_hz)
{
    s_ok = false;
    if ((carrier_hz == 0u) || (carrier_hz > 50000u)) {
        return false;
    }
    (void)memset(s_buf, 0, sizeof s_buf);
    hal_sd_ring_init(&s_ring, 1000000u / carrier_hz, 0u); /* every DMA starts in slot 0: count 0 */
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): Sdadc_Ip_Init(1/2/3, &SdadcHwUnit_n): differential inputs AN0/AN1 (board_pins),
     * decimation for ODR = 16 x carrier_hz, trigger = the SWG1 period start (via TRGMUX) so sample
     * 0 of every block sits at carrier phase 0, FIFO watermark 16, DMA request enabled, FIFO overrun
     * flag enabled. Dma_Ip_Init(&DmaIpInit): per SDADC one channel SDADCn CDR -> s_buf[n], four
     * scatter-gather TCDs (s_buf[n][0..3], 16 x 16 bit each, ESG to the next, TCD3 -> TCD0), INTMAJOR
     * on every TCD -> s32k_sdadc_dma_irq(n). TODO(HW): SDADC input range / gain for the windings. */
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

/* eDMA major loop of one SDADC's channel: that channel completed one block (its own interrupt). */
void s32k_sdadc_dma_irq(hal_sd_ch_t ch)
{
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): clear the channel's INT flag. Lost samples break the block-to-epoch mapping. */
    if (((uint32_t)ch < (uint32_t)HAL_SD_COUNT) && TI_SD_LOST(ch)) {
        s_ring.broken = true;
    }
#endif
    hal_sd_ring_complete(&s_ring, ch, hal_time_us());
}

uint32_t hal_sd_dma_slot(hal_sd_ch_t ch)
{
#ifdef TI_RTD_AVAILABLE
    if ((uint32_t)ch >= (uint32_t)HAL_SD_COUNT) {
        return HAL_SD_NBUF;
    }
    const uintptr_t base = (uintptr_t)&s_buf[ch][0][0];
    const uintptr_t d = TI_SD_DADDR(ch); /* the next address the DMA writes: inside the slot it is filling */
    const uintptr_t n = (d - base) / (HAL_SDADC_BLOCK_N * sizeof(int16_t));
    return ((d >= base) && (n < HAL_SD_NBUF)) ? (uint32_t)n : HAL_SD_NBUF;
#else
    (void)ch;
    return HAL_SD_NBUF; /* no DMA: unknown, so nothing is ever published or read */
#endif
}

const volatile int16_t *hal_sd_dma_block(hal_sd_ch_t ch, uint32_t slot)
{
    return &s_buf[(uint32_t)ch % (uint32_t)HAL_SD_COUNT][slot % HAL_SD_NBUF][0];
}

bool hal_sdadc_read_frame(hal_sd_frame_t *f) { return s_ok && hal_sd_ring_read(&s_ring, f); }

/* ---------------- SWG1 ---------------- */
bool hal_swg_start(uint32_t freq_hz, uint8_t amplitude_code)
{
    if ((freq_hz != 10000u) || (amplitude_code > HAL_SWG_CODE_MAX)) {
        return false; /* the resolver chain (card filter, -24 deg) is characterised at 10 kHz only */
    }
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD/HW-RM): SGEN_1 CTRL: IOFREQ from freq_hz and the SWG clock per the RM formula,
     * IOAMPL = amplitude_code, LDOS = 1 (load on the next period), then enable; confirm that
     * STAT.PHERR/FERR/SERR are clear after one period (the SGEN_CTRL / SGEN_STAT symbols here and below).
     * The IOAMPL code -> amplitude law: cal_swg_code_init assumes it linear from MINAPP to MAXAPP (the
     * datasheet gives the two ends); the trim — rslv_swg_trim() at run time, through this call — closes
     * on the monitor and does not rely on it. */
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
    IP_SGEN_1->CTRL = 0u;
#endif
}

bool hal_swg_error(void)
{
#ifdef TI_RTD_AVAILABLE
    const uint32_t st = IP_SGEN_1->STAT; /* PHERR | FERR | SERR */
    s_swg_err_latched = s_swg_err_latched || ((st & (SGEN_STAT_PHERR_MASK | SGEN_STAT_FERR_MASK | SGEN_STAT_SERR_MASK)) != 0u);
    return s_swg_err_latched;
#else
    return true; /* no generator: report the error so the resolver is never trusted */
#endif
}
