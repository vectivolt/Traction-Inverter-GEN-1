/* sdadc_ring.c — the resolver frame protocol shared by the S32K396 driver and the host simulation
 * (round 16, A14-R02: one SIN-DMA heartbeat no longer stands for three fresh channels).
 *
 * Producer: each channel's DMA major-loop interrupt (hal_sd_ring_complete) counts THAT channel's
 * blocks; block k of a channel sits in slot (k - 1) % HAL_SD_NBUF. An epoch is published only once
 * every channel has completed it (the per-channel handshake); its time stamp is the first completion
 * of that block minus one carrier period, so a late channel never makes the frame look newer. Each
 * count is checked against the channel's DMA write position. A channel a whole block behind another
 * (frozen, or its FIFO overran and dropped samples), an interrupt that finds its DMA two blocks on
 * (held off across a completion: the time of the skipped block is unknown), or an unknown position
 * all break the block-to-epoch mapping: the ring stops publishing until hal_sdadc_init(). Fail closed —
 * the resolver then ages out (resolver.c, A14-R01).
 *
 * Reader (hal_sd_ring_read): a seqlock on the published epoch, plus every channel's DMA write position
 * before and after the copy, plus a copy-time bound. With a 4-slot ring, a copy that starts with every
 * DMA at most one block past the epoch and ends within one carrier period cannot have been overwritten
 * — even if the completion interrupts were held off meanwhile, which does not stop the DMA. Anything
 * else returns false with *f untouched: a missing or doubtful frame is absent, never re-stamped. */
#include "sdadc.h"
#include "timer.h"

void hal_sd_ring_init(hal_sd_ring_t *r, uint32_t period_us, uint32_t count0)
{
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        r->done[ch] = count0;
    }
    for (uint32_t s = 0u; s < HAL_SD_NBUF; s++) {
        r->t_start[s] = 0u;
    }
    r->t_epoch = 0u;
    r->epoch = count0;
    r->broken = false;
    r->taken = count0;
    r->period_us = period_us;
}

void hal_sd_ring_complete(hal_sd_ring_t *r, hal_sd_ch_t ch, uint32_t now_us)
{
    if (((uint32_t)ch >= (uint32_t)HAL_SD_COUNT) || r->broken) {
        return;
    }
    const uint32_t hw = hal_sd_dma_slot(ch);
    const uint32_t adv = (hw - r->done[ch]) % HAL_SD_NBUF; /* blocks completed since the last count */
    if ((hw < HAL_SD_NBUF) && (adv == 0u)) {
        return; /* no new block: a repeated interrupt */
    }
    const uint32_t e = r->epoch; /* every count is at or past it: distances are small and wrap-safe */
    const uint32_t k = r->done[ch] + 1u;
    bool first = true;
    uint32_t lo = k - e;
    uint32_t hi = k - e;
    for (uint32_t c = 0u; c < (uint32_t)HAL_SD_COUNT; c++) {
        if (c != (uint32_t)ch) {
            const uint32_t d = r->done[c] - e;
            first = first && (d < (k - e));
            lo = (d < lo) ? d : lo;
            hi = (d > hi) ? d : hi;
        }
    }
    if ((hw >= HAL_SD_NBUF) || (adv != 1u) || ((hi - lo) > 1u)) {
        r->broken = true;
        return;
    }
    r->done[ch] = k;
    if (first) {
        r->t_start[(k - 1u) % HAL_SD_NBUF] = now_us - r->period_us;
    }
    if (lo >= 1u) { /* every channel has completed epoch e + 1 (in slot e % NBUF) */
        r->t_epoch = r->t_start[e % HAL_SD_NBUF];
        r->epoch = e + 1u; /* published last: the reader's seqlock */
    }
}

/* Every channel's DMA is writing at most max_ahead slots past the slot after epoch e's. */
static bool dma_within(uint32_t e, uint32_t max_ahead)
{
    bool ok = true;
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        const uint32_t s = hal_sd_dma_slot((hal_sd_ch_t)ch);
        ok = ok && (s < HAL_SD_NBUF) && (((s - e) % HAL_SD_NBUF) <= max_ahead);
    }
    return ok;
}

bool hal_sd_ring_read(hal_sd_ring_t *r, hal_sd_frame_t *f)
{
    const uint32_t e = r->epoch;
    if (r->broken || (e == r->taken)) {
        return false; /* nothing new */
    }
    const uint32_t t_e = r->t_epoch;
    const uint32_t t0 = hal_time_us();
    if (!dma_within(e, 1u)) {
        return false; /* a DMA already two blocks on: this epoch's slot is next in line */
    }
    hal_sd_frame_t n;
    const uint32_t slot = (e - 1u) % HAL_SD_NBUF;
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        const volatile int16_t *b = hal_sd_dma_block((hal_sd_ch_t)ch, slot);
        for (uint32_t k = 0u; k < HAL_SDADC_BLOCK_N; k++) {
            n.blk[ch][k] = b[k];
        }
    }
    if ((r->epoch != e) || r->broken || !dma_within(e, 2u) || ti_elapsed(hal_time_us(), t0, r->period_us)) {
        return false; /* published, overrun or preempted during the copy: not trusted */
    }
    n.epoch = e;
    n.t_us = t_e;
    *f = n;
    r->taken = e;
    return true;
}
