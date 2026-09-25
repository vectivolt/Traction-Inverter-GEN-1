/* test_sdadc.c — the resolver frame protocol (hal/sdadc.h, sdadc_ring.c) on the simulated per-channel eDMA.
 * Round 16 (A14-R02): every block carries its carrier period in sample 0 (sim_sdadc_tag), so a frame is
 * coherent exactly when its three tags agree. Round 18 (A16-R02): and its stamp must be that carrier period's
 * true start — within the 1 us of the interrupt that anchored the cadence — whatever the completion
 * interrupts' latency. Each test requires, at every read, either one such frame or an explicit "unavailable"
 * (false, the output untouched). */
#include <string.h>

#include "sdadc.h"
#include "sim.h"
#include "test.h"
#include "timer.h"

#define PERIOD_US 100u
#define LAT_MAX_US 30u /* cal_sd_irq_lat_max_us default */

static unsigned s_frames;
static unsigned s_bad;    /* frames whose blocks disagree, or whose stamp is not their period's start */
static unsigned s_repeat; /* an epoch returned twice, or out of order */
static uint32_t s_last_epoch;

static void start(uint32_t count0)
{
    sim_sdadc_count_base(count0);
    sim_sdadc_tag(true);
    (void)hal_sdadc_init(10000u, LAT_MAX_US);
    s_frames = 0u;
    s_bad = 0u;
    s_repeat = 0u;
    s_last_epoch = count0;
}

/* The true start (32-bit us) of the carrier period a block holds: the tag is its period index mod 2^14, and a
 * frame is at most a few periods old, so it is the latest period up to now with that tag. */
static uint32_t true_start_us(int16_t tag)
{
    const uint64_t now_p = sim_sdadc_period_index();
    const uint64_t back = ((now_p & 0x3FFFu) - (uint64_t)(uint16_t)tag) & 0x3FFFu;
    return (uint32_t)((now_p - back) * PERIOD_US);
}

/* One epoch of all three channels, stamped at its carrier period's true start (+ at most 1 us). */
static bool exact(const hal_sd_frame_t *f)
{
    const int16_t tag = f->blk[HAL_SD_EXC][0];
    return (f->blk[HAL_SD_SIN][0] == tag) && (f->blk[HAL_SD_COS][0] == tag) &&
           (ti_age(f->t_us, true_start_us(tag)) <= 1u);
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
    s_bad += exact(f) ? 0u : 1u;
    s_repeat += ((uint32_t)(f->epoch - s_last_epoch) >= 1u) && ((uint32_t)(f->epoch - s_last_epoch) < 0x80000000u) ? 0u : 1u;
    s_last_epoch = f->epoch;
    return true;
}

/* To us microseconds after the next carrier-period boundary. */
static void after_boundary(uint32_t us)
{
    sim_advance_ns((((sim_sdadc_period_index() + 1u) * 100000u) + ((uint64_t)us * 1000u)) - sim_now_ns());
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
    CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() == 0u);
}

/* The reviewer's first case: a frozen EXC or COS buffer read as fresh because SIN's interrupt kept
 * publishing. Now no frame is published without every channel: no frame at all while it is frozen, never a
 * stale block dressed as new, and the ring breaks once a channel is a whole block behind (lost step).
 * Round 18: a channel that runs again in step with the others (here after 20 periods: its slot came round)
 * is re-acquired from the DMA positions — frames again, exact, one re-acquisition; one that resumes out of
 * step (21 periods: a slot apart) never is. On the target a DMA that stops while its converter runs overruns
 * the FIFO: lost samples keep the ring down (lost_samples_keep_the_ring_down_until_init). */
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
            CHECK(sim_sdadc_ring()->broken);
            sim_sdadc_freeze((hal_sd_ch_t)ch, false);
            const unsigned frozen = s_frames;
            read_for(1000u, 50u, &f);
            CHECK(s_bad == 0u && s_repeat == 0u);
            CHECK((k == 0u) ? ((s_frames >= frozen + 5u) && !sim_sdadc_ring()->broken && (hal_sdadc_reacquired() == 1u))
                            : ((s_frames == frozen) && sim_sdadc_ring()->broken && (hal_sdadc_reacquired() == 0u)));
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
    CHECK(read1(&f) && exact(&f));
    CHECK((f.t_us % PERIOD_US) <= 2u); /* block start + the 1 us interrupt latency, not + 31 us */
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
    if (s_hook_mode == 1u) { /* only SIN's DMA moves on, into the next slot */
        sim_sdadc_complete_now(HAL_SD_SIN);
    } else { /* the reader is preempted for s_hook_mode us while every DMA keeps writing: 6 us across a
              * carrier boundary publishes a whole new epoch between the channel copies */
        sim_advance_us(s_hook_mode);
    }
}

/* The reviewer's second case: a completion between the EXC read and the SIN/COS reads gave EXC of one
 * epoch with SIN/COS of the next. Now the copy is checked as a whole: a publication during it (seqlock),
 * a DMA that reached the frame's slot, or a copy longer than a carrier period is "unavailable"; a DMA
 * that only moved on into its next slot leaves the frame intact, and it is returned whole. */
TEST(a_completion_between_channel_reads_never_mixes_epochs)
{
    const uint32_t mode[4] = {6u, 1u, 150u, 350u};
    const uint32_t at_us[4] = {95u, 20u, 20u, 20u}; /* where in the carrier period the read starts */
    const bool expect[4] = {false, true, false, false};
    for (uint32_t m = 0u; m < 4u; m++) {
        sim_reset();
        start(0u);
        hal_sd_frame_t f;
        (void)memset(&f, 0, sizeof f);
        read_for(500u, 50u, &f);
        after_boundary(at_us[m]); /* a new epoch published, not read yet */
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
        CHECK(got ? (exact(&f) && f.epoch == e0) : (memcmp(&before, &f, sizeof f) == 0));
        if (m == 0u) {
            CHECK(read1(&f) && exact(&f) && f.epoch == e0 + 1u); /* the new epoch, whole, next time */
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

/* The epoch counter across 2^32: consecutive, exact, stamped a carrier period apart. (Round 18: the ring
 * takes the DMA positions at its first completion and anchors at the second — one frame fewer in 4 ms.) */
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

/* The anchor itself served late: the ring (re)starts with every interrupt 25 us late, so its origin is 25 us late
 * too (nothing can tell it at the anchor). The first completion served promptly comes before its block's end on
 * that cadence — impossible unless the anchor was late — and moves the origin back: from then on every stamp is
 * exact again, without a break. */
TEST(a_late_anchor_is_moved_back_by_the_first_prompt_completion)
{
    sim_sdadc_irq_latency_ns(25000u);
    start(0u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    for (uint32_t t = 0u; t < 600u; t += 50u) {
        sim_advance_us(50u);
        if (hal_sdadc_read_frame(&f)) {
            CHECK(ti_age(f.t_us, true_start_us(f.blk[HAL_SD_EXC][0])) == 25u); /* the late anchor */
        }
    }
    sim_sdadc_irq_latency_ns(1000u);
    after_boundary(50u);
    (void)hal_sdadc_read_frame(&f); /* taken unchecked: a burst pending across the change moved the origin partly */
    read_for(1000u, 50u, &f);
    CHECK(s_frames >= 9u && s_bad == 0u && s_repeat == 0u);
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
 * their DMAs (the converters stop and restart phase-aligned) — no frame meanwhile; afterwards the first
 * completion is late on the cadence by the stall, the ring re-acquires, and every stamp is the true start
 * (a cadence stamp without the deadline would have dated the returning blocks 5 or 8 periods early). */
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
            CHECK(s_frames >= before + 10u && s_bad == 0u && s_repeat == 0u);
            CHECK(!sim_sdadc_ring()->broken && hal_sdadc_reacquired() == 1u);
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
 * re-acquisition — until hal_sdadc_init() restarts the converters. */
TEST(lost_samples_keep_the_ring_down_until_init)
{
    start(0u);
    hal_sd_frame_t f;
    (void)memset(&f, 0, sizeof f);
    read_for(500u, 50u, &f);
    const unsigned before = s_frames;
    sim_sdadc_overrun();
    read_for(2000u, 50u, &f);
    CHECK(s_frames <= before + 1u && sim_sdadc_ring()->broken && hal_sdadc_reacquired() == 0u);
    start(0u);
    read_for(1000u, 50u, &f);
    CHECK(s_frames >= 5u && s_bad == 0u && !sim_sdadc_ring()->broken);
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
    RUN(a_late_anchor_is_moved_back_by_the_first_prompt_completion);
    RUN(an_interrupt_held_off_past_the_deadline_is_rejected_then_reacquired);
    RUN(interrupts_held_off_for_a_lap_are_detected_never_fresh);
    RUN(one_channel_lapping_while_the_others_do_not);
    RUN(all_three_stalled_together_are_detected_and_reacquired);
    RUN(the_reader_preempted_across_a_lap_never_returns_the_frame);
    RUN(stamps_are_exact_across_the_microsecond_and_epoch_wraps);
    RUN(lost_samples_keep_the_ring_down_until_init);
}
