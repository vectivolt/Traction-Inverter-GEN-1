/* test_sdadc.c — the resolver frame protocol (hal/sdadc.h, sdadc_ring.c) on the simulated per-channel eDMA.
 * Round 16 (A14-R02): every block carries its carrier period in sample 0 (sim_sdadc_tag), so a frame is
 * coherent exactly when its three tags agree. Round 18 (A16-R02): and its stamp must be that carrier period's
 * start whatever the completion interrupts' latency. Round 19 (A17-R01): the start is the SWG's — the simulated
 * DMA blocks begin at the generator's start (+ its start latency), not at sim time 0 — and EVERY frame's stamp must
 * lie within the origin's declared uncertainty of it (the ring's unc_us: the start bracket, 1 us of timer resolution
 * and cal_swg_start_lat_us, 3 us here). Each test requires, at every read, either one such frame or an explicit
 * "unavailable" (false, the output untouched). */
#include <string.h>

#include "sdadc.h"
#include "sim.h"
#include "swg.h"
#include "test.h"
#include "timer.h"

#define PERIOD_US 100u
#define LAT_MAX_US 30u  /* cal_sd_irq_lat_max_us default */
#define START_LAT_US 2u /* cal_swg_start_lat_us default: the origin uncertain by 1 + 2 us (no time in the bracket) */
#define UNC_US (1u + START_LAT_US)

static unsigned s_frames;
static unsigned s_bad;    /* frames whose blocks disagree, or whose stamp is not their period's start */
static unsigned s_repeat; /* an epoch returned twice, or out of order */
static uint32_t s_last_epoch;

/* The converters armed, then the SWG started: it dates the cadence (round 19). */
static void start(uint32_t count0)
{
    sim_sdadc_count_base(count0);
    sim_sdadc_tag(true);
    (void)hal_sdadc_init(10000u, LAT_MAX_US, START_LAT_US);
    (void)hal_swg_start(10000u, 8u);
    s_frames = 0u;
    s_bad = 0u;
    s_repeat = 0u;
    s_last_epoch = count0;
}

/* The synchronized producer restart (as the application requests it): epochs count from the base again. */
static bool restart(uint32_t count0)
{
    s_last_epoch = count0;
    return hal_sdadc_restart();
}

/* The true start (ns) of the carrier period a block holds: the tag is its period index (from the SWG start) mod
 * 2^14, and a frame is at most a few periods old, so it is the latest period up to now with that tag. */
static uint64_t true_start_ns(int16_t tag)
{
    const uint64_t now_p = sim_sdadc_period_index();
    const uint64_t back = ((now_p & 0x3FFFu) - (uint64_t)(uint16_t)tag) & 0x3FFFu;
    return sim_sdadc_block_start_ns(now_p - back);
}

/* The frame's stamp minus its period's true start, ns (the 32-bit stamp across the wrap). */
static int64_t stamp_err_ns(const hal_sd_frame_t *f)
{
    const uint64_t t = true_start_ns(f->blk[HAL_SD_EXC][0]);
    const int32_t d_us = (int32_t)(f->t_us - (uint32_t)(t / 1000u));
    return ((int64_t)d_us * 1000) - (int64_t)(t % 1000u);
}

/* One epoch of all three channels, stamped at its carrier period's true start within the declared uncertainty. */
static bool honest(const hal_sd_frame_t *f)
{
    const int16_t tag = f->blk[HAL_SD_EXC][0];
    const int64_t err = stamp_err_ns(f);
    const int64_t lim = (int64_t)UNC_US * 1000;
    return (f->blk[HAL_SD_SIN][0] == tag) && (f->blk[HAL_SD_COS][0] == tag) && (err >= -lim) && (err <= lim);
}

/* One read as the current loop does it; checks what came back. */
static bool read1(hal_sd_frame_t *f)
{
    const hal_sd_frame_t before = *f;
    const bool got = hal_sdadc_read_frame(f);
    if (!got) {
        s_bad += (memcmp(&before, f, sizeof *f) != 0) ? 1u : 0u; /* unavailable: untouched */
        return false;
    }
    s_frames++;
    s_bad += honest(f) ? 0u : 1u;
    s_repeat += ((uint32_t)(f->epoch - s_last_epoch) >= 1u) && ((uint32_t)(f->epoch - s_last_epoch) < 0x80000000u) ? 0u : 1u;
    s_last_epoch = f->epoch;
    return true;
}

/* To us microseconds after the next carrier-period boundary (the SWG's cadence). */
static void after_boundary(uint32_t us)
{
    sim_advance_ns((sim_sdadc_block_start_ns(sim_sdadc_period_index() + 1u) + ((uint64_t)us * 1000u)) - sim_now_ns());
}

/* Read every step_us for us. */
static void read_for(uint32_t us, uint32_t step_us, hal_sd_frame_t *f)
{
    for (uint32_t t = 0u; t < us; t += step_us) {
        sim_advance_us(step_us);
        (void)read1(f);
    }
}

TEST(every_frame_is_one_epoch_of_all_three_channels)
{
    start(0u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    uint32_t prev_t = 0u;
    bool steady = true;
    for (uint32_t t = 0u; t < 20000u; t += 50u) { /* the current loop at 20 kHz, 2 ms */
        sim_advance_us(50u);
        const uint32_t e0 = f.epoch;
        if (read1(&f)) {
            steady = steady && ((s_frames == 1u) || ((f.epoch - e0) == 1u && (f.t_us - prev_t) == PERIOD_US));
            prev_t = f.t_us;
        }
    }
    CHECK(s_frames >= 195u && s_bad == 0u && s_repeat == 0u && steady);
    CHECK(!sim_sdadc_ring()->broken && !hal_sdadc_lost() && hal_sdadc_reacquired() == 0u);
}

/* The reviewer's first case (round 16): a frozen EXC or COS buffer read as fresh because SIN's interrupt kept
 * publishing. No frame is published without every channel: none while it is frozen, never a stale block dressed
 * as new, and the ring breaks once a channel is a whole block behind. Round 19 (A17-R01): the re-sync then compares
 * the DMA positions with the clock and finds the frozen channel behind it — a DMA out of phase with the carrier,
 * `lost`, with no platform flag — so its resume, in step (20 periods: its slot came round; round 18 re-acquired it
 * from the equal positions) or not (21), brings nothing back. The synchronized restart does: frames again, honest. */
TEST(a_frozen_channel_yields_no_frame)
{
    const uint32_t frozen_us[2] = {2000u, 2100u};
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        for (uint32_t k = 0u; k < 2u; k++) {
            sim_reset();
            start(0u);
            hal_sd_frame_t f;
            (void)memset(&f, 0, sizeof f);
            read_for(1000u, 50u, &f);
            const unsigned before = s_frames;
            CHECK(before >= 7u);
            sim_sdadc_freeze((hal_sd_ch_t)ch, true);
            read_for(frozen_us[k], 50u, &f);
            CHECK(s_frames <= before + 1u); /* at most the epoch completed as it froze */
            CHECK(sim_sdadc_ring()->broken && hal_sdadc_lost());
            sim_sdadc_freeze((hal_sd_ch_t)ch, false);
            const unsigned frozen = s_frames;
            read_for(1000u, 50u, &f);
            CHECK(s_frames == frozen && hal_sdadc_lost() && hal_sdadc_reacquired() == 0u);
            CHECK(restart(0u));
            read_for(1000u, 50u, &f);
            CHECK(s_frames >= frozen + 8u && s_bad == 0u && s_repeat == 0u);
            CHECK(!sim_sdadc_ring()->broken && !hal_sdadc_lost() && hal_sdadc_reacquired() == 0u);
        }
    }
}

/* A channel whose DMA completes 30 us late: until it has, its epoch is not published (the read says
 * "nothing new"); then the frame is whole, and its stamp is the block's start, not the late completion. */
TEST(a_late_channel_holds_the_frame_back_and_the_stamp_is_the_first)
{
    start(0u);
    sim_sdadc_delay_ns(HAL_SD_COS, 30000u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    read_for(500u, 50u, &f);
    after_boundary(50u);
    (void)read1(&f);     /* everything published so far is taken */
    after_boundary(10u);
    CHECK(!read1(&f));   /* EXC and SIN have completed this epoch, COS not */
    sim_advance_us(25u); /* 35 us after the boundary: COS too */
    CHECK(read1(&f) && honest(&f));
    CHECK(stamp_err_ns(&f) == 0); /* the block's start on the SWG's cadence, not the late completion (+ 31 us) */
    read_for(2000u, 50u, &f);
    CHECK(s_bad == 0u && s_repeat == 0u && !sim_sdadc_ring()->broken && s_frames >= 20u);
}

static uint32_t s_hook_mode;
static bool s_hook_done;

static void hook(hal_sd_ch_t ch)
{
    if ((ch != HAL_SD_SIN) || s_hook_done) {
        return; /* act once, after the EXC block was copied and before SIN's */
    }
    s_hook_done = true;
    sim_advance_us(s_hook_mode); /* the reader is preempted for s_hook_mode us while every DMA keeps writing */
}

/* The reviewer's second case: a completion between the EXC read and the SIN/COS reads gave EXC of one
 * epoch with SIN/COS of the next. Now the copy is checked as a whole: a publication during it (seqlock: 6 us
 * across a carrier boundary with the interrupts served), a DMA that reached the frame's slot, or a copy longer
 * than a carrier period is "unavailable"; DMAs that only moved on into their next slots (2 us across a boundary,
 * their completion interrupts still pending — round 19: before, a forced early completion modelled this, which the
 * ring now refuses: an_early_completion_breaks_the_ring) leave the frame intact, and it is returned whole. */
TEST(a_completion_between_channel_reads_never_mixes_epochs)
{
    const uint32_t mode[4] = {6u, 2u, 150u, 350u};
    const uint32_t at_us[4] = {95u, 99u, 20u, 20u}; /* where in the carrier period the read starts */
    const bool expect[4] = {false, true, false, false};
    for (uint32_t m = 0u; m < 4u; m++) {
        sim_reset();
        start(0u);
        hal_sd_frame_t f;
        (void)memset(&f, 0, sizeof f);
        read_for(500u, 50u, &f);
        after_boundary(at_us[m]); /* a new epoch published, not read yet */
        if (m == 1u) {
            sim_sdadc_irq_latency_ns(5000u); /* the boundary's completion interrupts pend during the read */
        }
        if (m >= 2u) {
            sim_sdadc_irq_latency_ns(1000000u); /* the completion interrupts held off during the read */
        }
        const uint32_t e0 = sim_sdadc_ring()->epoch;
        const hal_sd_frame_t before = f;
        s_hook_mode = mode[m];
        s_hook_done = false;
        sim_sdadc_read_hook(hook);
        const bool got = hal_sdadc_read_frame(&f);
        sim_sdadc_read_hook(NULL);
        CHECK(s_hook_done && got == expect[m]);
        CHECK(got ? (honest(&f) && f.epoch == e0) : (memcmp(&before, &f, sizeof f) == 0));
        if (m == 0u) {
            CHECK(read1(&f) && honest(&f) && f.epoch == e0 + 1u); /* the new epoch, whole, next time */
        }
    }
}

/* Interrupts held off while the DMA runs on: the ring still names an old epoch whose slot the DMA is
 * about to reach (the start check refuses it). The interrupts that finally run are 120 us past their
 * block's end — round 16 then stopped publishing for good; round 18: past the servicing deadline the
 * timing is ambiguous, the ring breaks and re-acquires from the DMA positions — frames again, exact. */
TEST(held_off_completion_interrupts_never_yield_a_fresh_frame)
{
    start(0u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    read_for(500u, 50u, &f);
    const unsigned before = s_frames;
    after_boundary(20u); /* epoch e1 published, not read yet */
    const uint32_t e1 = sim_sdadc_ring()->epoch;
    sim_sdadc_irq_latency_ns(1000000u);
    sim_advance_us(200u); /* two more blocks, their interrupts held off */
    CHECK(sim_sdadc_ring()->epoch == e1 && !sim_sdadc_ring()->broken); /* the seqlock alone sees nothing */
    CHECK(!read1(&f));                                                  /* the DMA positions do */
    sim_sdadc_irq_latency_ns(1000u); /* the pending interrupts run (one per channel for 2-3 blocks) */
    sim_advance_us(10u);
    CHECK(s_frames == before && sim_sdadc_ring()->broken);
    read_for(1000u, 50u, &f);
    CHECK(s_frames >= before + 5u && s_bad == 0u && s_repeat == 0u);
    CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() == 1u);
}

/* The epoch counter across 2^32: consecutive, honest, stamped a carrier period apart. */
TEST(the_epoch_counter_wraps)
{
    start(0xFFFFFFF0u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    f.epoch = 0xFFFFFFF0u;
    bool crossed = false;
    for (uint32_t t = 0u; t < 4000u; t += 50u) {
        sim_advance_us(50u);
        const uint32_t e0 = f.epoch;
        if (read1(&f)) {
            crossed = crossed || (f.epoch < e0);
        }
    }
    CHECK(crossed && s_frames >= 37u && s_bad == 0u && s_repeat == 0u && !sim_sdadc_ring()->broken);
    /* round 19: the origin moves with every publication by whole periods (the same cadence), so block distances stay
     * small however long the ring runs: it is the newest epoch's start */
    const hal_sd_ring_t *r = sim_sdadc_ring();
    CHECK((r->k_org == r->epoch) && ((uint32_t)r->t_org == r->t_epoch));
}

/* ======================= round 18 (A16-R02) ======================= */

/* The reviewer's case: the stamp was the completion interrupt's time minus a period, so an interrupt served
 * late stamped old data too new (100 us at 10 kHz is 24 deg el at 10 000 rpm, 4 pole pairs). Held off by up
 * to the servicing deadline (5, 20, 29 us), every completion is accepted and every stamp is still the block's
 * true start: the cadence, not the interrupt, dates the data. */
TEST(interrupts_held_off_within_the_deadline_keep_every_stamp_exact)
{
    start(0u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    read_for(500u, 50u, &f);
    const uint32_t lat_ns[4] = {20000u, 5000u, 29000u, 1000u};
    for (uint32_t k = 0u; k < 4u; k++) {
        sim_sdadc_irq_latency_ns(lat_ns[k]);
        read_for(1000u, 50u, &f);
    }
    CHECK(s_frames >= 40u && s_bad == 0u && s_repeat == 0u);
    CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() == 0u);
}

/* Held off once by 40 us (past the 30 us deadline, inside half a period) and by 60 us (twice the deadline):
 * that completion cannot be told from a lap, so its block is never published; the ring breaks, takes the DMA
 * positions at the next interrupt, anchors at the next carrier period, and frames resume within three periods
 * — exact, one re-acquisition. */
TEST(an_interrupt_held_off_past_the_deadline_is_rejected_then_reacquired)
{
    const uint32_t held_us[2] = {40u, 60u};
    for (uint32_t k = 0u; k < 2u; k++) {
        sim_reset();
        start(0u);
        hal_sd_frame_t f;
        (void)memset(&f, 0, sizeof f);
        read_for(500u, 50u, &f);
        after_boundary(50u);
        (void)read1(&f);
        const unsigned before = s_frames;
        sim_sdadc_irq_latency_ns(held_us[k] * 1000u);
        const int16_t late_tag = (int16_t)(sim_sdadc_period_index() & 0x3FFFu); /* the block completing next */
        after_boundary(held_us[k] + 1u); /* its interrupts ran held_us late */
        sim_sdadc_irq_latency_ns(1000u);
        CHECK(sim_sdadc_ring()->broken && !read1(&f));
        bool late_seen = false;
        uint32_t first_us = 0u;
        for (uint32_t us = 0u; us < 1000u; us += 50u) {
            sim_advance_us(50u);
            if (read1(&f)) {
                late_seen = late_seen || (f.blk[HAL_SD_EXC][0] == late_tag);
                first_us = (first_us == 0u) ? (us + 50u) : first_us;
            }
        }
        CHECK(!late_seen && first_us != 0u && first_us <= 3u * PERIOD_US);
        CHECK(s_frames >= before + 8u && s_bad == 0u && s_repeat == 0u);
        CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() == 1u);
    }
}

/* Held off for exactly a lap (four periods): the slot arithmetic sees nothing (a DMA four blocks on writes
 * the slot it wrote before), so a read in the meantime got the next lap's block under the old epoch's stamp.
 * Now the reader refuses a frame whose slot the cadence says was rewritten, the interrupts that run at last
 * break the ring (a lap past the deadline), and it re-acquires: frames again, exact. */
TEST(interrupts_held_off_for_a_lap_are_detected_never_fresh)
{
    start(0u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    read_for(500u, 50u, &f);
    const unsigned before = s_frames;
    after_boundary(20u); /* epoch e1 published, not read */
    const uint32_t e1 = sim_sdadc_ring()->epoch;
    sim_sdadc_irq_latency_ns(1000000u);
    sim_advance_us(400u); /* four more blocks: e1's slot now holds e1 + 4 */
    CHECK(sim_sdadc_ring()->epoch == e1 && !sim_sdadc_ring()->broken);
    CHECK(!read1(&f));
    sim_advance_us(50u);
    CHECK(!read1(&f));
    sim_sdadc_irq_latency_ns(1000u);
    sim_advance_us(10u);
    CHECK(sim_sdadc_ring()->broken && s_frames == before);
    read_for(1000u, 50u, &f);
    CHECK(s_frames >= before + 5u && s_bad == 0u && s_repeat == 0u && hal_sdadc_reacquired() == 1u);
}

/* One channel's completion interrupt held off for a lap (350 us) while the other two are served: its count
 * falls two blocks behind theirs (the ring breaks: no frame, nothing mixed), and when its interrupt runs its
 * DMA is exactly four blocks on — which the slot arithmetic alone would take for a repeated interrupt. The
 * ring re-acquires from the DMA positions (all three in step): frames again, exact. */
TEST(one_channel_lapping_while_the_others_do_not)
{
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        sim_reset();
        start(0u);
        hal_sd_frame_t f;
        (void)memset(&f, 0, sizeof f);
        read_for(500u, 50u, &f);
        after_boundary(50u);
        (void)read1(&f);
        const unsigned before = s_frames;
        sim_sdadc_irq_hold_ns((hal_sd_ch_t)ch, 350000u);
        after_boundary(0u);
        read_for(300u, 50u, &f); /* its interrupt is still held */
        CHECK(s_frames <= before + 1u);
        read_for(1500u, 50u, &f);
        CHECK(s_frames >= before + 8u && s_bad == 0u && s_repeat == 0u);
        CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() >= 1u);
    }
}

/* All three stalled together — their completion interrupts (5 and 8 periods: not and exactly two laps) or
 * their DMAs (the converters stop and restart on the carrier cadence) — no frame meanwhile; afterwards the first
 * completion is late on the cadence by the stall and the ring breaks (a cadence stamp without the deadline would
 * have dated the returning blocks 5 or 8 periods early). Held interrupts: the DMAs ran on, clock and positions
 * agree, the ring re-syncs — one re-acquisition, every stamp honest. Stalled DMAs (round 19): their counts are 5
 * or 8 blocks behind the clock; after 8 (two laps) every slot again holds the period the clock gives it and the
 * ring re-syncs, after 5 none does — `lost`, frames only after the restart (round 18 re-acquired both from the
 * equal positions, under a fresh origin). */
TEST(all_three_stalled_together_are_detected_and_reacquired)
{
    const uint32_t stall_us[2] = {500u, 800u};
    for (uint32_t how = 0u; how < 2u; how++) {
        for (uint32_t k = 0u; k < 2u; k++) {
            sim_reset();
            start(0u);
            hal_sd_frame_t f;
            (void)memset(&f, 0, sizeof f);
            read_for(500u, 50u, &f);
            after_boundary(50u);
            (void)read1(&f);
            const unsigned before = s_frames;
            for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
                if (how == 0u) {
                    sim_sdadc_irq_hold_ns((hal_sd_ch_t)ch, stall_us[k] * 1000u);
                } else {
                    sim_sdadc_freeze((hal_sd_ch_t)ch, true);
                }
            }
            read_for(stall_us[k], 50u, &f);
            CHECK(s_frames == before);
            for (uint32_t ch = 0u; (how == 1u) && (ch < (uint32_t)HAL_SD_COUNT); ch++) {
                sim_sdadc_freeze((hal_sd_ch_t)ch, false);
            }
            read_for(1500u, 50u, &f);
            const bool out_of_phase = (how == 1u) && (k == 0u);
            CHECK(hal_sdadc_lost() == out_of_phase);
            if (out_of_phase) {
                CHECK(s_frames == before && hal_sdadc_reacquired() == 0u);
                CHECK(restart(0u));
                read_for(1500u, 50u, &f);
            }
            CHECK(s_frames >= before + 10u && s_bad == 0u && s_repeat == 0u);
            CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() == (out_of_phase ? 0u : 1u));
        }
    }
}

static uint32_t s_preempt_us;
static void preempt(hal_sd_ch_t ch)
{
    if ((ch == HAL_SD_SIN) && !s_hook_done) {
        s_hook_done = true;
        sim_advance_us(s_preempt_us);
    }
}

/* The reader preempted between its channel copies for a whole lap (400 us), with the completion interrupts
 * served meanwhile (four epochs published: the seqlock) or held off (the copy-time bound and the cadence):
 * the frame is refused either way, and the next read returns the newest epoch, exact. */
TEST(the_reader_preempted_across_a_lap_never_returns_the_frame)
{
    for (uint32_t held = 0u; held < 2u; held++) {
        sim_reset();
        start(0u);
        hal_sd_frame_t f;
        (void)memset(&f, 0, sizeof f);
        read_for(500u, 50u, &f);
        after_boundary(20u);
        if (held == 1u) {
            sim_sdadc_irq_latency_ns(1000000u);
        }
        const hal_sd_frame_t before = f;
        s_preempt_us = 400u;
        s_hook_done = false;
        sim_sdadc_read_hook(preempt);
        const bool got = hal_sdadc_read_frame(&f);
        sim_sdadc_read_hook(NULL);
        CHECK(s_hook_done && !got && (memcmp(&before, &f, sizeof f) == 0));
        sim_sdadc_irq_latency_ns(1000u);
        read_for(1000u, 50u, &f);
        CHECK(s_frames >= 5u && s_bad == 0u && s_repeat == 0u && !sim_sdadc_ring()->broken);
    }
}

/* The stamps across the 32-bit microsecond wrap and the epoch counter's wrap together, with the interrupts'
 * latency changing every millisecond inside the deadline: every frame exact (its period's start modulo 2^32),
 * a period after the previous one, epochs consecutive. */
TEST(stamps_are_exact_across_the_microsecond_and_epoch_wraps)
{
    sim_reset_at_us(4294967296ull - 2000u);
    start(0xFFFFFFF8u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    f.epoch = 0xFFFFFFF8u;
    const uint32_t lat_ns[3] = {1000u, 20000u, 29000u};
    bool crossed_us = false;
    bool crossed_epoch = false;
    bool steady = true;
    uint32_t prev_t = 0u;
    for (uint32_t ms = 0u; ms < 6u; ms++) {
        sim_sdadc_irq_latency_ns(lat_ns[ms % 3u]);
        for (uint32_t t = 0u; t < 1000u; t += 50u) {
            sim_advance_us(50u);
            const uint32_t e0 = f.epoch;
            if (read1(&f)) {
                steady = steady && ((s_frames == 1u) || ((f.epoch - e0) == 1u && ti_age(f.t_us, prev_t) == PERIOD_US));
                crossed_us = crossed_us || (f.t_us < prev_t);
                crossed_epoch = crossed_epoch || (f.epoch < e0);
                prev_t = f.t_us;
            }
        }
    }
    CHECK(crossed_us && crossed_epoch && steady && s_frames >= 50u && s_bad == 0u && s_repeat == 0u);
    CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() == 0u);
}

/* Lost samples (the target driver's DMA/FIFO error flag, TI_SD_LOST) are no ambiguity: the channel's blocks
 * no longer start at carrier phase 0, and positions cannot restore that. The ring stays down — no frame, no
 * re-acquisition — until the synchronized producer restart (round 19: hal_sdadc_restart re-arms the converters,
 * restarts the SWG and re-anchors; round 18 needed hal_sdadc_init()), which keeps the re-acquisition count. */
TEST(lost_samples_keep_the_ring_down_until_a_restart)
{
    start(0u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    read_for(500u, 50u, &f);
    sim_sdadc_irq_latency_ns(60000u); /* one ambiguity first: one re-acquisition on record */
    after_boundary(70u);
    sim_sdadc_irq_latency_ns(1000u);
    read_for(500u, 50u, &f);
    CHECK(hal_sdadc_reacquired() == 1u && !sim_sdadc_ring()->broken);
    const unsigned before = s_frames;
    sim_sdadc_overrun();
    read_for(2000u, 50u, &f);
    CHECK(s_frames <= before + 1u && sim_sdadc_ring()->broken && hal_sdadc_lost() && hal_sdadc_reacquired() == 1u);
    CHECK(restart(0u));
    read_for(1000u, 50u, &f);
    CHECK(s_frames >= before + 8u && s_bad == 0u && s_repeat == 0u && !sim_sdadc_ring()->broken);
    CHECK(!hal_sdadc_lost() && hal_sdadc_reacquired() == 1u);
}

/* ======================= round 19 (A17-R01): the origin is the SWG start ======================= */

/* The reviewers' first counterexample: the origin came from a completion's own execution time — round 18 took the
 * DMA positions at the first completion and anchored at the next block's — so a late first callback (80 us; the
 * deadline is 30 us) became the reference: that frame stamped 80 us too new (19.2 deg el at 10 000 rpm, 4 pole
 * pairs), nothing broken. Now the origin is the SWG start and the first blocks' completions are judged against it
 * like every other. Held off by 0, 29, 30, 31 or 33 us (the deadline + the 3 us uncertainty) they are accepted and
 * blocks 0 and 1 are stamped at their true starts — the SWG start and one period on; by 34, 40, 60, 80 or 800 us (the
 * third recheck's case: published 700 us older than stamped) the ring breaks at once and neither is ever published.
 * Prompt again, the ring re-syncs from the clock: every frame honest. */
TEST(the_first_completion_is_judged_against_the_swg_start)
{
    const uint32_t lat_us[10] = {0u, 29u, 30u, 31u, 33u, 34u, 40u, 60u, 80u, 800u};
    for (uint32_t i = 0u; i < 10u; i++) {
        sim_reset();
        sim_sdadc_irq_latency_ns(lat_us[i] * 1000u); /* the first two blocks' completions */
        start(0u);
        CHECK(sim_sdadc_ring()->unc_us == UNC_US);
        const bool ok = lat_us[i] <= (LAT_MAX_US + UNC_US);
        hal_sd_frame_t f;
        (void)memset(&f, 0, sizeof f);
        for (int16_t b = 0; b < 2; b++) {
            after_boundary(lat_us[i] + 1u); /* block b's three completions have been served */
            const bool got = read1(&f);
            CHECK(got == ok && sim_sdadc_ring()->broken == !ok);
            CHECK(!got || ((f.blk[HAL_SD_EXC][0] == b) && (stamp_err_ns(&f) == 0)));
        }
        sim_sdadc_irq_latency_ns(1000u);
        bool early_blocks_later = false;
        for (uint32_t t = 0u; t < 1000u; t += 50u) {
            sim_advance_us(50u);
            if (read1(&f)) {
                early_blocks_later = early_blocks_later || (f.blk[HAL_SD_EXC][0] < 2);
            }
        }
        CHECK(!early_blocks_later && s_frames >= 9u && s_bad == 0u && s_repeat == 0u);
        CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() == (ok ? 0u : 1u));
    }
}

/* The origin's uncertainty budget: the SWG start latency (the generator's first period after the enable, and the
 * converters' trigger) is declared by cal_swg_start_lat_us (2 us); the bracket adds 1 us of timer resolution. With
 * the simulated latency at 1, 2 and 3 us every frame is stamped within those 3 us of its true start — exactly the
 * latency early here: the stamp is the enable itself, never a completion — and every completion is accepted,
 * prompt (1 us) or served at the deadline (30 us): the start latency sits inside the declared window. */
TEST(the_swg_start_latency_stays_inside_the_declared_uncertainty)
{
    for (uint32_t lat = 1u; lat <= 3u; lat++) {
        for (uint32_t d = 0u; d < 2u; d++) {
            sim_reset();
            sim_swg_start_latency_ns(lat * 1000u);
            sim_sdadc_irq_latency_ns((d == 0u) ? 1000u : (LAT_MAX_US * 1000u));
            start(0u);
            hal_sd_frame_t f;
            (void)memset(&f, 0, sizeof f);
            bool early_by_lat = true;
            for (uint32_t t = 0u; t < 3000u; t += 50u) {
                sim_advance_us(50u);
                if (read1(&f)) {
                    early_by_lat = early_by_lat && (stamp_err_ns(&f) == -(int64_t)(lat * 1000u));
                }
            }
            CHECK(s_frames >= 28u && s_bad == 0u && s_repeat == 0u && early_by_lat);
            CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() == 0u);
        }
    }
}

/* The second: a CONSTANT completion delay of 40 or 60 us from initialisation passed the 30 us deadline forever —
 * the deadline was measured from an origin the first delayed completion had set — so every block was published 40
 * or 60 us too new. Judged against the SWG start, the ring breaks at the first completion and publishes nothing
 * while the delay lasts (it is no phase loss: never `lost`); once the delay is gone it re-syncs, frames honest. */
TEST(a_constant_completion_delay_is_never_published)
{
    const uint32_t lat_us[2] = {40u, 60u};
    for (uint32_t k = 0u; k < 2u; k++) {
        sim_reset();
        sim_sdadc_irq_latency_ns(lat_us[k] * 1000u);
        start(0u);
        hal_sd_frame_t f;
        (void)memset(&f, 0, sizeof f);
        after_boundary(lat_us[k] + 1u);
        CHECK(sim_sdadc_ring()->broken && (sim_sdadc_ring()->relock || !sim_sdadc_ring()->synced));
        read_for(5000u, 50u, &f);
        CHECK(s_frames == 0u && s_bad == 0u && sim_sdadc_ring()->broken && !hal_sdadc_lost());
        sim_sdadc_irq_latency_ns(1000u);
        read_for(1000u, 50u, &f);
        CHECK(s_frames >= 7u && s_bad == 0u && s_repeat == 0u);
        CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() == 1u);
    }
}

/* The third: after a break the ring re-anchored from the next completion, so a delay rejected once was absorbed into
 * the new origin. Running normally, every completion is delayed 40 us (then 60 us) from some period on: the first
 * delayed one breaks the ring, and so does every later one — the re-sync takes its counts from the clock, never a
 * completion's time, and each delayed completion is judged against the same SWG-start origin. Nothing is published
 * while the delay lasts; frames come back, honest, once it ends. */
TEST(a_rejected_delay_is_never_absorbed_after_a_break)
{
    const uint32_t lat_us[2] = {40u, 60u};
    for (uint32_t k = 0u; k < 2u; k++) {
        sim_reset();
        start(0u);
        hal_sd_frame_t f;
        (void)memset(&f, 0, sizeof f);
        read_for(1000u, 50u, &f);
        const unsigned before = s_frames;
        CHECK(before >= 8u);
        sim_sdadc_irq_latency_ns(lat_us[k] * 1000u);
        read_for(5000u, 50u, &f);
        CHECK(s_frames <= before + 1u && s_bad == 0u && sim_sdadc_ring()->broken && !hal_sdadc_lost());
        sim_sdadc_irq_latency_ns(1000u);
        read_for(1000u, 50u, &f);
        CHECK(s_frames >= before + 8u && s_bad == 0u && s_repeat == 0u);
        CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() == 1u);
    }
}

/* The fourth: a channel whose DMA pauses for one carrier period and resumes on the cadence (no platform flag) is one
 * block behind the clock from then on — which, just after a boundary, looks like a later channel still completing
 * its block. The re-sync waits for that channel's own completion, and there its DMA is behind the block the clock
 * says just ended while the other two are past it: a DMA out of phase with the carrier, `lost` — within two periods
 * of the resume (the wait limit alone would take five). Round 18 did not re-acquire this (unequal
 * positions) but never flagged it either — the ring just stayed down; equal positions alone (after 4 more
 * periods of pause) it would have taken. Frames come back only after the synchronized restart, honest. */
TEST(a_channel_paused_one_period_is_lost_not_taken_for_a_late_one)
{
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        sim_reset();
        start(0u);
        hal_sd_frame_t f;
        (void)memset(&f, 0, sizeof f);
        read_for(1000u, 50u, &f);
        after_boundary(50u);
        (void)read1(&f);
        const unsigned before = s_frames;
        sim_sdadc_freeze((hal_sd_ch_t)ch, true);
        sim_advance_us(100u); /* it misses exactly one completion */
        sim_sdadc_freeze((hal_sd_ch_t)ch, false);
        after_boundary(5u); /* its first completion after the resume (EXC's breaks the ring: a period late) ... */
        after_boundary(5u); /* ... and by its next it is behind the two DMAs past the block: lost */
        CHECK(hal_sdadc_lost() && sim_sdadc_ring()->broken);
        read_for(1000u, 50u, &f);
        CHECK(s_frames <= before + 1u && s_bad == 0u && hal_sdadc_lost() && hal_sdadc_reacquired() == 0u);
        CHECK(restart(0u));
        read_for(1000u, 50u, &f);
        CHECK(s_frames >= before + 8u && s_bad == 0u && s_repeat == 0u);
        CHECK(!sim_sdadc_ring()->broken && !hal_sdadc_lost());
    }
}

/* The other side of that rule: the origin is uncertain by +/- 3 us, so near a block's end the clock cannot tell
 * whether it has passed. With the SWG's start latency at 2 us (inside the declared 3), every channel's completion
 * interrupt is held off 98 us once, so the three run 1 us after the next NOMINAL block end — 1 us before the true
 * one: the ring breaks (late), and the re-sync sees every DMA still on the block the clock may already count as
 * ended. That is an interrupt served late, not a phase loss: the re-sync waits, and the next completions (past the
 * true end) agree with the clock — frames again, honest, one re-acquisition, never `lost` (so no restart). */
TEST(a_late_interrupt_inside_the_origins_uncertainty_is_no_phase_loss)
{
    sim_swg_start_latency_ns(2000u);
    start(0u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    read_for(1000u, 50u, &f);
    const unsigned before = s_frames;
    after_boundary(50u);
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        sim_sdadc_irq_hold_ns((hal_sd_ch_t)ch, 98000u);
    }
    after_boundary(99u); /* the held interrupts have run, 1 us before the true end of the next block */
    CHECK(sim_sdadc_ring()->broken && !sim_sdadc_ring()->synced && !hal_sdadc_lost());
    read_for(1000u, 50u, &f);
    CHECK(s_frames >= before + 8u && s_bad == 0u && s_repeat == 0u);
    CHECK(!sim_sdadc_ring()->broken && !hal_sdadc_lost() && hal_sdadc_reacquired() == 1u);
}

/* No completion precedes its block's end on the cadence: SIN's DMA completing 20 us into a period (forced) breaks
 * the ring at once — round 18 took such a completion as proof that its anchor had been served late and moved the
 * origin back to it (80 us here; one early by less than the deadline re-dated every later frame by as much, never
 * broken). The block completed early is never published;
 * SIN's DMA then runs on in the slot the clock gives it (it skips the boundary it anticipated), so the ring re-syncs
 * from the clock and frames come back honest — one re-acquisition, no phase loss. */
TEST(an_early_completion_breaks_the_ring)
{
    start(0u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    read_for(1000u, 50u, &f);
    after_boundary(20u);
    (void)read1(&f);
    const unsigned before = s_frames;
    const int16_t early_tag = (int16_t)(sim_sdadc_period_index() & 0x3FFFu); /* the block SIN completes early */
    sim_sdadc_complete_now(HAL_SD_SIN);
    CHECK(sim_sdadc_ring()->broken && !hal_sdadc_lost());
    bool early_seen = false;
    for (uint32_t t = 0u; t < 1000u; t += 50u) {
        sim_advance_us(50u);
        if (read1(&f)) {
            early_seen = early_seen || (f.blk[HAL_SD_EXC][0] == early_tag);
        }
    }
    CHECK(!early_seen && s_frames >= before + 7u && s_bad == 0u && s_repeat == 0u);
    CHECK(!sim_sdadc_ring()->broken && !hal_sdadc_lost() && hal_sdadc_reacquired() == 1u);
}

/* The origin is 64-bit and the clock-derived index with it. Anchored 150 us before the 32-bit microsecond wrap with
 * the epoch counter 3 blocks before its own, a break after both wraps re-syncs from the clock across them, every
 * stamp honest. And all three DMAs silent for longer than 2^32 us (72 min: the converters stopped and restarted on
 * the carrier cadence, no completion meanwhile): back after a whole number of laps, every slot holds the period the
 * clock gives it — re-synced, honest; after one period more none does — lost. A 32-bit index would be off by
 * 2^32 mod 100 = 96 us there. */
TEST(the_origin_and_the_clock_index_hold_across_the_32bit_wrap)
{
    sim_reset_at_us(4294967296ull - 150u);
    start(0xFFFFFFFDu);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    f.epoch = 0xFFFFFFFDu;
    read_for(1000u, 50u, &f);
    CHECK(sim_now_ns() > 4294967296000ull && s_frames >= 8u && f.epoch < 0xFFFFFFF0u);
    sim_sdadc_irq_latency_ns(60000u); /* one break after both wraps */
    after_boundary(70u);
    sim_sdadc_irq_latency_ns(1000u);
    read_for(1000u, 50u, &f);
    CHECK(s_bad == 0u && s_repeat == 0u && !sim_sdadc_ring()->broken && hal_sdadc_reacquired() == 1u);
    const uint64_t laps[2] = {42949676u, 42949677u}; /* x 100 us > 2^32 us; the first a multiple of 4 */
    for (uint32_t k = 0u; k < 2u; k++) {
        sim_reset();
        start(0u);
        (void)memset(&f, 0, sizeof f);
        read_for(1000u, 50u, &f);
        after_boundary(50u);
        (void)read1(&f);
        const unsigned before = s_frames;
        for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
            sim_sdadc_freeze((hal_sd_ch_t)ch, true);
        }
        sim_advance_ns(laps[k] * 100000u);
        for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
            sim_sdadc_freeze((hal_sd_ch_t)ch, false);
        }
        read_for(1000u, 50u, &f);
        CHECK(hal_sdadc_lost() == (k == 1u) && s_bad == 0u && s_repeat == 0u);
        CHECK((k == 1u) ? (s_frames == before) : ((s_frames >= before + 7u) && (hal_sdadc_reacquired() == 1u)));
    }
}

/* An unanchored ring counts nothing: completion interrupts before the SWG start (stale ones, or a converter
 * triggered by something else) can neither publish, nor re-sync, nor wear the ring down to `lost` — there is no
 * cadence yet to judge them by (round 18 took the DMA positions at the first completion, whenever it came). */
TEST(nothing_counts_before_the_swg_start)
{
    (void)hal_sdadc_init(10000u, LAT_MAX_US, START_LAT_US); /* the platform half: every DMA armed in slot 0 */
    hal_sd_ring_t r;
    hal_sd_ring_init(&r, PERIOD_US, LAT_MAX_US, 0u);
    for (uint32_t k = 0u; k < 40u; k++) { /* stray completion interrupts on every channel, for 5 ms */
        sim_advance_us(125u);
        hal_sd_ring_complete(&r, (hal_sd_ch_t)(k % (uint32_t)HAL_SD_COUNT), sim_now_ns() / 1000u);
    }
    CHECK(!r.anchored && !r.synced && r.broken && !r.lost && (r.epoch == 0u));
    CHECK((r.done[HAL_SD_EXC] == 0u) && (r.done[HAL_SD_SIN] == 0u) && (r.done[HAL_SD_COS] == 0u));
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    CHECK(!hal_sd_ring_read(&r, &f));
}

/* The anchor refuses an origin too uncertain to judge a completion by: the re-sync window (half a period + twice the
 * uncertainty) must stay inside the period, so an uncertainty of a quarter period (25 us: a 24 us start latency
 * declared) is `lost` from the start — nothing published, a restart needed — and 24 us (23 declared) still runs. */
TEST(the_anchor_refuses_an_origin_too_uncertain)
{
    for (uint32_t k = 0u; k < 2u; k++) {
        sim_reset();
        sim_sdadc_tag(true);
        (void)hal_sdadc_init(10000u, LAT_MAX_US, (k == 0u) ? 24u : 23u);
        (void)hal_swg_start(10000u, 8u);
        hal_sd_frame_t f;
        (void)memset(&f, 0, sizeof f);
        unsigned n = 0u;
        for (uint32_t t = 0u; t < 1000u; t += 50u) {
            sim_advance_us(50u);
            n += hal_sdadc_read_frame(&f) ? 1u : 0u;
        }
        CHECK(hal_sdadc_lost() == (k == 0u) && ((k == 0u) ? (n == 0u) : (n >= 8u)));
    }
}

void suite_sdadc(void)
{
    RUN(every_frame_is_one_epoch_of_all_three_channels);
    RUN(a_frozen_channel_yields_no_frame);
    RUN(a_late_channel_holds_the_frame_back_and_the_stamp_is_the_first);
    RUN(a_completion_between_channel_reads_never_mixes_epochs);
    RUN(held_off_completion_interrupts_never_yield_a_fresh_frame);
    RUN(the_epoch_counter_wraps);
    RUN(interrupts_held_off_within_the_deadline_keep_every_stamp_exact);
    RUN(an_interrupt_held_off_past_the_deadline_is_rejected_then_reacquired);
    RUN(interrupts_held_off_for_a_lap_are_detected_never_fresh);
    RUN(one_channel_lapping_while_the_others_do_not);
    RUN(all_three_stalled_together_are_detected_and_reacquired);
    RUN(the_reader_preempted_across_a_lap_never_returns_the_frame);
    RUN(stamps_are_exact_across_the_microsecond_and_epoch_wraps);
    RUN(lost_samples_keep_the_ring_down_until_a_restart);
    RUN(the_first_completion_is_judged_against_the_swg_start);
    RUN(the_swg_start_latency_stays_inside_the_declared_uncertainty);
    RUN(a_constant_completion_delay_is_never_published);
    RUN(a_rejected_delay_is_never_absorbed_after_a_break);
    RUN(a_channel_paused_one_period_is_lost_not_taken_for_a_late_one);
    RUN(a_late_interrupt_inside_the_origins_uncertainty_is_no_phase_loss);
    RUN(an_early_completion_breaks_the_ring);
    RUN(the_origin_and_the_clock_index_hold_across_the_32bit_wrap);
    RUN(the_anchor_refuses_an_origin_too_uncertain);
    RUN(nothing_counts_before_the_swg_start);
}
