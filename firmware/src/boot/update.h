/* update.h — the application's firmware-update state (FW-38), driven by the UDS programming services (uds_update.c):
 *   enter     only on the diagnostic request, only while the application's conditions hold (upd_cond_fn: the bridge
 *             disarmed, the link discharged, the motor at standstill); entering calls upd_enter_fn, with which the
 *             application withdraws its validated arming evidence and forbids arming for the key cycle — the update
 *             state can never arm (the protective §6 paths keep their authority);
 *   download  RequestDownload / TransferData / RequestTransferExit into STAGE: the 1 ms task only copies the bytes
 *             into two RAM chunks, upd_service() (the background loop) erases the pages and programs the chunks;
 *   verify    the routine runs img_verify() on STAGE in the background, against the boot record's target and
 *             anti-rollback counter; requestRoutineResults reads the outcome;
 *   activate  a verified image, the conditions again, the boot record IDLE -> ACTIVATE (the bootloader checks the
 *             image again before it installs it at the next reset);
 *   reset     ECUReset once nothing is owed to flash or NVM and the request reads back; UPD_RESET_DELAY_MS later.
 * Outside any session: a trial boot (the record says TRIAL) confirms itself after UPD_CONFIRM_MS of running — alive,
 * its watchdogs answered — which lets the bootloader commit it and raise the counter (boot.h). */
#ifndef UPDATE_H
#define UPDATE_H

#include "boot.h"

#define UPD_CHUNK 4096u        /* RAM chunk programmed at once: a multiple of HAL_FLASH_WORD dividing HAL_FLASH_PAGE;
                                * round 23 (item 11): one TransferData block's size (two chunks of RAM, 8 KiB) */
#define UPD_BLOCK_MAX 4095u    /* maxNumberOfBlockLength (SID, counter, 4093 bytes). Round 23 (item 11): carried over the
                                * ISO 15765-2 transport of FW-40 (uds_diag.c: the segmented request), the classic first
                                * frame's 12-bit length; it was the single frame's 7 (5 bytes a block: ~210 s per MiB) */
#define UPD_CONFIRM_MS 5000u
#define UPD_RESET_DELAY_MS 20u /* the positive response leaves the CAN mailbox first */

#define UPD_NRC_BUSY 0x21u          /* busyRepeatRequest: the background still owes a chunk or an NVM write */
#define UPD_NRC_SUSPENDED 0x71u     /* transferDataSuspended: more data than RequestDownload announced */
#define UPD_NRC_WRONG_BSC 0x73u     /* wrongBlockSequenceCounter */
#define UPD_NRC_NOT_IN_SESSION 0x7Fu /* serviceNotSupportedInActiveSession */

typedef uint8_t (*upd_cond_fn)(void *ctx); /* 0 = the conditions hold, else the NRC that refuses */
typedef void (*upd_enter_fn)(void *ctx);

typedef enum {
    UPD_OFF = 0,  /* no programming session */
    UPD_SESSION,  /* programming session, nothing downloaded */
    UPD_DOWNLOAD,
    UPD_STAGED,   /* RequestTransferExit accepted */
    UPD_VERIFYING,
    UPD_VERIFIED,
    UPD_FAILED,   /* the verification refused the staged image */
    UPD_ACTIVATED /* the boot record says ACTIVATE */
} upd_phase_t;

void upd_init(upd_cond_fn cond, upd_enter_fn enter, void *ctx);
void upd_service(void); /* background loop: flash work, the verification, the reset, the trial confirmation */
upd_phase_t upd_phase(void);
bool upd_session(void);

/* The services (uds_update.c): 0 = done, else the NRC. */
uint8_t upd_enter(void);
void upd_leave(void);
uint8_t upd_download(uint32_t addr, uint32_t size, bool unlocked);
uint8_t upd_transfer(uint8_t bsc, const uint8_t *d, uint32_t n);
uint8_t upd_transfer_exit(void);
uint8_t upd_verify_start(void);
uint8_t upd_verify_result(uint8_t *status, uint8_t *err); /* status 0 running, 1 passed, 2 failed (err: img_result_t) */
uint8_t upd_activate(void);
uint8_t upd_reset(void);

#endif /* UPDATE_H */
