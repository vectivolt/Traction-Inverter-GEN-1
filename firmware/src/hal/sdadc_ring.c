/* sdadc_ring.c — the resolver frame protocol shared by the S32K396 driver and the host simulation
 * (round 16, A14-R02: one SIN-DMA heartbeat no longer stands for three fresh channels; round 18, A16-R02:
 * a time base independent of interrupt latency; round 19, A17-R01: an origin independent of every completion).
 *
 * Producer: each channel's DMA major-loop interrupt (hal_sd_ring_complete) counts THAT channel's
 * blocks; block k of a channel sits in slot (k - 1) % HAL_SD_NBUF. An epoch is published only once
 * every channel has completed it (the per-channel handshake), each count checked against the channel's
 * DMA write position. A channel a whole block behind another, an interrupt that finds its DMA two blocks
 * on, or an unknown position break the block-to-epoch mapping.
 *
 * The cadence (round 19). The SDADCs are triggered by the SWG period start, so block k starts at
 * t_org + (k - k_org) * period_us: t_org is the SWG START on the 64-bit microsecond timer and k_org the count of
 * the first carrier period (hal_sd_ring_anchor, from hal_swg_start: the enable bracketed by two time reads; the
 * uncertainty unc = the bracket + 1 us + cal_swg_start_lat_us). No completion sets or moves the origin: an unanchored
 * ring counts and publishes nothing, and EVERY completion — the first after the start and the first after a
 * break included — is judged against it, the uncertainty added on both sides. A block's first completion must
 * come within [-unc, lat_us + unc] of the block's end, a later channel's within [-unc, P/2 + unc]; earlier is
 * impossible (a completion never precedes its block's end), later is ambiguous (the 4-slot ring may have lapped,
 * or the channel holds another period's block under this count): the ring breaks, nothing is published or read.
 * Round 18 anchored at the first completion after (re)acquisition and moved the origin back by an earlier one,
 * so a late first completion, a constant delay, or the completion after a rejected one became the reference its
 * own lateness was judged by.
 *
 * Re-sync. A broken ring takes its counts from the clock — the block that ended within [-unc, P/2 + unc] of a
 * completion — and the DMA positions only confirm them: every DMA past that block, or a later one still on it (the
 * re-sync waits for its own completion). A DMA elsewhere, the completing one still on the block while another is
 * past it, or no agreement within NBUF periods of completions is a DMA out of phase with the carrier (it paused and
 * resumed, or dropped samples unflagged): `lost`, as are the platform's DMA error, FIFO-overrun and trigger-miss
 * flags. Equal positions alone prove nothing. A lost ring stays down until a synchronized producer restart
 * (hal_sdadc_restart: the DMA rings re-armed, the SWG restarted and re-anchored), which the application requests.
 * The first frame published after a re-sync counts one re-acquisition. Times are 64-bit (hal_time_us64()): the
 * clock-derived index holds across the 32-bit wrap and any silence; the origin is re-based by whole periods (the
 * same cadence) at every publication and re-sync, so block distances stay small across the 2^32 epoch wrap.
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
    r->anchored = false; /* first: a completion interrupt meanwhile counts nothing */
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        r->done[ch] = count0;
    }
    r->t_epoch = 0u;
    r->epoch = count0;
    r->t_org = 0u;
    r->k_org = count0 + 1u;
    r->unc_us = 0u;
    r->n_wait = 0u;
    r->synced = false;
    r->broken = true;  /* nothing to read before the first frame */
    r->relock = false; /* ... which is no re-acquisition */
    r->lost = false;
    r->n_reacq = 0u;
    r->taken = count0;
    r->period_us = period_us;
    r->lat_us = lat_us;
}

void hal_sd_ring_anchor(hal_sd_ring_t *r, uint64_t t_org_us, uint32_t k_org, uint32_t unc_us)
{
    r->anchored = false;
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        r->done[ch] = k_org - 1u; /* armed there, nothing completed: block k_org is the first carrier period */
    }
    r->epoch = k_org - 1u;
    r->t_org = t_org_us;
    r->k_org = k_org;
    r->unc_us = unc_us;
    r->n_wait = 0u;
    r->broken = true;
    r->synced = true;
    /* the re-sync window (P/2 + 2 unc) must stay inside a period: a wider bracket cannot date the cadence */
    r->lost = r->lost || (unc_us >= (r->period_us / 4u));
    r->anchored = true; /* last: the completions start counting */
}

static void ring_break(hal_sd_ring_t *r)
{
    r->broken = true;
    r->synced = false; /* the origin stays: the counts come back from the clock */
    r->relock = true;
}

static void ring_lost(hal_sd_ring_t *r)
{
    r->lost = true;
    r->broken = true;
}

/* The end of block k on the cadence (64-bit us); k is at or past k_org. */
static uint64_t blk_end(const hal_sd_ring_t *r, uint32_t k)
{
    return r->t_org + (((uint64_t)(k - r->k_org) + 1u) * (uint64_t)r->period_us);
}

/* Broken: ch has just completed. Its block j is the one whose end lies within [-unc, P/2 + unc] of now (none: this
 * interrupt came later than that — wait for the next); the DMA positions must agree with the clock. Every DMA past j:
 * in phase. Some still on j: a later channel (its own completion decides) — or ch itself, an interrupt from block
 * j - 1 served late while the clock cannot yet tell j's end (the origin's uncertainty): only another DMA already
 * past j proves ch behind. A DMA anywhere else, ch behind a DMA past j, or no agreement within NBUF periods of
 * completions (every DMA behind the clock): out of phase with the carrier — lost. */
static void resync(hal_sd_ring_t *r, hal_sd_ch_t ch, uint64_t now_us)
{
    const uint64_t per = r->period_us;
    const uint64_t unc = r->unc_us;
    if ((now_us + unc) < (r->t_org + per)) {
        return; /* before the first block's end: a stale interrupt */
    }
    const uint64_t since = (now_us + unc) - r->t_org; /* from the earliest the origin can be */
    if ((since % per) > ((per / 2u) + (2u * unc))) {
        return;
    }
    const uint64_t n = since / per;
    const uint32_t j = r->k_org + (uint32_t)(n - 1u); /* the block that ended: counts wrap like the DMA's */
    const uint32_t s = j % HAL_SD_NBUF;                /* where a DMA past block j writes */
    uint32_t past = 0u;
    bool ok = true;
    bool ch_on = false;
    for (uint32_t c = 0u; c < (uint32_t)HAL_SD_COUNT; c++) {
        const uint32_t sc = hal_sd_dma_slot((hal_sd_ch_t)c);
        past += (sc == s) ? 1u : 0u;
        ok = ok && ((sc == s) || (sc == ((j - 1u) % HAL_SD_NBUF)));
        ch_on = ch_on || ((c == (uint32_t)ch) && (sc != s));
    }
    if (!ok || (ch_on && (past > 0u))) {
        ring_lost(r); /* a DMA out of phase with the carrier: no position can restore it */
    } else if (past == (uint32_t)HAL_SD_COUNT) {
        r->t_org += (n - 1u) * per; /* re-based to block j's start: the same cadence */
        r->k_org = j;
        for (uint32_t c = 0u; c < (uint32_t)HAL_SD_COUNT; c++) {
            r->done[c] = j;
        }
        r->epoch = j; /* block j ended before the clock and the positions agreed: not published */
        r->n_wait = 0u;
        r->synced = true;
    } else {
        r->n_wait++;
        if (r->n_wait > (HAL_SD_NBUF * (uint32_t)HAL_SD_COUNT)) {
            ring_lost(r); /* NBUF periods of completions without agreement: every DMA behind the clock */
        }
    }
}

void hal_sd_ring_complete(hal_sd_ring_t *r, hal_sd_ch_t ch, uint64_t now_us)
{
    if (((uint32_t)ch >= (uint32_t)HAL_SD_COUNT) || !r->anchored) {
        return; /* round 19: no cadence before the SWG start dates it — nothing is counted */
    }
    if (r->lost) {
        r->broken = true; /* down until a synchronized producer restart */
        return;
    }
    if (!r->synced) {
        resync(r, ch, now_us);
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
    /* round 19: against block k's end on the absolute cadence, the origin's uncertainty on both sides */
    const uint64_t end = blk_end(r, k);
    const uint64_t unc = r->unc_us;
    if (adv == 0u) {
        if (first && (now_us >= (end + unc))) {
            ring_break(r); /* block k surely ended and the first DMA to finish it has not moved on: a lap or a stall */
        }
        return; /* else a repeated interrupt */
    }
    const uint64_t lim = unc + (first ? r->lat_us : (r->period_us / 2u));
    if (((now_us + unc) < end) || (now_us > (end + lim)) || (adv != 1u) || ((hi - lo) > 1u)) {
        ring_break(r); /* early (no completion precedes its block's end), too late to tell from a lap or from another
                        * period's block, a DMA two blocks on, or a channel a block behind another */
        return;
    }
    r->done[ch] = k;
    if (lo >= 1u) { /* every channel has completed epoch e + 1 (in slot e % NBUF) */
        const uint32_t e1 = e + 1u;
        r->t_org += (uint64_t)(e1 - r->k_org) * (uint64_t)r->period_us; /* re-based by whole periods */
        r->k_org = e1;
        r->t_epoch = (uint32_t)r->t_org; /* its start, in the hal_time_us() domain */
        r->epoch = e1; /* published last: the reader's seqlock */
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
