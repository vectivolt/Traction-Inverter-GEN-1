/* fs26.c — FW-12 driver. */
#include "fs26.h"

#include "spi_fs26.h"
#include "ti_crc.h"
#include "timer.h"

#define WD_REFRESH_US 2000u /* window 3 ms, 50 % closed: open 1.5–3.0 ms after the last answer */

/* ---------------- protocol helpers (also used by the host device model) ---------------- */
static uint8_t crc_of(uint32_t frame)
{
    const uint8_t b[3] = {(uint8_t)(frame >> 24), (uint8_t)(frame >> 16), (uint8_t)(frame >> 8)};
    return ti_crc8_1d(b, 3u, 0xFFu, 0x00u);
}

uint32_t fs26_frame(uint8_t hex_addr, bool write, uint16_t data)
{
    const uint32_t f = ((uint32_t)(hex_addr & 0x7Fu) << 25) | ((write ? 1u : 0u) << 24) | ((uint32_t)data << 8);
    return f | crc_of(f);
}

bool fs26_frame_crc_ok(uint32_t frame) { return crc_of(frame) == (uint8_t)(frame & 0xFFu); }

/* §22.6.1: ANSWER = NOT(((TOKEN x 4) + 6) - 4) / 4, 32-bit intermediate as NXP's drivers compute it */
uint16_t fs26_wd_answer(uint16_t token)
{
    uint32_t mr = token;
    mr *= 4u;
    mr += 6u;
    mr -= 4u;
    mr = ~mr;
    mr /= 4u;
    return (uint16_t)mr;
}

static uint16_t bitrev16(uint16_t x)
{
    uint16_t r = 0u;
    for (uint32_t i = 0u; i < 16u; i++) {
        r = (uint16_t)((uint16_t)(r << 1) | (uint16_t)((x >> i) & 1u));
    }
    return r;
}

/* Table 198: mirror the 16 token bits, complement, keep [12:0]; [15:13] select the outputs */
uint16_t fs26_release_word(uint16_t token, uint8_t select)
{
    return (uint16_t)(((uint16_t)(select & 7u) << 13) | ((uint16_t)~bitrev16(token) & 0x1FFFu));
}

uint16_t fs26_wd_cfg_value(void)
{
    return (uint16_t)((FS26_WD_ERR_LIMIT_2 << FS26_WD_ERR_LIMIT_SHIFT) | (0u << FS26_WD_RFR_LIMIT_SHIFT) |
                      (FS26_WD_FS_REACTION_RSTB_FS0B << FS26_WD_FS_REACTION_SHIFT));
}

uint16_t fs26_fssm_value(void)
{
    /* defaults kept (FLT_ERR_CNT_LIMIT 6, reaction RSTB+FS0B, RSTB 10 ms, 8 s timer on);
     * BACKUP_SAFETY_PATH_FS0B = 1, BACKUP_SAFETY_PATH_FS1B = 0 (round 9, A8-03) */
    return (uint16_t)((1u << FS26_FLT_ERR_CNT_LIMIT_SHIFT) | (2u << FS26_FLT_ERR_REACTION_SHIFT) | FS26_BACKUP_FS0B_BIT);
}

uint16_t fs26_wdw_value(void)
{
    return (uint16_t)((FS26_WDW_PERIOD_3MS << FS26_WDW_PERIOD_SHIFT) | (FS26_WDW_DC_50 << FS26_WDW_DC_SHIFT) |
                      FS26_WDW_RECOVERY_DEFAULT);
}

uint16_t fs26_ios2_value(void) { return (uint16_t)((0u << FS26_IOS2_TDELAY_SHIFT) | FS26_IOS2_TDUR_100MS); }

/* ---------------- transfers ---------------- */
static fs26_status_t xfer(fs26_t *f, uint8_t addr, bool wr, uint16_t data, uint16_t *rd)
{
    uint32_t rx = 0u;
    if (!hal_fs26_xfer(fs26_frame(addr, wr, data), &rx)) {
        f->n_comm_err++;
        return FS26_ERR_SPI;
    }
    if (!fs26_frame_crc_ok(rx)) {
        f->n_comm_err++;
        return FS26_ERR_CRC;
    }
    if (rd != NULL) {
        *rd = (uint16_t)(rx >> 8);
    }
    return FS26_OK;
}

fs26_status_t fs26_read(fs26_t *f, uint8_t addr, uint16_t *val) { return xfer(f, addr, false, 0u, val); }
fs26_status_t fs26_write(fs26_t *f, uint8_t addr, uint16_t val) { return xfer(f, addr, true, val, NULL); }

/* Read with one retry on a corrupted response (a CRC error never turns into a value). */
static fs26_status_t rd(fs26_t *f, uint8_t addr, uint16_t *val)
{
    fs26_status_t s = fs26_read(f, addr, val);
    if (s == FS26_ERR_CRC) {
        s = fs26_read(f, addr, val);
    }
    return s;
}

static fs26_status_t write_pair(fs26_t *f, uint8_t reg, uint8_t not_reg, uint16_t v, uint16_t mask, uint16_t extra)
{
    fs26_status_t s = fs26_write(f, reg, v);
    if (s == FS26_OK) {
        s = fs26_write(f, not_reg, (uint16_t)((uint16_t)(~v & mask) | extra));
    }
    return s;
}

static bool check(fs26_t *f, uint8_t addr, uint16_t mask, uint16_t expect)
{
    uint16_t v = 0u;
    return (rd(f, addr, &v) == FS26_OK) && ((v & mask) == (expect & mask));
}

static fs26_status_t fail(fs26_t *f, fs26_status_t s)
{
    f->last_err = s;
    return s;
}

fs26_status_t fs26_init(fs26_t *f, const ti_params_t *p)
{
    uint16_t v = 0u;
    *f = (fs26_t){0};
    if (rd(f, FS26_M_PROGID, &v) != FS26_OK) {
        return fail(f, FS26_ERR_SPI);
    }
    f->prog_id = v;
    if ((p->cal_fs26_prog_id == 0xFFFFu) || (v != p->cal_fs26_prog_id)) {
        return fail(f, FS26_ERR_PROGID);
    }
    if (rd(f, FS26_FS_STATES, &v) != FS26_OK) {
        return fail(f, FS26_ERR_SPI);
    }
    if ((v & FS26_STATES_DBG_MODE) != 0u) {
        return fail(f, FS26_ERR_DEBUG);
    }
    if ((v & FS26_STATES_OTP_CORRUPT) != 0u) {
        return fail(f, FS26_ERR_OTP);
    }
    if ((v & FS26_STATES_MASK) == FS26_STATE_INIT_FS) {
        fs26_status_t s = write_pair(f, FS26_FS_I_WD_CFG, FS26_FS_I_NOT_WD_CFG, fs26_wd_cfg_value(), FS26_WD_CFG_WMASK, 0u);
        s = (s == FS26_OK) ? write_pair(f, FS26_FS_I_FSSM, FS26_FS_I_NOT_FSSM, fs26_fssm_value(), FS26_FSSM_WMASK,
                                        FS26_FSSM_NOT_EXTRA) : s;
        s = (s == FS26_OK) ? write_pair(f, FS26_FS_WDW_DURATION, FS26_FS_NOT_WDW_DURATION, fs26_wdw_value(),
                                        FS26_WDW_WMASK, 0u) : s;
        s = (s == FS26_OK) ? fs26_write(f, FS26_FS_SAFE_IOS_2, fs26_ios2_value()) : s;
        if (s != FS26_OK) {
            return fail(f, s);
        }
    }
    /* readback: after an MCU-only reset the FS26 is past INIT_FS and these are read-only — they
     * must still hold FW-12's values */
    const bool ok = check(f, FS26_FS_I_WD_CFG, FS26_WD_CFG_WMASK, fs26_wd_cfg_value()) &&
                    check(f, FS26_FS_I_NOT_WD_CFG, FS26_WD_CFG_WMASK, (uint16_t)~fs26_wd_cfg_value()) &&
                    check(f, FS26_FS_I_FSSM, FS26_FSSM_WMASK, fs26_fssm_value()) &&
                    check(f, FS26_FS_I_NOT_FSSM, (uint16_t)(FS26_FSSM_WMASK | FS26_FSSM_NOT_EXTRA),
                          (uint16_t)((uint16_t)~fs26_fssm_value() | FS26_FSSM_NOT_EXTRA)) &&
                    check(f, FS26_FS_WDW_DURATION, FS26_WDW_WMASK, fs26_wdw_value()) &&
                    check(f, FS26_FS_NOT_WDW_DURATION, FS26_WDW_WMASK, (uint16_t)~fs26_wdw_value()) &&
                    check(f, FS26_FS_SAFE_IOS_2, FS26_IOS2_WMASK, fs26_ios2_value());
    if (!ok) {
        return fail(f, FS26_ERR_READBACK);
    }
    f->wd_running = true;
    if (fs26_wd_refresh(f) != FS26_OK) { /* closes INIT_FS */
        return fail(f, FS26_ERR_SPI);
    }
    if ((rd(f, FS26_FS_STATES, &v) != FS26_OK) || ((v & FS26_STATES_REG_CORRUPT) != 0u)) {
        return fail(f, FS26_ERR_READBACK);
    }
    if (rd(f, FS26_FS_SAFE_IOS_1, &v) == FS26_OK) {
        f->fs1b_short_high = (v & FS26_IOS1_FS1B_DIAG) != 0u;
    }
    f->init_done = true;
    return FS26_OK;
}

bool fs26_wd_due(const fs26_t *f, uint32_t now_us)
{
    return f->wd_running && ti_elapsed(now_us, f->last_refresh_us, WD_REFRESH_US);
}

fs26_status_t fs26_wd_refresh(fs26_t *f)
{
    uint16_t token = 0u;
    fs26_status_t s = rd(f, FS26_FS_WD_TOKEN, &token);
    if (s == FS26_OK) {
        s = fs26_write(f, FS26_FS_WD_ANSWER, fs26_wd_answer(token));
    }
    if (s == FS26_OK) {
        f->last_refresh_us = hal_time_us();
        f->n_refresh++;
        uint16_t cfg = 0u;
        if (rd(f, FS26_FS_I_WD_CFG, &cfg) == FS26_OK) {
            f->wd_err_cnt = (uint8_t)(cfg & FS26_WD_ERR_CNT_MASK);
        }
    }
    return s;
}

fs26_status_t fs26_release_safety_outputs(fs26_t *f)
{
    uint16_t v = 0u;
    if (rd(f, FS26_FS_I_FSSM, &v) != FS26_OK) {
        return FS26_ERR_SPI;
    }
    if ((v & FS26_FLT_ERR_CNT_MASK) != 0u) {
        return FS26_BUSY; /* the fault error counter decrements with good refreshes */
    }
    uint16_t token = 0u;
    if (rd(f, FS26_FS_WD_TOKEN, &token) != FS26_OK) {
        return FS26_ERR_SPI;
    }
    if (fs26_write(f, FS26_FS_RELEASE_FS0B_FS1B, fs26_release_word(token, FS26_REL_BOTH)) != FS26_OK) {
        return FS26_ERR_SPI;
    }
    return fs26_outputs_released(f) ? FS26_OK : FS26_ERR_RELEASE;
}

bool fs26_outputs_released(fs26_t *f)
{
    uint16_t v = 0u;
    if (rd(f, FS26_FS_SAFE_IOS_1, &v) != FS26_OK) {
        return false;
    }
    f->fs1b_short_high = (v & FS26_IOS1_FS1B_DIAG) != 0u;
    return ((v & FS26_IOS1_FS0B_SNS) != 0u) && ((v & FS26_IOS1_FS1B_DRV) != 0u);
}

fs26_status_t fs26_request_fs0b(fs26_t *f) { return fs26_write(f, FS26_FS_SAFE_IOS_1, FS26_IOS1_FS0B_REQ); }

fs26_status_t fs26_set_gpio1(fs26_t *f, bool high)
{
    return fs26_write(f, high ? FS26_M_REG_CTRL1 : FS26_M_REG_CTRL2, FS26_GPIO1_BIT);
}

fs26_status_t fs26_goto_lpoff(fs26_t *f)
{
    fs26_status_t s = fs26_write(f, FS26_FS_LP_REQ, FS26_LP_PRE_LPOFF);
    if (s == FS26_OK) {
        s = fs26_write(f, FS26_FS_LP_REQ, FS26_LP_GO_LPOFF);
    }
    f->wd_running = false;
    return s;
}
