/* uds.h — the diagnostic services this image answers on the diagnostic bus (HAL_CAN_DIAG): ISO 14229-1 over
 * single-frame ISO 15765-2 (PCI 0x0L, L = 1..7; responses padded to 8 bytes). Round 17 (FW-32):
 *   0x27 SecurityAccess  0x01 requestSeed -> 0x67 0x01 seed[4]; 0x02 sendKey key[4] -> 0x67 0x02.
 *                        The key comes from a build-time hook (app.c, TI_UDS_KEY_FN). The default build has
 *                        none: every seed request is refused (NRC 0x22) and nothing ever unlocks (fail
 *                        closed). A seed answers one key. UDS_BAD_KEY_LIMIT invalid keys refuse every
 *                        further seed until the MCU restarts (NRC 0x36).
 *   0x31 RoutineControl  0x01 startRoutine UDS_RID_CLEAR_SERVICE_LOCK -> 0x71 0x01 0xF0 0x10, only unlocked
 *                        (else NRC 0x33); the routine decides its own conditions (its NRC is returned) and a
 *                        completed run consumes the unlock.
 * Anything else: NRC 0x11 / 0x12 / 0x13 / 0x31; a frame that is not a single frame, or not addressed to
 * UDS_ID_REQ, gets no response. The IDs are this repository's definition until the OEM diagnostic
 * specification binds them. */
#ifndef UDS_H
#define UDS_H

#include "can.h"

#define UDS_ID_REQ 0x7E1u
#define UDS_ID_RSP 0x7E9u
#define UDS_RID_CLEAR_SERVICE_LOCK 0xF010u /* system-supplier range */
#define UDS_SA_LEN 4u
#define UDS_BAD_KEY_LIMIT 3u

#define UDS_NRC_SERVICE_NOT_SUPPORTED 0x11u
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED 0x12u
#define UDS_NRC_LENGTH 0x13u
#define UDS_NRC_CONDITIONS 0x22u
#define UDS_NRC_SEQUENCE 0x24u
#define UDS_NRC_OUT_OF_RANGE 0x31u
#define UDS_NRC_SECURITY_DENIED 0x33u
#define UDS_NRC_INVALID_KEY 0x35u
#define UDS_NRC_ATTEMPTS 0x36u
#define UDS_NRC_PROGRAMMING 0x72u

/* The SecurityAccess key of a seed (the product's algorithm); false = no key. */
typedef bool (*uds_key_fn_t)(const uint8_t seed[UDS_SA_LEN], uint8_t key[UDS_SA_LEN]);
/* The routine: 0 = done, else the NRC that refuses it (nothing done). */
typedef uint8_t (*uds_routine_fn_t)(void *ctx);

typedef struct {
    uds_key_fn_t key_fn; /* NULL: no unlock, ever */
    uds_routine_fn_t clear_fn;
    void *ctx;
    uint8_t seed[UDS_SA_LEN];
    bool seed_out; /* handed out, not yet answered */
    bool unlocked;
    uint8_t bad_keys;
} uds_t;

void uds_init(uds_t *u, uds_key_fn_t key_fn, uds_routine_fn_t clear_fn, void *ctx);
/* One request frame: true with the response in *rsp, false when there is nothing to answer. seed = the
 * value handed out if this is a seed request (0 is replaced: a zero seed means "already unlocked"). */
bool uds_handle(uds_t *u, const hal_can_frame_t *rq, uint32_t seed, hal_can_frame_t *rsp);

#endif /* UDS_H */
