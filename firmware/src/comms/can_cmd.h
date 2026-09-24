/* can_cmd.h — vehicle CAN-FD interface (FW-11, FW-08b, §4b). Frame layouts are this repository's
 * definition until the OEM DBC binds them; the protection is not layout-specific:
 *   - E2E per frame: CRC-8 SAE J1850 (0x1D, init/xor 0xFF) over DataID + payload, 4-bit alive
 *     counter; a repeated counter (frozen sender) or a jump > cal_can_ctr_max_jump is rejected;
 *   - torque command stale after 20 ms => ramp to zero torque (never hold the last value);
 *   - BMS limits (relayed by the VCU) have their own timeout => zero regen;
 *   - direction interlock: gear changes only near standstill; D never drives backwards, R never
 *     forwards; N/P => zero torque;
 *   - status frame carries "keep HV connected" (FW-08b), HV state, self-test done, the zero-
 *     torque confirmation the VCU/BMS waits for before a non-emergency opening (FW-08), and (round
 *     14) "no safe state proven", "service required / do not re-energise", "open the contactors",
 *     "limit the speed" and the arming evidence still missing.
 *
 * VCU_CMD 0x101 (8 B): b0 CRC | b1 [3:0] ctr, [5:4] gear, [6] enable, [7] fault reset |
 *   b2-3 torque int16 0.1 Nm | b4 [1:0] contactors (1 open, 2 precharge, 3 closed), [2] DESAT
 *   retry authorisation, [3] discharge request, [4] shutdown | b5 coolant degC + 40 (0xFF n/a)
 * VCU_BMS 0x102 (8 B): b0 CRC | b1 [3:0] ctr | b2-3 pack V 0.1 V | b4-5 charge (regen) power
 *   limit 0.1 kW | b6-7 discharge power limit 0.1 kW
 * INV_STATUS 0x201 (16 B): b0 CRC | b1 [3:0] ctr, [4] self-test done, [5] keep HV (FW-08b),
 *   [6] derate, [7] fault | b2 state | b3 [1:0] bridge, [3:2] HV state, [4] zero torque,
 *   [5] discharging, [6] precharge refused, [7] speed valid | b4-5 torque 0.1 Nm | b6-7 speed rpm |
 *   b8-9 V_DC 0.1 V (0xFFFF invalid) | b10 module degC + 40 | b11 confirmed DTCs | b12-13 first DTC |
 *   b14 [0] no safe state proven (an SPO held with neither rule (a) nor (b), A12-R08),
 *       [1] service required: do not re-energise (stuck-on QDIS), [2] open the contactors,
 *       [3] speed limit requested (no voltage-feasible current, F23) |
 *   b15 [4:0] arming evidence missing (arm_evidence.h ARM_EV_* bits). */
#ifndef CAN_CMD_H
#define CAN_CMD_H

#include "can.h"
#include "ti_params.h"

#define CAN_ID_VCU_CMD 0x101u
#define CAN_ID_VCU_BMS 0x102u
#define CAN_ID_INV_STATUS 0x201u

typedef struct {
    /* command */
    bool have_ctr;
    uint8_t ctr;
    uint32_t last_ms;
    bool ever;
    float torque_req_nm;
    ti_gear_t gear;
    bool enable_req, fault_reset_req, desat_retry_auth, discharge_req, shutdown_req;
    ti_contactor_t contactors;
    float coolant_c;
    bool coolant_valid;
    /* BMS limits */
    bool bms_have_ctr;
    uint8_t bms_ctr;
    uint32_t bms_last_ms;
    bool bms_ever;
    float v_pack, p_chg_w, p_dis_w;
    /* statistics */
    uint32_t n_crc, n_frozen, n_jump, n_len;
} can_cmd_t;

typedef struct {
    ti_gear_t active;
    bool change_refused;
} can_dir_t;

typedef struct {
    uint8_t state;
    uint8_t bridge;   /* 0 SPO, 1 idle (EN high), 2 modulating, 3 PWM-ASC */
    ti_hv_state_t hv;
    bool self_test_done;
    bool keep_hv;     /* FW-08b */
    bool derate;
    bool fault;
    bool zero_torque; /* FW-08: at zero torque, a non-emergency opening may proceed */
    bool discharging;
    bool precharge_refused;
    bool speed_valid;
    float torque_nm;
    float speed_rpm;
    float vdc_v;
    bool vdc_valid;
    float t_module_c;
    uint16_t n_dtc;
    uint16_t first_dtc;
    bool no_safe_state;       /* A12-R08 */
    bool service_required;    /* item 9: do not re-energise */
    bool open_contactors_req; /* item 9 */
    bool speed_limit_req;     /* F23 */
    uint8_t evidence_missing; /* ARM_EV_* bits not present */
} can_status_t;

void can_cmd_init(can_cmd_t *c);
/* Validates and applies one received frame; returns true if accepted. */
bool can_cmd_rx(can_cmd_t *c, const hal_can_frame_t *f, uint32_t now_ms, const ti_params_t *p);
bool can_cmd_fresh(const can_cmd_t *c, uint32_t now_ms, const ti_params_t *p);
bool can_bms_fresh(const can_cmd_t *c, uint32_t now_ms, const ti_params_t *p);
/* Direction interlock: the torque the gear allows at this speed. */
float can_dir_interlock(can_dir_t *d, ti_gear_t req, float t_req_nm, float speed_rpm, const ti_params_t *p);
/* Frame builders (status TX; VCU frames for HIL/tests). */
void can_status_encode(const can_status_t *s, uint8_t ctr, hal_can_frame_t *f);
void can_encode_vcu_cmd(hal_can_frame_t *f, uint8_t ctr, ti_gear_t gear, bool enable, bool fault_reset,
                        float torque_nm, ti_contactor_t cont, bool retry_auth, bool discharge, bool shutdown,
                        float coolant_c);
void can_encode_vcu_bms(hal_can_frame_t *f, uint8_t ctr, float v_pack, float p_chg_w, float p_dis_w);
uint8_t can_e2e_crc(uint32_t id, const uint8_t *data, uint8_t len);

#endif /* CAN_CMD_H */
