/* boot.c — see boot.h. */
#include "boot.h"

#include <string.h>

#include "nvlog.h"
#include "verify.h"

#define STEPS_MAX 4u    /* every path returns within two transitions */
#define NV_POLLS 1000u  /* nv_service() calls a record write may take */
#define COPY_CHUNK 256u

bool boot_rec_read(boot_rec_t *r)
{
    return nv_read(NV_REC_BOOT, r, (uint16_t)sizeof *r) && (r->magic == BOOT_REC_MAGIC) &&
           (r->layout == BOOT_REC_LAYOUT) && (r->state < (uint8_t)BOOT_ST_COUNT) && (r->lkg_valid <= 1u) &&
           (r->target > (uint32_t)TI_SKU_NONE) && (r->target < (uint32_t)TI_SKU_COUNT);
}

bool boot_rec_queue(boot_rec_t *r)
{
    r->magic = BOOT_REC_MAGIC;
    r->layout = BOOT_REC_LAYOUT;
    r->pad = 0u;
    return nv_queue(NV_REC_BOOT, r, (uint16_t)sizeof *r);
}

void boot_rec_make(boot_rec_t *r, uint32_t target, uint32_t sec_counter)
{
    (void)memset(r, 0, sizeof *r);
    r->state = (uint8_t)BOOT_ST_IDLE;
    r->target = target;
    r->sec_counter = sec_counter;
    r->magic = BOOT_REC_MAGIC;
    r->layout = BOOT_REC_LAYOUT;
}

bool boot_last_failed(uint8_t last) { return (last >= (uint8_t)BOOT_LAST_REJECTED) && (last <= (uint8_t)BOOT_LAST_EXEC_INVALID); }

/* The bootloader's synchronous write: queued, drained, read back. */
static bool commit(boot_rec_t *r)
{
    if (!boot_rec_queue(r)) {
        return false;
    }
    for (uint32_t k = 0u; (k < NV_POLLS) && !nv_idle(); k++) {
        nv_service();
    }
    boot_rec_t b;
    return nv_idle() && boot_rec_read(&b) && (memcmp(&b, r, sizeof b) == 0);
}

/* dst <- src: the whole pages holding len bytes; erased (all 0xFF) chunks are left erased. */
static bool copy(hal_flash_region_t dst, hal_flash_region_t src, uint32_t len)
{
    uint8_t buf[COPY_CHUNK];
    const uint32_t pages = (len + HAL_FLASH_PAGE - 1u) / HAL_FLASH_PAGE; /* len <= HAL_FLASH_REGION_SIZE */
    for (uint32_t p = 0u; p < pages; p++) {
        if (!hal_flash_erase(dst, p)) {
            return false;
        }
        for (uint32_t off = p * HAL_FLASH_PAGE; off < ((p + 1u) * HAL_FLASH_PAGE); off += COPY_CHUNK) {
            if (!hal_flash_read(src, off, buf, COPY_CHUNK)) {
                return false;
            }
            uint8_t all = 0xFFu;
            for (uint32_t i = 0u; i < COPY_CHUNK; i++) {
                all &= buf[i];
            }
            if ((all != 0xFFu) && !hal_flash_program(dst, off, buf, COPY_CHUNK)) {
                return false;
            }
        }
    }
    return true;
}

static boot_result_t done(boot_result_t res, const boot_rec_t *r, boot_rec_t *out)
{
    *out = *r;
    return res;
}

/* No record: adopt EXEC if it verifies under its own target (the EOL-programmed image is the trust anchor). */
static bool adopt(boot_rec_t *r)
{
    uint8_t raw[IMG_HDR_LEN];
    img_hdr_t h;
    if (!hal_flash_read(HAL_FLASH_EXEC, 0u, raw, IMG_HDR_LEN) || (img_parse(raw, &h) != IMG_OK) ||
        (img_verify(HAL_FLASH_EXEC, h.target, 0u, &h) != IMG_OK)) {
        return false;
    }
    boot_rec_make(r, h.target, h.sec_ver);
    return true;
}

boot_result_t boot_decide(boot_rec_t *out)
{
    boot_rec_t r;
    img_hdr_t h;
    if (!boot_rec_read(&r)) {
        if (!adopt(&r)) {
            (void)memset(&r, 0, sizeof r);
            return done(BOOT_NO_IMAGE, &r, out);
        }
        if (!commit(&r)) {
            return done(BOOT_HALT, &r, out);
        }
    }
    for (uint32_t step = 0u; step < STEPS_MAX; step++) {
        img_result_t v;
        switch ((boot_state_t)r.state) {
        case BOOT_ST_ACTIVATE:
            v = img_verify(HAL_FLASH_STAGE, r.target, r.sec_counter, &h);
            if ((v != IMG_OK) || (r.lkg_valid == 0u)) {
                r.state = (uint8_t)BOOT_ST_IDLE; /* refused: EXEC untouched */
                r.last = (uint8_t)((v != IMG_OK) ? BOOT_LAST_REJECTED : BOOT_LAST_NO_FALLBACK);
                r.last_err = (uint8_t)v;
                break;
            }
            r.state = (uint8_t)BOOT_ST_INSTALL; /* written before the copy starts */
            if (!commit(&r) || !copy(HAL_FLASH_EXEC, HAL_FLASH_STAGE, IMG_HDR_LEN + h.length)) {
                return done(BOOT_HALT, &r, out);
            }
            v = img_verify(HAL_FLASH_EXEC, r.target, r.sec_counter, &h);
            if (v != IMG_OK) {
                r.state = (uint8_t)BOOT_ST_RESTORE;
                r.last = (uint8_t)BOOT_LAST_EXEC_INVALID;
                r.last_err = (uint8_t)v;
                break;
            }
            r.state = (uint8_t)BOOT_ST_TRIAL;
            r.last = (uint8_t)BOOT_LAST_INSTALLED;
            r.last_err = (uint8_t)IMG_OK;
            return done(commit(&r) ? BOOT_RUN : BOOT_HALT, &r, out);
        case BOOT_ST_INSTALL: /* a power loss during the copy: EXEC is partial */
            r.state = (uint8_t)BOOT_ST_RESTORE;
            r.last = (uint8_t)BOOT_LAST_ABANDONED;
            break;
        case BOOT_ST_TRIAL: /* the new image did not confirm itself before this reset */
            r.state = (uint8_t)BOOT_ST_RESTORE;
            r.last = (uint8_t)BOOT_LAST_FIRST_BOOT_FAILED;
            break;
        case BOOT_ST_CONFIRMED:
        case BOOT_ST_COMMIT:
            v = img_verify(HAL_FLASH_EXEC, r.target, r.sec_counter, &h);
            if (v != IMG_OK) {
                r.state = (uint8_t)BOOT_ST_RESTORE;
                r.last = (uint8_t)BOOT_LAST_EXEC_INVALID;
                r.last_err = (uint8_t)v;
                break;
            }
            if (r.state == (uint8_t)BOOT_ST_CONFIRMED) {
                r.state = (uint8_t)BOOT_ST_COMMIT; /* the counter moves with the commit, before LKG is overwritten */
                r.lkg_valid = 0u;
                r.sec_counter = (h.sec_ver > r.sec_counter) ? h.sec_ver : r.sec_counter;
                break;
            }
            if (!copy(HAL_FLASH_LKG, HAL_FLASH_EXEC, IMG_HDR_LEN + h.length)) {
                return done(BOOT_HALT, &r, out);
            }
            r.lkg_valid = (uint8_t)((img_verify(HAL_FLASH_LKG, r.target, r.sec_counter, &h) == IMG_OK) ? 1u : 0u);
            r.state = (uint8_t)BOOT_ST_IDLE; /* a failed copy runs EXEC without a fallback; the next reset retries */
            return done(commit(&r) ? BOOT_RUN : BOOT_HALT, &r, out);
        case BOOT_ST_RESTORE:
            if ((r.lkg_valid == 0u) || (img_verify(HAL_FLASH_LKG, r.target, r.sec_counter, &h) != IMG_OK)) {
                return done(BOOT_NO_IMAGE, &r, out); /* nothing left to fall back to */
            }
            if (!copy(HAL_FLASH_EXEC, HAL_FLASH_LKG, IMG_HDR_LEN + h.length)) {
                return done(BOOT_HALT, &r, out);
            }
            if (img_verify(HAL_FLASH_EXEC, r.target, r.sec_counter, &h) != IMG_OK) {
                return done(BOOT_NO_IMAGE, &r, out); /* a flash fault: the copy does not read back */
            }
            r.state = (uint8_t)BOOT_ST_IDLE;
            return done(commit(&r) ? BOOT_RUN : BOOT_HALT, &r, out);
        case BOOT_ST_IDLE:
        default:
            v = img_verify(HAL_FLASH_EXEC, r.target, r.sec_counter, &h);
            if (v != IMG_OK) {
                r.state = (uint8_t)BOOT_ST_RESTORE;
                r.last = (uint8_t)BOOT_LAST_EXEC_INVALID;
                r.last_err = (uint8_t)v;
                break;
            }
            if (r.lkg_valid == 0u) {
                r.state = (uint8_t)BOOT_ST_COMMIT;
                break;
            }
            return done(BOOT_RUN, &r, out);
        }
        if (!commit(&r)) {
            return done(BOOT_HALT, &r, out);
        }
    }
    return done(BOOT_NO_IMAGE, &r, out);
}
