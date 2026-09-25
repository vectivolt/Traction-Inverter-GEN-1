/* test_uds.c — FW-32 protocol: SecurityAccess seed/key with the build-time key hook, the service-lock
 * routine behind it, and the refusals (no key, wrong key, sequence, attempts, malformed requests). */
#include <string.h>

#include "test.h"
#include "uds.h"

static unsigned s_runs;
static uint8_t s_routine_nrc;

static uint8_t routine(void *ctx)
{
    (void)ctx;
    s_runs += (s_routine_nrc == 0u) ? 1u : 0u;
    return s_routine_nrc;
}

static bool key_of(const uint8_t seed[UDS_SA_LEN], uint8_t key[UDS_SA_LEN])
{
    for (uint32_t i = 0u; i < UDS_SA_LEN; i++) {
        key[i] = (uint8_t)(seed[(i + 1u) % UDS_SA_LEN] ^ (0xA5u + i));
    }
    return true;
}

/* Request -> response payload (PCI length first); false when nothing is sent. */
static bool xfer(uds_t *u, const uint8_t *req, uint8_t n, uint32_t seed, uint8_t out[8])
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u};
    (void)memset(f.data, 0xAA, 8u);
    f.data[0] = n;
    (void)memcpy(&f.data[1], req, n);
    hal_can_frame_t r;
    if (!uds_handle(u, &f, seed, &r)) {
        return false;
    }
    (void)memcpy(out, r.data, 8u);
    return (r.id == UDS_ID_RSP) && (r.len == 8u);
}

static bool is_nrc(const uint8_t r[8], uint8_t sid, uint8_t nrc) { return (r[0] == 3u) && (r[1] == 0x7Fu) && (r[2] == sid) && (r[3] == nrc); }

static const uint8_t SEED_RQ[2] = {0x27u, 0x01u};
static const uint8_t CLEAR[4] = {0x31u, 0x01u, 0xF0u, 0x10u};

TEST(security_access_refused_without_a_key_function)
{
    uds_t u;
    uint8_t r[8];
    s_runs = 0u;
    s_routine_nrc = 0u;
    uds_init(&u, NULL, routine, NULL); /* the default build */
    CHECK(xfer(&u, SEED_RQ, 2u, 0x12345678u, r) && is_nrc(r, 0x27u, UDS_NRC_CONDITIONS));
    const uint8_t kq[6] = {0x27u, 0x02u, 0u, 0u, 0u, 0u};
    CHECK(xfer(&u, kq, 6u, 0u, r) && is_nrc(r, 0x27u, UDS_NRC_SEQUENCE));
    CHECK(xfer(&u, CLEAR, 4u, 0u, r) && is_nrc(r, 0x31u, UDS_NRC_SECURITY_DENIED));
    CHECK(s_runs == 0u && !u.unlocked);
}

TEST(seed_key_unlocks_one_routine_run)
{
    uds_t u;
    uint8_t r[8];
    s_runs = 0u;
    uds_init(&u, key_of, routine, NULL);
    CHECK(xfer(&u, SEED_RQ, 2u, 0x0A0B0C0Du, r) && r[0] == 6u && r[1] == 0x67u && r[2] == 0x01u);
    CHECK(r[3] == 0x0Du && r[4] == 0x0Cu && r[5] == 0x0Bu && r[6] == 0x0Au);
    uint8_t kq[6] = {0x27u, 0x02u};
    (void)key_of(&r[3], &kq[2]);
    CHECK(xfer(&u, kq, 6u, 0u, r) && r[0] == 2u && r[1] == 0x67u && r[2] == 0x02u && u.unlocked);
    CHECK(xfer(&u, kq, 6u, 0u, r) && is_nrc(r, 0x27u, UDS_NRC_SEQUENCE)); /* a seed answers one key */
    CHECK(xfer(&u, SEED_RQ, 2u, 0x55u, r) && r[1] == 0x67u && r[3] == 0u && r[4] == 0u && r[5] == 0u && r[6] == 0u);
    s_routine_nrc = UDS_NRC_CONDITIONS; /* the routine's own conditions refuse it: the unlock stays */
    CHECK(xfer(&u, CLEAR, 4u, 0u, r) && is_nrc(r, 0x31u, UDS_NRC_CONDITIONS) && s_runs == 0u && u.unlocked);
    s_routine_nrc = 0u;
    CHECK(xfer(&u, CLEAR, 4u, 0u, r) && r[0] == 4u && r[1] == 0x71u && r[2] == 0x01u && r[3] == 0xF0u && r[4] == 0x10u);
    CHECK(s_runs == 1u && !u.unlocked);
    CHECK(xfer(&u, CLEAR, 4u, 0u, r) && is_nrc(r, 0x31u, UDS_NRC_SECURITY_DENIED) && s_runs == 1u);
    CHECK(xfer(&u, SEED_RQ, 2u, 0u, r) && r[1] == 0x67u && (r[3] | r[4] | r[5] | r[6]) != 0u); /* never a zero seed */
}

TEST(wrong_keys_lock_out_and_malformed_requests_are_refused)
{
    uds_t u;
    uint8_t r[8];
    s_runs = 0u;
    s_routine_nrc = 0u;
    uds_init(&u, key_of, routine, NULL);
    for (unsigned k = 0u; k < UDS_BAD_KEY_LIMIT; k++) {
        CHECK(xfer(&u, SEED_RQ, 2u, 0x1000u + k, r) && r[1] == 0x67u);
        const uint8_t bad[6] = {0x27u, 0x02u, r[3], r[4], r[5], r[6]}; /* the seed echoed is not the key */
        const bool last = (k + 1u) == UDS_BAD_KEY_LIMIT;
        CHECK(xfer(&u, bad, 6u, 0u, r) && is_nrc(r, 0x27u, last ? UDS_NRC_ATTEMPTS : UDS_NRC_INVALID_KEY));
    }
    CHECK(xfer(&u, SEED_RQ, 2u, 0x2000u, r) && is_nrc(r, 0x27u, UDS_NRC_ATTEMPTS) && !u.unlocked);
    CHECK(xfer(&u, CLEAR, 4u, 0u, r) && is_nrc(r, 0x31u, UDS_NRC_SECURITY_DENIED) && s_runs == 0u);
    uds_init(&u, key_of, routine, NULL);
    const uint8_t sid10[2] = {0x10u, 0x03u};
    CHECK(xfer(&u, sid10, 2u, 0u, r) && is_nrc(r, 0x10u, UDS_NRC_SERVICE_NOT_SUPPORTED));
    const uint8_t sa5[2] = {0x27u, 0x05u};
    CHECK(xfer(&u, sa5, 2u, 0u, r) && is_nrc(r, 0x27u, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED));
    const uint8_t seed3[3] = {0x27u, 0x01u, 0x00u};
    CHECK(xfer(&u, seed3, 3u, 0u, r) && is_nrc(r, 0x27u, UDS_NRC_LENGTH));
    const uint8_t other_rid[4] = {0x31u, 0x01u, 0xF0u, 0x11u};
    CHECK(xfer(&u, other_rid, 4u, 0u, r) && is_nrc(r, 0x31u, UDS_NRC_OUT_OF_RANGE));
    const uint8_t stop[4] = {0x31u, 0x02u, 0xF0u, 0x10u};
    CHECK(xfer(&u, stop, 4u, 0u, r) && is_nrc(r, 0x31u, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED));
    const uint8_t with_opt[5] = {0x31u, 0x01u, 0xF0u, 0x10u, 0x00u};
    CHECK(xfer(&u, with_opt, 5u, 0u, r) && is_nrc(r, 0x31u, UDS_NRC_LENGTH));
    hal_can_frame_t ff = {.id = UDS_ID_REQ, .len = 8u, .data = {0x10u, 0x08u, 0x27u, 0x01u}}; /* a first frame */
    hal_can_frame_t other = {.id = 0x7E0u, .len = 8u, .data = {0x02u, 0x27u, 0x01u}};       /* another ECU's */
    hal_can_frame_t rsp;
    CHECK(!uds_handle(&u, &ff, 0u, &rsp) && !uds_handle(&u, &other, 0u, &rsp) && s_runs == 0u);
}

void suite_uds(void)
{
    RUN(security_access_refused_without_a_key_function);
    RUN(seed_key_unlocks_one_routine_run);
    RUN(wrong_keys_lock_out_and_malformed_requests_are_refused);
}
