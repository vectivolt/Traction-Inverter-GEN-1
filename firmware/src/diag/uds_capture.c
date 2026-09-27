/* uds_capture.c — FW-41: the capture's DIDs and routines (uds_capture.h). Runs in the 1 ms task (app.c: diag). */
#include "uds_capture.h"

#include <string.h>

#include "capture.h"
#include "uds.h"

#define SID_RDBI 0x22u
#define SID_WDBI 0x2Eu
#define SID_RC 0x31u
#define RC_START 0x01u
#define NRC_BUSY 0x21u /* busyRepeatRequest */
#define SF_MAX 62u     /* CAN-FD single frame: 64 bytes - the escape PCI */

static uint32_t s_cursor;
static uint32_t s_cursor_seq; /* the frozen capture the cursor belongs to (cap_status_t.seq) */

/* A single frame to UDS_ID_REQ, classic or CAN-FD escape format: its payload and length. */
static bool sf_parse(const hal_can_frame_t *rq, const uint8_t **m, uint8_t *n)
{
    if ((rq->id != UDS_ID_REQ) || (rq->len < 2u) || (rq->len > HAL_CAN_MAX_LEN) || ((rq->data[0] & 0xF0u) != 0u)) {
        return false;
    }
    if (rq->data[0] != 0u) {
        *n = rq->data[0];
        *m = &rq->data[1];
        return (*n <= 7u) && (*n <= (rq->len - 1u));
    }
    *n = rq->data[1];
    *m = &rq->data[2];
    return (rq->len > 8u) && (*n >= 1u) && (*n <= (rq->len - 2u));
}

static bool sf_reply(hal_can_frame_t *rsp, const uint8_t *pl, uint32_t n)
{
    static const uint8_t FD_LEN[] = {12u, 16u, 20u, 24u, 32u, 48u, 64u};
    *rsp = (hal_can_frame_t){.id = UDS_ID_RSP, .len = 8u};
    (void)memset(rsp->data, 0xAA, HAL_CAN_MAX_LEN);
    if (n <= 7u) {
        rsp->data[0] = (uint8_t)n;
        (void)memcpy(&rsp->data[1], pl, n);
        return true;
    }
    if (n > SF_MAX) {
        return false; /* never: a block response is 62 bytes at most */
    }
    uint32_t k = 0u;
    while ((k < (sizeof FD_LEN - 1u)) && (FD_LEN[k] < (n + 2u))) { /* at most 6 steps */
        k++;
    }
    rsp->len = FD_LEN[k];
    rsp->data[0] = 0u;
    rsp->data[1] = (uint8_t)n;
    (void)memcpy(&rsp->data[2], pl, n);
    return true;
}

static bool nrc(hal_can_frame_t *rsp, uint8_t sid, uint8_t code)
{
    const uint8_t pl[3] = {0x7Fu, sid, code};
    return sf_reply(rsp, pl, 3u);
}

static uint32_t blocks(const cap_status_t *s)
{
    return (s->image_bytes + (UDS_CAP_BLOCK_BYTES - 1u)) / UDS_CAP_BLOCK_BYTES;
}

static bool read_did(uint16_t did, hal_can_frame_t *rsp)
{
    cap_status_t s;
    cap_status(&s);
    const uint32_t nb = blocks(&s);
    if (did == UDS_DID_CAP_STATUS) {
        const uint8_t pl[7] = {0x62u, 0xFDu, 0x40u, (uint8_t)((uint32_t)s.st | ((uint32_t)s.why << 4)), s.id,
                               (uint8_t)(nb >> 8), (uint8_t)nb};
        return sf_reply(rsp, pl, 7u);
    }
    if (nb == 0u) {
        return nrc(rsp, SID_RDBI, UDS_NRC_CONDITIONS); /* not frozen, or an arm pending */
    }
    if (s_cursor_seq != s.seq) {
        s_cursor = 0u; /* a new capture: from the start */
        s_cursor_seq = s.seq;
    }
    if (s_cursor >= nb) {
        return nrc(rsp, SID_RDBI, UDS_NRC_OUT_OF_RANGE);
    }
    const uint32_t off = s_cursor * UDS_CAP_BLOCK_BYTES;
    const uint32_t len = ((s.image_bytes - off) < UDS_CAP_BLOCK_BYTES) ? (s.image_bytes - off) : UDS_CAP_BLOCK_BYTES;
    uint8_t pl[6u + UDS_CAP_BLOCK_BYTES] = {0x62u, 0xFDu, 0x41u, s.id, (uint8_t)(s_cursor >> 8), (uint8_t)s_cursor};
    if (!cap_image_read(off, &pl[6], len)) {
        return nrc(rsp, SID_RDBI, UDS_NRC_CONDITIONS);
    }
    s_cursor++;
    return sf_reply(rsp, pl, 6u + len);
}

static bool seek(const uint8_t *m, uint8_t n, hal_can_frame_t *rsp)
{
    if (n != 5u) {
        return nrc(rsp, SID_WDBI, UDS_NRC_LENGTH);
    }
    cap_status_t s;
    cap_status(&s);
    const uint32_t k = ((uint32_t)m[3] << 8) | m[4];
    if (blocks(&s) == 0u) {
        return nrc(rsp, SID_WDBI, UDS_NRC_CONDITIONS);
    }
    if (k >= blocks(&s)) {
        return nrc(rsp, SID_WDBI, UDS_NRC_OUT_OF_RANGE);
    }
    s_cursor = k;
    s_cursor_seq = s.seq;
    const uint8_t pl[3] = {0x6Eu, 0xFDu, 0x41u};
    return sf_reply(rsp, pl, 3u);
}

static bool routine(uint16_t rid, const uint8_t *m, uint8_t n, hal_can_frame_t *rsp)
{
    cap_res_t r = CAP_ESTATE;
    if (rid == UDS_RID_CAP_TRIGGER) {
        if (n != 4u) {
            return nrc(rsp, SID_RC, UDS_NRC_LENGTH);
        }
        r = cap_trigger();
    } else if (n == 4u) {
        r = cap_arm(NULL);
    } else if (n == 12u) {
        const cap_cfg_t c = {.pre = (uint16_t)(((uint32_t)m[4] << 8) | m[5]), .sources = m[6],
                             .lvl_ch = (uint8_t)(m[7] & 0x7Fu), .lvl_falling = ((m[7] & 0x80u) != 0u),
                             .lvl = (int16_t)(uint16_t)(((uint32_t)m[8] << 8) | m[9]),
                             .hys = (uint16_t)(((uint32_t)m[10] << 8) | m[11])};
        r = cap_arm(&c);
    } else {
        return nrc(rsp, SID_RC, UDS_NRC_LENGTH);
    }
    if (r == CAP_EBUSY) {
        return nrc(rsp, SID_RC, NRC_BUSY);
    }
    if (r == CAP_ERANGE) {
        return nrc(rsp, SID_RC, UDS_NRC_OUT_OF_RANGE);
    }
    if (r == CAP_ESTATE) {
        return nrc(rsp, SID_RC, UDS_NRC_CONDITIONS);
    }
    const uint8_t pl[4] = {SID_RC + 0x40u, RC_START, m[2], m[3]};
    return sf_reply(rsp, pl, 4u);
}

bool uds_capture_handle(const hal_can_frame_t *rq, hal_can_frame_t *rsp)
{
    const uint8_t *m = NULL;
    uint8_t n = 0u;
    if (!sf_parse(rq, &m, &n) || (n < 3u)) {
        return false;
    }
    const uint16_t did = (uint16_t)(((uint32_t)m[1] << 8) | m[2]);
    if ((m[0] == SID_RDBI) && (n == 3u) && ((did == UDS_DID_CAP_STATUS) || (did == UDS_DID_CAP_BLOCK))) {
        return read_did(did, rsp);
    }
    if ((m[0] == SID_WDBI) && (did == UDS_DID_CAP_BLOCK)) {
        return seek(m, n, rsp);
    }
    if ((m[0] == SID_RC) && (n >= 4u) && (m[1] == RC_START)) {
        const uint16_t rid = (uint16_t)(((uint32_t)m[2] << 8) | m[3]);
        if ((rid == UDS_RID_CAP_ARM) || (rid == UDS_RID_CAP_TRIGGER)) {
            return routine(rid, m, n, rsp);
        }
    }
    return false; /* not the capture's: the rest of the UDS server answers */
}
