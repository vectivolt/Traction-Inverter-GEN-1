/* uds_capture.h — FW-41: the waveform capture on the diagnostic bus (contract §10i). ISO 14229-1 over single-frame
 * ISO 15765-2 on the CAN-FD diagnostic bus: a request up to 7 bytes in the classic format (PCI 0x0L) or up to 62 in
 * the CAN-FD escape format (0x00, SF_DL); a response up to 7 bytes classic (8-byte frame), longer ones in the escape
 * format in the smallest CAN-FD frame that holds them. Read-only for control: nothing here is read by control.
 *   22 FD 40          status  -> 62 FD 40 [state | reason << 4] [capture id] [blocks BE16]   (blocks 0 unless frozen)
 *   22 FD 41          block   -> 62 FD 41 [capture id] [block BE16] [<= 56 image bytes], then the cursor moves on
 *   2E FD 41 [blk BE16] seek  -> 6E FD 41; the cursor starts at 0 for every new frozen capture
 *   31 01 F0 41 [cfg] arm     -> 71 01 F0 41; no cfg = the present one; cfg = [pre BE16] [sources] [level channel |
 *                                0x80 falling] [level BE16] [hysteresis BE16] (raw units of that channel)
 *   31 01 F0 42       trigger -> 71 01 F0 42 (armed only)
 * NRC: 0x13 length, 0x21 an arm not yet taken by the ISR, 0x22 not frozen / not armed, 0x31 out of range.
 * Not gated by SecurityAccess: these services change nothing but the capture's own RAM. uds_handle() calls this first;
 * false = not a capture request (anything else passes on). FW-40's DID table can call cap_*() (capture.h) instead. */
#ifndef UDS_CAPTURE_H
#define UDS_CAPTURE_H

#include "can.h"

#define UDS_DID_CAP_STATUS 0xFD40u /* system-supplier range */
#define UDS_DID_CAP_BLOCK 0xFD41u
#define UDS_RID_CAP_ARM 0xF041u
#define UDS_RID_CAP_TRIGGER 0xF042u
#define UDS_CAP_BLOCK_BYTES 56u    /* image bytes per block: 62 - 0x62, the DID, the id, the block number */

bool uds_capture_handle(const hal_can_frame_t *rq, hal_can_frame_t *rsp);

#endif /* UDS_CAPTURE_H */
