/* gate_selftest.h — FW-16 boot self-test of every shutdown term through DRV_EN_RB / ASC_CMD_RB,
 * each tested while every other term is known permissive, both polarities (contract §7 table):
 *   a  MCU_GATE_EN 1, FS0B_REQ          -> DRV_EN_RB 0, ASC_CMD_RB 1 within 20 us (FS1B preset)
 *   b  release FS0B/FS1B, ASC_CLR       -> DRV_EN_RB 1, ASC_CMD_RB 0
 *   c  MCU_GATE_EN 0 then 1             -> 0 then 1
 *   d  FS_GPIO1 low, MCU_EN_FLYBK_HS 0 until RDY_HS low (time recorded), then 1 until high
 *   e  the same for the LS bank
 *   f  ASC_REQ then ASC_CLR             -> ASC_CMD_RB 1 then 0
 *   g  FS_GPIO1 high, both MCU enables 0 for 2x the recorded drop time -> RDY stays high
 *   h  ASC_REQ; each FLT pad driven low (output-buffer enable, data 0), released, cleared through
 *      the one-shot -> ASC_CMD_RB 0 at once, DRV_EN_RB 0 within 60 us, FFLAG set; after the clear
 *      DRV_EN_RB 1 and ASC_CMD_RB 1; finally ASC_CLR -> ASC_CMD_RB 0; FFLAG cleared.
 * Measured conditions: gate power up; both V_DC channels valid; residual energy <= 0.1 J — both
 * channels < 3 V, or from any reading < 60 V after a QDIS 2-tau top-up (not counted by FW-17);
 * VCU reports the contactors open; standstill (resolver valid, |n| < n_ss); PWM low; latch clear.
 * A failed condition SKIPS the test (never runs on assumption): the caller then uses the stored
 * pass of this or the previous key cycle, or refuses to arm. */
#ifndef GATE_SELFTEST_H
#define GATE_SELFTEST_H

#include "discharge.h"
#include "fs26.h"

typedef enum {
    ST_IDLE = 0,
    ST_TOPUP,
    ST_A,
    ST_B,
    ST_C,
    ST_D_DROP,
    ST_D_RISE,
    ST_E_DROP,
    ST_E_RISE,
    ST_F,
    ST_G,
    ST_H,
    ST_PASS,
    ST_FAIL,
    ST_SKIP
} st_state_t;

typedef enum { ST_RES_BUSY = 0, ST_RES_PASS, ST_RES_FAIL, ST_RES_SKIP } st_res_t;
typedef enum { ST_ENERGY_OK = 0, ST_ENERGY_TOPUP, ST_ENERGY_REFUSE } st_energy_t;

typedef struct {
    bool rdy_both;
    bool vdc_both_valid;
    float v_ch[2];
    bool contactors_open;
    bool resolver_valid;
    float speed_rpm;
    float n_ss_rpm;
    bool pwm_low;
    bool flt_high;
} st_cond_t;

typedef struct {
    st_state_t s;
    st_state_t failed;
    uint32_t t_ms;
    uint32_t drop_hs_ms;
    uint32_t drop_ls_ms;
    volatile bool in_h;
} st_t;

void st_init(st_t *t);
bool st_conditions(const st_cond_t *c);
st_energy_t st_energy(const st_cond_t *c, const ti_params_t *p);
st_res_t st_step(st_t *t, const st_cond_t *c, fs26_t *fs, dis_t *dis, const vdc_t *v, ti_contactor_t cont,
                 uint32_t now_ms, const ti_params_t *p);
bool st_in_step_h(const st_t *t);

#endif /* GATE_SELFTEST_H */
