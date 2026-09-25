/* sim_fs26.c — FS26 fail-safe/main logic model sufficient for FW-12 and FW-16 (DS Rev.3):
 *  - 32-bit frames with CRC-8 both ways; a MOSI CRC error drops the frame;
 *  - INIT_FS: INIT registers writable with their NOT pairs; the first good WD answer closes INIT;
 *    REG_CORRUPT = pair mismatch; afterwards INIT registers are read-only;
 *  - challenger WD: window from FS_WDW_DURATION (closed/open duty), answer = f(token), error
 *    counter +2 bad / -1 good, limit from WD_ERR_LIMIT, reaction RSTB + FS0B; refresh counter
 *    decrements the fault error counter every WD_RFR_LIMIT good answers;
 *  - FS0B/FS1B asserted at POR; FS1B asserts with FS0B (TDELAY 0) for TDUR on later events;
 *    FS0B_REQ; release word from the token (Table 198) needing FLT_ERR_CNT = 0 for FS0B;
 *  - GPIO1 via M_REG_CTRL1/2; LPOFF via FS_LP_REQ 0xA5, 0x5A. The LFSR polynomial is not
 *    published; the model uses its own (the driver always reads the token). */
#include <string.h>

#include "fs26_regs.h"
#include "sim.h"
#include "ti_crc.h"

typedef struct {
    sim_fs26_cfg_t cfg;
    uint8_t state;
    uint16_t wd_cfg, not_wd_cfg, fssm, not_fssm, wdw, not_wdw, ios2;
    uint16_t token;
    uint8_t wd_err, wd_rfr, flt_err;
    uint64_t win_start, init_start;
    bool fs0b, fs1b, fs1b_timed;
    uint64_t fs1b_end;
    bool rstb_event, gpio1, lpoff, reg_corrupt;
    uint8_t lp_step;
    uint32_t crc_err;
    bool corrupt_next;
    uint16_t amux;  /* M_AMUX_CTRL */
    float vsup_v;   /* KL30 at the VSUP pin */
} fs26_t;

static fs26_t F;
static const sim_fs26_cfg_t CFG_DEFAULT = {.prog_id = 0x4A21u, .device_id = 0x2600u};
static sim_fs26_cfg_t s_cfg = {.prog_id = 0x4A21u, .device_id = 0x2600u};

static uint16_t not_of(uint16_t v, uint16_t mask) { return (uint16_t)(~v & mask); }

/* The AMUX pin (Tables 55/56, 130): the selected input over the divider; disabled or another channel: 0 V (the
 * model shows VSUP only, the one channel the firmware selects). */
static void amux_out(void)
{
    const bool vsup = ((F.amux & (1u << 6)) != 0u) && ((F.amux & 0x1Fu) == 0x11u);
    const float ratio = ((F.amux & (1u << 5)) != 0u) ? 14.0f : 7.5f;
    sim_adc_set_v(HAL_ADC_SBC_AMUX, vsup ? (F.vsup_v / ratio) : 0.0f);
}

void sim_fs26_vsup(float v)
{
    F.vsup_v = v;
    amux_out();
}
static void assert_fs0b_event(void);

void sim_fs26_reset(void)
{
    (void)memset(&F, 0, sizeof F);
    F.cfg = s_cfg;
    F.state = FS26_STATE_INIT_FS;
    F.wd_cfg = 0x4200u;
    F.fssm = 0x50C0u;
    F.wdw = 0x308Bu;
    F.ios2 = 0x000Bu;
    F.not_wd_cfg = not_of(F.wd_cfg, FS26_WD_CFG_WMASK);
    F.not_fssm = (uint16_t)(not_of(F.fssm, FS26_FSSM_WMASK) | FS26_FSSM_NOT_EXTRA);
    F.not_wdw = not_of(F.wdw, FS26_WDW_WMASK);
    F.token = FS26_WD_TOKEN_DEFAULT;
    F.flt_err = 1u;
    F.fs0b = true;
    F.fs1b = true;
    F.gpio1 = F.cfg.gpio1_slotted;
    F.init_start = sim_now_ns();
    F.vsup_v = 13.5f;
    amux_out();
}

/* The FS26 watchdog reaction reset the MCU (RSTB + FS0B): the FS26 itself keeps its INIT
 * configuration, its window timing and FS_GPIO1; FS0B/FS1B are asserted. */
void sim_fs26_mcu_reset(void)
{
    sim_mcu_reset(); /* RSTB resets the MCU: its peripherals come back unlocked, at reset values */
    assert_fs0b_event();
    F.fs1b_timed = false; /* held until the MCU releases it */
    F.flt_err = (uint8_t)((F.flt_err < 12u) ? (F.flt_err + 1u) : 12u);
    F.rstb_event = true;
    F.state = FS26_STATE_SAFETY_OUT_NOT_RELEASED;
    F.win_start = sim_now_ns();
}

void sim_fs26_config(const sim_fs26_cfg_t *c)
{
    s_cfg = (c != NULL) ? *c : CFG_DEFAULT;
    F.cfg = s_cfg;
}

static uint8_t err_limit(void)
{
    static const uint8_t L[4] = {8u, 6u, 4u, 2u};
    return L[(F.wd_cfg >> FS26_WD_ERR_LIMIT_SHIFT) & 3u];
}

static uint8_t rfr_limit(void)
{
    static const uint8_t L[4] = {6u, 4u, 2u, 1u};
    return L[(F.wd_cfg >> FS26_WD_RFR_LIMIT_SHIFT) & 3u];
}

static uint64_t period_ns(void)
{
    static const uint16_t MS[16] = {0u, 1u, 2u, 3u, 4u, 6u, 8u, 12u, 16u, 24u, 32u, 64u, 128u, 256u, 512u, 1024u};
    const double ns = (double)MS[(F.wdw >> FS26_WDW_PERIOD_SHIFT) & 0xFu] * 1.0e6 / (1.0 + (double)F.cfg.osc_error);
    return (uint64_t)ns; /* timed by the fail-safe oscillator (round 17: its tolerance, sim_fs26_cfg_t) */
}

static uint64_t closed_ns(void)
{
    static const uint16_t PERMILLE[8] = {312u, 375u, 500u, 625u, 687u, 750u, 812u, 500u};
    return period_ns() * PERMILLE[(F.wdw >> FS26_WDW_DC_SHIFT) & 7u] / 1000u;
}

static void assert_fs0b_event(void)
{
    F.fs0b = true;
    F.fs1b = true; /* FS1B_TDELAY 0: with FS0B */
    F.fs1b_timed = true;
    F.fs1b_end = sim_now_ns() + 100000000u; /* FS1B_TDUR = 100 ms (the only setting FW-12 allows) */
    if (F.state == FS26_STATE_NORMAL) {
        F.state = FS26_STATE_SAFETY_OUT_NOT_RELEASED;
    }
}

static void wd_bad(void)
{
    F.wd_err = (uint8_t)((F.wd_err + 2u > 12u) ? 12u : (F.wd_err + 2u));
    F.win_start = sim_now_ns();
    if (F.wd_err >= err_limit()) {
        const uint8_t reaction = (uint8_t)((F.wd_cfg >> FS26_WD_FS_REACTION_SHIFT) & 3u);
        if (reaction >= 1u) {
            assert_fs0b_event();
            F.flt_err = (uint8_t)((F.flt_err < 12u) ? (F.flt_err + 1u) : 12u);
        }
        if (reaction >= 2u) {
            F.rstb_event = true;
        }
    }
}

static void wd_good(void)
{
    F.wd_err = (F.wd_err > 0u) ? (uint8_t)(F.wd_err - 1u) : 0u;
    F.wd_rfr++;
    if (F.wd_rfr >= rfr_limit()) {
        F.wd_rfr = 0u;
        F.flt_err = (F.flt_err > 0u) ? (uint8_t)(F.flt_err - 1u) : 0u;
    }
    const uint16_t lsb = (uint16_t)(F.token & 1u);
    F.token = (uint16_t)(F.token >> 1);
    if (lsb != 0u) {
        F.token ^= 0xB400u;
    }
    if (F.token == 0u) {
        F.token = FS26_WD_TOKEN_DEFAULT;
    }
    F.win_start = sim_now_ns();
}

static bool pairs_ok(void)
{
    const bool wd = ((F.wd_cfg ^ F.not_wd_cfg) & FS26_WD_CFG_WMASK) == FS26_WD_CFG_WMASK;
    const bool fssm = (((F.fssm ^ F.not_fssm) & FS26_FSSM_WMASK) == FS26_FSSM_WMASK) &&
                      ((F.not_fssm & FS26_FSSM_NOT_EXTRA) != 0u);
    const bool wdw = ((F.wdw ^ F.not_wdw) & FS26_WDW_WMASK) == FS26_WDW_WMASK;
    return wd && fssm && wdw;
}

static void answer(uint16_t a)
{
    const bool value_ok = (a == fs26_wd_answer(F.token));
    if (F.state == FS26_STATE_INIT_FS) {
        if (value_ok) {
            F.reg_corrupt = !pairs_ok();
            wd_good();
            F.state = F.fs0b ? FS26_STATE_SAFETY_OUT_NOT_RELEASED : FS26_STATE_NORMAL;
        } else {
            wd_bad();
        }
        return;
    }
    if (((sim_now_ns() - F.win_start) < closed_ns()) || !value_ok) {
        wd_bad();
    } else {
        wd_good();
    }
}

static void release(uint16_t w)
{
    const uint8_t sel = (uint8_t)(w >> 13);
    if (w != fs26_release_word(F.token, sel)) {
        return;
    }
    const bool fs0b_ok = (F.flt_err == 0u) && (F.wd_err == 0u);
    if (((sel == FS26_REL_FS0B) || (sel == FS26_REL_BOTH)) && fs0b_ok) {
        F.fs0b = false;
    }
    if ((sel == FS26_REL_FS1B) || (sel == FS26_REL_BOTH)) {
        F.fs1b = false;
        F.fs1b_timed = false;
    }
    if (!F.fs0b && (F.state == FS26_STATE_SAFETY_OUT_NOT_RELEASED)) {
        F.state = FS26_STATE_NORMAL;
    }
}

static void write_reg(uint8_t a, uint16_t d)
{
    const bool init = (F.state == FS26_STATE_INIT_FS);
    switch (a) {
    case FS26_FS_I_WD_CFG: if (init) { F.wd_cfg = (uint16_t)(d & FS26_WD_CFG_WMASK); } break;
    case FS26_FS_I_NOT_WD_CFG: if (init) { F.not_wd_cfg = d; } break;
    case FS26_FS_I_FSSM: if (init) { F.fssm = (uint16_t)(d & FS26_FSSM_WMASK); } break;
    case FS26_FS_I_NOT_FSSM: if (init) { F.not_fssm = d; } break;
    case FS26_FS_WDW_DURATION: if (init) { F.wdw = (uint16_t)(d & FS26_WDW_WMASK); } break;
    case FS26_FS_NOT_WDW_DURATION: if (init) { F.not_wdw = d; } break;
    case FS26_FS_SAFE_IOS_2: F.ios2 = (uint16_t)(d & FS26_IOS2_WMASK); break;
    case FS26_FS_WD_ANSWER: answer(d); break;
    case FS26_FS_RELEASE_FS0B_FS1B: release(d); break;
    case FS26_FS_SAFE_IOS_1:
        if ((d & FS26_IOS1_FS0B_REQ) != 0u) {
            assert_fs0b_event();
        }
        if ((d & FS26_IOS1_FS1B_REQ) != 0u) {
            F.fs1b = true;
            F.fs1b_timed = true;
            F.fs1b_end = sim_now_ns() + 100000000u;
        }
        if ((d & FS26_IOS1_RSTB_EVENT) != 0u) {
            F.rstb_event = false;
        }
        break;
    case FS26_M_REG_CTRL1: if ((d & FS26_GPIO1_BIT) != 0u) { F.gpio1 = true; } break;
    case FS26_M_REG_CTRL2: if ((d & FS26_GPIO1_BIT) != 0u) { F.gpio1 = false; } break;
    case FS26_M_AMUX_CTRL:
        F.amux = (uint16_t)(d & 0x7Fu);
        amux_out();
        break;
    case FS26_FS_LP_REQ:
        if (d == FS26_LP_PRE_LPOFF) {
            F.lp_step = 1u;
        } else if ((d == FS26_LP_GO_LPOFF) && (F.lp_step == 1u)) {
            F.lpoff = true;
        } else {
            F.lp_step = 0u;
        }
        break;
    default: break;
    }
}

static uint16_t read_reg(uint8_t a)
{
    const bool fs1b_pin_low = F.fs1b && !F.cfg.fs1b_short_high;
    switch (a) {
    case FS26_M_DEVICEID: return F.cfg.device_id;
    case FS26_M_PROGID: return F.cfg.prog_id;
    case FS26_M_AMUX_CTRL: return F.amux;
    case FS26_FS_I_WD_CFG: return (uint16_t)((F.wd_cfg & FS26_WD_CFG_WMASK) | (uint16_t)(F.wd_rfr << 4) | F.wd_err);
    case FS26_FS_I_NOT_WD_CFG: return F.not_wd_cfg;
    case FS26_FS_I_FSSM: return (uint16_t)((F.fssm & FS26_FSSM_WMASK) | F.flt_err);
    case FS26_FS_I_NOT_FSSM: return F.not_fssm;
    case FS26_FS_WDW_DURATION: return F.wdw;
    case FS26_FS_NOT_WDW_DURATION: return F.not_wdw;
    case FS26_FS_WD_TOKEN: return F.token;
    case FS26_FS_SAFE_IOS_2: return F.ios2;
    case FS26_FS_SAFE_IOS_1: {
        uint16_t v = 0u;
        v |= F.fs0b ? 0u : (FS26_IOS1_FS0B_DRV | FS26_IOS1_FS0B_SNS);
        v |= F.fs1b ? 0u : FS26_IOS1_FS1B_DRV;
        v |= fs1b_pin_low ? 0u : FS26_IOS1_FS1B_SNS;
        v |= (F.fs1b && F.cfg.fs1b_short_high) ? FS26_IOS1_FS1B_DIAG : 0u;
        v |= F.rstb_event ? FS26_IOS1_RSTB_EVENT : 0u;
        return v;
    }
    case FS26_FS_STATES: {
        uint16_t v = F.state;
        v |= F.cfg.dbg_mode ? FS26_STATES_DBG_MODE : 0u;
        v |= F.cfg.otp_corrupt ? FS26_STATES_OTP_CORRUPT : 0u;
        v |= F.reg_corrupt ? FS26_STATES_REG_CORRUPT : 0u;
        return v;
    }
    default: return 0u;
    }
}

bool sim_fs26_xfer(uint32_t tx, uint32_t *rx)
{
    uint16_t data = 0u;
    if (F.lpoff) {
        *rx = 0u;
        return true;
    }
    if (!fs26_frame_crc_ok(tx)) {
        F.crc_err++;
    } else {
        const uint8_t a = (uint8_t)(tx >> 25);
        const bool wr = ((tx >> 24) & 1u) != 0u;
        data = read_reg(a); /* write responses carry the previous content */
        if (wr) {
            write_reg(a, (uint16_t)(tx >> 8));
        }
    }
    const uint8_t status = (F.state == FS26_STATE_INIT_FS) ? 0x00u : 0x40u;
    uint32_t r = fs26_frame(0u, false, data);
    r = (r & 0x00FFFF00u) | ((uint32_t)status << 24);
    /* re-CRC over the response bits 31..8 */
    const uint8_t b[3] = {(uint8_t)(r >> 24), (uint8_t)(r >> 16), (uint8_t)(r >> 8)};
    r |= ti_crc8_1d(b, 3u, 0xFFu, 0x00u);
    if (F.corrupt_next) {
        F.corrupt_next = false;
        r ^= 0x00010000u;
    }
    *rx = r;
    sim_chain_eval();
    return true;
}

void sim_fs26_eval(void)
{
    const uint64_t now = sim_now_ns();
    if (F.lpoff) {
        return;
    }
    if (F.fs1b_timed && (now >= F.fs1b_end)) {
        F.fs1b = false;
        F.fs1b_timed = false;
    }
    if (F.state == FS26_STATE_INIT_FS) {
        if ((now - F.init_start) >= 256000000u) { /* INIT_FS closes by timeout = WD error */
            F.init_start = now;
            wd_bad();
        }
    } else if ((period_ns() > 0u) && ((now - F.win_start) >= period_ns())) {
        wd_bad(); /* no answer in the window */
    } else {
        /* running */
    }
}

uint64_t sim_fs26_next_event_ns(void)
{
    uint64_t t = UINT64_MAX;
    if (F.lpoff) {
        return t;
    }
    if (F.fs1b_timed) {
        t = F.fs1b_end;
    }
    const uint64_t w = (F.state == FS26_STATE_INIT_FS) ? (F.init_start + 256000000u)
                                                        : ((period_ns() > 0u) ? (F.win_start + period_ns()) : UINT64_MAX);
    return (w < t) ? w : t;
}

bool sim_fs26_fs0b_asserted(void) { return F.fs0b && !F.lpoff; }
bool sim_fs26_fs1b_asserted(void) { return F.fs1b; }
bool sim_fs26_fs1b_pin_low(void) { return F.fs1b && !F.cfg.fs1b_short_high; }
bool sim_fs26_gpio1(void) { return F.gpio1 && !F.lpoff; }
bool sim_fs26_rstb_event(void) { return F.rstb_event; }
bool sim_fs26_lpoff(void) { return F.lpoff; }
uint8_t sim_fs26_wd_err_cnt(void) { return F.wd_err; }
uint8_t sim_fs26_state(void) { return F.state; }
uint32_t sim_fs26_crc_errors(void) { return F.crc_err; }
void sim_fs26_corrupt_next_miso(void) { F.corrupt_next = true; }
