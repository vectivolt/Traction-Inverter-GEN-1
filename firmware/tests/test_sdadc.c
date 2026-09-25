/* test_sdadc.c — round 16 (A14-R02): the resolver frame protocol (hal/sdadc.h, sdadc_ring.c) on the
 * simulated per-channel eDMA. Every block carries its carrier period in sample 0 (sim_sdadc_tag), so a
 * frame is coherent exactly when its three tags agree with each other and with its time stamp. Each
 * test requires, at every read, either one coherent frame or an explicit "unavailable" (false, the
 * output untouched). */
#include <string.h>

#include "sdadc.h"
#include "sim.h"
#include "test.h"
#include "timer.h"

#define PERIOD_US 100u

static unsigned s_frames;
static unsigned s_bad;    /* frames whose blocks or stamp disagree */
static unsigned s_repeat; /* an epoch returned twice, or out of order */
static uint32_t s_last_epoch;

static void start(uint32_t count0)
{
    sim_sdadc_count_base(count0);
    sim_sdadc_tag(true);
    (void)hal_sdadc_init(10000u);
    s_frames = 0u;
    s_bad = 0u;
    s_repeat = 0u;
    s_last_epoch = count0;
}

static bool coherent(const hal_sd_frame_t *f)
{
    const int16_t tag = (int16_t)((f->t_us / PERIOD_US) & 0x3FFFu); /* the stamp's carrier period */
    return (f->blk[HAL_SD_EXC][0] == tag) && (f->blk[HAL_SD_SIN][0] == tag) && (f->blk[HAL_SD_COS][0] == tag);
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
    s_bad += coherent(f) ? 0u : 1u;
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
    CHECK(!sim_sdadc_ring()->broken);
}

/* The reviewer's first case: a frozen EXC or COS buffer read as fresh because SIN's interrupt kept
 * publishing. Now no frame is published without every channel: no frame at all, never a stale block
 * dressed as new, and the ring stops for good once a channel is a whole block behind (lost step). */
TEST(a_frozen_channel_yields_no_frame)
{
    for (uint32_t ch = 0u; ch < (uint32_t)HAL_SD_COUNT; ch++) {
        sim_reset();
        start(0u);
        hal_sd_frame_t f;
        (void)memset(&f, 0, sizeof f);
        read_for(1000u, 50u, &f);
        const unsigned before = s_frames;
        CHECK(before >= 7u);
        sim_sdadc_freeze((hal_sd_ch_t)ch, true);
        read_for(2000u, 50u, &f);
        CHECK(s_frames <= before + 1u); /* at most the epoch completed as it froze */
        CHECK(sim_sdadc_ring()->broken);
        sim_sdadc_freeze((hal_sd_ch_t)ch, false); /* it resumes one or more blocks out of step */
        read_for(1000u, 50u, &f);
        CHECK(s_frames <= before + 1u && s_bad == 0u && s_repeat == 0u);
    }
}

/* A channel whose DMA completes 30 us late: until it has, its epoch is not published (the read says
 * "nothing new"); then the frame is whole, and its stamp is the FIRST completion, not the late one. */
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
    CHECK(read1(&f) && coherent(&f));
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
        CHECK(got ? (coherent(&f) && f.epoch == e0) : (memcmp(&before, &f, sizeof f) == 0));
        if (m == 0u) {
            CHECK(read1(&f) && coherent(&f) && f.epoch == e0 + 1u); /* the new epoch, whole, next time */
        }
    }
}

/* Interrupts held off while the DMA runs on: the ring still names an old epoch whose slot the DMA is
 * about to reach (the start check refuses it), and the interrupt that finally runs finds its DMA two
 * blocks on — the time of the skipped block is unknown, so the ring stops publishing (fail closed). */
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
    read_for(1000u, 50u, &f);
    CHECK(s_frames == before && sim_sdadc_ring()->broken && s_bad == 0u);
}

/* The epoch counter across 2^32: consecutive, coherent, stamped a carrier period apart. */
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
    CHECK(crossed && s_frames >= 38u && s_bad == 0u && s_repeat == 0u && !sim_sdadc_ring()->broken);
}

void suite_sdadc(void)
{
    RUN(every_frame_is_one_epoch_of_all_three_channels);
    RUN(a_frozen_channel_yields_no_frame);
    RUN(a_late_channel_holds_the_frame_back_and_the_stamp_is_the_first);
    RUN(a_completion_between_channel_reads_never_mixes_epochs);
    RUN(held_off_completion_interrupts_never_yield_a_fresh_frame);
    RUN(the_epoch_counter_wraps);
}
