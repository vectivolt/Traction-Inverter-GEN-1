/* uds.c — SecurityAccess and the service-lock routine (FW-32). */
#include "uds.h"

#include <string.h>

#include "uds_capture.h"
#include "uds_diag.h"
#include "uds_update.h"

#define SID_SA 0x27u
#define SID_RC 0x31u
#define SA_SEED 0x01u
#define SA_KEY 0x02u
#define RC_START 0x01u

void uds_init(uds_t *u, uds_key_fn_t key_fn, uds_routine_fn_t clear_fn, void *ctx)
{
    *u = (uds_t){0};
    u->key_fn = key_fn;
    u->clear_fn = clear_fn;
    u->ctx = ctx;
}

static bool reply(hal_can_frame_t *rsp, const uint8_t *pl, uint8_t n)
{
    *rsp = (hal_can_frame_t){.id = UDS_ID_RSP, .len = 8u};
    (void)memset(rsp->data, 0xAA, 8u);
    rsp->data[0] = n;
    (void)memcpy(&rsp->data[1], pl, n);
    return true;
}

static bool nrc(hal_can_frame_t *rsp, uint8_t sid, uint8_t code)
{
    const uint8_t pl[3] = {0x7Fu, sid, code};
    return reply(rsp, pl, 3u);
}

static bool security_access(uds_t *u, const uint8_t *m, uint8_t n, uint32_t seed, hal_can_frame_t *rsp)
{
    if (m[1] == SA_SEED) {
        if (n != 2u) {
            return nrc(rsp, SID_SA, UDS_NRC_LENGTH);
        }
        if (u->key_fn == NULL) {
            return nrc(rsp, SID_SA, UDS_NRC_CONDITIONS); /* no key configured in this build: fail closed */
        }
        if (u->bad_keys >= UDS_BAD_KEY_LIMIT) {
            return nrc(rsp, SID_SA, UDS_NRC_ATTEMPTS);
        }
        uint8_t pl[2u + UDS_SA_LEN] = {SID_SA + 0x40u, SA_SEED, 0u, 0u, 0u, 0u};
        if (!u->unlocked) { /* unlocked: the zero seed */
            seed = (seed != 0u) ? seed : 1u;
            for (uint32_t i = 0u; i < UDS_SA_LEN; i++) {
                u->seed[i] = (uint8_t)(seed >> (8u * i));
                pl[2u + i] = u->seed[i];
            }
            u->seed_out = true;
        }
        return reply(rsp, pl, (uint8_t)sizeof pl);
    }
    if (m[1] == SA_KEY) {
        if (n != (2u + UDS_SA_LEN)) {
            return nrc(rsp, SID_SA, UDS_NRC_LENGTH);
        }
        if (u->bad_keys >= UDS_BAD_KEY_LIMIT) {
            return nrc(rsp, SID_SA, UDS_NRC_ATTEMPTS);
        }
        if (!u->seed_out) {
            return nrc(rsp, SID_SA, UDS_NRC_SEQUENCE);
        }
        u->seed_out = false; /* one key per seed */
        uint8_t k[UDS_SA_LEN] = {0u};
        uint8_t diff = ((u->key_fn != NULL) && u->key_fn(u->seed, k)) ? 0u : 1u;
        for (uint32_t i = 0u; i < UDS_SA_LEN; i++) { /* every byte compared: no early exit */
            diff = (uint8_t)(diff | (uint8_t)(k[i] ^ m[2u + i]));
        }
        if (diff != 0u) {
            u->bad_keys++;
            return nrc(rsp, SID_SA, (u->bad_keys >= UDS_BAD_KEY_LIMIT) ? UDS_NRC_ATTEMPTS : UDS_NRC_INVALID_KEY);
        }
        u->unlocked = true;
        const uint8_t pl[2] = {SID_SA + 0x40u, SA_KEY};
        return reply(rsp, pl, 2u);
    }
    return nrc(rsp, SID_SA, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
}

static bool routine_control(uds_t *u, const uint8_t *m, uint8_t n, hal_can_frame_t *rsp)
{
    if (n < 4u) {
        return nrc(rsp, SID_RC, UDS_NRC_LENGTH);
    }
    if (m[1] != RC_START) {
        return nrc(rsp, SID_RC, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }
    if ((((uint32_t)m[2] << 8) | m[3]) != UDS_RID_CLEAR_SERVICE_LOCK) {
        return nrc(rsp, SID_RC, UDS_NRC_OUT_OF_RANGE);
    }
    if (n != 4u) {
        return nrc(rsp, SID_RC, UDS_NRC_LENGTH);
    }
    if (!u->unlocked) {
        return nrc(rsp, SID_RC, UDS_NRC_SECURITY_DENIED);
    }
    const uint8_t code = (u->clear_fn != NULL) ? u->clear_fn(u->ctx) : UDS_NRC_CONDITIONS;
    if (code != 0u) {
        return nrc(rsp, SID_RC, code);
    }
    u->unlocked = false; /* one run per unlock */
    const uint8_t pl[4] = {SID_RC + 0x40u, RC_START, m[2], m[3]};
    return reply(rsp, pl, 4u);
}

bool uds_handle(uds_t *u, const hal_can_frame_t *rq, uint32_t seed, hal_can_frame_t *rsp)
{
    if (uds_capture_handle(rq, rsp)) {
        return true; /* FW-41: the waveform capture's DIDs and routines (src/diag/uds_capture.h) */
    }
    if (uds_diag_rx(u, rq)) {
        return false; /* FW-40: 0x14/0x19/0x22/0x2A and their flow control; the response goes out from uds_diag_tick */
    }
    if ((rq->id != UDS_ID_REQ) || (rq->len < 2u) || (rq->len > HAL_CAN_MAX_LEN) || ((rq->data[0] & 0xF0u) != 0u)) {
        return false; /* not ours, or not a single frame: no response */
    }
    const uint8_t *m;
    uint32_t n;
    if (rq->data[0] != 0u) { /* the classic single frame: 1..7 bytes */
        n = rq->data[0];
        m = &rq->data[1];
        if ((n > 7u) || (n > (uint32_t)(rq->len - 1u))) {
            return false;
        }
    } else { /* the CAN-FD escape single frame (ISO 15765-2: SF_DL in byte 1, 8..62 bytes) — round 23, second pass: the
              * programming services' short blocks (an image whose last block is 8..62 bytes) arrive this way; before,
              * only the FW-40 services (uds_diag_rx above) parsed it and every other service dropped the frame */
        if (rq->len <= 8u) {
            return false;
        }
        n = rq->data[1];
        m = &rq->data[2];
        if ((n < 1u) || (n > (uint32_t)(rq->len - 2u))) {
            return false;
        }
    }
    if (upd_uds_claims(m, n)) {
        return upd_uds(u, m, n, rsp); /* FW-38: the programming services (src/boot/uds_update.h) */
    }
    if (m[0] == SID_SA) {
        return (n < 2u) ? nrc(rsp, SID_SA, UDS_NRC_LENGTH) : security_access(u, m, n, seed, rsp);
    }
    if (m[0] == SID_RC) {
        return routine_control(u, m, n, rsp);
    }
    return nrc(rsp, m[0], UDS_NRC_SERVICE_NOT_SUPPORTED);
}
