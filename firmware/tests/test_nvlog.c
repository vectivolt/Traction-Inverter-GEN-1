/* test_nvlog.c — A/B records, torn writes, the non-blocking bounded queue, the fault ring. */
#include "nvlog.h"
#include "sim.h"
#include "test.h"

static void drain(void)
{
    for (int k = 0; k < 200 && !nv_idle(); k++) {
        nv_service();
    }
}

TEST(write_read_alternates_slots)
{
    nv_init();
    uint32_t v = 5u;
    CHECK(nv_queue(NV_REC_KEYCYCLE, &v, 4u));
    drain();
    uint32_t r = 0u;
    CHECK(nv_read(NV_REC_KEYCYCLE, &r, 4u) && r == 5u);
    v = 6u;
    (void)nv_queue(NV_REC_KEYCYCLE, &v, 4u);
    drain();
    CHECK(nv_read(NV_REC_KEYCYCLE, &r, 4u) && r == 6u);
    nv_init(); /* reboot: rescans both slots */
    CHECK(nv_read(NV_REC_KEYCYCLE, &r, 4u) && r == 6u);
    CHECK(sim_nvm_writes_done() == 2u);
}

TEST(queue_never_blocks_and_counts_overflow)
{
    nv_init();
    const nv_fault_t e = {.code = 1u};
    const uint32_t before = sim_nvm_writes_done();
    for (unsigned k = 0u; k < NV_QUEUE_DEPTH; k++) {
        CHECK(nv_queue(NV_REC_FAULT, &e, (uint16_t)sizeof e));
    }
    CHECK(sim_nvm_writes_done() == before); /* nothing written by queueing */
    CHECK(!nv_queue(NV_REC_FAULT, &e, (uint16_t)sizeof e));
    CHECK(nv_overflows() == 1u && nv_pending() == NV_QUEUE_DEPTH);
    drain();
    CHECK(nv_idle() && sim_nvm_writes_done() == before + NV_QUEUE_DEPTH);
}

TEST(brownout_tears_write_previous_record_survives)
{
    nv_init();
    nv_desat_t d = {.key_cycle = 7u, .bank = 1u};
    (void)nv_queue(NV_REC_DESAT, &d, (uint16_t)sizeof d);
    drain();
    d.key_cycle = 8u;
    d.bank = 2u;
    sim_nvm_set_write_polls(5u);
    (void)nv_queue(NV_REC_DESAT, &d, (uint16_t)sizeof d);
    nv_service(); /* write started */
    nv_service();
    sim_nvm_power_loss(); /* brown-out mid-write */
    nv_init();           /* reboot */
    nv_desat_t r = {0};
    CHECK(nv_read(NV_REC_DESAT, &r, (uint16_t)sizeof r));
    CHECK(r.key_cycle == 7u && r.bank == 1u); /* the torn slot is rejected by its CRC */
}

TEST(fault_ring_newest_first)
{
    nv_init();
    for (uint16_t k = 1u; k <= 20u; k++) {
        const nv_fault_t e = {.code = k};
        (void)nv_queue(NV_REC_FAULT, &e, (uint16_t)sizeof e);
        drain();
    }
    nv_fault_t r;
    CHECK(nv_read_fault(0u, &r) && r.code == 20u);
    CHECK(nv_read_fault(1u, &r) && r.code == 19u);
    CHECK(nv_read_fault(15u, &r) && r.code == 5u); /* ring of 16 */
    CHECK(!nv_read_fault(16u, &r));
}

void suite_nvlog(void)
{
    RUN(write_read_alternates_slots);
    RUN(queue_never_blocks_and_counts_overflow);
    RUN(brownout_tears_write_previous_record_survives);
    RUN(fault_ring_newest_first);
}
