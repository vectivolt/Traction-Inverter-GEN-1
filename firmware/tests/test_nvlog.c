/* test_nvlog.c — A/B records, torn writes, the non-blocking bounded queue, the fault ring; round 23: a torn write of a
 * record that ends in its own CRC-32 (the slot check's residue blindness), the legacy slot format, the slot map. */
#include <string.h>

#include "calib.h"
#include "nvlog.h"
#include "sim.h"
#include "test.h"
#include "ti_crc.h"

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

/* ---------------- round 23 (item 9): the slot check's residue blindness ---------------- */

/* Write rec into its next slot and tear the write after `bytes` bytes of the slot image (header included). */
static void torn_write(nv_rec_t t, const void *rec, uint16_t len, uint32_t bytes)
{
    sim_nvm_set_write_polls(5u);
    (void)nv_queue(t, rec, len);
    nv_service(); /* started */
    nv_service();
    sim_nvm_power_loss_bytes(bytes);
    sim_nvm_set_write_polls(3u);
}

/* The FW-38 finding, reproduced on the FW-20 record: v1 in slot A, v2 in slot B, then v3 — v1 with its last table entry
 * changed — into slot A, torn half way. The slot keeps v3's header and v1's tail; v3 and v1 agree before the change, so
 * the slot holds v1 byte for byte under v3's header. With a slot CRC-32 of the record's own polynomial the tail's
 * own CRC-32 cancels in the slot's linear part (CRC(X || CRC(X)) is the same for every X) and the slot validated:
 * the newest, CRC-clean record read back was v1, two writes back. The slot check now differs from the record's
 * CRC (CRC-32C): the torn slot is refused and v2 — the previous record — is read. */
TEST(a_torn_calib_write_never_resurrects_the_record_two_writes_back)
{
    const ti_params_t *p = ti_params_get(TI_SKU_8XX_SIC);
    const uint8_t sn[8] = {'T', 'I', '-', '0', '0', '0', '0', '1'};
    calib_t v1;
    calib_nominal(&v1, p, sn);
    v1.motor_id = 7u;
    calib_seal(&v1);
    calib_t v2 = v1;
    v2.motor.psi_wb = 0.14f;
    calib_seal(&v2);
    calib_t v3 = v1;
    v3.mtpa.iq_a[MTPA_LUT_MAX - 1u] = 1.0f; /* the last field before the record's CRC-32: in the torn-off half */
    calib_seal(&v3);
    nv_init();
    (void)nv_queue(NV_REC_CALIB, &v1, (uint16_t)sizeof v1);
    drain();
    (void)nv_queue(NV_REC_CALIB, &v2, (uint16_t)sizeof v2);
    drain();
    torn_write(NV_REC_CALIB, &v3, (uint16_t)sizeof v3, (NV_HDR_SIZE + (uint32_t)sizeof v3) / 2u);
    nv_init(); /* the next power-up */
    calib_t r;
    (void)memset(&r, 0, sizeof r);
    CHECK(nv_read(NV_REC_CALIB, &r, (uint16_t)sizeof r));
    CHECK(memcmp(&r, &v1, sizeof r) != 0); /* never the record from two writes back */
    CHECK(memcmp(&r, &v2, sizeof r) == 0 && calib_check(&r, p, sn) == 0u);
}

/* Any record that ends in its own CRC-32 (calib_t, arm_validation_t), torn anywhere after the slot header: 3000 random
 * record pairs (the new one differs from the one in the slot from a random byte on) and tear points. A torn slot may be
 * read only when it holds the new record whole (the tear fell after its last differing byte); otherwise the previous
 * record is read — never a mix of the two, never the stale one. */
TEST(a_torn_write_of_a_record_ending_in_its_own_crc_never_reads_back_mixed)
{
    uint32_t lcg = 0x1234567u;
    unsigned mixed = 0u;
    unsigned whole = 0u;
    unsigned previous = 0u;
    for (unsigned it = 0u; it < 3000u; it++) {
        uint8_t a[64];
        uint8_t b[64];
        uint8_t c[64];
        for (unsigned k = 0u; k < 60u; k++) {
            lcg = (lcg * 1664525u) + 1013904223u;
            a[k] = (uint8_t)(lcg >> 24);
            b[k] = (uint8_t)(lcg >> 16);
        }
        lcg = (lcg * 1664525u) + 1013904223u;
        const uint32_t from = (lcg >> 8) % 60u; /* c: a with its bytes from `from` on changed */
        (void)memcpy(c, a, 60u);
        for (unsigned k = from; k < 60u; k++) {
            c[k] = (uint8_t)(a[k] ^ (uint8_t)(1u + ((lcg >> (k % 24u)) & 0x7Fu)));
        }
        uint32_t x = ti_crc32(a, 60u);
        (void)memcpy(&a[60], &x, 4u);
        x = ti_crc32(b, 60u);
        (void)memcpy(&b[60], &x, 4u);
        x = ti_crc32(c, 60u);
        (void)memcpy(&c[60], &x, 4u);
        sim_nvm_wipe();
        nv_init();
        (void)nv_queue(NV_REC_VALIDATION, a, 64u); /* slot A */
        drain();
        (void)nv_queue(NV_REC_VALIDATION, b, 64u); /* slot B */
        drain();
        lcg = (lcg * 1664525u) + 1013904223u;
        torn_write(NV_REC_VALIDATION, c, 64u, NV_HDR_SIZE + ((lcg >> 8) % 64u)); /* over A */
        nv_init();
        uint8_t r[64];
        (void)memset(r, 0, sizeof r);
        (void)nv_read(NV_REC_VALIDATION, r, 64u);
        if (memcmp(r, c, 64u) == 0) {
            whole++;
        } else if (memcmp(r, b, 64u) == 0) {
            previous++;
        } else {
            mixed++;
        }
    }
    CHECK(mixed == 0u && (whole + previous) == 3000u && previous > 1000u);
}

/* The CRC-32C of the slot check: the check value of "123456789" (CRC-32/ISCSI 0xE3069283), and it is not the CRC-32. */
TEST(slot_check_is_a_crc32c)
{
    const uint8_t s[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(ti_crc32c(s, 9u) == 0xE3069283u && ti_crc32(s, 9u) == 0xCBF43926u);
}

/* A slot written by an earlier image (magic "TINV", the slot check a CRC-32) stays readable — nothing is lost at the
 * update; the next write of that record goes to the other slot in the new format, and a write torn over the legacy
 * slot later is refused: the previous record is read. */
TEST(legacy_slots_stay_readable_and_are_replaced_in_the_new_format)
{
    const uint32_t kc = 41u;
    uint8_t img[NV_HDR_SIZE + 4u];
    const uint32_t magic = 0x54494E56u; /* "TINV" */
    const uint16_t type = (uint16_t)NV_REC_KEYCYCLE;
    const uint16_t len = 4u;
    const uint32_t seq = 9u;
    const uint32_t zero = 0u;
    (void)memcpy(&img[0], &magic, 4u);
    (void)memcpy(&img[4], &type, 2u);
    (void)memcpy(&img[6], &len, 2u);
    (void)memcpy(&img[8], &seq, 4u);
    (void)memcpy(&img[12], &zero, 4u);
    (void)memcpy(&img[16], &kc, 4u);
    const uint32_t crc = ti_crc32(img, sizeof img); /* the legacy slot check, crc field 0 */
    (void)memcpy(&img[12], &crc, 4u);
    CHECK(hal_nvm_write_start(4u, img, sizeof img)); /* KEYCYCLE slot A */
    for (int k = 0; (k < 10) && (hal_nvm_poll() != HAL_NVM_DONE_OK); k++) {
    }
    nv_init();
    uint32_t r = 0u;
    CHECK(nv_read(NV_REC_KEYCYCLE, &r, 4u) && r == 41u);
    uint32_t v = 42u;
    (void)nv_queue(NV_REC_KEYCYCLE, &v, 4u); /* slot B, the new format */
    drain();
    nv_init();
    CHECK(nv_read(NV_REC_KEYCYCLE, &r, 4u) && r == 42u);
    v = 0x12345643u; /* its high half differs from the legacy slot's: the tear leaves a mix */
    torn_write(NV_REC_KEYCYCLE, &v, 4u, NV_HDR_SIZE + 2u); /* over the legacy slot A, torn */
    nv_init();
    CHECK(nv_read(NV_REC_KEYCYCLE, &r, 4u) && r == 42u);
}

/* ---------------- round 23 (item 10): the slot map ---------------- */

/* Every record type in its own slots (a distinct value written to each reads back from each after a power-up), the
 * map inside the slots, and room for 16 more A/B records: the 32-slot map was full (FW-38's boot record took 30/31). */
TEST(every_record_has_its_own_slots_and_the_map_has_room)
{
    CHECK(NV_SLOTS_USED <= HAL_NVM_SLOTS && (HAL_NVM_SLOTS - NV_SLOTS_USED) >= 32u);
    nv_init();
    for (uint32_t t = 0u; t < (uint32_t)NV_REC_COUNT; t++) {
        const uint32_t v = 0xA5000000u | t;
        if (t != (uint32_t)NV_REC_FAULT) {
            (void)nv_queue((nv_rec_t)t, &v, 4u);
            drain();
            (void)nv_queue((nv_rec_t)t, &v, 4u); /* both slots */
            drain();
        }
    }
    nv_init();
    for (uint32_t t = 0u; t < (uint32_t)NV_REC_COUNT; t++) {
        uint32_t r = 0u;
        CHECK((t == (uint32_t)NV_REC_FAULT) || (nv_read((nv_rec_t)t, &r, 4u) && r == (0xA5000000u | t)));
    }
}

void suite_nvlog(void)
{
    RUN(write_read_alternates_slots);
    RUN(queue_never_blocks_and_counts_overflow);
    RUN(brownout_tears_write_previous_record_survives);
    RUN(fault_ring_newest_first);
    RUN(a_torn_calib_write_never_resurrects_the_record_two_writes_back);
    RUN(a_torn_write_of_a_record_ending_in_its_own_crc_never_reads_back_mixed);
    RUN(slot_check_is_a_crc32c);
    RUN(legacy_slots_stay_readable_and_are_replaced_in_the_new_format);
    RUN(every_record_has_its_own_slots_and_the_map_has_room);
}
