/* runstats.h — FW-43 (round 23): run-time statistics. No safety relevance: nothing reads them back into a decision.
 * Accumulated in the 1 ms task (app.c). The DC-link power is V_DC x I_DC; this hardware has no DC shunt, so I_DC is
 * the bridge's DC current from the current loop's own voltages and measured currents, I_DC = 1.5 (v_d i_d + v_q i_q)
 * / V_DC, while the bridge modulates — 0 in SPO and ASC (the rectified regen of an SPO at speed is not counted):
 *   energy drawn from the link (motoring) and returned to it (regenerating), separately, in J (reported in Wh);
 *   key-on time (every state but OFF and SAFE_POWERDOWN) and time in RUN/DERATE, s;
 *   the highest valid module NTC, coolant (VCU, fresh command) and motor-sensor temperatures ever read;
 *   DTC occurrences per class (RS_CLS_*): each new occurrence in the DTC store counts once in its class; a DTC no
 *   row below names counts as RS_CLS_INFO;
 *   key cycles: the existing counter (NV_REC_KEYCYCLE, app.c), reported, not duplicated.
 * Distance is NOT kept: the contract has no vehicle-speed or wheel signal (VCU_CMD and VCU_BMS carry none), and the
 * motor speed gives no distance without the final drive and the wheel, which are not inputs of this inverter.
 * Persistence: the run-time record NV_REC_RUNTIME (the NVM layer's A/B pair, sequence and CRC-32; RS_LAYOUT_VERSION
 * in the payload) is queued every cal_rs_save_s, once on entering SAFE_POWERDOWN (which waits for the queue before
 * LPOFF: a controlled shutdown keeps everything) and when FW-44 adopts an offset. CAVEAT (VESC's, now ours,
 * explicitly): an UNCLEAN shutdown — KL30 lost, a reset or a crash outside SAFE_POWERDOWN — loses what accumulated
 * since the last queued record, at most cal_rs_save_s of it; a write torn by the power loss leaves the previous
 * record (A/B). The same record carries the FW-44 tracked current offsets (sense/offtrack.h), bound to the
 * calibration record's CRC (the slot budget: 4 of the 32 NVM slots were free).
 * Read-only DID RS_DID (0xFE43, the system-supplier range, until the OEM diagnostic specification binds it):
 * ReadDataByIdentifier 0x22 on the diagnostic bus (CAN-FD), the request a single frame (classic PCI 0x03, or the
 * CAN-FD escape 0x00 0x03), the RS_DID_LEN-byte record answered in one CAN-FD single frame (escape PCI, 64 bytes),
 * big-endian: +0 energy drawn Wh (u32) | +4 energy returned Wh (u32) | +8 key-on time s (u32) | +12 time in
 * RUN/DERATE s (u32) | +16 key cycles (u32) | +20/+22/+24 highest module / coolant / motor temperature 0.1 degC
 * (i16; 0x8000 = none) | +26 DTC occurrences per class RS_CLS_INFO..RS_CLS_INTEGRITY (8 x u32). No write service
 * exists for it. */
#ifndef RUNSTATS_H
#define RUNSTATS_H

#include "can.h"
#include "dtc.h"
#include "ti_params.h"

#define RS_LAYOUT_VERSION 1u
#define RS_T_NONE (-1000.0f) /* no valid reading yet */

typedef enum {
    RS_CLS_INFO = 0,     /* information, and any DTC not classified below */
    RS_CLS_POWER_STAGE,  /* DESAT, FW-15 recovery, over-current, over-voltage, gate power, the SPO energy rule */
    RS_CLS_SENSOR,       /* phase currents, V_DC, resolver, temperature sensors */
    RS_CLS_SUPPLY,       /* the FS26 and its outputs, V5GD, a sustained LV over-voltage */
    RS_CLS_VEHICLE,      /* CAN, BMS, E2E, HVIL */
    RS_CLS_LIMIT,        /* over-temperature, infeasible torque, overspeed (FW-42) */
    RS_CLS_HV_PATH,      /* discharge, precharge, the tau check */
    RS_CLS_INTEGRITY,    /* identity, calibration, parameters, arming evidence, self-tests, NVM, non-finite control */
    RS_CLS_COUNT
} rs_cls_t;

/* The run-time record (NV_REC_RUNTIME), 96 bytes, no implicit padding. */
typedef struct {
    uint16_t version;          /* RS_LAYOUT_VERSION */
    uint16_t pad0;
    uint32_t cal_crc;          /* FW-44: the calibration record (its crc32) the tracked offsets belong to */
    uint64_t e_mot_j;          /* energy drawn from the DC link, motoring */
    uint64_t e_reg_j;          /* energy returned to the DC link, regenerating */
    uint32_t t_on_s;           /* key-on time */
    uint32_t t_run_s;          /* time in RUN/DERATE */
    float t_mod_max_c;         /* highest valid module NTC reading (RS_T_NONE: none yet) */
    float t_cool_max_c;        /* highest coolant temperature the VCU reported in a fresh command */
    float t_mot_max_c;         /* highest valid motor-sensor reading */
    uint32_t n_dtc[RS_CLS_COUNT];
    float ofs_v[3];            /* FW-44: tracked zero-current offsets, V at the pin */
    uint32_t ofs_key_cycle;    /* FW-44: key cycle of the last adoption; 0 = none */
    uint32_t pad1;
} nv_runtime_t;

typedef struct {
    nv_runtime_t rec;
    float r_mot_j;             /* sub-joule residues */
    float r_reg_j;
    uint32_t on_ms;            /* sub-second residues */
    uint32_t run_ms;
    uint8_t occ_seen[DTC_COUNT]; /* DTC occurrences already counted this power-up */
    uint32_t t_save_ms;
    bool pending;              /* a queue that was full: retried at the next tick */
    bool down_saved;           /* the controlled-shutdown record is queued */
} rs_t;

typedef struct {
    bool on;         /* key on: any state but OFF and SAFE_POWERDOWN */
    bool run;        /* RUN or DERATE */
    bool mod;        /* the bridge modulates: p_w is the DC-link power */
    float p_w;       /* V_DC x I_DC, + motoring, - regenerating */
    float t_mod_c;
    bool t_mod_ok;
    float t_cool_c;
    bool t_cool_ok;
    float t_mot_c;
    bool t_mot_ok;
} rs_in_t;

#define RS_DID 0xFE43u /* FW-43 (FW-41's capture holds 0xFD40/0xFD41) */
#define RS_DID_LEN (26u + (4u * (uint32_t)RS_CLS_COUNT)) /* 58 bytes: fits one CAN-FD single frame with SID + DID */

/* rec: a valid record of RS_LAYOUT_VERSION read from NVM, or NULL (all zero, the maxima RS_T_NONE). */
void rs_init(rs_t *s, const nv_runtime_t *rec, uint32_t now_ms);
void rs_step(rs_t *s, const rs_in_t *in); /* one 1 ms task */
void rs_count_dtcs(rs_t *s);              /* new DTC occurrences since the last call, by class */
/* Queues the record every cal_rs_save_s, once on entering controlled shutdown (shutdown true), and when forced. */
void rs_persist(rs_t *s, bool shutdown, bool force, uint32_t now_ms, const ti_params_t *p);
rs_cls_t rs_dtc_class(dtc_id_t id);
uint32_t rs_wh(uint64_t j);
/* The DID's RS_DID_LEN data bytes (layout above). */
void rs_did_read(const nv_runtime_t *r, uint32_t key_cycle, uint8_t out[RS_DID_LEN]);
/* A single-frame ReadDataByIdentifier of RS_DID alone, to the diagnostic request ID: true with the positive response
 * in *rsp. Anything else is not this DID's: false, and the other diagnostic handlers answer it. */
bool rs_uds_handle(const nv_runtime_t *r, uint32_t key_cycle, const hal_can_frame_t *rq, hal_can_frame_t *rsp);

#endif /* RUNSTATS_H */
