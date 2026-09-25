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
 * each channel's DMA write position. */
#ifndef HAL_SDADC_H
#define HAL_SDADC_H

#include "ti_types.h"

#define HAL_SDADC_BLOCK_N 16u
#define HAL_SD_NBUF 4u /* ring slots per channel; a power of two, so slot = count % NBUF across the 2^32 wrap */

typedef enum { HAL_SD_EXC = 0, HAL_SD_SIN, HAL_SD_COS, HAL_SD_COUNT } hal_sd_ch_t;

typedef struct {
    int16_t blk[HAL_SD_COUNT][HAL_SDADC_BLOCK_N]; /* signed codes, full scale +/-32767 = +/-VREFP */
    uint32_t epoch; /* acquisition epoch: carrier periods every channel has completed (wraps) */
    uint32_t t_us;  /* start of the epoch (carrier phase 0), hal_time_us() domain */
} hal_sd_frame_t;

bool hal_sdadc_init(uint32_t carrier_hz);

/* The newest coherent frame not returned before. false: none — no new epoch, a channel has not
 * completed it, its slot may have been rewritten during the copy, or a channel lost step — and *f is
 * untouched: never a partial or mixed frame, never a fresh stamp on old data. */
bool hal_sdadc_read_frame(hal_sd_frame_t *f);

/* ---- the frame protocol both platforms share (sdadc_ring.c) ---- */
typedef struct {
    volatile uint32_t done[HAL_SD_COUNT];   /* blocks each channel's DMA completed (its own interrupt) */
    volatile uint32_t t_start[HAL_SD_NBUF]; /* start of the block in each slot (its first completion) */
    volatile uint32_t t_epoch;              /* start of the published epoch (written before it) */
    volatile uint32_t epoch;                /* newest epoch EVERY channel completed */
    volatile bool broken;                   /* a channel lost step: nothing is published until re-init */
    uint32_t taken;                         /* newest epoch the reader returned */
    uint32_t period_us;
} hal_sd_ring_t;

/* count0: the block count the DMA rings start at (slot = count % NBUF; the target starts at 0). */
void hal_sd_ring_init(hal_sd_ring_t *r, uint32_t period_us, uint32_t count0);
/* The major-loop interrupt of one channel (it outranks the reader). */
void hal_sd_ring_complete(hal_sd_ring_t *r, hal_sd_ch_t ch, uint32_t now_us);
/* The reader (current-loop ISR): see hal_sdadc_read_frame(). */
bool hal_sd_ring_read(hal_sd_ring_t *r, hal_sd_frame_t *f);

/* Platform half. The slot the channel's DMA is writing NOW, from its destination address — not from
 * any interrupt: the DMA keeps writing while interrupts are held off. >= HAL_SD_NBUF: unknown. */
uint32_t hal_sd_dma_slot(hal_sd_ch_t ch);
const volatile int16_t *hal_sd_dma_block(hal_sd_ch_t ch, uint32_t slot);

#endif /* HAL_SDADC_H */
