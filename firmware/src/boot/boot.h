/* boot.h — the FW-38 boot record and the bootloader's decision (host-testable; the bootloader binary that runs it at
 * every reset — startup, the jump, its FS26 handling — is a bring-up item, docs/target-bringup.md "FW-38").
 *
 * Regions (hal/flash.h): EXEC runs, STAGE receives downloads, LKG holds the last known good image. In BOOT_ST_IDLE,
 * EXEC and LKG hold the same committed image and the counter is at least its security version.
 * The boot record (NVM, NV_REC_BOOT; the A/B slots keep the previous record when a write is torn) is a write-ahead
 * journal: each state is written before its flash work starts, and each flash step copies from an intact source, so
 * a power loss at any point resumes to a defined image:
 *   IDLE       verify EXEC (target, counter) -> run it; EXEC invalid -> RESTORE; no valid LKG copy -> COMMIT first
 *   ACTIVATE   (the application's request) verify STAGE — signature, hash, target, security version >= the counter;
 *              refused -> IDLE with EXEC untouched; else INSTALL
 *   INSTALL    copy STAGE -> EXEC, verify EXEC, TRIAL, run the new image once. Found at a reset (a power loss during
 *              the copy): the activation is abandoned -> RESTORE
 *   TRIAL      found at a reset: the new image never confirmed itself (a crash, a watchdog, a power loss) -> RESTORE
 *   CONFIRMED  (written by the running image, update.c) -> COMMIT, the counter raised to EXEC's security version
 *   COMMIT     copy EXEC -> LKG, verify LKG -> IDLE (a power loss: COMMIT again)
 *   RESTORE    verify LKG (>= the counter), copy LKG -> EXEC, verify EXEC -> IDLE (a power loss: RESTORE again)
 * No record (EOL programmed EXEC only, or both copies lost): EXEC is adopted if it verifies under its own target; the
 * record takes that target and security version, and COMMIT makes the LKG copy. */
#ifndef BOOT_H
#define BOOT_H

#include "image.h"

#define BOOT_REC_MAGIC 0x544F4F42u /* "BOOT" */
#define BOOT_REC_LAYOUT 1u

typedef enum {
    BOOT_ST_IDLE = 0,
    BOOT_ST_ACTIVATE,
    BOOT_ST_INSTALL,
    BOOT_ST_TRIAL,
    BOOT_ST_CONFIRMED,
    BOOT_ST_COMMIT,
    BOOT_ST_RESTORE,
    BOOT_ST_COUNT
} boot_state_t;

/* The outcome of the last activation or fallback, kept for the application (DTC_FW_FALLBACK) and the tester. */
typedef enum {
    BOOT_LAST_NONE = 0,
    BOOT_LAST_INSTALLED,         /* the staged image was installed (then TRIAL, CONFIRMED, COMMIT) */
    BOOT_LAST_REJECTED,          /* the staged image failed the check (last_err): nothing installed */
    BOOT_LAST_NO_FALLBACK,       /* no valid LKG copy to fall back to: the activation was refused */
    BOOT_LAST_ABANDONED,         /* power lost during the install copy: the last known good restored */
    BOOT_LAST_FIRST_BOOT_FAILED, /* the new image did not confirm itself: the last known good restored */
    BOOT_LAST_EXEC_INVALID       /* EXEC failed its check (last_err): the last known good restored */
} boot_last_t;

/* No CRC of its own: the NVM slot's CRC-32 covers the record. A record ending in its own CRC-32 would make that slot
 * CRC blind to it — a CRC-32 over M || CRC32(M) is the same for every M — so a torn write would validate with the
 * stale bytes left in the slot, the record from two writes back: an anti-rollback counter could go back
 * (test_update: power_loss_at_every_write_of_the_swap..., a_torn_record_write_never_takes_the_counter_back). */
typedef struct {
    uint32_t magic;
    uint16_t layout;
    uint8_t state;        /* boot_state_t */
    uint8_t last;         /* boot_last_t */
    uint32_t target;      /* this card's SKU: every image must be signed for it */
    uint32_t sec_counter; /* anti-rollback: the highest committed security version */
    uint8_t lkg_valid;    /* LKG holds a verified copy of the committed image */
    uint8_t last_err;     /* img_result_t behind the last refusal */
    uint16_t pad;
} boot_rec_t;

/* The record in NVM: present (slot CRC, A/B), magic, layout, the state, the target and the flag in range. */
bool boot_rec_read(boot_rec_t *r);
/* Stamps r (magic, layout) and queues it (nv_queue): the application's asynchronous write. */
bool boot_rec_queue(boot_rec_t *r);
/* EOL side and tests: an IDLE record for this card, no LKG copy yet (the first boot makes it). */
void boot_rec_make(boot_rec_t *r, uint32_t target, uint32_t sec_counter);
/* The last activation or fallback failed (the application reports DTC_FW_FALLBACK). */
bool boot_last_failed(uint8_t last);

typedef enum {
    BOOT_RUN = 0,  /* start EXEC: it verified in this call */
    BOOT_NO_IMAGE, /* nothing verifies: stay in the bootloader, the gate inhibited in hardware */
    BOOT_HALT      /* a flash or NVM write failed (a power loss): nothing after it ran; the next reset resumes */
} boot_result_t;

/* The whole decision of one reset, after nv_init(): flash copies and record writes included. *out = the record as
 * it stands when the decision returns. */
boot_result_t boot_decide(boot_rec_t *out);

#endif /* BOOT_H */
