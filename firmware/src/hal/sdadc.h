/* hal/sdadc.h — resolver sigma-delta channels (GEN3 allocation, ball map rev A.12):
 *   SDADC1 AN0/AN1 = excitation monitor (VREXM_P/N, A12/D12)
 *   SDADC2 AN0/AN1 = SIN (C15/A16)
 *   SDADC3 AN0/AN1 = COS (B13/B14)
 * The three run differentially at a data rate of HAL_SDADC_BLOCK_N samples per excitation period,
 * started from the same trigger as the SWG so sample 0 is carrier phase 0: block k of every channel
 * is the same carrier period (epoch k). Each channel has its own eDMA channel into a ring of
 * HAL_SD_NBUF blocks and its own major-loop interrupt.
 *
 * Round 16 (A14-R02): the application reads one coherent FRAME — the three blocks of one epoch and
 * that epoch's time stamp, copied together — or nothing. The frame protocol (sdadc_ring.c) is the same
 * code on both platforms; the platform supplies the buffers, the per-channel completion interrupt and
 * each channel's DMA write position.
 * Round 18 (A16-R02): the stamp is the block's start on the SDADC cadence (the DMA completes exactly every
 * period; the data rate and the microsecond timer share one PLL), not the completion interrupt's time, and
 * a completion serviced later than irq_lat_max_us after its block's end breaks the ring.
 * Round 19 (A17-R01): the cadence's ORIGIN is the SWG start, never a completion. hal_swg_start() brackets the
 * generator enable with two 64-bit time reads and anchors the ring (hal_sd_ring_anchor): the first carrier
 * period starts at the later read within +/- unc_us (the bracket + cal_swg_start_lat_us). Every completion —
 * the first included — is judged against that origin; after an ambiguity the counts come from the clock and
 * the DMA positions only confirm them. A DMA out of phase with the clock is `lost`: down until a synchronized
 * producer restart (hal_sdadc_restart), which the application requests. */
#ifndef HAL_SDADC_H
#define HAL_SDADC_H

#include "ti_types.h"

#define HAL_SDADC_BLOCK_N 16u
#define HAL_SD_NBUF 4u /* ring slots per channel; a power of two, so slot = count % NBUF across the 2^32 wrap */

typedef enum { HAL_SD_EXC = 0, HAL_SD_SIN, HAL_SD_COS, HAL_SD_COUNT } hal_sd_ch_t;

typedef struct {
    int16_t blk[HAL_SD_COUNT][HAL_SDADC_BLOCK_N]; /* signed codes, full scale +/-32767 = +/-VREFP */
    uint32_t epoch; /* acquisition epoch: carrier periods every channel has completed (wraps) */
    uint32_t t_us;  /* start of the epoch (carrier phase 0) on the cadence, hal_time_us() domain */
} hal_sd_frame_t;

/* carrier_hz: a whole number of microseconds per period (the cadence stamp); irq_lat_max_us: the servicing
 * deadline of a block's first completion (cal_sd_irq_lat_max_us, below half a period); swg_start_lat_us: the SWG
 * enable to the first block's carrier phase 0 (cal_swg_start_lat_us, round 19). Stops the SWG: the converters and
 * their DMA are (re)armed with the generator stopped, and hal_swg_start() starts them and dates the cadence. */
bool hal_sdadc_init(uint32_t carrier_hz, uint32_t irq_lat_max_us, uint32_t swg_start_lat_us);
/* Round 18: the ring's re-acquisitions since init (the first frame published again after a break). */
uint32_t hal_sdadc_reacquired(void);
/* Round 19: a DMA lost the carrier phase (the ring's clock/position check, or the platform's DMA error, FIFO
 * overrun or trigger-miss flag): nothing is published until hal_sdadc_restart(). */
bool hal_sdadc_lost(void);
/* Round 19: the synchronized producer restart — the DMA rings re-armed, the SWG restarted at its present code and
 * the ring re-anchored (hal_sdadc_init + hal_swg_start); the re-acquisition count is kept. */
bool hal_sdadc_restart(void);

/* The newest coherent frame not returned before. false: none — no new epoch, a channel has not
 * completed it, its slot may have been rewritten during the copy, or a channel lost step — and *f is
 * untouched: never a partial or mixed frame, never a fresh stamp on old data. */
bool hal_sdadc_read_frame(hal_sd_frame_t *f);

/* ---- the frame protocol both platforms share (sdadc_ring.c) ---- */
typedef struct {
    volatile uint32_t done[HAL_SD_COUNT]; /* blocks each channel's DMA completed (its own interrupt) */
    volatile uint32_t t_epoch;            /* start of the published epoch (written before it) */
    volatile uint32_t epoch;              /* newest epoch EVERY channel completed */
    volatile uint64_t t_org;              /* round 19: block k_org starts at t_org +/- unc_us (64-bit us) */
    volatile uint32_t k_org;
    volatile uint32_t unc_us;             /* round 19: the origin's uncertainty (the SWG start bracket) */
    uint32_t n_wait;                      /* round 19: re-sync attempts without agreement (producer only) */
    volatile bool anchored;               /* round 19: t_org/k_org set by hal_sd_ring_anchor(), never by a completion */
    volatile bool synced;                 /* done[] match the clock and the DMA positions */
    volatile bool broken;                 /* nothing is published or read (from init or a break until a frame) */
    volatile bool relock;                 /* broken by an ambiguity: the next frame counts a re-acquisition */
    volatile bool lost;                   /* a DMA out of phase with the carrier: down until a producer restart */
    volatile uint32_t n_reacq;            /* re-acquisitions since init */
    uint32_t taken;                       /* newest epoch the reader returned */
    uint32_t period_us;
    uint32_t lat_us;                      /* a block's first completion is serviced within this of its end */
} hal_sd_ring_t;

/* count0: the block count the DMA rings start at (slot = count % NBUF; the target starts at 0). */
void hal_sd_ring_init(hal_sd_ring_t *r, uint32_t period_us, uint32_t lat_us, uint32_t count0);
/* Round 19: the SWG start (the platform's hal_swg_start): block k_org — the first carrier period, count0 + 1 —
 * starts at t_org_us (64-bit, hal_time_us64() domain) within +/- unc_us. unc_us >= period/4 cannot date the
 * cadence: the ring is lost. */
void hal_sd_ring_anchor(hal_sd_ring_t *r, uint64_t t_org_us, uint32_t k_org, uint32_t unc_us);
/* The major-loop interrupt of one channel (it outranks the reader); now_us: hal_time_us64() at its entry. */
void hal_sd_ring_complete(hal_sd_ring_t *r, hal_sd_ch_t ch, uint64_t now_us);
/* The reader (current-loop ISR): see hal_sdadc_read_frame(). */
bool hal_sd_ring_read(hal_sd_ring_t *r, hal_sd_frame_t *f);

/* Platform half. The slot the channel's DMA is writing NOW, from its destination address — not from
 * any interrupt: the DMA keeps writing while interrupts are held off. >= HAL_SD_NBUF: unknown. */
uint32_t hal_sd_dma_slot(hal_sd_ch_t ch);
const volatile int16_t *hal_sd_dma_block(hal_sd_ch_t ch, uint32_t slot);

#endif /* HAL_SDADC_H */
