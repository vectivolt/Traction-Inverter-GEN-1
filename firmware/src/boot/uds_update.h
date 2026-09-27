/* uds_update.h — the FW-38 programming services on the diagnostic bus (uds.h: the transport, the addresses, the
 * SecurityAccess unlock). uds_handle() hands every request upd_uds_claims() takes to upd_uds() (one line in uds.c):
 *   0x10 DiagnosticSessionControl 0x02 programmingSession (enters the update state, update.h) — and, in it, 0x01
 *        back to the default session; response 0x50 sub P2 = 50 ms, P2* = 5000 ms
 *   0x34 RequestDownload  dataFormat 0x00, addressAndLengthFormat 0xSA (1..4 bytes each), address 0 (STAGE),
 *        size <= the region; needs the unlock; response 0x74 0x20 maxNumberOfBlockLength (UPD_BLOCK_MAX)
 *   0x36 TransferData     blockSequenceCounter from 1, wrapping 0xFF -> 0x00; the previous counter again is a repeat
 *        (acknowledged, not written); response 0x76 counter
 *   0x37 RequestTransferExit  every announced byte received; response 0x77
 *   0x31 RoutineControl   0x01 / 0x03 UPD_RID_VERIFY (start / results: status 0 running, 1 passed, 2 failed + the
 *        img_result_t); 0x01 UPD_RID_ACTIVATE
 *   0x11 ECUReset 0x01 hardReset (in the programming session): response 0x51 0x01, the reset UPD_RESET_DELAY_MS later */
#ifndef UDS_UPDATE_H
#define UDS_UPDATE_H

#include "uds.h"

#define UPD_RID_VERIFY 0xFF01u   /* ISO 14229-1 checkProgrammingDependencies */
#define UPD_RID_ACTIVATE 0xF038u /* system-supplier range */

/* n: the request's length — a single frame's, or (round 23, item 11) a segmented request's up to UPD_BLOCK_MAX */
bool upd_uds_claims(const uint8_t *m, uint32_t n);
bool upd_uds(const uds_t *u, const uint8_t *m, uint32_t n, hal_can_frame_t *rsp);

#endif /* UDS_UPDATE_H */
