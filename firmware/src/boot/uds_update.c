/* uds_update.c — see uds_update.h. */
#include "uds_update.h"

#include <string.h>

#include "update.h"

#define SID_DSC 0x10u
#define SID_ER 0x11u
#define SID_RC 0x31u
#define SID_RD 0x34u
#define SID_TD 0x36u
#define SID_RTE 0x37u

static uint32_t rid_of(const uint8_t *m) { return ((uint32_t)m[2] << 8) | m[3]; }

bool upd_uds_claims(const uint8_t *m, uint32_t n)
{
    switch (m[0]) {
    case SID_RD:
    case SID_TD:
    case SID_RTE:
        return true;
    case SID_DSC:
        return (n >= 2u) && ((m[1] == 0x02u) || upd_session());
    case SID_ER:
        return upd_session();
    case SID_RC:
        return (n >= 4u) && ((rid_of(m) == UPD_RID_VERIFY) || (rid_of(m) == UPD_RID_ACTIVATE));
    default:
        return false;
    }
}

static bool reply(hal_can_frame_t *rsp, const uint8_t *pl, uint8_t n)
{
    *rsp = (hal_can_frame_t){.id = UDS_ID_RSP, .len = 8u};
    (void)memset(rsp->data, 0xAA, 8u);
    rsp->data[0] = n;
    (void)memcpy(&rsp->data[1], pl, n);
    return true;
}

/* code 0: the positive response pl; else the negative response */
static bool answer(hal_can_frame_t *rsp, uint8_t sid, uint8_t code, const uint8_t *pl, uint8_t n)
{
    const uint8_t neg[3] = {0x7Fu, sid, code};
    return (code == 0u) ? reply(rsp, pl, n) : reply(rsp, neg, 3u);
}

static bool session(const uint8_t *m, uint32_t n, hal_can_frame_t *rsp)
{
    const uint8_t pl[6] = {SID_DSC + 0x40u, m[1], 0x00u, 0x32u, 0x01u, 0xF4u};
    uint8_t code = UDS_NRC_SUBFUNCTION_NOT_SUPPORTED;
    if (n != 2u) {
        code = UDS_NRC_LENGTH;
    } else if (m[1] == 0x02u) {
        code = upd_enter();
    } else if (m[1] == 0x01u) {
        upd_leave();
        code = 0u;
    } else {
        /* no other session from the programming session */
    }
    return answer(rsp, SID_DSC, code, pl, 6u);
}

static bool ecu_reset(const uint8_t *m, uint32_t n, hal_can_frame_t *rsp)
{
    const uint8_t pl[2] = {SID_ER + 0x40u, 0x01u};
    const uint8_t code = (n != 2u) ? UDS_NRC_LENGTH : ((m[1] != 0x01u) ? UDS_NRC_SUBFUNCTION_NOT_SUPPORTED : upd_reset());
    return answer(rsp, SID_ER, code, pl, 2u);
}

static uint32_t be(const uint8_t *p, uint32_t len)
{
    uint32_t v = 0u;
    for (uint32_t i = 0u; i < len; i++) {
        v = (v << 8) | p[i];
    }
    return v;
}

static bool request_download(const uds_t *u, const uint8_t *m, uint32_t n, hal_can_frame_t *rsp)
{
    const uint8_t pl[4] = {SID_RD + 0x40u, 0x20u, (uint8_t)(UPD_BLOCK_MAX >> 8), (uint8_t)(UPD_BLOCK_MAX & 0xFFu)};
    const uint32_t an = (n >= 3u) ? (m[2] & 0x0Fu) : 0u;
    const uint32_t sn = (n >= 3u) ? (uint32_t)(m[2] >> 4) : 0u;
    uint8_t code;
    if (n < 5u) {
        code = UDS_NRC_LENGTH;
    } else if ((m[1] != 0x00u) || (an < 1u) || (an > 4u) || (sn < 1u) || (sn > 4u)) {
        code = UDS_NRC_OUT_OF_RANGE; /* compressed/encrypted data or a format outside 1..4 bytes */
    } else if (n != (3u + an + sn)) {
        code = UDS_NRC_LENGTH;
    } else {
        code = upd_download(be(&m[3], an), be(&m[3u + an], sn), u->unlocked);
    }
    return answer(rsp, SID_RD, code, pl, 4u);
}

static bool transfer_data(const uint8_t *m, uint32_t n, hal_can_frame_t *rsp)
{
    const uint8_t pl[2] = {SID_TD + 0x40u, (n >= 2u) ? m[1] : 0u};
    const uint8_t code = ((n < 3u) || (n > UPD_BLOCK_MAX)) ? UDS_NRC_LENGTH : upd_transfer(m[1], &m[2], n - 2u);
    return answer(rsp, SID_TD, code, pl, 2u);
}

static bool transfer_exit(uint32_t n, hal_can_frame_t *rsp)
{
    const uint8_t pl[1] = {SID_RTE + 0x40u};
    return answer(rsp, SID_RTE, (n != 1u) ? UDS_NRC_LENGTH : upd_transfer_exit(), pl, 1u);
}

static bool routine(const uint8_t *m, uint32_t n, hal_can_frame_t *rsp)
{
    uint8_t pl[6] = {SID_RC + 0x40u, m[1], m[2], m[3], 0u, 0u};
    uint8_t len = 4u;
    uint8_t code = UDS_NRC_SUBFUNCTION_NOT_SUPPORTED;
    if (n != 4u) {
        code = UDS_NRC_LENGTH;
    } else if (m[1] == 0x01u) {
        code = (rid_of(m) == UPD_RID_VERIFY) ? upd_verify_start() : upd_activate();
    } else if ((m[1] == 0x03u) && (rid_of(m) == UPD_RID_VERIFY)) {
        code = upd_verify_result(&pl[4], &pl[5]);
        len = 6u;
    } else {
        /* stopRoutine, or results of the activation: not supported */
    }
    return answer(rsp, SID_RC, code, pl, len);
}

bool upd_uds(const uds_t *u, const uint8_t *m, uint32_t n, hal_can_frame_t *rsp)
{
    switch (m[0]) {
    case SID_DSC:
        return session(m, n, rsp);
    case SID_ER:
        return ecu_reset(m, n, rsp);
    case SID_RD:
        return request_download(u, m, n, rsp);
    case SID_TD:
        return transfer_data(m, n, rsp);
    case SID_RTE:
        return transfer_exit(n, rsp);
    default:
        return routine(m, n, rsp);
    }
}
