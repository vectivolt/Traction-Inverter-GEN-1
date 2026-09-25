/* sim_hal.c — host implementation of every HAL interface plus the simulation clock.
 * Nominal analog scalings mirror the card: HC5FW 2.5 V + 2.22 mV/A; V_DC pin = VOFS + V/455.84;
 * V5GD_SNS = V5GD/2; HW_ID = 5 V * R/(R + 10k); KL15 via a 0.3 V diode and 47k/10k; module NTC
 * (R + 100) against 5.1k; board NTC against 10k; PT1000 against 10k; HVIL 3.0/2.0/2.5 V. */
#include <math.h>
#include <string.h>

#include "../s32k396/s32k396_cfg.h" /* the register images the target writes */
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
static bool s_slow_model;   /* the target's slow list: nothing converted before its first start */
static bool s_slow_started;
static uint8_t s_phase_stop; /* bit k: phase channel k delivers no new conversion (round 16 injection) */
static uint32_t s_adc_read_ns; /* round 18: a read takes this long, then stamps (the target's order) */

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
/* The eFlexPWM_1 lock-down registers as a small register file behind a REG_PROT model: after
 * hal_pwm_init() locks them, a write from any bus master is rejected and leaves the register as it
 * was; an MCU reset returns them to their reset values, unlocked. CTRL2 is in the file (its INDEP
 * bit is read back) but not locked, as on the target: its FORCE bit is written at every mode change.
 * The outputs follow the registers: DISMAP0 of SM0 says which fault takes which outputs low,
 * FCTRL.FIE which fault interrupts. */
static struct {
    hal_pwm_mode_t mode;
    float duty[3];
    uint8_t fflag, fin;
    uint16_t reg[SIM_PWM_NREG];
    bool prot_locked;
    bool route_bound; /* board configuration: FLT pads -> FAULT0/FAULT2 */
    uint64_t hs_off_ns, asc_ns, mod_ns;
    uint32_t nan_writes;
} P;

static void pwm_regs_reset(void)
{
    for (uint32_t r = 0u; r < (uint32_t)SIM_PWM_NREG; r++) {
        const bool dismap = (r >= (uint32_t)SIM_PWM_DISMAP0_SM0) && (r <= (uint32_t)SIM_PWM_DISMAP1_SM2);
        P.reg[r] = dismap ? 0xFFFFu : 0u; /* reset: every fault disables every output, no interrupt */
    }
    P.prot_locked = false;
}

static uint16_t pwm_image(sim_pwm_reg_t r)
{
    if (r == SIM_PWM_FCTRL) {
        return pwm_fault_fctrl();
    }
    if ((r >= SIM_PWM_DISMAP0_SM0) && (r <= SIM_PWM_DISMAP0_SM2)) {
        return pwm_fault_dismap0();
    }
    if ((r >= SIM_PWM_CTRL2_SM0) && (r <= SIM_PWM_CTRL2_SM2)) {
        return pwm_ctrl2((uint32_t)r - (uint32_t)SIM_PWM_CTRL2_SM0);
    }
    return (r == SIM_PWM_FFILT) ? (uint16_t)PWM_FFILT_VALUE
                                : ((r == SIM_PWM_FCTRL2) ? (uint16_t)PWM_FCTRL2_VALUE : (uint16_t)0u);
}

bool sim_pwm_reg_write(sim_pwm_reg_t r, uint16_t v, sim_master_t m)
{
    (void)m; /* REG_PROT soft locks refuse every master alike (CPU, eDMA) */
    const bool ctrl2 = (r >= SIM_PWM_CTRL2_SM0) && (r <= SIM_PWM_CTRL2_SM2);
    if (((uint32_t)r >= (uint32_t)SIM_PWM_NREG) || (P.prot_locked && !ctrl2)) {
        return false;
    }
    P.reg[r] = v;
    return true;
}

uint16_t sim_pwm_reg_read(sim_pwm_reg_t r) { return ((uint32_t)r < (uint32_t)SIM_PWM_NREG) ? P.reg[r] : 0u; }
bool sim_pwm_prot_locked(void) { return P.prot_locked; }
void sim_pwm_fault_route_bind(bool bound) { P.route_bound = bound; }
bool sim_pwm_route_active(void) { return P.route_bound; }

static uint8_t dis_hs(void) { return (uint8_t)(P.reg[SIM_PWM_DISMAP0_SM0] & HAL_PWM_FAULT_ALL); }
static uint8_t dis_ls(void) { return (uint8_t)((P.reg[SIM_PWM_DISMAP0_SM0] >> 4) & HAL_PWM_FAULT_ALL); }

void sim_pwm_fault_inputs_changed(void)
{
    P.fin = (uint8_t)(sim_chain_pwm_fault_pins() | (s_wd_level ? HAL_PWM_FAULT_ADC_WD : 0u));
    const uint8_t newly = (uint8_t)(P.fin & (uint8_t)~P.fflag);
    if (newly != 0u) {
        P.fflag |= newly;
        P.hs_off_ns = s_now;
        const uint8_t fie = (uint8_t)(P.reg[SIM_PWM_FCTRL] & HAL_PWM_FAULT_ALL);
        if ((s_isr != NULL) && !s_isr_pending && ((newly & fie) != 0u)) {
            s_isr_pending = true;
            s_isr_due = s_now + s_isr_latency;
        }
    }
}

bool sim_pwm_hs_cmd(void) { return (P.mode == HAL_PWM_MOD) && ((P.fflag & dis_hs()) == 0u); }
bool sim_pwm_ls_cmd(void)
{
    return ((P.mode == HAL_PWM_ASC) || (P.mode == HAL_PWM_MOD)) && ((P.fflag & dis_ls()) == 0u);
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
static void sd_run(uint64_t until_ns); /* the resolver DMA model (below) */

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
    sd_run(s_now); /* DMA completions and their interrupts up to now */
}

void sim_advance_us(uint32_t us) { sim_advance_ns((uint64_t)us * 1000u); }

/* ================= timer HAL ================= */
uint32_t hal_time_us(void) { return (uint32_t)(s_now / 1000u); } /* the raw 32-bit counter */

static ti_time64_t s_t64; /* the same extension code as the target (timer.h) */

uint64_t hal_time_us64(void)
{
    hal_crit_enter();
    const uint64_t t = ti_time64_extend(&s_t64, hal_time_us());
    hal_crit_exit();
    return t;
}
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
bool hal_pwm_config_matches(void)
{
    bool ok = true;
    for (uint32_t r = 0u; r < (uint32_t)SIM_PWM_NREG; r++) {
        ok = ok && (P.reg[r] == pwm_image((sim_pwm_reg_t)r));
    }
    return ok;
}

bool hal_pwm_protection_locked(void) { return P.prot_locked; } /* the lock bits as read back */
bool hal_pwm_fault_route_bound(void) { return P.route_bound; }

bool hal_pwm_init(uint32_t fsw_hz, uint32_t dead_time_ns)
{
    (void)fsw_hz;
    (void)dead_time_ns;
    P.mode = HAL_PWM_OFF;
    for (uint32_t r = 0u; r < (uint32_t)SIM_PWM_NREG; r++) {
        (void)sim_pwm_reg_write((sim_pwm_reg_t)r, pwm_image((sim_pwm_reg_t)r), SIM_MASTER_CPU);
    }
    P.prot_locked = true; /* REG_PROT soft locks + hard lock, as the target sets them */
    return hal_pwm_config_matches();
}

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
    if (P.mode != HAL_PWM_MOD) {
        P.mod_ns = s_now; /* modulation (re)starts: the first high-side pulse can come at once */
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
bool sim_pwm_hs_forced_off(void) { return (P.fflag & dis_hs()) != 0u; }
bool sim_pwm_ls_forced_off(void) { return (P.fflag & dis_ls()) != 0u; }
uint64_t sim_pwm_hs_off_ns(void) { return P.hs_off_ns; }
uint64_t sim_pwm_asc_set_ns(void) { return P.asc_ns; }
uint64_t sim_pwm_mod_ns(void) { return P.mod_ns; }
uint32_t sim_pwm_nan_writes(void) { return P.nan_writes; }

/* ================= ADC HAL ================= */
bool hal_adc_init(void)
{
    s_slow_started = false;
    return true;
}

void sim_adc_require_slow_start(bool on) { s_slow_model = on; }
void sim_adc_phase_stop(uint8_t mask) { s_phase_stop = (uint8_t)(mask & 0x7u); }
void sim_adc_read_delay_ns(uint32_t ns) { s_adc_read_ns = ns; }

/* Round 18 (A16-R01): the target reads the data register, then stamps with hal_time_us() (s32k396_adc.c), so
 * every stamp is later than the ISR entry; the clock moves on by the read's time first (events included: a
 * higher-priority interrupt may run meanwhile). 0 = the old frozen-clock model. */
static void adc_read_time(void)
{
    if (s_adc_read_ns > 0u) {
        sim_advance_ns(s_adc_read_ns);
    }
}

/* The phase currents (BCTU) and V_DC (continuous) convert on hardware triggers; the rest only once
 * software has started the slow list. */
static bool slow_unconverted(hal_adc_sig_t sig)
{
    return s_slow_model && !s_slow_started && (sig != HAL_ADC_ISNS_U) && (sig != HAL_ADC_ISNS_V) &&
           (sig != HAL_ADC_ISNS_W) && (sig != HAL_ADC_VDC1) && (sig != HAL_ADC_VDC2);
}

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
    adc_read_time();
    if (slow_unconverted(sig)) {
        *code = 0u;
        *t_us = 0u;
        return false; /* never converted */
    }
    *code = (sig == HAL_ADC_INTRLOK_N) ? v_to_code(hvil_v()) : s_code[sig];
    *t_us = (uint32_t)((s_frozen[sig] ? s_frozen_t[sig] : s_now) / 1000u);
    return true;
}

bool hal_adc_read_phase(uint16_t codes[3], uint32_t *t_us)
{
    adc_read_time();
    if (s_phase_stop != 0u) {
        return false; /* hal/adc.h round 16: no complete triplet, nothing written */
    }
    codes[0] = s_code[HAL_ADC_ISNS_U];
    codes[1] = s_code[HAL_ADC_ISNS_V];
    codes[2] = s_code[HAL_ADC_ISNS_W];
    *t_us = (uint32_t)((s_frozen[HAL_ADC_ISNS_U] ? s_frozen_t[HAL_ADC_ISNS_U] : s_now) / 1000u);
    return true;
}

void hal_adc_start_slow(void) { s_slow_started = true; }

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

/* ================= SDADC + eDMA + SWG ================= */
/* The resolver excitation chain of the card (round 16, the planes of A14-N01): SWG (IOAMPL code; the
 * part's MAXAPP corner, linear down to MINAPP = 0.209 MAXAPP) -> MFB |H(10 kHz)| 2.072 x 2 (ALM2402
 * bridge) -> RSX 2.2 ohm per line -> monitor tap (the protected node) -> PTC per line -> primary.
 * The monitor chain gives 2500 codes of carrier amplitude per V pp at its plane, the sin/cos chains
 * 2074 codes per V pp at the winding (ratio_nom 0.8 at the cold EOL condition: 0.8 x 2500 x 72.6 / 70). */
#define SIM_EXC_GAIN 4.14348f
#define SIM_RSX_OHM 2.2f
#define SIM_SWG_MIN_FRAC 0.2093f /* MINAPP / MAXAPP: 0.394 / 1.884 = 0.438 / 2.093 = 0.482 / 2.302 */
#define SIM_MON_CODE_PER_VPP 2500.0f
#define SIM_SC_CODE_PER_VPP 2074.2857f

static sim_resolver_t s_rs;
static bool s_rs_set;
static uint64_t s_rs_t;
static float s_rs_glitch;
static uint32_t s_carrier_hz = 10000u;
static bool s_swg_run, s_swg_fail;
static uint8_t s_swg_code = 12u;
static float s_swg_maxapp = 2.093f; /* DS Table 40 typical; corners 1.884 / 2.302 */
static float s_r_pri = 70.0f;       /* resolver primary */
static float s_r_ptc = 1.3f;        /* each line's PTC (cold; up to 5 ohm for an hour after a trip) */
static float s_amp_max;             /* largest amplifier amplitude commanded (V pp) */

/* eDMA: per channel a ring of HAL_SD_NBUF blocks, its own completion and its own interrupt. */
static int16_t s_sdbuf[HAL_SD_COUNT][HAL_SD_NBUF][HAL_SDADC_BLOCK_N];
static hal_sd_ring_t s_ring;
static bool s_sd_on, s_sd_tag;
static uint32_t s_sd_base;
static uint32_t s_irq_lat_ns;
static sim_sd_hook_fn s_sd_hook;
static struct {
    uint32_t hw;      /* blocks this DMA completed (hardware); it is writing slot hw % NBUF */
    uint64_t blk;     /* carrier period of the block it is acquiring (starts at blk x period) */
    uint64_t next_ns; /* hardware completion of that block */
    bool pend;        /* completion interrupt pending (one flag: two completions make one interrupt) */
    uint64_t irq_ns;
    bool frozen;
    uint32_t delay_ns;
    uint32_t hold_ns; /* round 18: this channel's next interrupt comes that much later (once) */
} s_dma[HAL_SD_COUNT];

static uint64_t sd_period_ns(void) { return 1000000000ull / s_carrier_hz; }

static float swg_vpp(void)
{
    return s_swg_maxapp * (SIM_SWG_MIN_FRAC + ((1.0f - SIM_SWG_MIN_FRAC) * (float)s_swg_code / 15.0f));
}

void sim_exc_planes(float *amp_vpp, float *mon_vpp, float *wind_vpp)
{
    const float load = s_r_pri + (2.0f * s_r_ptc);
    const float amp = swg_vpp() * SIM_EXC_GAIN;
    const float mon = amp * load / (load + (2.0f * SIM_RSX_OHM));
    *amp_vpp = amp;
    *mon_vpp = mon;
    *wind_vpp = mon * s_r_pri / load;
}

static void dma_fill(hal_sd_ch_t ch, uint64_t blk, int16_t out[HAL_SDADC_BLOCK_N])
{
    const uint64_t period = sd_period_ns();
    const uint64_t tb = blk * period;
    const bool live = s_rs_set && s_swg_run && !s_swg_fail;
    float amp = 0.0f;
    float mon = 0.0f;
    float wind = 0.0f;
    sim_exc_planes(&amp, &mon, &wind);
    if (live && (amp > s_amp_max)) {
        s_amp_max = amp;
    }
    const float a = (ch == HAL_SD_EXC) ? (mon * SIM_MON_CODE_PER_VPP) : (wind * SIM_SC_CODE_PER_VPP * s_rs.out_gain);
    const float th = s_rs.theta0_rad + s_rs.omega_rad_s * ((float)(int64_t)(tb - s_rs_t) * 1e-9f) + s_rs_glitch;
    const float lag = s_rs.lag_deg * (3.14159265f / 180.0f);
    for (uint32_t k = 0u; k < HAL_SDADC_BLOCK_N; k++) {
        const float ph = 6.28318531f * (float)k / (float)HAL_SDADC_BLOCK_N;
        const float n = s_rs.noise_code * sinf(1.7f * (float)k + 0.37f * (float)(blk % 100000u));
        const float thk = th + (s_rs.omega_rad_s * (float)((period / HAL_SDADC_BLOCK_N) * k) * 1e-9f); /* moves within the block */
        float v = 0.0f;
        if (live) {
            if (ch == HAL_SD_EXC) {
                v = a * sinf(ph);
            } else if (ch == HAL_SD_SIN) {
                v = a * s_rs.sin_gain * sinf(thk) * sinf(ph - lag);
            } else {
                v = a * s_rs.cos_gain * cosf(thk) * sinf(ph - lag);
            }
        }
        v += n;
        v = (v > 32767.0f) ? 32767.0f : ((v < -32767.0f) ? -32767.0f : v);
        out[k] = (int16_t)lrintf(v);
    }
    if (s_sd_tag) {
        out[0] = (int16_t)(blk & 0x3FFFu); /* test tag: which carrier period this block holds */
    }
}

/* The DMA of ch completes the block it is acquiring (hardware): the block lands in its slot, the
 * interrupt becomes pending (a pending one absorbs it). */
static void dma_complete(uint32_t ch, uint64_t t_ns)
{
    dma_fill((hal_sd_ch_t)ch, s_dma[ch].blk, s_sdbuf[ch][s_dma[ch].hw % HAL_SD_NBUF]);
    s_dma[ch].hw++;
    s_dma[ch].blk++;
    s_dma[ch].next_ns = ((s_dma[ch].blk + 1u) * sd_period_ns()) + s_dma[ch].delay_ns;
    if (!s_dma[ch].pend) {
        s_dma[ch].pend = true;
        s_dma[ch].irq_ns = t_ns + s_irq_lat_ns + s_dma[ch].hold_ns;
        s_dma[ch].hold_ns = 0u;
    }
}

static void dma_irq(uint32_t ch, uint64_t t_ns)
{
    s_dma[ch].pend = false;
    hal_sd_ring_complete(&s_ring, (hal_sd_ch_t)ch, (uint32_t)(t_ns / 1000u));
}

/* Every DMA completion and completion interrupt due by until_ns, in time order. */
static void sd_run(uint64_t until_ns)
{
    for (uint32_t guard = 0u; s_sd_on && (guard < 4000000u); guard++) {
        uint32_t ch = HAL_SD_COUNT;
        bool irq = false;
        uint64_t t = UINT64_MAX;
        for (uint32_t c = 0u; c < (uint32_t)HAL_SD_COUNT; c++) {
            if (!s_dma[c].frozen && (s_dma[c].next_ns < t)) {
                t = s_dma[c].next_ns;
                ch = c;
                irq = false;
            }
            if (s_dma[c].pend && (s_dma[c].irq_ns < t)) {
                t = s_dma[c].irq_ns;
                ch = c;
                irq = true;
            }
        }
        if ((ch == HAL_SD_COUNT) || (t > until_ns)) {
            break;
        }
        if (irq) {
            dma_irq(ch, t);
        } else {
            dma_complete(ch, t);
        }
    }
}

bool hal_sdadc_init(uint32_t carrier_hz, uint32_t irq_lat_max_us)
{
    if ((carrier_hz == 0u) || ((1000000u % carrier_hz) != 0u)) {
        return false; /* as the target: the cadence stamp needs a whole number of microseconds per period */
    }
    s_carrier_hz = carrier_hz;
    const uint64_t period = sd_period_ns();
    hal_sd_ring_init(&s_ring, (uint32_t)(period / 1000u), irq_lat_max_us, s_sd_base);
    (void)memset(s_sdbuf, 0, sizeof s_sdbuf);
    for (uint32_t c = 0u; c < (uint32_t)HAL_SD_COUNT; c++) {
        s_dma[c].hw = s_sd_base;
        s_dma[c].blk = (s_now / period) + 1u; /* the first whole carrier period */
        s_dma[c].next_ns = ((s_dma[c].blk + 1u) * period) + s_dma[c].delay_ns;
        s_dma[c].pend = false;
    }
    s_sd_on = true;
    return true;
}

bool hal_sdadc_read_frame(hal_sd_frame_t *f)
{
    sd_run(s_now);
    return s_sd_on && hal_sd_ring_read(&s_ring, f);
}

uint32_t hal_sdadc_reacquired(void) { return s_ring.n_reacq; }

uint32_t hal_sd_dma_slot(hal_sd_ch_t ch)
{
    return ((uint32_t)ch < (uint32_t)HAL_SD_COUNT) ? (s_dma[ch].hw % HAL_SD_NBUF) : HAL_SD_NBUF;
}

const volatile int16_t *hal_sd_dma_block(hal_sd_ch_t ch, uint32_t slot)
{
    if (s_sd_hook != NULL) {
        s_sd_hook(ch); /* a test acts between the reader's channel copies */
    }
    return &s_sdbuf[(uint32_t)ch % (uint32_t)HAL_SD_COUNT][slot % HAL_SD_NBUF][0];
}

void sim_sdadc_freeze(hal_sd_ch_t ch, bool frozen)
{
    if ((uint32_t)ch >= (uint32_t)HAL_SD_COUNT) {
        return;
    }
    sd_run(s_now);
    if (s_dma[ch].frozen && !frozen) { /* the DMA resumes with the period being acquired now */
        s_dma[ch].blk = s_now / sd_period_ns();
        s_dma[ch].next_ns = ((s_dma[ch].blk + 1u) * sd_period_ns()) + s_dma[ch].delay_ns;
    }
    s_dma[ch].frozen = frozen;
}

void sim_sdadc_delay_ns(hal_sd_ch_t ch, uint32_t ns)
{
    if ((uint32_t)ch < (uint32_t)HAL_SD_COUNT) {
        sd_run(s_now);
        s_dma[ch].delay_ns = ns;
        s_dma[ch].next_ns = ((s_dma[ch].blk + 1u) * sd_period_ns()) + ns;
    }
}

void sim_sdadc_irq_latency_ns(uint32_t ns)
{
    sd_run(s_now);
    s_irq_lat_ns = ns;
    for (uint32_t c = 0u; c < (uint32_t)HAL_SD_COUNT; c++) { /* pending interrupts: no later than the new latency */
        s_dma[c].irq_ns = min_u64(s_dma[c].irq_ns, s_now + ns);
    }
}

void sim_sdadc_irq_hold_ns(hal_sd_ch_t ch, uint32_t ns)
{
    if ((uint32_t)ch < (uint32_t)HAL_SD_COUNT) {
        sd_run(s_now);
        s_dma[ch].hold_ns = ns;
    }
}

void sim_sdadc_overrun(void)
{
    sd_run(s_now);
    s_ring.lost = true; /* the target driver's DMA/FIFO error flag (TI_SD_LOST) */
}

void sim_sdadc_complete_now(hal_sd_ch_t ch)
{
    if ((uint32_t)ch < (uint32_t)HAL_SD_COUNT) {
        dma_complete((uint32_t)ch, s_now);
        dma_irq((uint32_t)ch, s_now); /* the interrupt preempts whatever runs */
    }
}

void sim_sdadc_read_hook(sim_sd_hook_fn fn) { s_sd_hook = fn; }
void sim_sdadc_count_base(uint32_t count0) { s_sd_base = count0; }
void sim_sdadc_tag(bool on) { s_sd_tag = on; }
const hal_sd_ring_t *sim_sdadc_ring(void) { return &s_ring; }
uint64_t sim_sdadc_period_index(void) { return s_now / sd_period_ns(); }

void sim_resolver_set(const sim_resolver_t *r)
{
    sd_run(s_now);
    s_rs = *r;
    s_rs_set = true;
    s_rs_t = s_now;
    s_rs_glitch = 0.0f;
}

void sim_resolver_glitch(float delta_rad)
{
    sd_run(s_now);
    s_rs_glitch += delta_rad;
}

void sim_resolver_load(float r_pri_ohm, float r_ptc_ohm)
{
    sd_run(s_now);
    s_r_pri = r_pri_ohm;
    s_r_ptc = r_ptc_ohm;
}

void sim_swg_maxapp(float vpp)
{
    sd_run(s_now);
    s_swg_maxapp = vpp;
}

float sim_exc_amp_vpp_max(void) { return s_amp_max; }

bool hal_swg_start(uint32_t freq_hz, uint8_t amplitude_code)
{
    if (amplitude_code > HAL_SWG_CODE_MAX) {
        return false;
    }
    sd_run(s_now);
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
static sim_isr_fn s_spi_hook;
void sim_fs26_xfer_hook(sim_isr_fn fn) { s_spi_hook = fn; }
bool hal_fs26_spi_init(void) { return true; }
bool hal_fs26_xfer(uint32_t tx, uint32_t *rx)
{
    s_now += 10000u; /* 32 bits at 4 MHz + CS framing */
    if (s_spi_hook != NULL) {
        s_spi_hook(); /* round 18: an interrupt that preempts the caller while it waits on the transfer */
    }
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

void sim_mcu_reset(void)
{
    pwm_regs_reset();
    P.mode = HAL_PWM_OFF;
    P.fflag = 0u;
    for (uint32_t i = 0u; i < (uint32_t)HAL_DO_COUNT; i++) {
        const bool level = (i == (uint32_t)HAL_DO_ASC_CLR_N); /* pads released: board pulls */
        if (s_out[i] != level) {
            hal_gpio_write((hal_do_t)i, level);
        }
    }
    s_t64 = (ti_time64_t){0}; /* the STM restarts its extension state with the firmware */
    sim_pwm_fault_inputs_changed();
}

void sim_set_hwid_ohm(float r) { sim_adc_set_v(HAL_ADC_HW_ID, ntc_v(r, 10000.0f)); }

void sim_set_kl15(float v) { sim_adc_set_v(HAL_ADC_IGN, (v > 0.3f) ? ((v - 0.3f) * 10.0f / 57.0f) : 0.0f); }

void sim_reset(void) { sim_reset_at_us(1000000u); } /* 1 s: no time stamp is ever 0 */

void sim_reset_at_us(uint64_t t_us)
{
    s_now = t_us * 1000u;
    s_t64 = (ti_time64_t){0};
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
    s_slow_model = false;
    s_slow_started = false;
    s_phase_stop = 0u;
    s_adc_read_ns = 0u;
    s_hvil = HVIL_M_CLOSED;
    (void)memset(&P, 0, sizeof P);
    pwm_regs_reset();
    (void)memset(s_out, 0, sizeof s_out);
    s_out[HAL_DO_ASC_CLR_N] = true;
    s_n_edges = 0u;
    s_rs_set = false;
    s_carrier_hz = 10000u;
    s_swg_run = false;
    s_swg_fail = false;
    s_swg_code = 12u;
    s_swg_maxapp = 2.093f;
    s_r_pri = 70.0f;
    s_r_ptc = 1.3f;
    s_amp_max = 0.0f;
    s_sd_on = false;
    s_sd_tag = false;
    s_sd_base = 0u;
    s_irq_lat_ns = 1000u; /* 1 us from DMA completion to its handler */
    s_sd_hook = NULL;
    s_spi_hook = NULL;
    (void)memset(s_dma, 0, sizeof s_dma);
    (void)memset(&s_ring, 0, sizeof s_ring);
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
