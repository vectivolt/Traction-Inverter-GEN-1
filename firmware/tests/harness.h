/* harness.h — scenario harness: a VCU/BMS model on CAN, a link-voltage plant (precharge,
 * contactors, active and passive discharge), a speed/resolver model, phase currents per current-loop
 * sample (an ideal current loop: the currents follow the FOC reference while the bridge modulates,
 * or a fixed amplitude a test sets), and a scheduler that runs the current-loop ISR at 2*f_sw, the
 * 1 kHz task and the background loop in simulated time. h_setup() also does what the EOL/HIL rig
 * and a filled board configuration do (round 14): binds the FLT -> PWM fault route and stores a
 * sealed validation record for this image and card, so the scenarios can arm. */
#ifndef HARNESS_H
#define HARNESS_H

#include "app.h"
#include "gpio.h"
#include "pwm.h"
#include "sim.h"
#include "timer.h"

typedef struct {
    /* VCU / BMS */
    bool send_cmd;
    bool send_bms;
    bool freeze_ctr;     /* repeat the same alive counter (frozen sender) */
    ti_contactor_t contactors;
    ti_gear_t gear;
    bool enable;
    bool fault_reset;
    bool retry_auth;
    bool discharge;
    bool shutdown;
    float torque_nm;
    float coolant_c;
    float v_pack;
    float p_chg_w;
    float p_dis_w;
    uint8_t ctr;
    uint8_t bms_ctr;
    /* plant */
    float link_v;
    float plateau_frac;  /* precharge plateau as a fraction of the pack (shorted QDIS < 1) */
    float tau_pre_s;
    float tau_dis_s;     /* active discharge time constant seen by the plant */
    bool qdis_stuck_off;
    bool link_override;  /* the test drives the V_DC channels itself */
    float ch2_err;       /* relative error of channel 2 */
    float speed_rpm;
    float i_pk_a;        /* phase current amplitude the plant reports (A peak) */
    bool isns_u_open;    /* phase-U signal wire open: 0 V through the 100 k pull-down */
    uint8_t isns_stuck;  /* bit k: phase k's sensor output stuck at its zero-current level (F24) */
    bool qdis_stuck_on;  /* the QDIS path conducts whatever the command (item 9) */
    float rslv_amp;      /* resolver sin/cos amplitude scale (1 nominal) */
    uint32_t t_ms;
} h_env_t;

extern h_env_t H;
extern ti_params_t h_p; /* mutable copy of the SKU parameters with the FS26 PROG_ID bound */
extern calib_t h_cal;

void h_setup(ti_sku_t sku);
void h_unprovision(void);               /* undo the EOL/HIL provisioning: no route, no record */
void h_store_validation(const arm_validation_t *v); /* write a validation record into NVM */
const uint8_t *h_serial(void);
void h_boot(void);                      /* app_init + ISR hook; nothing advanced */
void h_run_ms(uint32_t ms);
bool h_run_until(sm_state_t st, uint32_t max_ms);
bool h_to_armed(void);                  /* through FW-16 and precharge to ARMED_ZERO_TORQUE */
bool h_to_run(float torque_nm);
void h_set_speed(float rpm);
void h_ramp_speed(float rpm, uint32_t ms); /* linear, within the resolver acceleration plausibility */
void h_isr_only_us(uint32_t us);        /* advance time running only the current ISR */
void h_tick(void);                      /* one tick of the grid (its ISRs, then the task): no VCU frame, no plant step */
void h_isr_now(void);                   /* one current-loop ISR at once: a trigger preempting the task where it is */

/* Unit-level helpers (no app): an initialised, released FS26 with its watchdog serviced. */
bool h_fs_ready(fs26_t *fs, ti_params_t *p);
void h_wait_ms(fs26_t *fs, uint32_t ms); /* advance, refreshing the FS26 watchdog every 2 ms */

#endif /* HARNESS_H */
