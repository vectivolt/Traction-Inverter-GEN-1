/* sdadc_ring.c — the resolver frame protocol shared by the S32K396 driver and the host simulation
 * (round 16, A14-R02: one SIN-DMA heartbeat no longer stands for three fresh channels; round 18, A16-R02:
 * a time base independent of interrupt latency).
 *
 * Producer: each channel's DMA major-loop interrupt (hal_sd_ring_complete) counts THAT channel's
 * blocks; block k of a channel sits in slot (k - 1) % HAL_SD_NBUF. An epoch is published only once
 * every channel has completed it (the per-channel handshake), each count checked against the channel's
 * DMA write position. A channel a whole block behind another, an interrupt that finds its DMA two blocks
 * on, or an unknown position break the block-to-epoch mapping.
 *
 * Round 18 — the stamp. The DMA completes exactly on the SDADC cadence (the data rate and the microsecond
 * timer derive from one PLL), so block k starts at t_org + (k - k_org) * period_us — unsigned, wrap-safe
 * across both the epoch and the microsecond wrap — whatever the interrupt's latency. The origin is anchored
 * at the first completion after (re)acquisition. A completion is never early: one that comes before its
 * block's end on the cadence shows the anchor was serviced late, and the origin moves back to it. The FIRST
 * completion of every block must be serviced within lat_us of that block's end: a later one cannot be told
 * from a whole lap of the 4-slot ring (the slot arithmetic is modulo 4), so the timing is ambiguous and the
 * ring breaks — nothing is published or read. A later channel's completion of the block must come within half
 * a period of that end (a channel may complete a little late: the round-16 tests model 30 us), else it holds
 * another period's data under this block's count (a DMA that stopped and resumed): the ring breaks too.
 *
 * Re-acquisition. A broken ring takes its counts from the DMA positions at a completion that finds all three
 * DMAs writing the same slot (the channels in step), numbered past every epoch published; the next completion
 * anchors a fresh origin; the first frame published again ends the break and counts one re-acquisition. A
 * stopped DMA may sit in the slot the others come round to: it then completes nothing, or its next block comes
 * a period late and breaks the ring again — never a frame. The resolver meanwhile ages its last frame out
 * (resolver.c, FW-28) and re-primes if the gap outlasts its hold. Lost samples (`lost`: the platform's DMA/FIFO
 * error) are no ambiguity — that channel's blocks no longer start at carrier phase 0 — so that ring stays down
 * until hal_sdadc_init() (fail closed).
 *
 * Reader (hal_sd_ring_read): a seqlock on the published epoch, every channel's DMA write position before
 * and after the copy, a copy-time bound, and (round 18) the cadence: epoch e's slot is rewritten only from
 * t_e + NBUF periods on, so a copy that ends within NBUF - 1 periods of t_e is intact whatever the slot
 * arithmetic says — a lap while the completion interrupts were held off is refused. Anything else returns
 * false with *f untouched: a missing or doubtful frame is absent, never re-stamped. */
#include "sdadc.h"
#include "timer.h"

void hal_sd_ring_init(hal_sd_ring_t *r, uint32_t period_us, uint32_t lat_us, uint32_t count0)
{
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        r->done[ch] = count0;
    }
    r->t_epoch = 0u;
    r->epoch = count0;
    r->t_org = 0u;
    r->k_org = count0;
    r->anchored = false;
    r->synced = false; /* the first completion takes the DMA positions, the next anchors: as after a break */
    r->broken = true;  /* nothing to read before the first frame */
    r->relock = false; /* ... which is no re-acquisition */
    r->lost = false;
    r->n_reacq = 0u;
    r->taken = count0;
    r->period_us = period_us;
    r->lat_us = lat_us;
}

static void ring_break(hal_sd_ring_t *r)
{
    r->broken = true;
    r->synced = false;
    r->anchored = false;
    r->relock = true;
}

/* Broken: counts unknown. Once every DMA writes the same slot s, the count whose next block goes to s, numbered
 * past every published epoch (an in-flight reader's epoch changes). */
static void resync(hal_sd_ring_t *r)
{
    const uint32_t s = hal_sd_dma_slot(HAL_SD_EXC);
    bool same = s < HAL_SD_NBUF;
    for (uint32_t ch = 1u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        same = same && (hal_sd_dma_slot((hal_sd_ch_t)ch) == s);
    }
    if (same) {
        const uint32_t b = r->epoch + 1u + ((s - r->epoch - 1u) % HAL_SD_NBUF);
        for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
            r->done[ch] = b;
        }
        r->epoch = b;
        r->synced = true;
    }
}

void hal_sd_ring_complete(hal_sd_ring_t *r, hal_sd_ch_t ch, uint32_t now_us)
{
    if ((uint32_t)ch >= (uint32_t)HAL_SD_COUNT) {
        return;
    }
    if (r->lost) {
        r->broken = true; /* lost samples: down until hal_sdadc_init() */
        return;
    }
    if (!r->synced) {
        resync(r);
        return;
    }
    const uint32_t hw = hal_sd_dma_slot(ch);
    const uint32_t adv = (hw - r->done[ch]) % HAL_SD_NBUF; /* blocks completed since the last count, modulo a lap */
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
    if (hw >= HAL_SD_NBUF) {
        ring_break(r);
        return;
    }
    if (r->anchored) { /* this completion against block k's end on the cadence */
        const uint32_t late = now_us - (r->t_org + ((k - r->k_org + 1u) * r->period_us));
        const bool early = late >= 0x80000000u;
        if (adv == 0u) {
            if (early || !first) {
                return; /* a repeated interrupt */
            }
            ring_break(r); /* block k has ended and the first DMA to finish it has not moved on: a lap or a stall */
            return;
        }
        if (early) {
            if (first) {
                r->t_org += late; /* the anchor was serviced late by (0 - late): a completion is never early */
            }
        } else if (late > (first ? r->lat_us : (r->period_us / 2u))) {
            ring_break(r); /* too late to tell a delay from a lap, or another period's block under this count */
            return;
        } else {
            /* on the cadence */
        }
    } else if (adv == 0u) {
        return; /* a repeated interrupt */
    } else {
        /* the anchor below */
    }
    if ((adv != 1u) || ((hi - lo) > 1u)) {
        ring_break(r);
        return;
    }
    if (!r->anchored) { /* the first completion after (re)acquisition: block k started one period ago */
        r->t_org = now_us - r->period_us;
        r->k_org = k;
        r->anchored = true;
    }
    r->done[ch] = k;
    if (lo >= 1u) { /* every channel has completed epoch e + 1 (in slot e % NBUF) */
        r->t_epoch = r->t_org + ((e + 1u - r->k_org) * r->period_us);
        r->epoch = e + 1u; /* published last: the reader's seqlock */
        if (r->broken) {
            r->n_reacq += r->relock ? 1u : 0u;
            r->relock = false;
            r->broken = false;
        }
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
    const uint32_t t1 = hal_time_us();
    if ((r->epoch != e) || r->broken || !dma_within(e, 2u) || ti_elapsed(t1, t0, r->period_us) ||
        ti_elapsed(t1, t_e, (HAL_SD_NBUF - 1u) * r->period_us)) {
        return false; /* published, overrun, preempted during the copy, or old enough for a lap: not trusted */
    }
    n.epoch = e;
    n.t_us = t_e;
    *f = n;
    r->taken = e;
    return true;
}
