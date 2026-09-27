/* update.c — see update.h. */
#include "update.h"

#include <string.h>

#include "dtc.h"
#include "nvlog.h"
#include "timer.h"
#include "uds.h"
#include "verify.h"

static struct {
    upd_phase_t phase;
    upd_cond_fn cond;
    upd_enter_fn enter;
    void *ctx;
    uint32_t size;     /* memorySize of the download */
    uint32_t received;
    uint8_t bsc;       /* the block sequence counter expected next */
    uint8_t buf[2][UPD_CHUNK];
    uint32_t off[2];   /* STAGE offset of each chunk */
    bool owed[2];      /* filled, not yet programmed */
    uint8_t cur;       /* the chunk being filled */
    uint32_t fill;
    uint32_t prog;     /* the next STAGE offset to program: chunks go out in order */
    bool flash_err;
    img_result_t vres;
    bool trial;
    uint32_t t0_ms;
    bool reset;
    uint32_t reset_ms;
} s;

void upd_init(upd_cond_fn cond, upd_enter_fn enter, void *ctx)
{
    (void)memset(&s, 0, sizeof s);
    s.cond = cond;
    s.enter = enter;
    s.ctx = ctx;
    s.t0_ms = hal_time_ms();
    boot_rec_t r;
    if (boot_rec_read(&r)) {
        s.trial = (r.state == (uint8_t)BOOT_ST_TRIAL);
        if (boot_last_failed(r.last)) {
            dtc_set(DTC_FW_FALLBACK, s.t0_ms);
        }
    }
}

upd_phase_t upd_phase(void) { return s.phase; }
bool upd_session(void) { return s.phase != UPD_OFF; }

static bool owed(void) { return s.owed[0] || s.owed[1]; }

uint8_t upd_enter(void)
{
    if (s.phase != UPD_OFF) {
        return 0u; /* already in the session */
    }
    const uint8_t c = (s.cond != NULL) ? s.cond(s.ctx) : UDS_NRC_CONDITIONS;
    if (c != 0u) {
        return c;
    }
    if (s.enter != NULL) {
        s.enter(s.ctx);
    }
    s.phase = UPD_SESSION;
    return 0u;
}

/* Back to the default session: the download is dropped; the arming stays forbidden until the next power-up. */
void upd_leave(void)
{
    s.phase = UPD_OFF;
    s.owed[0] = false;
    s.owed[1] = false;
}

uint8_t upd_download(uint32_t addr, uint32_t size, bool unlocked)
{
    if (s.phase == UPD_OFF) {
        return UPD_NRC_NOT_IN_SESSION;
    }
    if (!unlocked) {
        return UDS_NRC_SECURITY_DENIED;
    }
    if ((s.phase == UPD_DOWNLOAD) || (s.phase == UPD_VERIFYING) || (s.phase == UPD_ACTIVATED)) {
        return UDS_NRC_CONDITIONS;
    }
    if (owed()) {
        return UPD_NRC_BUSY;
    }
    if ((addr != 0u) || (size <= IMG_HDR_LEN) || (size > HAL_FLASH_REGION_SIZE)) {
        return UDS_NRC_OUT_OF_RANGE;
    }
    s.size = size;
    s.received = 0u;
    s.bsc = 1u;
    s.cur = 0u;
    s.off[0] = 0u;
    s.fill = 0u;
    s.prog = 0u;
    s.flash_err = false;
    s.phase = UPD_DOWNLOAD;
    return 0u;
}

/* The chunk being filled goes to the background; the other one takes over. */
static void chunk_done(void)
{
    s.owed[s.cur] = true;
    const uint32_t next = s.off[s.cur] + UPD_CHUNK;
    s.cur ^= 1u;
    s.off[s.cur] = next;
    s.fill = 0u;
}

uint8_t upd_transfer(uint8_t bsc, const uint8_t *d, uint32_t n)
{
    if (s.phase != UPD_DOWNLOAD) {
        return (s.phase == UPD_OFF) ? UPD_NRC_NOT_IN_SESSION : UDS_NRC_SEQUENCE;
    }
    if (s.flash_err) {
        return UDS_NRC_PROGRAMMING;
    }
    if ((s.received > 0u) && (bsc == (uint8_t)(s.bsc - 1u))) {
        return 0u; /* the last block again (its response was lost): acknowledged, not written twice */
    }
    if (bsc != s.bsc) {
        return UPD_NRC_WRONG_BSC;
    }
    if ((n == 0u) || (n > UPD_CHUNK)) {
        return UDS_NRC_LENGTH;
    }
    if (n > (s.size - s.received)) {
        return UPD_NRC_SUSPENDED;
    }
    if (((s.fill + n) >= UPD_CHUNK) && s.owed[s.cur ^ 1u]) {
        return UPD_NRC_BUSY; /* nothing consumed: the tester repeats the same block */
    }
    for (uint32_t i = 0u; i < n; i++) {
        s.buf[s.cur][s.fill] = d[i];
        s.fill++;
        if (s.fill == UPD_CHUNK) {
            chunk_done();
        }
    }
    s.received += n;
    s.bsc++;
    return 0u;
}

uint8_t upd_transfer_exit(void)
{
    if (s.phase != UPD_DOWNLOAD) {
        return (s.phase == UPD_OFF) ? UPD_NRC_NOT_IN_SESSION : UDS_NRC_SEQUENCE;
    }
    if (s.received != s.size) {
        return UDS_NRC_SEQUENCE;
    }
    if (s.fill > 0u) { /* the tail: padded erased, programmed as a whole chunk */
        (void)memset(&s.buf[s.cur][s.fill], 0xFF, UPD_CHUNK - s.fill);
        s.owed[s.cur] = true;
        s.fill = 0u;
    }
    s.phase = UPD_STAGED;
    return 0u;
}

uint8_t upd_verify_start(void)
{
    if (s.phase == UPD_OFF) {
        return UDS_NRC_OUT_OF_RANGE; /* no such routine outside the programming session */
    }
    if ((s.phase != UPD_STAGED) && (s.phase != UPD_VERIFIED) && (s.phase != UPD_FAILED)) {
        return UDS_NRC_SEQUENCE;
    }
    s.phase = UPD_VERIFYING;
    return 0u;
}

uint8_t upd_verify_result(uint8_t *status, uint8_t *err)
{
    if (s.phase == UPD_OFF) {
        return UDS_NRC_OUT_OF_RANGE;
    }
    *err = (uint8_t)s.vres;
    if (s.phase == UPD_VERIFYING) {
        *status = 0u;
    } else if ((s.phase == UPD_VERIFIED) || (s.phase == UPD_ACTIVATED)) {
        *status = 1u;
    } else if (s.phase == UPD_FAILED) {
        *status = 2u;
    } else {
        return UDS_NRC_SEQUENCE; /* nothing verified yet */
    }
    return 0u;
}

uint8_t upd_activate(void)
{
    if (s.phase == UPD_OFF) {
        return UDS_NRC_OUT_OF_RANGE;
    }
    if (s.phase != UPD_VERIFIED) {
        return UDS_NRC_SEQUENCE;
    }
    const uint8_t c = (s.cond != NULL) ? s.cond(s.ctx) : UDS_NRC_CONDITIONS;
    if (c != 0u) {
        return c;
    }
    boot_rec_t r;
    if (!boot_rec_read(&r) || (r.state != (uint8_t)BOOT_ST_IDLE)) {
        return UDS_NRC_CONDITIONS; /* a trial or a commit still pending: the next reset first */
    }
    r.state = (uint8_t)BOOT_ST_ACTIVATE;
    if (!boot_rec_queue(&r)) {
        return UDS_NRC_PROGRAMMING;
    }
    s.phase = UPD_ACTIVATED;
    return 0u;
}

uint8_t upd_reset(void)
{
    if (owed() || !nv_idle()) {
        return UPD_NRC_BUSY;
    }
    boot_rec_t r;
    if ((s.phase == UPD_ACTIVATED) && (!boot_rec_read(&r) || (r.state != (uint8_t)BOOT_ST_ACTIVATE))) {
        return UDS_NRC_PROGRAMMING; /* the request did not reach NVM: no reset into a silent no-op */
    }
    s.reset = true;
    s.reset_ms = hal_time_ms();
    return 0u;
}

void upd_service(void)
{
    for (uint32_t k = 0u; k < 2u; k++) { /* the owed chunks, in STAGE order; a page is erased at its first chunk */
        uint32_t i = 2u;
        for (uint32_t j = 0u; j < 2u; j++) {
            i = (s.owed[j] && (s.off[j] == s.prog)) ? j : i;
        }
        if (i == 2u) {
            break;
        }
        const bool ok = (((s.prog % HAL_FLASH_PAGE) != 0u) || hal_flash_erase(HAL_FLASH_STAGE, s.prog / HAL_FLASH_PAGE)) &&
                        hal_flash_program(HAL_FLASH_STAGE, s.prog, s.buf[i], UPD_CHUNK);
        s.flash_err = s.flash_err || !ok;
        s.owed[i] = false;
        s.prog += UPD_CHUNK;
    }
    if ((s.phase == UPD_VERIFYING) && !owed()) {
        boot_rec_t r;
        img_hdr_t h;
        if (s.flash_err) {
            s.vres = IMG_ERR_READ;
        } else if (!boot_rec_read(&r)) {
            s.vres = IMG_ERR_TARGET; /* no record: this card's target is unknown */
        } else {
            s.vres = img_verify(HAL_FLASH_STAGE, r.target, r.sec_counter, &h);
        }
        s.phase = (s.vres == IMG_OK) ? UPD_VERIFIED : UPD_FAILED;
    }
    const uint32_t now = hal_time_ms();
    if (s.reset && ti_elapsed(now, s.reset_ms, UPD_RESET_DELAY_MS)) {
        s.reset = false;
        hal_sys_reset();
    }
    if (s.trial && ti_elapsed(now, s.t0_ms, UPD_CONFIRM_MS)) {
        boot_rec_t r;
        s.trial = false;
        if (boot_rec_read(&r) && (r.state == (uint8_t)BOOT_ST_TRIAL)) {
            r.state = (uint8_t)BOOT_ST_CONFIRMED;
            s.trial = !boot_rec_queue(&r); /* a full queue: again next time */
        }
    }
}
