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

typedef enum { NV_REC_CALIB = 0, NV_REC_SELFTEST, NV_REC_KEYCYCLE, NV_REC_DESAT, NV_REC_DTC, NV_REC_FAULT, NV_REC_COUNT } nv_rec_t;

#define NV_HDR_SIZE 16u
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
