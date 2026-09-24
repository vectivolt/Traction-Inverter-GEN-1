/* state_machine.h — the inverter operating states. A pure step function: inputs are sampled by
 * app.c every 1 ms, outputs are requests app.c executes. Transitions follow the §9 start-up
 * sequence (FW-16 runs BEFORE precharge: the VCU precharges only after "self-test done"):
 *   OFF -> INIT (FW-12, FW-01/02, FW-20, fault-latch clear) -> SENSOR_SELFTEST (resolver, both
 *   V_DC, currents, V5GD; FS0B/FS1B release; ASC decision from speed; gate power + FS_GPIO1)
 *   -> VEHICLE_HANDSHAKE (fresh CAN) -> GATE_SELFTEST (FW-16 or the stored pass) -> PRECHARGE_WAIT
 *   (FW-19) -> ARMED_ZERO_TORQUE (MCU_GATE_EN) <-> RUN <-> DERATE;
 *   FAULT from anywhere a fault needs it; DISCHARGE (FW-17) and SAFE_POWERDOWN (LPOFF) on request. */
#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "ti_params.h"

typedef enum {
    SM_OFF = 0,
    SM_INIT,
    SM_SENSOR_SELFTEST,
    SM_VEHICLE_HANDSHAKE,
    SM_PRECHARGE_WAIT,
    SM_GATE_SELFTEST,
    SM_ARMED_ZERO_TORQUE,
    SM_RUN,
    SM_DERATE,
    SM_FAULT,
    SM_DISCHARGE,
    SM_SAFE_POWERDOWN,
    SM_STATE_COUNT
} sm_state_t;

typedef enum { SM_BUSY = 0, SM_OK, SM_FAIL } sm_tri_t;

typedef struct {
    uint32_t now_ms;
    bool ign_on;
    sm_tri_t init;              /* FW-12 readback, FW-01/02 identity, FW-20 calibration, gains */
    bool sensors_ok;            /* resolver, both V_DC, currents (offset), temperatures */
    bool v5gd_ok;
    bool flt_low_at_boot;       /* pending DESAT from before the reset (V5GD healthy) */
    bool rdy_before_gate_power; /* RDY up before §9 step 6: FS_GPIO1 OTP wrong */
    bool fs0b_released;
    bool gate_power_ready;
    bool gate_power_failed;
    bool cmd_fresh;
    bool enable_req;
    float torque_req_nm;        /* after the direction interlock */
    bool torque_ramped_out;     /* the applied torque reached zero */
    ti_contactor_t contactors;
    bool link_at_pack;          /* contactors closed and V_DC within 3 % of the pack */
    bool shutdown_req;
    bool discharge_req;
    sm_tri_t selftest;          /* FW-16 run or stored pass */
    sm_tri_t precharge;         /* FW-19 */
    bool fault_needed;          /* a fault row, or a failure that forbids arming this key cycle */
    bool retry_allowed;         /* FW-15: VCU-authorised, >= 1 s, once per key cycle */
    bool recovery_done;         /* FW-15 steps 2–4 completed */
    bool derate_active;
    bool discharge_done;
    bool nvm_idle;
    float speed_rpm;
    float n_x_rpm;
} sm_in_t;

typedef struct {
    bool req_flt_clear;    /* §9 step 2 */
    bool req_fs0b_release; /* §9 step 4 (MCU_GATE_EN still low) */
    bool req_asc_decision; /* §9 step 5 */
    bool req_gate_power;   /* §9 step 6 */
    bool req_selftest;     /* §9 step 7 */
    bool self_test_done;   /* reported to the VCU */
    bool arm;              /* §9 step 8: MCU_GATE_EN permitted */
    bool torque_enable;
    bool torque_reduced;   /* the one DESAT retry runs at reduced torque */
    bool req_recovery;     /* FW-15 recovery for the authorised retry */
    bool req_discharge;
    bool req_lpoff;
} sm_out_t;

typedef struct {
    sm_state_t st;
    sm_state_t before_discharge;
    uint32_t t_enter_ms;
    bool step4_done;
    bool step5_done;
    bool step6_done;
    bool self_test_done;
    bool retry_mode;
    bool init_clear_done;
    bool dis_req_seen; /* the VCU discharge request is edge-triggered */
} sm_t;

void sm_init(sm_t *s);
void sm_step(sm_t *s, const sm_in_t *in, sm_out_t *out, const ti_params_t *p);
const char *sm_name(sm_state_t st);

#endif /* STATE_MACHINE_H */
