/* uds_diag.h — FW-40 (round 23): the diagnostic services a service tool needs (contract §10h), on the diagnostic bus
 * beside FW-32 (uds.h) and FW-41 (uds_capture.h). ISO 14229-1:
 *   0x19 ReadDTCInformation     01 number of DTCs by status mask, 02 DTCs by status mask, 04 snapshot records by DTC
 *                               number (the NVM fault ring: nv_fault_t), 0A supported DTCs
 *   0x22 ReadDataByIdentifier   read-only telemetry 0xF200-0xF208 and identity 0xFD20-0xFD22, up to 8 per request;
 *                               every 0x22 reaching it (the other items' DIDs are answered before: uds_diag.c); round 23
 *                               (FW-46): 0xFD46, the ripple table the next FW-39 commit writes (72 bytes, commission.h)
 *   0x2E WriteDataByIdentifier  round 23 (FW-46): 0xFD46 only — the ripple table, 75 bytes, a segmented request (FW-39's
 *                               staging and interlocks: commission.h mc_ripple_write); a single frame of it is NRC 0x13
 *   0x2A ReadDataByPeriodicIdentifier  the telemetry DIDs as a stream: mode 01 every 100 ms, 02 every 10 ms, 03 every
 *                               1 ms, 04 stop; up to UDS_DIAG_PDID_MAX at once; UUDT frames [pDID, data] on
 *                               UDS_ID_PERIODIC; at most one frame per 1 ms task, and none in a task that sent a
 *                               response frame; a busy TX mailbox drops it (never waited for, never sent twice)
 *   0x14 ClearDiagnosticInformation  gated as the FW-32 routine: SecurityAccess (one clear per unlock), the bridge
 *                               disarmed, HV absent; the latches a clear does not release keep their DTCs (uds_diag.c)
 * DTC number = dtc_code(): 0xD10000 | id (ISO 14229-1 DTC format 0x01); status availability mask 0x7F.
 * Transport: ISO 15765-2 on the CAN-FD bus (TX_DL 64). A request is one single frame to UDS_ID_REQ (classic PCI 0x0L, or
 * the escape format 0x00 SF_DL up to 62 bytes) — round 23 (item 11): or a first frame and consecutive frames up to
 * UDS_DIAG_RX_MAX bytes under this ECU's flow control (block size UDS_DIAG_RX_BS, STmin 0; N_Cr 1 s), for FW-38's
 * TransferData blocks and FW-46's 2E FD 46 (the only ones that take one). A response is queued, then sent by
 * uds_diag_tick() one frame per task,
 * retried while the TX mailbox is busy: up to 7 bytes a classic 8-byte single frame, up to 62 an escape single frame,
 * longer a first frame, then consecutive frames under the tester's flow control (BS, STmin, WAIT, overflow;
 * N_As = N_Bs = 1 s). A new FW-40 request aborts a pending response. Nothing here writes anything control reads
 * (0x14 writes the DTC store; of it only the FW-15 retry gate reads DTC_DESAT_REPEAT, which a clear keeps). */
#ifndef UDS_DIAG_H
#define UDS_DIAG_H

#include "nvlog.h"
#include "uds.h"

#define UDS_ID_PERIODIC 0x6E9u  /* the periodic (UUDT) frames: this repository's until the OEM specification binds it */
#define UDS_DIAG_MSG_MAX 1024u  /* the longest response: 0x19 04 with every ring record, 0x19 0A with every DTC */
#define UDS_DIAG_PDID_MAX 4u
#define UDS_DIAG_RX_MAX 4095u   /* round 23 (item 11): the longest request received segmented — the classic first frame's
                                 * 12-bit length; FW-38's TransferData blocks (UPD_BLOCK_MAX) */
#define UDS_DIAG_RX_BS 4u       /* the block size the receiver asks for: the frames one 1 ms task reads (app.c) */

typedef struct {
    uint8_t pdid;  /* DID 0xF200 | pdid */
    uint8_t mode;  /* 1 slow, 2 medium, 3 fast */
    uint32_t t_ms; /* last sent (or dropped) */
} uds_pdid_t;

typedef struct {
    /* the response in flight (ISO 15765-2) */
    uint8_t msg[UDS_DIAG_MSG_MAX];
    uint16_t len;
    uint16_t off;     /* bytes sent */
    uint8_t tp;       /* uds_diag.c: TP_IDLE / TP_FIRST / TP_WAIT_FC / TP_CF */
    uint8_t sn;       /* the next consecutive frame's sequence number */
    uint8_t bs;       /* block size of the last flow control (0: no more) */
    uint8_t bs_left;
    uint8_t n_wait;   /* WAIT flow controls for this message */
    uint32_t stmin_us;
    uint32_t t_cf_us; /* the last frame sent */
    uint32_t t_tp_ms; /* N_As / N_Bs start */
    /* the 0x19 04 ring scan (a few records per task) */
    bool scan;
    bool scan_top_ok;
    uint8_t scan_rec;  /* the record asked for: 1..NV_FAULT_RING, 0xFF all */
    uint8_t scan_age;  /* the next ring position (0 = the newest record) */
    uint8_t scan_hits; /* records of the DTC seen so far */
    uint8_t scan_again;
    uint16_t scan_id;
    nv_fault_t scan_top; /* the newest record when the scan started: a record written meanwhile restarts it */
    /* the periodic DIDs */
    uds_pdid_t per[UDS_DIAG_PDID_MAX];
    uint8_t n_per;
    uint8_t rr;         /* round robin */
    uint32_t n_sent;
    uint32_t n_dropped; /* the TX mailbox was busy */
    uint32_t n_aborted; /* responses abandoned: N_As, N_Bs, overflow, too many WAITs, a new request; receptions abandoned */
    /* round 23 (item 11): a segmented request being received (ISO 15765-2) */
    uint8_t rx[UDS_DIAG_RX_MAX];
    uint16_t rx_len;
    uint16_t rx_off;
    uint8_t rx_sn;      /* the next consecutive frame's sequence number */
    uint8_t rx_bs_left; /* consecutive frames until the next flow control */
    bool rx_on;
    uint8_t fc_owed;    /* the flow control to send (its PCI byte: CTS 0x30 or overflow 0x32), 0 none */
    uint32_t t_rx_ms;   /* N_Cr start */
} uds_diag_t;

/* From uds_handle(): true = an FW-40 frame (a request for 0x14, 0x19, 0x22 or 0x2A, or the flow control a segmented
 * response waits for), taken; its response goes out from uds_diag_tick(). */
bool uds_diag_rx(uds_t *u, const hal_can_frame_t *rq);
/* 1 ms task, after the requests (app.c: diag): the 0x19 04 ring scan, then at most one frame — a response frame
 * first, else one periodic DID. */
void uds_diag_tick(uds_t *u);

#endif /* UDS_DIAG_H */
