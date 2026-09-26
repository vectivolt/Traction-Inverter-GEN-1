/* sim.h — host simulation of every HAL interface (tests only).
 *
 * Time is a 64-bit nanosecond clock that moves only through sim_advance_ns()/sim_advance_us()
 * and hal_delay_us(): tests control it completely. Behind the HAL sit three models:
 *   - the card safety chain (sim_chain.c): DRV_EN = FS0B . MCU_GATE_EN . RDY_HS . RDY_LS . FLT_OK
 *     with the RC delays of §7, the fault latch + one-shot, the ASC latch (FS1B preset, ASC_REQ
 *     clock, ASC_CLR), UASCG, the NSI6611 FLT mute/release rule, gate-power RDY timing, V5GD loss,
 *     and "stuck permissive" injection per term (FW-16);
 *   - the FS26 (sim_fs26.c): SPI frames + CRC, INIT registers and NOT pairs, challenger watchdog
 *     windows and error counters, FS0B/FS1B assertion/release, FS0B_REQ, GPIO1, LPOFF;
 *   - peripherals (sim_hal.c): ADC values + analog watchdog, a free-running V_DC sampler for the
 *     FW-06 timing test, eFlexPWM outputs/fault flags, SDADC resolver blocks, CAN, NVM with
 *     power-loss injection, SWG, internal watchdog. */
#ifndef SIM_H
#define SIM_H

#include "adc.h"
#include "can.h"
#include "gpio.h"
#include "pwm.h"
#include "sdadc.h"
#include "ti_types.h"

/* ---------------- time and ISR hook ---------------- */
void sim_reset(void); /* power-on reset of every model (retained RAM is the caller's business) */
/* The same with the clock starting at t_us: e.g. just below the 32-bit microsecond wrap
 * (4294.967296 s) to run the firmware across it (A12-R06). hal_time_us64() starts from the raw
 * counter, as on the target. */
void sim_reset_at_us(uint64_t t_us);
void sim_advance_ns(uint64_t ns);
void sim_advance_us(uint32_t us);
uint64_t sim_now_ns(void);
typedef void (*sim_isr_fn)(void);
void sim_set_fault_isr(sim_isr_fn fn);       /* called when an eFlexPWM FFLAG sets */
void sim_set_isr_latency_ns(uint32_t ns);    /* fault ISR entry latency (default 300 ns) */

/* ---------------- analog inputs ---------------- */
void sim_adc_set_code(hal_adc_sig_t sig, uint16_t code);
void sim_adc_set_v(hal_adc_sig_t sig, float v_pin);
void sim_adc_freeze(hal_adc_sig_t sig, bool frozen); /* time stamp stops advancing (stale) */
/* Round 15: model the target's slow list — every input but the phase currents and V_DC reads "never
 * converted" (false, code 0) after hal_adc_init() until hal_adc_start_slow() has run. Default off. */
void sim_adc_require_slow_start(bool on);
/* Round 16 (A14-R03): bit k set = phase channel k (U, V, W) delivers no new conversion, as a stopped
 * converter or BCTU does: hal_adc_read_phase() returns false and writes nothing. 0 = healthy. */
void sim_adc_phase_stop(uint8_t mask);
/* Round 18 (A16-R01): each hal_adc_read()/hal_adc_read_phase() takes ns of simulated time before it stamps,
 * as the target reads the register and then hal_time_us(): every stamp is later than the ISR entry. The
 * clock runs events meanwhile (a DMA completion, the fault ISR). 0 (default) = the old frozen-clock model. */
void sim_adc_read_delay_ns(uint32_t ns);
void sim_set_phase_currents(float ia, float ib, float ic); /* nominal HC5FW scaling */
void sim_set_link_v(float v_ch1, float v_ch2);            /* nominal divider + VOFS 0.5 V */
void sim_set_vofs(float v_pin);
void sim_set_v5gd(float v5gd);                            /* sets V5GD_SNS and the chain supply */
void sim_set_temp(hal_adc_sig_t sig, float degc);         /* module/board NTC or PT1000 */
void sim_set_hwid_ohm(float r);                           /* power-board RHWID */
void sim_set_kl15(float v);
#define SIM_HVIL_CLOSED 0u
#define SIM_HVIL_OPEN 1u
#define SIM_HVIL_SHORT_GND 2u
#define SIM_HVIL_SHORT_BAT 3u
void sim_hvil_set(uint8_t mode);
/* FW-06 timing: V_DC channels follow v0 + slope*(t - t0) and are sampled free-running every
 * period_ns with the given phase; a sample becomes a code conv_ns later (then the watchdog). */
void sim_vdc_ramp(float v0, float slope_v_per_us, uint32_t period_ns, uint32_t phase_ns,
                  uint32_t conv_ns, uint32_t analog_lag_ns);
uint64_t sim_vdc_ramp_crossing_ns(float v_link); /* when the true link crosses v_link */

/* ---------------- resolver: excitation chain, SDADC + eDMA (round 16) ---------------- */
typedef struct {
    float theta0_rad;   /* resolver electrical angle at set time */
    float omega_rad_s;  /* resolver electrical speed */
    float lag_deg;      /* sin/cos carrier lag behind the monitor (card filter 24 deg) */
    float sin_gain;     /* per-channel gain error (1.0 nominal) */
    float cos_gain;
    float noise_code;   /* deterministic pseudo-noise amplitude */
    float out_gain;     /* resolver output vs its EOL transformation ratio (1 nominal; 0.6 = amplitude fault) */
} sim_resolver_t;
void sim_resolver_set(const sim_resolver_t *r);
void sim_resolver_glitch(float delta_rad); /* instantaneous angle jump */
/* The excitation chain: SWG code x MAXAPP corner -> x 4.14 (MFB + bridge) = amplifier -> RSX 2.2 ohm per
 * line -> monitor plane -> PTC per line -> primary (the winding). Defaults: 2.093 V pp (typical), 70 ohm,
 * 1.3 ohm (cold). The monitor reads 2500 codes per V pp, the sin/cos 2074 codes per winding V pp. */
void sim_swg_maxapp(float vpp);
void sim_resolver_load(float r_pri_ohm, float r_ptc_ohm);
void sim_exc_planes(float *amp_vpp, float *mon_vpp, float *wind_vpp); /* at the present code */
float sim_exc_amp_vpp_max(void); /* largest amplifier amplitude the SWG has commanded since reset */
/* Per-channel eDMA (A14-R02): each SDADC's DMA completes one block per carrier period into its own
 * 4-slot ring and raises its own interrupt (1 us later by default), which runs the shared frame protocol
 * (hal/sdadc.h). A channel can be frozen (its DMA stops), delayed (completes ns late), or completed at
 * once; the interrupt latency can exceed a carrier period (interrupts held off: the DMA keeps writing).
 * The read hook runs each time the reader fetches a channel's block (between its channel copies). */
typedef void (*sim_sd_hook_fn)(hal_sd_ch_t ch);
void sim_sdadc_freeze(hal_sd_ch_t ch, bool frozen);
void sim_sdadc_delay_ns(hal_sd_ch_t ch, uint32_t ns);
void sim_sdadc_irq_latency_ns(uint32_t ns);
void sim_sdadc_complete_now(hal_sd_ch_t ch); /* its DMA completes now; the interrupt runs at once */
/* Round 18 (A16-R02): that channel's next completion interrupt comes ns later than the latency (once; its DMA runs
 * on, later completions fold into the pending interrupt). sim_sdadc_overrun(): the target's DMA/FIFO error flag —
 * samples lost, the ring stays down until hal_sdadc_init(). */
void sim_sdadc_irq_hold_ns(hal_sd_ch_t ch, uint32_t ns);
void sim_sdadc_overrun(void);
void sim_sdadc_read_hook(sim_sd_hook_fn fn);
void sim_sdadc_count_base(uint32_t count0);  /* block counters start here at the next hal_sdadc_init() */
void sim_sdadc_tag(bool on);                 /* sample 0 of every block = its carrier period (& 0x3FFF) */
const hal_sd_ring_t *sim_sdadc_ring(void);
/* Round 19 (A17-R01): the converters are triggered by the SWG, as on the target: nothing is acquired before
 * hal_swg_start(), and carrier period k (block k of every channel, k = 0 the first) starts at t0 + k x period, t0 =
 * the SWG enable + the start latency below — the DMA block boundaries derive from the SWG start, not from sim time
 * 0. hal_sdadc_init() stops the SWG and re-arms the DMAs (a frozen one runs again); hal_swg_start() anchors the
 * ring as the target does. The start latency (default 0) is what cal_swg_start_lat_us declares; a test sets 1-3 us
 * to prove it stays inside the declared uncertainty. */
void sim_swg_start_latency_ns(uint32_t ns);
uint64_t sim_sdadc_period_index(void);        /* the carrier period now being acquired, counted from the SWG start */
uint64_t sim_sdadc_block_start_ns(uint64_t k); /* the true start of carrier period k (every stamp's reference) */

/* ---------------- safety chain ---------------- */
#define SIM_STUCK_FS0B_TERM 0x0001u    /* AND input sees FS0B permissive */
#define SIM_STUCK_FS1B_PRESET 0x0002u  /* FS1B cannot preset the ASC latch */
#define SIM_STUCK_CHAIN_LOW 0x0004u    /* DRV_EN / read-back stuck low */
#define SIM_STUCK_ASC_CLR_DEAD 0x0008u /* ASC_CLR cannot clear */
#define SIM_STUCK_MCU_EN_TERM 0x0010u  /* UAND1.B permissive */
#define SIM_STUCK_RDY_HS_TERM 0x0020u
#define SIM_STUCK_RDY_LS_TERM 0x0040u
#define SIM_STUCK_ASC_CLK_DEAD 0x0080u /* ASC_REQ cannot set the latch */
#define SIM_STUCK_GPIO1_OR_DEAD 0x0100u /* FS_GPIO1 input of UOR1/UOR2 open */
#define SIM_STUCK_FLTOK_TERM 0x0200u   /* fault-latch path permissive */
#define SIM_STUCK_UASCG 0x0400u        /* ASC not masked by FLT */
#define SIM_STUCK_PWM_FAULT_ROUTE 0x0800u /* FLT pins do not reach eFlexPWM FAULT0/2 */
void sim_chain_stuck(uint32_t mask);
void sim_chain_desat(bool hs, bool persist); /* a driver of that bank latches FLT (DESAT) */
bool sim_chain_drv_en(void);
bool sim_chain_asc_latch(void);
bool sim_chain_fault_latch(void);
bool sim_chain_ls_on(void); /* low sides conducting (via ASC or PWM with DRV_EN) */
bool sim_chain_hs_on(void);
void sim_chain_rdy_timing(uint32_t rise_ms, uint32_t fall_ms);

/* ---------------- gpio edge log ---------------- */
/* Time (ns) of the first edge of an output at or after t_from_ns; UINT64_MAX if none. */
uint64_t sim_gpio_edge_ns(hal_do_t pin, bool rising, uint64_t t_from_ns);
uint32_t sim_gpio_edge_count(hal_do_t pin, bool rising);

/* ---------------- eFlexPWM lock-down + REG_PROT model (F01/F02) ---------------- */
typedef enum {
    SIM_PWM_FCTRL = 0,
    SIM_PWM_FCTRL2,
    SIM_PWM_FFILT,
    SIM_PWM_DISMAP0_SM0,
    SIM_PWM_DISMAP0_SM1,
    SIM_PWM_DISMAP0_SM2,
    SIM_PWM_DISMAP1_SM0,
    SIM_PWM_DISMAP1_SM1,
    SIM_PWM_DISMAP1_SM2,
    SIM_PWM_OCTRL_SM0,
    SIM_PWM_OCTRL_SM1,
    SIM_PWM_OCTRL_SM2,
    SIM_PWM_CTRL2_SM0,
    SIM_PWM_CTRL2_SM1,
    SIM_PWM_CTRL2_SM2,
    SIM_PWM_NREG
} sim_pwm_reg_t;
typedef enum { SIM_MASTER_CPU = 0, SIM_MASTER_DMA } sim_master_t;
/* A write by a bus master (a stray application pointer, an eDMA descriptor): false = rejected by
 * REG_PROT, the register keeps its value. */
bool sim_pwm_reg_write(sim_pwm_reg_t r, uint16_t v, sim_master_t m);
uint16_t sim_pwm_reg_read(sim_pwm_reg_t r);
bool sim_pwm_prot_locked(void);
/* The board configuration binds FLT_HS_N/FLT_LS_N to FAULT0/FAULT2 (s32k396_board_cfg.h filled from the RM).
 * Host default: UNBOUND — the FLT pins do not reach the PWM fault inputs and
 * hal_pwm_fault_route_bound() is false. */
void sim_pwm_fault_route_bind(bool bound);
/* MCU functional reset (the FS26 RSTB): PWM registers and the REG_PROT lock back to reset values,
 * outputs released to the board pulls, until the firmware initialises them again. */
void sim_mcu_reset(void);

/* ---------------- PWM ---------------- */
hal_pwm_mode_t sim_pwm_mode_raw(void);
float sim_pwm_duty(uint32_t phase);
bool sim_pwm_hs_forced_off(void); /* a latched fault holds the high sides off (DISMAP A) */
bool sim_pwm_ls_forced_off(void); /* ... the low sides (DISMAP B) */
uint64_t sim_pwm_hs_off_ns(void); /* last time the high sides were forced/turned off */
uint64_t sim_pwm_asc_set_ns(void); /* last time PWM-ASC was applied */
uint64_t sim_pwm_mod_ns(void);     /* last time modulation started (the first high-side pulse can come at once) */
uint32_t sim_pwm_nan_writes(void);  /* duty writes rejected as non-finite (must stay 0) */

/* ---------------- FS26 ---------------- */
typedef struct {
    uint16_t prog_id;
    uint16_t device_id;
    bool otp_corrupt;
    bool dbg_mode;
    bool fs1b_short_high;   /* FAULT_OUT shorted to KL30 */
    bool gpio1_slotted;     /* wrong OTP: GPIO1 high at power-up */
    float osc_error;        /* fail-safe oscillator off by this fraction (FFSOSC_ACC ±5 %, DS Table 143): every
                               watchdog window lasts nominal / (1 + osc_error) */
} sim_fs26_cfg_t;
void sim_fs26_config(const sim_fs26_cfg_t *c);
void sim_fs26_mcu_reset(void); /* WD reaction RSTB + FS0B: FS26 keeps its INIT registers and FS_GPIO1 */
void sim_fs26_vsup(float v);   /* KL30 at the VSUP pin (13.5 V after a POR); the AMUX shows it once configured */
bool sim_fs26_fs0b_asserted(void);
bool sim_fs26_fs1b_asserted(void);
bool sim_fs26_gpio1(void);
bool sim_fs26_rstb_event(void);
bool sim_fs26_lpoff(void);
uint8_t sim_fs26_wd_err_cnt(void);
uint8_t sim_fs26_state(void);
uint32_t sim_fs26_crc_errors(void);
void sim_fs26_corrupt_next_miso(void); /* flips a bit of the next response */
/* Round 18: fn runs inside every FS26 SPI transfer — a higher-priority interrupt (the current-loop ISR)
 * preempting the 1 ms task after it read its time. NULL (default): none. */
void sim_fs26_xfer_hook(sim_isr_fn fn);

/* ---------------- CAN / NVM / misc ---------------- */
void sim_can_inject(uint8_t bus, const hal_can_frame_t *f);
bool sim_can_pop_tx(uint8_t bus, hal_can_frame_t *f);
void sim_nvm_set_write_polls(uint32_t polls);
void sim_nvm_power_loss(void); /* tears an in-progress write and aborts it */
uint32_t sim_nvm_writes_done(void);
void sim_nvm_wipe(void);
uint32_t sim_wdog_kicks(void);
void sim_swg_fail(bool fail);
bool sim_swg_running(void);
uint8_t sim_swg_code(void); /* last amplitude code written */

/* model internals shared between the sim files */
void sim_chain_reset(void);
void sim_chain_eval(void);
void sim_chain_on_output(hal_do_t pin, bool level);
bool sim_chain_read(hal_di_t pin);
void sim_chain_pad(hal_di_t pin, bool drive_low);
uint64_t sim_chain_next_event_ns(void);
void sim_fs26_reset(void);
bool sim_fs26_xfer(uint32_t tx, uint32_t *rx);
uint64_t sim_fs26_next_event_ns(void);
void sim_fs26_eval(void);
void sim_pwm_fault_inputs_changed(void);
bool sim_pwm_hs_cmd(void);
bool sim_pwm_ls_cmd(void);
bool sim_fs26_fs1b_pin_low(void);
uint8_t sim_chain_pwm_fault_pins(void);
bool sim_pwm_route_active(void);
void sim_chain_set_v5gd(float v);

#endif /* SIM_H */
