/* bridge.h — the only code that drives MCU_GATE_EN, ASC_REQ, ASC_CLR, FLT_CLR and the PWM mode.
 * Hardware-first: the eFlexPWM fault inputs and the DRV_EN chain already force the bridge safe;
 * these sequences only confirm, hold or leave that state in the order the contract requires.
 *   SPO           : PWM off; MCU_GATE_EN low as well when en_low (disarmed).
 *   PWM-ASC (§4c) : 1) high sides off  2) ASC_REQ edge (latch; CASCD delays the LS >= 3.4 us)
 *                   3) no sooner than the dead time after 1), low sides 100 % on with
 *                   MCU_GATE_EN high, so the LS DESAT stays documented (EN high, IN+ high).
 *   ASC exit (FW-06a, MCU-commanded only): ASC_CLR pulse while PWM-ASC still holds the LS on,
 *                   then the next state; the first HS pulse >= 1 us after the clear.
 *   FW-15 recovery: >= 1.5 ms with DRV_EN low, ASC_CLR always; PWM low + MCU_GATE_EN high and one
 *                   FLT_CLR high->low; after the one-shot confirm FLT high, RDY high, ASC_CMD_RB 0,
 *                   then clear FFLAG. */
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
} bridge_t;

void br_init(bridge_t *b);
void br_spo(bridge_t *b, bool en_low);
bool br_arm_idle(bridge_t *b);
bool br_modulate(bridge_t *b, const float duty[3], const ti_params_t *p);
/* t_hs_off_us: when the high sides went off (hardware fault time, or now). */
void br_enter_pwm_asc(bridge_t *b, uint32_t t_hs_off_us, const ti_params_t *p);
/* allowed: the caller proved n < n_x, or the battery present with current control ready. */
bool br_exit_asc(bridge_t *b, bool allowed);
void br_asc_clear_pulse(void);
void br_flt_clear_pulse(void);
void br_rec_start(bridge_t *b, uint32_t t_fault_us);
br_rec_t br_rec_step(bridge_t *b, uint32_t now_us, const ti_params_t *p);

#endif /* BRIDGE_H */
