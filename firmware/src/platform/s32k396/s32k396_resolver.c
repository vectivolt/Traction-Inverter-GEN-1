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
 * longer vouches for the other two, and a completion between channel reads can no longer mix epochs.
 * Round 18 (A16-R02): the frame's stamp is its block start on the SDADC cadence, not this interrupt's time
 * (the SDADC data rate and the STM share the PLL: docs/timing.md); a block's first completion served later
 * than cal_sd_irq_lat_max_us after the block's end breaks the ring.
 * Round 19 (A17-R01): the cadence's origin is the SWG start — hal_swg_start() brackets the enable with two
 * hal_time_us64() reads (PRIMASK: nothing runs between them) and anchors the ring — never a completion. A broken
 * ring re-syncs from the clock, the TCD destination addresses only confirming it; a DMA out of phase, or lost
 * samples, keep it down until hal_sdadc_restart(). */
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
static uint32_t s_carrier_hz;   /* round 19: kept for hal_sdadc_restart() */
static uint32_t s_start_lat_us; /* cal_swg_start_lat_us */
static bool s_swg_on;           /* the generator runs: hal_swg_start() only updates its amplitude */
static uint8_t s_swg_code;      /* its present IOAMPL code (the restart continues from it) */

bool hal_sdadc_init(uint32_t carrier_hz, uint32_t irq_lat_max_us, uint32_t swg_start_lat_us)
{
    s_ok = false;
    hal_swg_stop(); /* round 19: re-armed with the generator stopped; hal_swg_start() starts and dates the cadence */
    if ((carrier_hz == 0u) || (carrier_hz > 50000u) || ((1000000u % carrier_hz) != 0u)) {
        return false; /* the cadence stamp needs a whole number of microseconds per period */
    }
    s_carrier_hz = carrier_hz;
    s_start_lat_us = swg_start_lat_us;
    (void)memset(s_buf, 0, sizeof s_buf);
    hal_sd_ring_init(&s_ring, 1000000u / carrier_hz, irq_lat_max_us, 0u); /* every DMA starts in slot 0: count 0 */
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): Sdadc_Ip_Init(1/2/3, &SdadcHwUnit_n): differential inputs AN0/AN1 (board_pins),
     * decimation for ODR = 16 x carrier_hz, trigger = the SWG1 period start (via TRGMUX) so sample
     * 0 of every block sits at carrier phase 0, FIFO watermark 16, DMA request enabled, FIFO overrun
     * flag enabled. Dma_Ip_Init(&DmaIpInit): per SDADC one channel SDADCn CDR -> s_buf[n], four
     * scatter-gather TCDs (s_buf[n][0..3], 16 x 16 bit each, ESG to the next, TCD3 -> TCD0), INTMAJOR
     * on every TCD -> s32k_sdadc_dma_irq(n). TODO(HW): SDADC input range / gain for the windings. */
    /* TODO(RTD): a restart re-runs this: the SDADCs and their eDMA channels stopped first, the error, overrun and
     * INT flags cleared, every TCD chain back at slot 0 (count 0) before the SWG starts again (T-41) */
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
    /* TODO(RTD): the SDADC's own FIFO-overrun and trigger/conversion error flags of this channel set lost as well:
     * a converter that drops samples or misses its trigger must never resume out of phase silently (T-41) */
    if (((uint32_t)ch < (uint32_t)HAL_SD_COUNT) && TI_SD_LOST(ch)) {
        s_ring.lost = true; /* round 18: the blocks lost carrier phase 0 — no re-acquisition from positions */
    }
#endif
    /* TODO(HW): the latency of this interrupt after its DMA completion, distribution under load (T-40) */
    hal_sd_ring_complete(&s_ring, ch, hal_time_us64());
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
uint32_t hal_sdadc_reacquired(void) { return s_ring.n_reacq; }
bool hal_sdadc_lost(void) { return s_ring.lost; }

bool hal_sdadc_restart(void)
{
    const uint32_t n = s_ring.n_reacq;
    const bool ok = hal_sdadc_init(s_carrier_hz, s_ring.lat_us, s_start_lat_us) &&
                    hal_swg_start(s_carrier_hz, s_swg_code);
    s_ring.n_reacq = n; /* the caller counts the restart itself */
    return ok;
}

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
    const uint32_t ctrl = SGEN_CTRL_IOAMPL(amplitude_code) | SGEN_CTRL_IOFREQ(TI_SWG_IOFREQ_10K) | SGEN_CTRL_LDOS(1u);
    s_swg_code = amplitude_code;
    if (s_swg_on) {
        /* TODO(HW-RM): running, an IOAMPL update with LDOS = 1 loads at the next period and keeps every period
         * boundary — the cadence the ring was anchored to (T-42) */
        IP_SGEN_1->CTRL = ctrl;
        return true;
    }
    /* round 19 (A17-R01): the start dates the SDADC cadence. PRIMASK keeps the bracket to the write itself. */
    hal_crit_enter();
    const uint64_t t_a = hal_time_us64();
    IP_SGEN_1->CTRL = ctrl;
    const uint64_t t_b = hal_time_us64();
    hal_crit_exit();
    s_swg_on = true;
    s_swg_err_latched = false;
    /* TODO(HW): the SGEN start to its first period plus the TRGMUX/SDADC trigger latency (cal_swg_start_lat_us), and
     * the first block's sample 0 at carrier phase 0, measured against a GPIO set before the enable (T-42) */
    hal_sd_ring_anchor(&s_ring, t_b, 1u, (uint32_t)(t_b - t_a) + 1u + s_start_lat_us); /* the DMAs start at count 0 */
    return true;
#else
    return false;
#endif
}

void hal_swg_stop(void)
{
    s_swg_on = false;
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
