/* nvlog.h — NVM records behind an asynchronous, bounded queue (FW-15, round 12 R2-F19).
 * nv_queue() copies the record into RAM and returns at once (callable from the fault ISR); the
 * background loop (nv_service) writes it. Single records live in two slots (A/B) with a sequence
 * number and CRC-32: a write torn by a brown-out leaves the previous record readable. Fault events
 * go to a 16-slot ring. A full queue drops the new record and counts it (the FW-15 bank is also in
 * retained RAM, safety/fault_mgr.c). */
#ifndef NVLOG_H
#define NVLOG_H

#include "nvm.h"
#include "ti_types.h"

/* Slots: CALIB 0/1, SELFTEST 2/3, KEYCYCLE 4/5, DESAT 6/7, DTC 8/9, FAULT ring 10..25,
 * VALIDATION 26/27 (the EOL/HIL arming-evidence record, safety/arm_evidence.h). Records after the fault ring take
 * A/B pairs in enum order: round 23, RUNTIME 28/29 (the FW-43 run-time statistics with the FW-44 tracked current
 * offsets, nvm/runstats.h), BOOT 30/31 (the FW-38 boot record, boot/boot.h). Round 23 (item 10): the map has
 * HAL_NVM_SLOTS = 64 slots, 32..63 free (16 more A/B records); the slots of the records above did not move.
 * Round 23 (item 9): the slot check is a CRC-32C over the header and the payload (nvlog.c), a record's own CRC-32
 * notwithstanding. */
typedef enum {
    NV_REC_CALIB = 0,
    NV_REC_SELFTEST,
    NV_REC_KEYCYCLE,
    NV_REC_DESAT,
    NV_REC_DTC,       /* the service lock (nv_service_t) */
    NV_REC_FAULT,
    NV_REC_VALIDATION,
    NV_REC_RUNTIME,   /* round 23: FW-43 statistics + FW-44 tracked offsets (nv_runtime_t, nvm/runstats.h) */
    NV_REC_BOOT,      /* round 23: FW-38 boot record — anti-rollback counter, last known good (boot/boot.h) */
    NV_REC_COUNT
} nv_rec_t;

#define NV_HDR_SIZE 16u
#define NV_FAULT_SLOT0 10u
/* the slots the records take: the fault ring's, then A/B pairs from NV_REC_VALIDATION on */
#define NV_SLOTS_USED (NV_FAULT_SLOT0 + NV_FAULT_RING + (2u * ((uint32_t)NV_REC_COUNT - (uint32_t)NV_REC_VALIDATION)))
#define NV_PAYLOAD_MAX (HAL_NVM_SLOT_SIZE - NV_HDR_SIZE)
#define NV_QUEUE_DEPTH 8u
#define NV_FAULT_RING 16u

/* FW-16 stored pass, FW-15 DESAT record, key-cycle counter, fault event */
typedef struct {
    uint32_t key_cycle;
    uint8_t passed;
    uint8_t failed_step;
    uint16_t pad;
} nv_selftest_t;

typedef struct {
    uint32_t key_cycle;
    uint8_t bank; /* 1 = HS, 2 = LS */
    uint8_t retry_used;
    uint16_t pad;
    float speed_rpm;
} nv_desat_t;

/* "Service required, do not re-energise" (a stuck-on QDIS): survives key cycles until the UDS routine
 * (FW-32) rewrites it as CLEARED, with the key cycle of the clear; read at every boot. */
#define NV_SERVICE_MAGIC 0x53455256u   /* "SERV": locked */
#define NV_SERVICE_CLEARED 0x434C5244u /* "CLRD": cleared by the service routine (the record of the clear) */
typedef struct {
    uint32_t magic;
    uint16_t dtc;
    uint16_t pad;
    uint32_t key_cycle;
} nv_service_t;

/* FW-40 (round 23): the operating context a fault event records beside its electrical one — the UDS 0x19 04 snapshot
 * (comms/uds_diag.c). Appended to nv_fault_t: a record written before FW-40 reads ext = 0 and zeros here. */
typedef struct {
    uint8_t ext;     /* 1: the fields below were recorded */
    uint8_t state;   /* sm_state_t at the event */
    uint8_t t_valid; /* b0 a module NTC valid (t_mod_c), b1 MT1, b2 MT2 */
    uint8_t pad;
    float t_cmd_nm;  /* torque command */
    float t_act_nm;  /* torque applied: what the issued current references represent (FW-37) */
    float t_mod_c;   /* hottest valid module NTC */
    float t_mt1_c;   /* motor temperature sensors */
    float t_mt2_c;
} nv_fault_ctx_t;

typedef struct {
    uint32_t key_cycle;
    uint32_t t_ms;
    uint16_t code;
    uint8_t row;
    uint8_t action;
    float speed_rpm;
    float id_a;
    float iq_a;
    float vdc_v;
    nv_fault_ctx_t op; /* FW-40 */
} nv_fault_t;

void nv_init(void);
bool nv_queue(nv_rec_t type, const void *payload, uint16_t len);
void nv_service(void);
bool nv_read(nv_rec_t type, void *payload, uint16_t len);
bool nv_read_fault(uint32_t age, nv_fault_t *out); /* 0 = newest */
uint32_t nv_overflows(void);
uint32_t nv_pending(void);
bool nv_idle(void);

#endif /* NVLOG_H */
