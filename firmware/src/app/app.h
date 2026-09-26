/* app.h — the integration layer: ISR entry points, the 1 kHz task and the background loop.
 *   app_isr_current : PWM-synchronised, twice per PWM period (double update, 16–20 kHz SiC,
 *                     10 kHz IGBT): currents, V_DC, resolver, FOC, guards, PWM write.
 *   app_isr_fault   : eFlexPWM fault interrupt (FFLAG): FW-15 step 1, FW-06 ASC request, FW-05.
 *   app_task_1ms    : sensors, CAN (vehicle + UDS), FS26 watchdog, fault manager, state machine, torque path.
 *   app_idle        : NVM queue (never on a safety path).
 * One instance (g_app); the target vector table and the host simulation call the same code. */
#ifndef APP_H
#define APP_H

#include "arm_evidence.h"
#include "bridge.h"
#include "calib.h"
#include "can_cmd.h"
#include "dclink.h"
#include "discharge.h"
#include "fault_mgr.h"
#include "foc.h"
#include "fs26.h"
#include "gains.h"
#include "gate_power.h"
#include "gate_selftest.h"
#include "hvil.h"
#include "vsup.h"
#include "hwid.h"
#include "ign.h"
#include "state_machine.h"
#include "uds.h"

/* This image's identity: the EOL/HIL validation record (arm_evidence.h) is bound to it, so a new
 * image needs a new validation. TODO(REL): the release process derives it from the build. */
#define TI_FW_ID 0x0A0F0013u /* rev A.18, round 19: a new image (the resolver cadence dated by the SWG start, never
                              * by a completion; re-sync from the clock; the synchronized producer restart) needs its
                              * own EOL/HIL validation record before it arms; the FW-20 calibration record stays
                              * layout 2. Round 18 was 0x0A0F0012, round 17 0x0A0F0011 */

typedef struct {
    const ti_params_t *p;
    calib_t cal;
    uint32_t cal_err;
    uint8_t serial[8];
    gain_set_t gains;
    bool gains_ok;
    /* sensing / control */
    isns_t isns;
    vdc_t vdc;
    temp_t temp;
    hvil_t hvil;
    vsup_t vsup;           /* round 17: LV supply (VSUP) supervision */
    ign_t ign;
    rslv_t rslv;
    foc_t foc;
    torque_lim_t tlim;
    dcl_t dcl;
    /* comms, discharge */
    can_cmd_t can;
    can_dir_t dir;
    uds_t uds;              /* FW-32: SecurityAccess + the service-lock routine on the diagnostic bus */
    dis_t dis;
    pch_t pch;
    /* safety */
    fs26_t fs;
    gp_t gp;
    st_t st;
    bridge_t br;
    fm_t fm;
    sm_t sm;
    sm_out_t so;
    /* status */
    uint32_t key_cycle;
    bool cold_start;        /* power-on (not an MCU reset inside the key cycle) */
    ti_sku_t hw_sku;
    bool no_arm;            /* a failure that forbids arming this key cycle */
    sm_tri_t init;
    bool fs0b_released;
    bool asc_hold;          /* §9 step 5 kept ASC (n >= n_x at boot) */
    sm_tri_t selftest;
    bool gate_power_requested;
    bool rdy_early;
    bool flt_boot_logged;
    bool rec_active;
    bool rec_for_retry;     /* purpose of the running FW-15 recovery */
    bool rec_done_asc;      /* FW-15 reset done to re-enter PWM-ASC (FLT_HS at n >= n_x) */
    bool rec_done_retry;    /* FW-15 reset done for the one VCU-authorised retry */
    uint32_t t_fault_us;
    bool v5gd_cleared;      /* §4c: the ASC latch cleared on this V5GD event */
    uint8_t evidence;       /* ARM_EV_* present (round 14): anything missing forbids arming */
    bool service_required;  /* stuck-on QDIS: do not re-energise, open the contactors (NVM-kept) */
    bool speed_limit_req;   /* F23: no voltage-feasible current at this speed */
    /* torque path */
    float t_cmd_nm;
    volatile float id_ref;
    volatile float iq_ref;
    volatile bool mod_req;
    volatile bool zero_now;
    float speed_rpm;
    bool speed_known;
    bool rslv_seen;          /* the resolver was valid at least once: its speed may be held, its loss is "control lost" */
    uint32_t speed_valid_ms; /* last time the resolver speed was valid */
    float n_x_rpm;
    /* current offset self-test */
    float offs_acc[3];
    uint16_t offs_n;
    bool offs_ok;
    /* timing */
    uint32_t last_task_ms;
    uint32_t last_tx_ms;
    uint8_t tx_ctr;
    uint8_t swg_amp;
    uint32_t n_isr;
    volatile uint32_t t_isr_us; /* entry of the last current-loop ISR (round 16 liveness) */
    uint32_t sd_reacq;          /* round 18: resolver ring re-acquisitions already recorded (DTC_RSLV_REACQUIRED) */
} app_t;

/* Retained across an MCU reset inside a key cycle (not across power-down). */
typedef struct {
    uint32_t magic;
    uint32_t key_cycle;
    uint32_t rslv_restarts; /* round 19: resolver producer restarts this key cycle (cal_rslv_restart_max) */
} app_session_t;

extern app_t g_app;
extern app_session_t g_app_session;

void app_init(app_t *a, const ti_params_t *p, const calib_t *cal, const uint8_t serial[8]);
void app_isr_current(app_t *a);
void app_isr_fault(app_t *a);
void app_task_1ms(app_t *a);
void app_idle(app_t *a);
void app_fault_isr_entry(void);
uint32_t app_isr_period_us(const app_t *a);

#endif /* APP_H */
