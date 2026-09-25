/* fs26.h — FS26 safety SBC driver (FW-12).
 * Boot (fs26_init): SPI readback of what the OTP defines — M_PROGID against the procured variant,
 * FS_STATES OTP_CORRUPT / DBG_MODE — then, in INIT_FS, the INIT registers FW-12 fixes and their
 * NOT pairs, read back masked:
 *   WD_ERR_LIMIT = 2, WD_FS_REACTION = RSTB + FS0B, window 3 ms (50 % closed);
 *   BACKUP_SAFETY_PATH_FS0B = 1, BACKUP_SAFETY_PATH_FS1B = 0;
 *   FS1B_TDELAY = 0, FS1B_TDUR = 100 ms;
 * and closes INIT_FS with the first good challenger answer (REG_CORRUPT must stay 0).
 * The FS26 DS Rev.3 exposes the OTP bank itself only in OTP/Debug mode: the per-field OTP set of
 * design-basis §8a is proven at EOL; at every boot the firmware proves the variant (PROG_ID), the
 * OTP CRC monitor and the INIT configuration. FS_GPIO1 "push-pull, not slotted" is proven
 * indirectly: gate power up before §9 step 6 => DTC (safety/state_machine.c).
 * Run time: challenger refresh on every second 1 ms task, ≈ 2.0 ms (open window 1.58–2.86 ms at the fail-safe
 * oscillator's ±5 %; fs26_wd_due, round 17), FS0B/FS1B release
 * with the token-derived word (§9 step 4), FS0B_REQ for FW-16 step a, GPIO1 control, LPOFF. */
#ifndef FS26_H
#define FS26_H

#include "fs26_regs.h"
#include "ti_params.h"

typedef enum {
    FS26_OK = 0,
    FS26_BUSY,
    FS26_ERR_SPI,
    FS26_ERR_CRC,
    FS26_ERR_PROGID,
    FS26_ERR_OTP,
    FS26_ERR_DEBUG,
    FS26_ERR_READBACK,
    FS26_ERR_RELEASE
} fs26_status_t;

typedef struct {
    bool init_done;
    bool wd_running;
    uint32_t last_refresh_us;
    uint32_t n_refresh;
    uint32_t n_comm_err;
    uint8_t wd_err_cnt;
    bool fs1b_short_high; /* FS1B_DIAG: DTC, no arming until repaired (BACKUP_SAFETY_PATH_FS1B = 0) */
    uint16_t prog_id;
    fs26_status_t last_err;
} fs26_t;

/* INIT values FW-12 requires (exported for the tests). */
uint16_t fs26_wd_cfg_value(void);
uint16_t fs26_fssm_value(void);
uint16_t fs26_wdw_value(void);
uint16_t fs26_ios2_value(void);

fs26_status_t fs26_init(fs26_t *f, const ti_params_t *p);
bool fs26_wd_due(const fs26_t *f, uint32_t now_us);
fs26_status_t fs26_wd_refresh(fs26_t *f);
fs26_status_t fs26_release_safety_outputs(fs26_t *f);
bool fs26_outputs_released(fs26_t *f);
fs26_status_t fs26_request_fs0b(fs26_t *f);
fs26_status_t fs26_set_gpio1(fs26_t *f, bool high);
fs26_status_t fs26_goto_lpoff(fs26_t *f);
fs26_status_t fs26_read(fs26_t *f, uint8_t addr, uint16_t *val);
fs26_status_t fs26_write(fs26_t *f, uint8_t addr, uint16_t val);

#endif /* FS26_H */
