/* nvlog.c — A/B records, fault ring, bounded write queue. */
#include "nvlog.h"

#include <string.h>

#include "dtc.h"
#include "ti_crc.h"
#include "timer.h"

/* Round 23 (item 9): the slot check is a CRC-32C over the header (magic, type, length, sequence) and the payload. It was
 * a CRC-32 of the IEEE polynomial — the one calib_t (FW-20) and arm_validation_t (FW-24) end in: CRC(X || CRC(X)) is the
 * same for every X, so the linear part of such a slot check cancels on a record's own CRC-32 wherever it sits, and a
 * write torn after its header validated with the stale tail (the record from two writes back, CRC-clean). CRC-32C
 * shares no such relation with CRC-32: a torn record is refused like any other corruption. A slot of an earlier image
 * ("TINV", the CRC-32 check) stays readable, never written: the next write of that record goes to its other slot in
 * the new format — nothing is lost at the update, and a tear from then on is caught. */
#define NV_MAGIC 0x54494E32u    /* "TIN2": the slot check is a CRC-32C */
#define NV_MAGIC_V1 0x54494E56u /* "TINV": an earlier image's slot, its check a CRC-32 — read only */
#define FAULT_SLOT0 NV_FAULT_SLOT0
#define VALID_SLOT0 (FAULT_SLOT0 + NV_FAULT_RING) /* after the fault ring */
#define SLOTS_USED NV_SLOTS_USED
_Static_assert(SLOTS_USED <= HAL_NVM_SLOTS, "the NVM record layout exceeds the slots");

typedef struct {
    uint32_t magic;
    uint16_t type;
    uint16_t len;
    uint32_t seq;
    uint32_t crc;
} nv_hdr_t;

typedef struct {
    uint8_t type;
    uint16_t len;
    uint8_t data[NV_PAYLOAD_MAX];
} nv_job_t;

static nv_job_t s_q[NV_QUEUE_DEPTH];
static uint32_t s_head, s_tail, s_overflow;
static bool s_busy;
static uint16_t s_busy_slot;
static uint32_t s_seq;
static uint8_t s_buf[HAL_NVM_SLOT_SIZE];
/* latest valid seq per slot (0 = empty/invalid) */
static uint32_t s_slot_seq[HAL_NVM_SLOTS];

static uint16_t slot_a(nv_rec_t t)
{
    /* records after the fault ring (VALIDATION, round 23 RUNTIME, ...) take A/B pairs in enum order */
    return (t >= NV_REC_VALIDATION) ? (uint16_t)(VALID_SLOT0 + (2u * ((uint32_t)t - (uint32_t)NV_REC_VALIDATION)))
                                    : (uint16_t)((uint16_t)t * 2u);
}

static uint32_t rec_crc(const nv_hdr_t *h, const uint8_t *payload)
{
    uint8_t tmp[HAL_NVM_SLOT_SIZE];
    nv_hdr_t z = *h;
    z.crc = 0u;
    (void)memcpy(tmp, &z, sizeof z);
    (void)memcpy(&tmp[sizeof z], payload, h->len);
    return (h->magic == NV_MAGIC_V1) ? ti_crc32(tmp, sizeof z + h->len) : ti_crc32c(tmp, sizeof z + h->len);
}

/* Reads a slot; returns its seq if magic, type, length and CRC hold, else 0. */
static uint32_t slot_valid(uint16_t slot, nv_rec_t type, uint8_t *payload, uint16_t len)
{
    uint8_t raw[HAL_NVM_SLOT_SIZE];
    nv_hdr_t h;
    if (!hal_nvm_read(slot, raw, HAL_NVM_SLOT_SIZE)) {
        return 0u;
    }
    (void)memcpy(&h, raw, sizeof h);
    if (((h.magic != NV_MAGIC) && (h.magic != NV_MAGIC_V1)) || (h.type != (uint16_t)type) || (h.len > NV_PAYLOAD_MAX) ||
        (h.seq == 0u) || (h.crc != rec_crc(&h, &raw[sizeof h]))) {
        return 0u;
    }
    if (payload != NULL) {
        (void)memcpy(payload, &raw[sizeof h], (len < h.len) ? len : h.len);
    }
    return h.seq;
}

static nv_rec_t slot_type(uint16_t slot)
{
    if (slot >= VALID_SLOT0) {
        return (nv_rec_t)((uint32_t)NV_REC_VALIDATION + ((uint32_t)(slot - VALID_SLOT0) / 2u));
    }
    return (slot >= FAULT_SLOT0) ? NV_REC_FAULT : (nv_rec_t)(slot / 2u);
}

void nv_init(void)
{
    s_head = 0u;
    s_tail = 0u;
    s_overflow = 0u;
    s_busy = false;
    s_seq = 0u;
    for (uint16_t s = 0u; s < (uint16_t)SLOTS_USED; s++) {
        s_slot_seq[s] = slot_valid(s, slot_type(s), NULL, 0u);
        s_seq = (s_slot_seq[s] > s_seq) ? s_slot_seq[s] : s_seq;
    }
}

bool nv_queue(nv_rec_t type, const void *payload, uint16_t len)
{
    bool ok = false;
    if ((type >= NV_REC_COUNT) || (len > NV_PAYLOAD_MAX)) {
        return false;
    }
    hal_crit_enter();
    if ((s_head - s_tail) < NV_QUEUE_DEPTH) {
        nv_job_t *j = &s_q[s_head % NV_QUEUE_DEPTH];
        j->type = (uint8_t)type;
        j->len = len;
        (void)memcpy(j->data, payload, len);
        s_head++;
        ok = true;
    } else {
        s_overflow++;
    }
    hal_crit_exit();
    return ok;
}

static uint16_t target_slot(nv_rec_t type)
{
    if (type == NV_REC_FAULT) {
        uint16_t best = FAULT_SLOT0;
        for (uint16_t s = FAULT_SLOT0; s < (uint16_t)(FAULT_SLOT0 + NV_FAULT_RING); s++) {
            best = (s_slot_seq[s] < s_slot_seq[best]) ? s : best;
        }
        return best;
    }
    const uint16_t a = slot_a(type);
    return (s_slot_seq[a] <= s_slot_seq[a + 1u]) ? a : (uint16_t)(a + 1u); /* overwrite the older */
}

static void start_next(void)
{
    if (s_head == s_tail) {
        return;
    }
    const nv_job_t *j = &s_q[s_tail % NV_QUEUE_DEPTH];
    nv_hdr_t h = {.magic = NV_MAGIC, .type = j->type, .len = j->len, .seq = s_seq + 1u, .crc = 0u};
    h.crc = rec_crc(&h, j->data);
    (void)memset(s_buf, 0xFF, sizeof s_buf);
    (void)memcpy(s_buf, &h, sizeof h);
    (void)memcpy(&s_buf[sizeof h], j->data, j->len);
    const uint16_t slot = target_slot((nv_rec_t)j->type);
    if (hal_nvm_write_start(slot, s_buf, (uint32_t)sizeof h + j->len)) {
        s_busy = true;
        s_busy_slot = slot;
        s_seq = h.seq;
        s_slot_seq[slot] = 0u; /* torn until proven otherwise */
    }
}

void nv_service(void)
{
    if (!s_busy) {
        start_next();
        return;
    }
    const hal_nvm_status_t st = hal_nvm_poll();
    if (st == HAL_NVM_BUSY) {
        return;
    }
    s_busy = false;
    if (st == HAL_NVM_DONE_OK) {
        s_slot_seq[s_busy_slot] = s_seq;
    } else {
        dtc_set(DTC_NVM, hal_time_ms());
    }
    s_tail++; /* the job is consumed either way: no retry loop in front of anything */
}

bool nv_read(nv_rec_t type, void *payload, uint16_t len)
{
    if ((type >= NV_REC_COUNT) || (type == NV_REC_FAULT)) {
        return false;
    }
    uint8_t a[NV_PAYLOAD_MAX];
    uint8_t b[NV_PAYLOAD_MAX];
    const uint16_t sa = slot_a(type);
    const uint32_t qa = slot_valid(sa, type, a, NV_PAYLOAD_MAX);
    const uint32_t qb = slot_valid((uint16_t)(sa + 1u), type, b, NV_PAYLOAD_MAX);
    if ((qa == 0u) && (qb == 0u)) {
        return false;
    }
    (void)memcpy(payload, (qa > qb) ? a : b, len);
    return true;
}

bool nv_read_fault(uint32_t age, nv_fault_t *out)
{
    /* order the ring by seq, newest first; the ring is 16 deep so this stays small */
    uint32_t prev = UINT32_MAX;
    for (uint32_t k = 0u; k <= age; k++) {
        uint32_t best_seq = 0u;
        uint16_t best = 0xFFFFu;
        for (uint16_t s = FAULT_SLOT0; s < (uint16_t)(FAULT_SLOT0 + NV_FAULT_RING); s++) {
            if ((s_slot_seq[s] > best_seq) && (s_slot_seq[s] < prev)) {
                best_seq = s_slot_seq[s];
                best = s;
            }
        }
        if (best == 0xFFFFu) {
            return false;
        }
        if (k == age) {
            return slot_valid(best, NV_REC_FAULT, (uint8_t *)out, (uint16_t)sizeof *out) != 0u;
        }
        prev = best_seq;
    }
    return false;
}

uint32_t nv_overflows(void) { return s_overflow; }
uint32_t nv_pending(void) { return s_head - s_tail; }
bool nv_idle(void) { return !s_busy && (s_head == s_tail); }
