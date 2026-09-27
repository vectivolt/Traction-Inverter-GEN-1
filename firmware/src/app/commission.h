/* commission.h — FW-39 (round 23): motor self-commissioning in an interlocked service mode (contract §10g).
 *
 * A service tool runs one identification routine at a time on the diagnostic bus. Nothing here bypasses the
 * safety chain: a routine starts only
 *   - after the existing SecurityAccess unlock (FW-32's seed/key; one start per unlock),
 *   - with the tool's attestation of the rig in the request: MC_ATTEST_LOCKED (the rotor held by the rig's brake)
 *     for the standstill routines, MC_ATTEST_DYNO_FWD/REV (a dyno drives the rotor at a constant low speed in that
 *     direction) for the back-EMF routine,
 *   - with every service CAL inside its range (mc_cal_validate: a bad one refuses the service mode, never arming),
 *   - armed through the normal path: ARMED_ZERO_TORQUE with the bridge armed idle — so the FW-24 evidence, the FW-20
 *     record, FW-16 and precharge are behind it — and the evidence still complete, no §6 row, no active DTC (the
 *     service records excepted), a fresh VCU command asking no torque, the VCU's vehicle speed valid and zero, HV
 *     inside the SKU's window with the battery path proven, and the motor speed the routine needs (zero within
 *     cal lock_rpm, or inside the dyno window and below dyno_nx_frac x n_x);
 * and those are re-checked every 1 ms while it runs, with the tool's heartbeat (any request of RID 0xF020 at least
 * every hb_timeout_ms), the locked rotor's motion, the dyno's steadiness, a current bound, the loop's voltage
 * headroom and the routine's schedule: any loss aborts it (mc_reason_t) to the normal safe state — no service
 * modulation, the bridge armed idle, or the §6 decision when a fault row caused it — and records DTC_MC_ABORTED.
 * No torque command is accepted in service mode: from a routine's start the state machine does not see the VCU's
 * enable (mc_torque_barred) — it stays in ARMED_ZERO_TORQUE, the current references are the routine's, per sample —
 * an enable is itself a precondition loss that ends the routine, and the bar holds after the routine until the VCU
 * has withdrawn its enable once: the request that arrived during service mode is never executed.
 *
 * Routines (the current loop regulates; the measurement is its own voltage output f->vd/vq, the voltage it asks the
 * bridge for, and the measured currents, accumulated in the current-loop ISR, estimated in the 1 ms task):
 *   MC_RT_RS       two DC levels (rs_i1_a, rs_i2_a) along phase U's axis on the locked rotor; Rs = dV / dI, which
 *                  cancels the inverter's constant voltage error (dead time, device drops) between the levels;
 *   MC_RT_LDQ      a sinusoidal CURRENT reference (hf_i_a at f_isr / hf_n) along d, then along q, at standstill, on
 *                  a DC bias along phase U's axis (hf_bias_a >= 2 hf_i_a: no phase current crosses zero, so the
 *                  inverter's dead-time error is linearised at ONE operating point for both runs — without it that
 *                  error is a different real matrix in each run and the solve turns the difference into reactance);
 *                  the 2x2 impedance from both runs' phasors, the actuation delay and hold undone, L = Im(Z)/w, then
 *                  Ld, Lq as its eigenvalues — the axes from the back-EMF zero of this key cycle if the dyno routine
 *                  gave one, else the record's (AXES if the saliency axis disagrees). A voltage injection would need
 *                  a summing point inside foc_step and puts an unbounded current into an unknown inductance: the
 *                  current reference goes through the loop that exists and bounds the amplitude by construction;
 *   MC_RT_PSI_ZERO i_d = i_q = 0 with the rotor dyno-driven: psi from the back-EMF magnitude, the electrical zero
 *                  from its phase against the resolver angle (e_d = w psi sin eps, e_q = w psi cos eps), the
 *                  resolver direction against the voltage vector's rotation (DIR_PHASES) and against the attested
 *                  dyno direction (DIR_DYNO).
 * Each quantity has a value, a standard uncertainty (the spread of MC_BLOCKS block estimates + a type-B floor) and a
 * verdict. Results are never used live: a VALID one inside the FW-20 class limits (calib_check's own ranges) and
 * within its band of the record is staged; one beyond the band waits for a second run that agrees within confirm_k
 * combined uncertainties. RID 0xF021 (a fresh unlock) seals the record with the staged values through FW-20
 * (calib_seal, calib_check: CRC, ranges, SKU, serial = device UID, motor ID) and queues it as a new NV_REC_CALIB
 * version; the running key cycle keeps its record, the next key cycle's init validates the new one.
 *
 * Round 23 (FW-45): the L_d/L_q routine at a bias index k (0 .. MOTOR_MAP_N - 1) measures the saturation maps: the DC bias
 * of breakpoint k (k/5 of the SKU's current limit; at least hf_bias_a, at most the limit — and for the d run the
 * demagnetisation limit — less the HF amplitude) along the axis each run injects on: the d run at i_d = -bias, the q run at
 * i_q = +bias, on the true axes (the back-EMF zero of this key cycle, else the record's). The HF impedance gives the
 * DIFFERENTIAL inductance there; each point is judged and confirmed like a quantity (band_l_rel of the record's
 * differential at that current), and the commit turns a whole staged axis (all six points) into the map's APPARENT
 * inductance — lambda_k = lambda_(k-1) + (D_(k-1) + D_k) / 2 x step, L_k = lambda_k / i_k, L_0 = D_0 — and sets that axis'
 * scalar to its point 0 unless the scalar is staged too; an axis with only some points staged refuses the commit.
 * Round 23 (FW-46): the torque-ripple table the tool writes by DID 0xFD46 is staged here too and committed with the rest.
 *
 * UDS (RoutineControl 0x31, request 0x7E1, response 0x7E9 — the FW-32 identifiers; a single frame, classic or — the
 * 8-byte biased start — CAN-FD escape):
 *   31 01 F0 20 [routine] [attest BE16] [k]  start      -> 71 01 F0 20 [routine]   (k: FW-45, MC_RT_LDQ only, 0 .. 5)
 *   31 02 F0 20                          stop           -> 71 02 F0 20        (NRC 0x24 when nothing runs)
 *   31 03 F0 20 [index]                  results        -> 71 03 F0 20 [index] [b1] [b2]:
 *        0x00 state, reason | 0x01 routine, staged mask (bit q; bit 5 a map point, bit 6 the ripple table; bit 7
 *        committed) | 0x02 (FW-45) the staged points of the L_d map, of the L_q map (bit k) | 0x10+q verdict, flags (bit 0
 *        pending, 1 staged, 2 beyond the band; FW-45: bit 3 the run was at bias index k = bits 4-6 — the flags are then that
 *        point's) | 0x20+q value BE16 | 0x30+q uncertainty BE16 (units: MC_UNIT_*) | 0x40+k, 0x50+k (FW-45) the L_d, L_q
 *        differential inductance the last run at bias k measured, BE16 MC_UNIT_L
 *   31 01 F0 21                          commit         -> 71 01 F0 21
 * NRC: 0x12 sub-function, 0x13 length, 0x22 conditions (the reason: index 0), 0x24 sequence, 0x31 out of range,
 * 0x33 not unlocked, 0x72 the record could not be queued.
 * DID 0xFD46 (FW-46; uds_diag.c carries it — segmented both ways): 2E FD 46 [36 x int16 BE, 0.01 A] writes the staging area
 * (-> 6E FD 46), 22 FD 46 reads the table the next commit writes: the staged one, else the active record's. */
#ifndef COMMISSION_H
#define COMMISSION_H

#include "app.h"

#define UDS_RID_MC_RUN 0xF020u
#define UDS_RID_MC_COMMIT 0xF021u
#define MC_ATTEST_LOCKED 0x4C4Bu   /* "LK" */
#define MC_ATTEST_DYNO_FWD 0x4446u /* "DF" */
#define MC_ATTEST_DYNO_REV 0x4452u /* "DR" */
#define MC_BLOCKS 8u
#define MC_ACC 8u
#define MC_HF_N_MAX 64u
/* result units (BE16): Rs 10 uOhm, Ld/Lq 0.1 uH, psi 10 uWb, zero 0.1 mrad */
#define MC_UNIT_RS 1.0e-5f
#define MC_UNIT_L 1.0e-7f
#define MC_UNIT_PSI 1.0e-5f
#define MC_UNIT_ZERO 1.0e-4f
#define MC_BIAS_NONE 0xFFu /* round 23 (FW-45): the unbiased L_d/L_q routine */
#define MC_DID_RIPPLE 0xFD46u /* round 23 (FW-46) */

typedef enum { MC_RT_NONE = 0, MC_RT_RS, MC_RT_LDQ, MC_RT_PSI_ZERO, MC_RT_COUNT } mc_routine_t;
typedef enum { MC_Q_RS = 0, MC_Q_LD, MC_Q_LQ, MC_Q_PSI, MC_Q_ZERO, MC_Q_COUNT } mc_qty_t;
typedef enum { MC_IDLE = 0, MC_RUNNING, MC_DONE, MC_ABORTED } mc_state_t;
typedef enum {
    MC_V_NONE = 0,
    MC_V_VALID,
    MC_V_NOISY,       /* the uncertainty above u_max_rel (zero_u_max_rad) */
    MC_V_CLASS,       /* outside the FW-20 motor class limits */
    MC_V_NOT_REACHED, /* the loop did not reach the test current */
    MC_V_AXES,        /* the saliency axis disagrees with the zero used */
    MC_V_DIR_PHASES,  /* the voltage vector turns against the resolver */
    MC_V_DIR_DYNO     /* the resolver turns against the attested dyno direction */
} mc_verdict_t;
typedef enum {
    MC_R_NONE = 0,
    MC_R_ATTEST,         /* no or the wrong attestation for the routine */
    MC_R_CAL,            /* a service CAL outside its range */
    MC_R_STATE,          /* not ARMED_ZERO_TORQUE with the bridge armed through the normal path */
    MC_R_EVIDENCE,       /* FW-24 evidence, FW-20 record or gains incomplete, or arming forbidden */
    MC_R_FAULT,          /* a §6 row active */
    MC_R_DTC,            /* an active DTC */
    MC_R_VCU,            /* the VCU command stale, or it asks torque */
    MC_R_VEHICLE_SPEED,  /* the VCU's vehicle speed not valid and zero */
    MC_R_HV,             /* V_DC outside the SKU window, or the battery path not proven */
    MC_R_SPEED,          /* the motor speed not what the routine needs */
    MC_R_MOVED,          /* the locked rotor moved */
    MC_R_HEARTBEAT,      /* the tool's heartbeat lapsed */
    MC_R_CURRENT,        /* the current beyond the routine's bound */
    MC_R_VOLTAGE,        /* the current loop saturated */
    MC_R_TIMEOUT,        /* the routine overran its schedule */
    MC_R_STOPPED         /* the tool stopped it (no DTC) */
} mc_reason_t;

/* Service CALs: range-checked at every start (mc_cal_validate), scoped to the service mode. */
typedef struct {
    uint32_t hb_timeout_ms;
    float vspeed_max_kmh;
    float lock_rpm;
    float lock_rad;
    float dyno_rpm_min;
    float dyno_rpm_max;
    float dyno_nx_frac;
    float dyno_steady_frac;
    float rs_i1_a;
    float rs_i2_a;
    uint32_t rs_settle_ms;
    uint32_t rs_meas_ms;
    float hf_i_a;
    float hf_bias_a;
    uint32_t hf_n;
    uint32_t hf_settle_ms;
    uint32_t hf_meas_ms;
    uint32_t psi_settle_ms;
    uint32_t psi_meas_ms;
    float i_margin_a;
    float u_floor_rel;
    float u_max_rel;
    float t_unc_us;
    float zero_u_max_rad;
    float band_rs_rel;
    float band_l_rel;
    float band_psi_rel;
    float band_zero_rad;
    float confirm_k;
    float axes_max_rad;
    float axes_min_rel;
} mc_cal_t;

typedef struct {
    float value; /* the last run's estimate (for the zero: the new electrical zero, [0, 2 pi)) */
    float u;     /* its standard uncertainty */
    uint8_t verdict;
    bool beyond;  /* further from the record than its band */
    bool pending; /* beyond the band, waiting for a confirming run */
    float pend_value;
    float pend_u;
    bool staged; /* confirmed: goes into the next record */
    float staged_value;
} mc_result_t;

typedef struct {
    const mc_cal_t *cal;
    mc_state_t st;
    mc_routine_t rt;
    mc_reason_t reason; /* the last refusal or abort */
    int8_t dir;         /* the attested dyno direction; 0 locked */
    uint32_t t_start_ms;
    uint32_t t_hb_ms;
    uint32_t dur_ms;
    float th0;          /* the electrical angle at the start */
    float c0, s0;       /* its cosine and sine */
    float rpm0;         /* the speed at the start */
    float i_bound_a;
    /* the ISR's plan, fixed before run is set */
    uint32_t n_phases, n_settle, n_block, n_phase, hf_n;
    float ref_d[2], ref_q[2], hf_amp;
    float hf_sin[MC_HF_N_MAX], hf_cos[MC_HF_N_MAX];
    /* the ISR's, while run */
    volatile bool run;
    volatile bool done;
    uint32_t k;
    float acc[2][MC_BLOCKS][MC_ACC + 1u];
    float cross, va_prev, vb_prev;
    /* the task's */
    mc_result_t q[MC_Q_COUNT];
    bool eps_valid; /* a VALID back-EMF zero this key cycle: the axes of the LDQ routine */
    float eps_rad;  /* ... as the offset from the record's zero */
    bool committed;
    bool bar; /* the VCU's enable hidden from the state machine: from a start until the VCU withdraws it */
    uint8_t bias_k;                 /* round 23 (FW-45): the L_d/L_q run's bias index, MC_BIAS_NONE unbiased */
    mc_result_t mp[2][MOTOR_MAP_N]; /* ... per axis (d, q) and index: the differential inductance there, judged */
    bool rip_staged;                /* round 23 (FW-46): the table 0x2E FD46 wrote, for the commit */
    int16_t rip[TQ_RIPPLE_N];
} mc_t;

extern mc_t g_mc;
extern const mc_cal_t MC_CAL_DEFAULT;

void mc_init(void); /* app_init: nothing running, nothing staged, the default CALs */
uint32_t mc_cal_validate(const mc_cal_t *c); /* the number of violations */
/* A diagnostic request for RID 0xF020/0xF021: true with the response (app.c diag(), before the UDS server). */
bool mc_uds_handle(app_t *a, const hal_can_frame_t *rq, hal_can_frame_t *rsp);
/* 1 ms task, before the state machine: the preconditions, the watch, the estimates at the end. */
void mc_task(app_t *a, uint32_t t_ms);
/* The routine asks the bridge to modulate (torque_path). */
bool mc_modulating(void);
/* The VCU's enable is not the state machine's (gather): from a routine's start until the VCU has withdrawn it. */
bool mc_torque_barred(void);
/* Current-loop ISR: the references before foc_step, the sample after it. */
void mc_isr_refs(app_t *a);
void mc_isr_sample(app_t *a);
/* Round 23 (FW-46): 2E FD 46 — m the request from the SID, n its length: 0 = staged, else the NRC. */
uint8_t mc_ripple_write(app_t *a, const uint8_t *m, uint32_t n);
/* ... 22 FD 46: the 72 bytes of the table the next commit writes (big-endian int16, 0.01 A). */
void mc_ripple_read(const app_t *a, uint8_t out[2u * TQ_RIPPLE_N]);

#endif /* COMMISSION_H */
