/* bridge.h — the only code that drives MCU_GATE_EN, ASC_REQ, ASC_CLR, FLT_CLR and the PWM mode.
 * Hardware-first: the eFlexPWM fault inputs and the DRV_EN chain already force the bridge safe;
 * these sequences only confirm, hold or leave that state in the order the contract requires.
 *   SPO           : PWM off at once; MCU_GATE_EN low as well when en_low (disarmed) — subject to
 *                   the DESAT hold below.
 *   PWM-ASC (§4c) : 1) high sides off  2) ASC_REQ edge (latch; CASCD delays the LS >= 3.4 us)
 *                   3) no sooner than the dead time after 1), low sides 100 % on with
 *                   MCU_GATE_EN high, so the LS DESAT stays documented (EN high, IN+ high).
 *   ASC exit (FW-06a, MCU-commanded only): ASC_CLR pulse while PWM-ASC still holds the LS on,
 *                   then the next state; the first HS pulse >= 1 us after the clear.
 *   FW-15 recovery: >= 1.5 ms with DRV_EN low, ASC_CLR always; PWM low + MCU_GATE_EN high and one
 *                   FLT_CLR high->low; after the one-shot confirm FLT high, RDY high, ASC_CMD_RB 0,
 *                   then clear FFLAG. The 1.5 ms runs from the fault stamp on the bridge's own clock
 *                   (round 18): br_rec_step reads the time itself, never a caller's.
 *
 * DESAT hold (A12-R05). MCU_GATE_EN is an UNDELAYED input of the DRV_EN AND gate, while the fault
 * latch drops DRV_EN only 22–53 us after a FLT so the NSI6611 can finish its local soft turn-off
 * (RST/EN during the soft turn-off is not specified by the vendor). So once a FLT line is seen low,
 * no software path lowers MCU_GATE_EN until cal_desat_en_hold_us after that first sight: every
 * drop request inside the hold (br_spo, br_rec_start, the FW-15 failure path, FW-16) only records
 * a pending drop, and br_service() — polled from the current-loop ISR and the 1 ms task — carries
 * it out when the hold has run. The PWM inhibit is never delayed. Without a FLT line low the drop
 * is immediate. The hold start is always a fresh time read AFTER the FLT lines were read, never a
 * caller's time stamp: a stamp taken before the FLT edge would end the hold too early. */
#ifndef BRIDGE_H
#define BRIDGE_H

#include "ti_params.h"

typedef enum { BR_DISARMED = 0, BR_IDLE, BR_MOD, BR_ASC } br_mode_t;
typedef enum { BR_REC_IDLE = 0, BR_REC_WAIT_LOW, BR_REC_WAIT_EN, BR_REC_DONE, BR_REC_FAIL } br_rec_t;

typedef struct {
    br_mode_t mode;
    uint32_t t_asc_clear_us;
    bool asc_cleared_recently;
    br_rec_t rec;
    uint32_t rec_t_fault_us;
    uint32_t rec_t_pulse_us;
    uint32_t n_asc_entries;
    /* DESAT hold */
    uint32_t hold_us;     /* cal_desat_en_hold_us */
    uint8_t flt_seen;     /* FLT lines seen low at the last look (bit 0 HS, bit 1 LS) */
    bool hold_active;
    uint32_t t_hold_us;   /* first sight of the FLT that started the hold */
    bool en_drop_pending; /* an EN drop requested inside the hold */
    uint32_t t_pwm_off_us; /* round 18: the bridge's own stamp of the last PWM turn-off it made or was told of (FW-34 class) */
} bridge_t;

void br_init(bridge_t *b, const ti_params_t *p);
/* Poll (current-loop ISR, 1 ms task): tracks the FLT lines and performs a pending EN drop once
 * the hold has run. */
void br_service(bridge_t *b);
bool br_en_drop_pending(const bridge_t *b);
void br_spo(bridge_t *b, bool en_low);
bool br_arm_idle(bridge_t *b);
bool br_modulate(bridge_t *b, const float duty[3], const ti_params_t *p);
/* t_hs_off_us: when the high sides went off (hardware fault time, or now). */
void br_enter_pwm_asc(bridge_t *b, uint32_t t_hs_off_us, const ti_params_t *p);
/* Round 18: the hardware (FAULT0/2) inhibited the PWM at or before t_us — the fault ISR says so on entry, so the
 * dead time before PWM-ASC is counted from the bridge's own stamp when that is newer than a caller's. */
void br_note_pwm_off(bridge_t *b, uint32_t t_us);
/* allowed: the caller proved n < n_x, or the battery present with current control ready. */
bool br_exit_asc(bridge_t *b, bool allowed);
void br_asc_clear_pulse(void);
void br_flt_clear_pulse(void);
void br_rec_start(bridge_t *b, uint32_t t_fault_us);
br_rec_t br_rec_step(bridge_t *b, const ti_params_t *p);

#endif /* BRIDGE_H */
