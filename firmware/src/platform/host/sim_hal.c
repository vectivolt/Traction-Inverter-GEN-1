/* sim_hal.c — host implementation of every HAL interface plus the simulation clock.
 * Nominal analog scalings mirror the card: HC5FW 2.5 V + 2.22 mV/A; V_DC pin = VOFS + V/455.84;
 * V5GD_SNS = V5GD/2; HW_ID = 5 V * R/(R + 10k); KL15 via a 0.3 V diode and 47k/10k; module NTC
 * (R + 100) against 5.1k; board NTC against 10k; PT1000 against 10k; HVIL 3.0/2.0/2.5 V. */
#include <math.h>
#include <string.h>

#include "can.h"
#include "fs26_regs.h"
#include "nvm.h"
#include "sdadc.h"
#include "sim.h"
#include "spi_fs26.h"
#include "swg.h"
#include "ti_crc.h"
#include "timer.h"
#include "wdog.h"

/* ================= clock and ISR ================= */
static uint64_t s_now;
static sim_isr_fn s_isr;
static bool s_isr_pending, s_in_isr;
static uint64_t s_isr_due;
static uint32_t s_isr_latency = 300u;

uint64_t sim_now_ns(void) { return s_now; }
void sim_set_fault_isr(sim_isr_fn fn) { s_isr = fn; }
void sim_set_isr_latency_ns(uint32_t ns) { s_isr_latency = ns; }

/* ================= ADC state ================= */
static uint16_t s_code[HAL_ADC_COUNT];
static bool s_frozen[HAL_ADC_COUNT];
static uint64_t s_frozen_t[HAL_ADC_COUNT];
static struct {
    bool en;
    uint16_t lo, hi;
} s_wd[HAL_ADC_COUNT];
static uint32_t s_wd_status;
static bool s_wd_level;

typedef enum { HVIL_M_CLOSED = 0, HVIL_M_OPEN, HVIL_M_SHORT_GND, HVIL_M_SHORT_BAT } hvil_mode_t;
static hvil_mode_t s_hvil;

static struct {
    bool active;
    float v0, slope_v_ns;
    uint64_t t0, period, next_sample, conv, lag, conv_due;
    bool conv_pending;
    uint16_t conv_code;
} s_ramp;

static uint16_t v_to_code(float v)
{
    float c = v * ((float)TI_ADC_MAX_CODE / TI_ADC_VREF_V);
    c = (c < 0.0f) ? 0.0f : ((c > (float)TI_ADC_MAX_CODE) ? (float)TI_ADC_MAX_CODE : c);
    return (uint16_t)(c + 0.5f);
}

static bool routed(hal_adc_sig_t s)
{
    return (s == HAL_ADC_ISNS_U) || (s == HAL_ADC_ISNS_V) || (s == HAL_ADC_ISNS_W) || (s == HAL_ADC_VDC1) ||
           (s == HAL_ADC_VDC2);
}

static void wd_eval(void)
{
    bool level = false;
    for (uint32_t i = 0u; i < (uint32_t)HAL_ADC_COUNT; i++) {
        if (s_wd[i].en && ((s_code[i] >= s_wd[i].hi) || ((s_wd[i].lo > 0u) && (s_code[i] <= s_wd[i].lo)))) {
            s_wd_status |= (1u << i);
            level = level || routed((hal_adc_sig_t)i);
        }
    }
    if (level != s_wd_level) {
        s_wd_level = level;
        sim_pwm_fault_inputs_changed();
    }
}

/* ================= PWM state ================= */
static struct {
    hal_pwm_mode_t mode;
    float duty[3];
    uint8_t fflag, fin;
    bool locked;
    uint64_t hs_off_ns, asc_ns;
    uint32_t nan_writes;
} P;

void sim_pwm_fault_inputs_changed(void)
{
    P.fin = (uint8_t)(sim_chain_pwm_fault_pins() | (s_wd_level ? HAL_PWM_FAULT_ADC_WD : 0u));
    const uint8_t newly = (uint8_t)(P.fin & (uint8_t)~P.fflag);
    if (newly != 0u) {
        P.fflag |= newly;
        P.hs_off_ns = s_now;
        if ((s_isr != NULL) && !s_isr_pending) {
            s_isr_pending = true;
            s_isr_due = s_now + s_isr_latency;
        }
    }
}

bool sim_pwm_hs_cmd(void) { return (P.mode == HAL_PWM_MOD) && ((P.fflag & HAL_PWM_FAULT_ALL) == 0u); }
bool sim_pwm_ls_cmd(void)
{
    return ((P.mode == HAL_PWM_ASC) || (P.mode == HAL_PWM_MOD)) &&
           ((P.fflag & (HAL_PWM_FAULT_FLT_HS | HAL_PWM_FAULT_FLT_LS)) == 0u);
}

/* ================= FW-06 ramp sampler ================= */
static float ramp_v(uint64_t t)
{
    const uint64_t seen = (t > s_ramp.lag) ? (t - s_ramp.lag) : 0u;
    return (seen <= s_ramp.t0) ? s_ramp.v0 : (s_ramp.v0 + s_ramp.slope_v_ns * (float)(seen - s_ramp.t0));
}

static void ramp_process(void)
{
    if (!s_ramp.active) {
        return;
    }
    if (s_ramp.conv_pending && (s_ramp.conv_due <= s_now)) {
        s_ramp.conv_pending = false;
        s_code[HAL_ADC_VDC1] = s_ramp.conv_code;
        s_code[HAL_ADC_VDC2] = s_ramp.conv_code;
        wd_eval();
    }
    if (s_ramp.next_sample <= s_now) {
        const float v = ramp_v(s_ramp.next_sample);
        s_ramp.conv_code = v_to_code(0.5f + v / ((6.0f * 470e3f + 6.2e3f) / 6.2e3f));
        s_ramp.conv_pending = true;
        s_ramp.conv_due = s_ramp.next_sample + s_ramp.conv;
        s_ramp.next_sample += s_ramp.period;
    }
}

void sim_vdc_ramp(float v0, float slope_v_per_us, uint32_t period_ns, uint32_t phase_ns, uint32_t conv_ns,
                  uint32_t analog_lag_ns)
{
    s_ramp.active = true;
    s_ramp.v0 = v0;
    s_ramp.slope_v_ns = slope_v_per_us / 1000.0f;
    s_ramp.t0 = s_now;
    s_ramp.period = period_ns;
    s_ramp.next_sample = s_now + phase_ns;
    s_ramp.conv = conv_ns;
    s_ramp.lag = analog_lag_ns;
    s_ramp.conv_pending = false;
}

uint64_t sim_vdc_ramp_crossing_ns(float v_link)
{
    return s_ramp.t0 + (uint64_t)((v_link - s_ramp.v0) / s_ramp.slope_v_ns);
}

/* ================= clock stepping ================= */
static uint64_t min_u64(uint64_t a, uint64_t b) { return (a < b) ? a : b; }

void sim_advance_ns(uint64_t ns)
{
    const uint64_t target = s_now + ns;
    for (uint32_t guard = 0u; guard < 2000000u; guard++) {
        uint64_t next = target;
        next = min_u64(next, sim_chain_next_event_ns());
        next = min_u64(next, sim_fs26_next_event_ns());
        if (s_ramp.active) {
            next = min_u64(next, s_ramp.next_sample);
            if (s_ramp.conv_pending) {
                next = min_u64(next, s_ramp.conv_due);
            }
        }
        if (s_isr_pending) {
            next = min_u64(next, s_isr_due);
        }
        if (next > s_now) {
            s_now = next;
        }
        sim_chain_eval();
        sim_fs26_eval();
        ramp_process();
        if (s_isr_pending && (s_isr_due <= s_now) && !s_in_isr) {
            s_isr_pending = false;
            s_in_isr = true;
            s_isr();
            s_in_isr = false;
        }
        if ((s_now >= target) && !(s_isr_pending && (s_isr_due <= s_now) && !s_in_isr)) {
            break;
        }
    }
}

void sim_advance_us(uint32_t us) { sim_advance_ns((uint64_t)us * 1000u); }

/* ================= timer HAL ================= */
uint32_t hal_time_us(void) { return (uint32_t)(s_now / 1000u); }
void hal_delay_us(uint32_t us) { sim_advance_ns((uint64_t)us * 1000u); }
void hal_crit_enter(void) {}
void hal_crit_exit(void) {}

/* ================= GPIO HAL + edge log ================= */
#define EDGE_LOG 8192u
static bool s_out[HAL_DO_COUNT];
static struct {
    uint8_t pin;
    bool level;
    uint64_t t;
} s_edges[EDGE_LOG];
static uint32_t s_n_edges;

void hal_gpio_write(hal_do_t pin, bool level)
{
    if (pin >= HAL_DO_COUNT) {
        return;
    }
    if (s_out[pin] != level) {
        const uint32_t i = s_n_edges % EDGE_LOG;
        s_edges[i].pin = (uint8_t)pin;
        s_edges[i].level = level;
        s_edges[i].t = s_now;
        s_n_edges++;
    }
    s_out[pin] = level;
    sim_chain_on_output(pin, level);
}

bool hal_gpio_out_state(hal_do_t pin) { return (pin < HAL_DO_COUNT) ? s_out[pin] : false; }
bool hal_gpio_read(hal_di_t pin) { return sim_chain_read(pin); }

bool hal_gpio_flt_pad_drive_low(hal_di_t pin, bool enable)
{
    if ((pin != HAL_DI_FLT_HS_N) && (pin != HAL_DI_FLT_LS_N)) {
        return false;
    }
    sim_chain_pad(pin, enable);
    return true;
}

uint64_t sim_gpio_edge_ns(hal_do_t pin, bool rising, uint64_t t_from_ns)
{
    const uint32_t n = (s_n_edges < EDGE_LOG) ? s_n_edges : EDGE_LOG;
    const uint32_t first = s_n_edges - n;
    for (uint32_t k = first; k < s_n_edges; k++) {
        const uint32_t i = k % EDGE_LOG;
        if ((s_edges[i].pin == (uint8_t)pin) && (s_edges[i].level == rising) && (s_edges[i].t >= t_from_ns)) {
            return s_edges[i].t;
        }
    }
    return UINT64_MAX;
}

uint32_t sim_gpio_edge_count(hal_do_t pin, bool rising)
{
    uint32_t c = 0u;
    const uint32_t n = (s_n_edges < EDGE_LOG) ? s_n_edges : EDGE_LOG;
    for (uint32_t k = s_n_edges - n; k < s_n_edges; k++) {
        const uint32_t i = k % EDGE_LOG;
        c += ((s_edges[i].pin == (uint8_t)pin) && (s_edges[i].level == rising)) ? 1u : 0u;
    }
    return c;
}

/* ================= PWM HAL ================= */
bool hal_pwm_init(uint32_t fsw_hz, uint32_t dead_time_ns)
{
    (void)fsw_hz;
    (void)dead_time_ns;
    P.mode = HAL_PWM_OFF;
    P.locked = true;
    return true;
}

bool hal_pwm_config_locked(void) { return P.locked; }

void hal_pwm_force_off(void)
{
    if (P.mode == HAL_PWM_MOD) {
        P.hs_off_ns = s_now;
    }
    P.mode = HAL_PWM_OFF;
    sim_chain_eval();
}

void hal_pwm_set_asc(void)
{
    if (P.mode == HAL_PWM_MOD) {
        P.hs_off_ns = s_now;
    }
    P.mode = HAL_PWM_ASC;
    P.asc_ns = s_now;
    sim_chain_eval();
}

void hal_pwm_set_duty(const float duty[3])
{
    for (uint32_t i = 0u; i < 3u; i++) {
        if (!isfinite(duty[i]) || (duty[i] < 0.0f) || (duty[i] > 1.0f)) {
            P.nan_writes++;
            return;
        }
    }
    P.mode = HAL_PWM_MOD;
    for (uint32_t i = 0u; i < 3u; i++) {
        P.duty[i] = duty[i];
    }
    sim_chain_eval();
}

hal_pwm_mode_t hal_pwm_mode(void) { return P.mode; }
uint8_t hal_pwm_fault_flags(void) { return P.fflag; }
uint8_t hal_pwm_fault_inputs(void) { return P.fin; }

uint8_t hal_pwm_fault_clear(uint8_t mask)
{
    sim_pwm_fault_inputs_changed();
    P.fflag &= (uint8_t)~(uint8_t)(mask & (uint8_t)~P.fin);
    return P.fflag;
}

hal_pwm_mode_t sim_pwm_mode_raw(void) { return P.mode; }
float sim_pwm_duty(uint32_t phase) { return (phase < 3u) ? P.duty[phase] : 0.0f; }
bool sim_pwm_hs_forced_off(void) { return (P.fflag & HAL_PWM_FAULT_ALL) != 0u; }
uint64_t sim_pwm_hs_off_ns(void) { return P.hs_off_ns; }
uint64_t sim_pwm_asc_set_ns(void) { return P.asc_ns; }
uint32_t sim_pwm_nan_writes(void) { return P.nan_writes; }

/* ================= ADC HAL ================= */
bool hal_adc_init(void) { return true; }

static float hvil_v(void)
{
    switch (s_hvil) {
    case HVIL_M_CLOSED: return s_out[HAL_DO_INTRLOK_P] ? 3.0f : 2.0f;
    case HVIL_M_OPEN: return 2.5f;
    case HVIL_M_SHORT_GND: return 0.0f;
    default: return 5.0f;
    }
}

bool hal_adc_read(hal_adc_sig_t sig, uint16_t *code, uint32_t *t_us)
{
    if (sig >= HAL_ADC_COUNT) {
        return false;
    }
    *code = (sig == HAL_ADC_INTRLOK_N) ? v_to_code(hvil_v()) : s_code[sig];
    *t_us = (uint32_t)((s_frozen[sig] ? s_frozen_t[sig] : s_now) / 1000u);
    return true;
}

bool hal_adc_read_phase(uint16_t codes[3], uint32_t *t_us)
{
    codes[0] = s_code[HAL_ADC_ISNS_U];
    codes[1] = s_code[HAL_ADC_ISNS_V];
    codes[2] = s_code[HAL_ADC_ISNS_W];
    *t_us = (uint32_t)((s_frozen[HAL_ADC_ISNS_U] ? s_frozen_t[HAL_ADC_ISNS_U] : s_now) / 1000u);
    return true;
}

void hal_adc_start_slow(void) {}

bool hal_adc_set_watchdog(hal_adc_sig_t sig, uint16_t lo_trip, uint16_t hi_trip)
{
    if (sig >= HAL_ADC_COUNT) {
        return false;
    }
    s_wd[sig].en = true;
    s_wd[sig].lo = lo_trip;
    s_wd[sig].hi = hi_trip;
    wd_eval();
    return true;
}

uint32_t hal_adc_watchdog_status(void) { return s_wd_status; }

void hal_adc_watchdog_clear(uint32_t mask)
{
    s_wd_status &= ~mask;
    s_wd_level = false;
    wd_eval();
    sim_pwm_fault_inputs_changed();
}

void sim_adc_set_code(hal_adc_sig_t sig, uint16_t code)
{
    if (sig < HAL_ADC_COUNT) {
        s_code[sig] = code;
        wd_eval();
    }
}

void sim_adc_set_v(hal_adc_sig_t sig, float v_pin) { sim_adc_set_code(sig, v_to_code(v_pin)); }

void sim_adc_freeze(hal_adc_sig_t sig, bool frozen)
{
    if (sig < HAL_ADC_COUNT) {
        s_frozen[sig] = frozen;
        s_frozen_t[sig] = s_now;
    }
}

void sim_set_phase_currents(float ia, float ib, float ic)
{
    sim_adc_set_v(HAL_ADC_ISNS_U, 2.5f + 2.22e-3f * ia);
    sim_adc_set_v(HAL_ADC_ISNS_V, 2.5f + 2.22e-3f * ib);
    sim_adc_set_v(HAL_ADC_ISNS_W, 2.5f + 2.22e-3f * ic);
}

static float s_vofs = 0.5f;
void sim_set_vofs(float v_pin)
{
    s_vofs = v_pin;
    sim_adc_set_v(HAL_ADC_VOFS, v_pin);
}

void sim_set_link_v(float v_ch1, float v_ch2)
{
    const float k = (6.0f * 470e3f + 6.2e3f) / 6.2e3f;
    s_ramp.active = false;
    sim_adc_set_v(HAL_ADC_VDC1, s_vofs + v_ch1 / k);
    sim_adc_set_v(HAL_ADC_VDC2, s_vofs + v_ch2 / k);
}

void sim_set_v5gd(float v5gd)
{
    sim_adc_set_v(HAL_ADC_V5GD, 0.5f * v5gd);
    sim_chain_set_v5gd(v5gd);
}

void sim_hvil_set(uint8_t mode) { s_hvil = (hvil_mode_t)mode; }

/* ================= SDADC + SWG ================= */
static sim_resolver_t s_rs;
static bool s_rs_set;
static uint64_t s_rs_t;
static float s_rs_glitch;
static int64_t s_blk_last[HAL_SD_COUNT];
static uint32_t s_carrier_hz = 10000u;
static bool s_swg_run, s_swg_fail;
static uint8_t s_swg_code = 12u; /* the SWG output scales with the amplitude code (12 = nominal) */

bool hal_sdadc_init(uint32_t carrier_hz)
{
    s_carrier_hz = carrier_hz;
    return true;
}

void sim_resolver_set(const sim_resolver_t *r)
{
    s_rs = *r;
    s_rs_set = true;
    s_rs_t = s_now;
    s_rs_glitch = 0.0f;
}

void sim_resolver_glitch(float delta_rad) { s_rs_glitch += delta_rad; }

bool hal_sdadc_read_block(hal_sd_ch_t ch, int16_t out[HAL_SDADC_BLOCK_N], uint32_t *t_us)
{
    const uint64_t period = 1000000000u / s_carrier_hz;
    const int64_t idx = (int64_t)(s_now / period) - 1;
    if ((ch >= HAL_SD_COUNT) || (idx <= s_blk_last[ch])) {
        return false;
    }
    s_blk_last[ch] = idx;
    const uint64_t tb = (uint64_t)idx * period;
    *t_us = (uint32_t)(tb / 1000u);
    const bool live = s_rs_set && s_swg_run && !s_swg_fail;
    const float exc_k = (float)s_swg_code / 12.0f; /* windings follow the excitation */
    const float th = s_rs.theta0_rad + s_rs.omega_rad_s * ((float)(int64_t)(tb - s_rs_t) * 1e-9f) + s_rs_glitch;
    const float lag = s_rs.lag_deg * (3.14159265f / 180.0f);
    for (uint32_t k = 0u; k < HAL_SDADC_BLOCK_N; k++) {
        const float ph = 6.28318531f * (float)k / (float)HAL_SDADC_BLOCK_N;
        const float n = s_rs.noise_code * sinf(1.7f * (float)k + 0.37f * (float)idx);
        const float thk = th + (s_rs.omega_rad_s * (float)((period / HAL_SDADC_BLOCK_N) * k) * 1e-9f); /* moves within the block */
        float v = 0.0f;
        if (live) {
            if (ch == HAL_SD_EXC) {
                v = exc_k * s_rs.exc_amp * sinf(ph);
            } else if (ch == HAL_SD_SIN) {
                v = exc_k * s_rs.sincos_amp * s_rs.sin_gain * sinf(thk) * sinf(ph - lag);
            } else {
                v = exc_k * s_rs.sincos_amp * s_rs.cos_gain * cosf(thk) * sinf(ph - lag);
            }
        }
        v += n;
        v = (v > 32767.0f) ? 32767.0f : ((v < -32767.0f) ? -32767.0f : v);
        out[k] = (int16_t)lrintf(v);
    }
    return true;
}

bool hal_swg_start(uint32_t freq_hz, uint8_t amplitude_code)
{
    s_swg_code = amplitude_code;
    s_carrier_hz = freq_hz;
    s_swg_run = true;
    return !s_swg_fail;
}

void hal_swg_stop(void) { s_swg_run = false; }
bool hal_swg_error(void) { return s_swg_fail; }
void sim_swg_fail(bool fail) { s_swg_fail = fail; }
bool sim_swg_running(void) { return s_swg_run; }
uint8_t sim_swg_code(void) { return s_swg_code; }

/* ================= SPI (FS26) ================= */
bool hal_fs26_spi_init(void) { return true; }
bool hal_fs26_xfer(uint32_t tx, uint32_t *rx)
{
    s_now += 10000u; /* 32 bits at 4 MHz + CS framing */
    return sim_fs26_xfer(tx, rx);
}

/* ================= CAN ================= */
#define CANQ 32u
static hal_can_frame_t s_rxq[2][CANQ], s_txq[2][CANQ];
static uint32_t s_rx_h[2], s_rx_t[2], s_tx_h[2], s_tx_t[2];

bool hal_can_init(uint8_t bus) { return bus < 2u; }

bool hal_can_rx(uint8_t bus, hal_can_frame_t *f)
{
    if ((bus >= 2u) || (s_rx_h[bus] == s_rx_t[bus])) {
        return false;
    }
    *f = s_rxq[bus][s_rx_t[bus] % CANQ];
    s_rx_t[bus]++;
    return true;
}

bool hal_can_tx(uint8_t bus, const hal_can_frame_t *f)
{
    if (bus >= 2u) {
        return false;
    }
    s_txq[bus][s_tx_h[bus] % CANQ] = *f;
    s_tx_h[bus]++;
    if ((s_tx_h[bus] - s_tx_t[bus]) > CANQ) {
        s_tx_t[bus] = s_tx_h[bus] - CANQ;
    }
    return true;
}

void sim_can_inject(uint8_t bus, const hal_can_frame_t *f)
{
    if ((bus < 2u) && ((s_rx_h[bus] - s_rx_t[bus]) < CANQ)) {
        s_rxq[bus][s_rx_h[bus] % CANQ] = *f;
        s_rx_h[bus]++;
    }
}

bool sim_can_pop_tx(uint8_t bus, hal_can_frame_t *f)
{
    if ((bus >= 2u) || (s_tx_h[bus] == s_tx_t[bus])) {
        return false;
    }
    *f = s_txq[bus][s_tx_t[bus] % CANQ];
    s_tx_t[bus]++;
    return true;
}

/* ================= NVM ================= */
static uint8_t s_nvm[HAL_NVM_SLOTS][HAL_NVM_SLOT_SIZE];
static struct {
    bool busy;
    uint16_t slot;
    uint32_t len, polls;
    uint8_t buf[HAL_NVM_SLOT_SIZE];
    hal_nvm_status_t last;
} N;
static uint32_t s_nvm_polls = 3u, s_nvm_done;

bool hal_nvm_read(uint16_t slot, void *buf, uint32_t len)
{
    if ((slot >= HAL_NVM_SLOTS) || (len > HAL_NVM_SLOT_SIZE)) {
        return false;
    }
    (void)memcpy(buf, s_nvm[slot], len);
    return true;
}

bool hal_nvm_write_start(uint16_t slot, const void *buf, uint32_t len)
{
    if (N.busy || (slot >= HAL_NVM_SLOTS) || (len > HAL_NVM_SLOT_SIZE)) {
        return false;
    }
    N.busy = true;
    N.slot = slot;
    N.len = len;
    N.polls = s_nvm_polls;
    (void)memcpy(N.buf, buf, len);
    return true;
}

hal_nvm_status_t hal_nvm_poll(void)
{
    if (!N.busy) {
        const hal_nvm_status_t r = N.last;
        N.last = HAL_NVM_IDLE;
        return r;
    }
    if (N.polls > 0u) {
        N.polls--;
        return HAL_NVM_BUSY;
    }
    (void)memset(s_nvm[N.slot], 0xFF, HAL_NVM_SLOT_SIZE);
    (void)memcpy(s_nvm[N.slot], N.buf, N.len);
    N.busy = false;
    s_nvm_done++;
    return HAL_NVM_DONE_OK;
}

void sim_nvm_set_write_polls(uint32_t polls) { s_nvm_polls = polls; }

void sim_nvm_power_loss(void)
{
    if (N.busy) {
        (void)memcpy(s_nvm[N.slot], N.buf, N.len / 2u); /* torn: first half new, rest old */
        N.busy = false;
    }
    N.last = HAL_NVM_IDLE;
}

uint32_t sim_nvm_writes_done(void) { return s_nvm_done; }
void sim_nvm_wipe(void) { (void)memset(s_nvm, 0xFF, sizeof s_nvm); }

/* ================= internal watchdog ================= */
static uint32_t s_kicks;
bool hal_wdog_init(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return true;
}
void hal_wdog_kick(void) { s_kicks++; }
uint32_t sim_wdog_kicks(void) { return s_kicks; }

/* ================= reset ================= */
static float ntc_v(float r, float pullup) { return 5.0f * r / (r + pullup); }

void sim_set_temp(hal_adc_sig_t sig, float degc)
{
    const float tk = degc + 273.15f;
    if ((sig == HAL_ADC_TMOD_U) || (sig == HAL_ADC_TMOD_V) || (sig == HAL_ADC_TMOD_W)) {
        const float r = 5000.0f * expf(3375.0f * (1.0f / tk - 1.0f / 298.15f));
        sim_adc_set_v(sig, ntc_v(r + 100.0f, 5100.0f));
    } else if ((sig == HAL_ADC_NTC_A) || (sig == HAL_ADC_NTC_H)) {
        const float r = 10000.0f * expf(3435.0f * (1.0f / tk - 1.0f / 298.15f));
        sim_adc_set_v(sig, ntc_v(r, 10000.0f));
    } else if ((sig == HAL_ADC_MT1) || (sig == HAL_ADC_MT2)) {
        const float r = 1000.0f * (1.0f + 3.9083e-3f * degc - 5.775e-7f * degc * degc);
        sim_adc_set_v(sig, ntc_v(r, 10000.0f));
    } else {
        /* not a temperature input */
    }
}

void sim_set_hwid_ohm(float r) { sim_adc_set_v(HAL_ADC_HW_ID, ntc_v(r, 10000.0f)); }

void sim_set_kl15(float v) { sim_adc_set_v(HAL_ADC_IGN, (v > 0.3f) ? ((v - 0.3f) * 10.0f / 57.0f) : 0.0f); }

void sim_reset(void)
{
    s_now = 1000000000u; /* 1 s: no time stamp is ever 0 */
    s_isr = NULL;
    s_isr_pending = false;
    s_in_isr = false;
    s_isr_latency = 300u;
    (void)memset(s_code, 0, sizeof s_code);
    (void)memset(s_frozen, 0, sizeof s_frozen);
    (void)memset(s_wd, 0, sizeof s_wd);
    (void)memset(&s_ramp, 0, sizeof s_ramp);
    s_wd_status = 0u;
    s_wd_level = false;
    s_hvil = HVIL_M_CLOSED;
    (void)memset(&P, 0, sizeof P);
    (void)memset(s_out, 0, sizeof s_out);
    s_out[HAL_DO_ASC_CLR_N] = true;
    s_n_edges = 0u;
    s_rs_set = false;
    for (uint32_t i = 0u; i < (uint32_t)HAL_SD_COUNT; i++) {
        s_blk_last[i] = (int64_t)(s_now / 100000u);
    }
    s_carrier_hz = 10000u;
    s_swg_run = false;
    s_swg_fail = false;
    s_swg_code = 12u;
    (void)memset(s_rx_h, 0, sizeof s_rx_h);
    (void)memset(s_rx_t, 0, sizeof s_rx_t);
    (void)memset(s_tx_h, 0, sizeof s_tx_h);
    (void)memset(s_tx_t, 0, sizeof s_tx_t);
    (void)memset(&N, 0, sizeof N);
    s_nvm_polls = 3u;
    s_kicks = 0u;
    s_vofs = 0.5f;
    sim_chain_reset();
    sim_fs26_reset();
    /* healthy card at 25 degC, 8XX SiC identity, KL15 on, link at 0 V */
    sim_set_vofs(0.5f);
    sim_set_link_v(0.0f, 0.0f);
    sim_set_v5gd(5.0f);
    sim_set_phase_currents(0.0f, 0.0f, 0.0f);
    sim_set_hwid_ohm(10000.0f);
    sim_set_kl15(13.5f);
    const hal_adc_sig_t temps[7] = {HAL_ADC_TMOD_U, HAL_ADC_TMOD_V, HAL_ADC_TMOD_W, HAL_ADC_NTC_H,
                                    HAL_ADC_NTC_A,  HAL_ADC_MT1,    HAL_ADC_MT2};
    for (uint32_t i = 0u; i < 7u; i++) {
        sim_set_temp(temps[i], 25.0f);
    }
    sim_chain_eval();
}
