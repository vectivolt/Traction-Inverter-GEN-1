/* dtc.h — UDS-style (ISO 14229-1) diagnostic trouble codes: one status byte per DTC, occurrence
 * counter, first/last time. A single store per ECU. Persistence goes through the NVM queue
 * (nvm/nvlog.c) — never blocking. Code = 0xD1_0000 | id (manufacturer range; OEM mapping at
 * integration). */
#ifndef DTC_H
#define DTC_H

#include "ti_types.h"

typedef enum {
    DTC_NONE = 0,
    DTC_FS26_PROGID,        /* FW-12 OTP variant unbound / wrong */
    DTC_FS26_OTP_CORRUPT,
    DTC_FS26_DEBUG_MODE,
    DTC_FS26_INIT_READBACK, /* INIT registers or NOT pairs */
    DTC_FS26_SPI,
    DTC_FS26_WD,
    DTC_FS26_RELEASE,
    DTC_FS1B_SHORT_HIGH,    /* FAULT_OUT short to KL30: no arming until repaired */
    DTC_FS26_GPIO1_OTP,     /* gate power up before §9 step 6: GPIO1 slotted */
    DTC_HWID_OPEN,
    DTC_HWID_SHORT,
    DTC_HWID_UNKNOWN,
    DTC_SKU_MISMATCH,       /* FW-02 */
    DTC_CALIB_INVALID,      /* FW-20 */
    DTC_PARAMS_INVALID,
    DTC_PWM_LOCK,
    DTC_DESAT_HS,
    DTC_DESAT_LS,
    DTC_DESAT_REPEAT,       /* second DESAT: latched */
    DTC_DESAT_PENDING_BOOT, /* FLT low at boot */
    DTC_FLT_RECOVERY_FAIL,
    DTC_OVERCURRENT,
    DTC_OVERVOLTAGE,
    DTC_ISNS_OPEN,
    DTC_ISNS_RANGE,
    DTC_ISNS_SUM,
    DTC_ISNS_OFFSET,
    DTC_ISNS_STALE,
    DTC_VDC_DISAGREE,
    DTC_VOFS,
    DTC_V5GD,
    DTC_VDC_FAILSAFE,
    DTC_VDC_STALE,
    DTC_VDC_BMS,
    DTC_RSLV_AMPLITUDE,
    DTC_RSLV_EXCITATION,
    DTC_RSLV_TRACKING,
    DTC_RSLV_ACCEL,
    DTC_RSLV_RATE,
    DTC_TEMP_MODULE,
    DTC_TEMP_BOARD,
    DTC_TEMP_MOTOR,
    DTC_OVERTEMP,
    DTC_HVIL_OPEN,
    DTC_HVIL_SHORT,
    DTC_CAN_TIMEOUT,
    DTC_BMS_TIMEOUT,
    DTC_CAN_E2E,
    DTC_QDIS_STUCK_OFF,
    DTC_QDIS_STUCK_ON,
    DTC_QDIS_RATE_LIMIT,
    DTC_TAU_MISMATCH,       /* FW-02 key-off plausibility */
    DTC_PRECHARGE_PLATEAU,  /* FW-19 */
    DTC_PRECHARGE_TAU,
    DTC_PRECHARGE_TIMEOUT,
    DTC_SELFTEST_FAIL,      /* FW-16 step mismatch */
    DTC_SELFTEST_NO_PASS,   /* skipped with no stored pass */
    DTC_GATE_POWER,         /* FW-14 */
    DTC_SPO_ENERGY,         /* §6 SPO forced while neither rule (a) nor (b) holds */
    DTC_CTRL_NONFINITE,     /* NaN/Inf caught before the PWM */
    DTC_NVM,
    DTC_SENSOR_SELFTEST,
    DTC_GAINS,              /* no gain set within the §2 ceiling */
    DTC_ARM_EVIDENCE,       /* round 14: IMCR route unbound or no valid EOL/HIL validation record */
    DTC_TORQUE_INFEASIBLE,  /* F23: no voltage-feasible current even at iq = 0: zero torque + speed limit */
    DTC_ISNS_STUCK,         /* F24: a phase shows no current where its reference asks for it */
    DTC_RSLV_STALE,         /* round 16 (A14-R01/R02): no coherent resolver frame within cal_rslv_hold_us */
    DTC_RSLV_SWG_SAT,       /* round 16 (A14-N01): SWG trim at its top code, monitor still below the setpoint */
    DTC_SERVICE_LOCK_CLEARED, /* round 17 (FW-32): the UDS routine cleared the stuck-on QDIS lock (a record, not a failure) */
    DTC_LV_OVERVOLTAGE,     /* round 17: VSUP above VSUP_OV — information; its first/last stamps give the duration */
    DTC_LV_OV_SUSTAINED,    /* round 17: VSUPOV longer than its band allows: the orderly ramp to zero torque */
    DTC_LV_VSUP_UNKNOWN,    /* round 17: no VSUP reading from the FS26 AMUX: LV supervision off (information) */
    DTC_RSLV_REACQUIRED,    /* round 18 (A16-R02): the resolver frame ring re-acquired after an ambiguous completion —
                               information (the FW-28 age-out selects any safe state); one occurrence per event */
    DTC_COUNT
} dtc_id_t;

#define DTC_TF 0x01u     /* testFailed */
#define DTC_TFTOC 0x02u  /* testFailedThisOperationCycle */
#define DTC_PDTC 0x04u   /* pendingDTC */
#define DTC_CDTC 0x08u   /* confirmedDTC */
#define DTC_TNCSLC 0x10u /* testNotCompletedSinceLastClear */
#define DTC_TFSLC 0x20u  /* testFailedSinceLastClear */
#define DTC_TNCTOC 0x40u /* testNotCompletedThisOperationCycle */

typedef struct {
    uint8_t status;
    uint8_t occ;
    uint32_t first_ms;
    uint32_t last_ms;
} dtc_entry_t;

void dtc_init(void);
void dtc_set(dtc_id_t id, uint32_t now_ms); /* now_ms from hal_time_ms(): the one ms domain */
void dtc_pass(dtc_id_t id);
bool dtc_active(dtc_id_t id);
uint8_t dtc_status(dtc_id_t id);
uint8_t dtc_occurrences(dtc_id_t id);
/* First and last failure time (ms) since the last clear; false if it never failed. */
bool dtc_times(dtc_id_t id, uint32_t *first_ms, uint32_t *last_ms);
uint32_t dtc_code(dtc_id_t id);
uint16_t dtc_confirmed_count(void);
dtc_id_t dtc_first_active(void);
void dtc_clear_all(void);   /* UDS 0x14 */
void dtc_new_cycle(void);   /* operation-cycle start */
bool dtc_take_dirty(void);  /* persistence hint for the NVM queue */

#endif /* DTC_H */
