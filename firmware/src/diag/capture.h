/* capture.h — FW-41 (round 23): sampled-waveform capture, read-only (contract §10i).
 *
 * A static RAM ring of CAP_N records, one per current-loop ISR, written by cap_isr() as the LAST statement of
 * app_isr_current() from values that ISR already holds (no conversion started, nothing waited for, no loop): the
 * phase currents, i_d/i_q measured and their references, v_d/v_q, V_DC, the resolver observer's angle and speed,
 * the ISR's entry time and the state/fault flags. Nothing in control reads anything here.
 *
 * Triggers, while ARMED: a new DTC occurrence (dtc_events()) or a new §6 row (fm.active) — DESAT, over-current,
 * over-voltage, resolver/current loss, the torque postcondition, … —, the command (cap_trigger), and a level on one
 * channel with hysteresis. The trigger record lands at index `pre` of the frozen capture when `pre` records preceded
 * it since arming (else at the number that did); CAP_N - 1 - pre records follow it, then the ring FREEZES: nothing is
 * written until the next arm — a read-out never ends it, so a failed transfer can be repeated.
 *
 * Concurrency (one core): the ISR owns every field it writes. The task only posts requests — an arm with its
 * configuration, a command trigger — that the ISR takes at its next run, and reads the ring only while it is FROZEN
 * with no arm pending (the ISR writes nothing then), through volatile accesses.
 *
 * RAM (build-time CAP_N, a power of two): CAP_N x 32 B = 64 KiB at 2048 — 12.5 % of the S32K396's 512 KB system SRAM
 * (S32K39 data sheet Rev.3, Table 1: 800 KB in total, 288 KB of it the three cores' TCMs). At 2 f_sw that is 102 ms of
 * waveform at 20 kHz, 128 ms at 16 kHz, 205 ms at 10 kHz. */
#ifndef CAPTURE_H
#define CAPTURE_H

#include "app.h"

#ifndef CAP_N
#define CAP_N 2048u
#endif
#define CAP_REC_BYTES 32u  /* u32 t_us, u32 flags, CAP_NCH x i16 */
#define CAP_NCH 12u        /* int16 channels per record (header channels 2 .. 13) */
#define CAP_HDR_BYTES 320u /* the image's header (contract §10i) */
#define CAP_RAW_NONE INT16_MIN /* a channel whose value was not a number */

/* record flags (u32) */
#define CAP_F_ROWS 0x000003FFu    /* bits 0-9: fm.active, one per ss_row_t */
#define CAP_F_BR_SHIFT 10u        /* bits 10-11: br.mode */
#define CAP_F_SM_SHIFT 12u        /* bits 12-15: sm.st */
#define CAP_F_ISNS_VALID 0x00010000u
#define CAP_F_VDC_VALID 0x00020000u
#define CAP_F_RSLV_VALID 0x00040000u
#define CAP_F_MOD_REQ 0x00080000u
#define CAP_F_ZERO_NOW 0x00100000u
#define CAP_F_FOC_SAT 0x00200000u
#define CAP_F_FOC_GUARD 0x00400000u
#define CAP_F_ACT_SHIFT 24u       /* bits 24-26: fm.dec.action */
#define CAP_F_TRIGGER 0x80000000u /* the trigger record */

/* trigger sources (the command trigger always works) */
#define CAP_SRC_DTC 0x01u
#define CAP_SRC_ROW 0x02u
#define CAP_SRC_LEVEL 0x04u

typedef enum { CAP_OFF = 0, CAP_ARMED, CAP_TRIGGERED, CAP_FROZEN } cap_state_t;
typedef enum { CAP_WHY_NONE = 0, CAP_WHY_FAULT, CAP_WHY_CMD, CAP_WHY_LEVEL } cap_why_t;
typedef enum { CAP_OK = 0, CAP_EBUSY, CAP_ERANGE, CAP_ESTATE } cap_res_t;

typedef struct {
    uint16_t pre;     /* records before the trigger record: 0 .. CAP_N - 1 */
    uint8_t sources;  /* CAP_SRC_* */
    uint8_t lvl_ch;   /* the level channel's header number, 2 .. 13 */
    bool lvl_falling; /* false: fires at >= lvl after < lvl - hys; true: at <= lvl after > lvl + hys */
    int16_t lvl;      /* raw units of that channel (value / scale) */
    uint16_t hys;     /* raw, 0 .. 32767 */
} cap_cfg_t;

typedef struct {
    cap_state_t st;
    cap_why_t why;
    uint8_t id;          /* +1 at every freeze: a read-out of blocks from two captures is detectable */
    uint32_t seq;        /* freezes since power-up, never reset by cap_init: two captures with one id differ here */
    bool arm_pending;
    uint32_t image_bytes; /* 0 unless FROZEN (CAP_HDR_BYTES + records x CAP_REC_BYTES) */
} cap_status_t;

/* ISR side */
void cap_init(const app_t *a); /* end of app_init: ARMED with the default configuration (pre CAP_N/2, DTC + rows) */
void cap_isr(const app_t *a);  /* the last statement of app_isr_current */

/* task side (the diagnostic path) */
cap_res_t cap_arm(const cap_cfg_t *c); /* (re-)arm at the next ISR; NULL keeps the configuration. ERANGE: nothing done */
cap_res_t cap_trigger(void);           /* the command trigger at the next ISR; ESTATE unless ARMED */
void cap_status(cap_status_t *s);
void cap_config(cap_cfg_t *c);         /* the configuration of the running or frozen capture */
/* Bytes [off, off + len) of the frozen image (header, then the records in time order, little-endian); false,
 * nothing written, unless FROZEN with no arm pending and the range inside the image. */
bool cap_image_read(uint32_t off, uint8_t *dst, uint32_t len);

#endif /* CAPTURE_H */
