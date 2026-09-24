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
#include "ti_types.h"

/* ---------------- time and ISR hook ---------------- */
void sim_reset(void); /* power-on reset of every model (retained RAM is the caller's business) */
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

/* ---------------- resolver (SDADC blocks) ---------------- */
typedef struct {
    float theta0_rad;   /* resolver electrical angle at set time */
    float omega_rad_s;  /* resolver electrical speed */
    float sincos_amp;   /* sin/cos carrier amplitude, codes (nominal 16000) */
    float exc_amp;      /* monitor amplitude, codes (nominal 20000) */
    float lag_deg;      /* sin/cos carrier lag behind the monitor (card filter 24 deg) */
    float sin_gain;     /* per-channel gain error (1.0 nominal) */
    float cos_gain;
    float noise_code;   /* deterministic pseudo-noise amplitude */
} sim_resolver_t;
void sim_resolver_set(const sim_resolver_t *r);
void sim_resolver_glitch(float delta_rad); /* instantaneous angle jump */

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

/* ---------------- PWM ---------------- */
hal_pwm_mode_t sim_pwm_mode_raw(void);
float sim_pwm_duty(uint32_t phase);
bool sim_pwm_hs_forced_off(void);
uint64_t sim_pwm_hs_off_ns(void); /* last time the high sides were forced/turned off */
uint64_t sim_pwm_asc_set_ns(void); /* last time PWM-ASC was applied */
uint32_t sim_pwm_nan_writes(void);  /* duty writes rejected as non-finite (must stay 0) */

/* ---------------- FS26 ---------------- */
typedef struct {
    uint16_t prog_id;
    uint16_t device_id;
    bool otp_corrupt;
    bool dbg_mode;
    bool fs1b_short_high;   /* FAULT_OUT shorted to KL30 */
    bool gpio1_slotted;     /* wrong OTP: GPIO1 high at power-up */
} sim_fs26_cfg_t;
void sim_fs26_config(const sim_fs26_cfg_t *c);
void sim_fs26_mcu_reset(void); /* WD reaction RSTB + FS0B: FS26 keeps its INIT registers and FS_GPIO1 */
bool sim_fs26_fs0b_asserted(void);
bool sim_fs26_fs1b_asserted(void);
bool sim_fs26_gpio1(void);
bool sim_fs26_rstb_event(void);
bool sim_fs26_lpoff(void);
uint8_t sim_fs26_wd_err_cnt(void);
uint8_t sim_fs26_state(void);
uint32_t sim_fs26_crc_errors(void);
void sim_fs26_corrupt_next_miso(void); /* flips a bit of the next response */

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
void sim_chain_set_v5gd(float v);

#endif /* SIM_H */
