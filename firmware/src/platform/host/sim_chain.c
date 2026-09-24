/* sim_chain.c — model of the control-card safety chain and the NSI6611 driver behaviour
 * (firmware-contract §4c, §7; design-basis §7/§8). Timings are fixed points inside the ranges
 * the contract gives, so tests exercise the firmware's own waits:
 *   fault latch -> AND  35 us fall (22–53), 30 us rise (16–46); one-shot CLR low 140 us (72–210)
 *   NSI6611 FLT mute 1.0 ms (0.55–1.3); RDY rise 150 ms (S1 73–240), RDY drop 3 ms
 * Logic (74LVC74 fault latch, Q = fault): PRE = FLT_HS_N & FLT_LS_N low; CLR from the one-shot.
 * During CLR low /Q is high whatever PRE does (so DRV_EN can rise and the drivers see their
 * rising edge); when CLR returns, a still-low PRE re-sets the latch (fail-safe). */
#include <stdint.h>
#include <string.h>

#include "fs26_regs.h"
#include "sim.h"

#define T_FLTOK_FALL_NS 35000u
#define T_FLTOK_RISE_NS 30000u
#define T_ONESHOT_NS 140000u
#define T_MUTE_NS 1000000u

typedef struct {
    bool pend;
    bool val;
    uint64_t due;
} delayed_t;

typedef struct {
    uint32_t stuck;
    bool out[HAL_DO_COUNT];
    bool pad_hs, pad_ls;
    bool desat_hs, desat_ls, persist_hs, persist_ls;
    uint64_t desat_t_hs, desat_t_ls;
    float v5gd;
    bool fault_latch;
    bool fltok;         /* delayed FLT_OK at the AND input */
    delayed_t fltok_d;
    bool oneshot;
    uint64_t oneshot_end;
    bool asc_latch;
    bool rdy_hs, rdy_ls;
    delayed_t rdy_hs_d, rdy_ls_d;
    bool drv_en;
    uint64_t rise_ns, fall_ns;
} chain_t;

static chain_t C;

void sim_chain_reset(void)
{
    (void)memset(&C, 0, sizeof C);
    C.out[HAL_DO_ASC_CLR_N] = true; /* pull-up: no clear */
    C.v5gd = 5.0f;
    C.fltok = true;
    C.rise_ns = 150000000u;
    C.fall_ns = 3000000u;
}

void sim_chain_rdy_timing(uint32_t rise_ms, uint32_t fall_ms)
{
    C.rise_ns = (uint64_t)rise_ms * 1000000u;
    C.fall_ns = (uint64_t)fall_ms * 1000000u;
}

void sim_chain_stuck(uint32_t mask)
{
    C.stuck = mask;
    sim_chain_eval();
}

static bool has(uint32_t m) { return (C.stuck & m) != 0u; }

static bool supply_reads_low(void) { return C.v5gd < 1.0f; } /* FLT/RDY pull-ups on V5GD */

static bool flt_n(bool hs)
{
    if (supply_reads_low()) {
        return false;
    }
    return hs ? !(C.desat_hs || C.pad_hs) : !(C.desat_ls || C.pad_ls);
}

static bool no_flt(void) { return flt_n(true) && flt_n(false); }

static bool flyback_on(bool hs)
{
    const bool gpio1 = sim_fs26_gpio1() && !has(SIM_STUCK_GPIO1_OR_DEAD);
    return (hs ? C.out[HAL_DO_EN_FLYBK_HS] : C.out[HAL_DO_EN_FLYBK_LS]) || gpio1;
}

static void schedule(delayed_t *d, bool current, bool target, uint64_t delay)
{
    const uint64_t now = sim_now_ns();
    if (target == current) {
        d->pend = false;
    } else if (!d->pend || (d->val != target)) {
        d->pend = true;
        d->val = target;
        d->due = now + delay;
    } else {
        /* already scheduled toward target */
    }
}

static bool fire(delayed_t *d, bool *state)
{
    if (d->pend && (d->due <= sim_now_ns())) {
        d->pend = false;
        *state = d->val;
        return true;
    }
    return false;
}

static void release_on_en_edge(bool hs)
{
    bool *f = hs ? &C.desat_hs : &C.desat_ls;
    uint64_t *t = hs ? &C.desat_t_hs : &C.desat_t_ls;
    const bool persist = hs ? C.persist_hs : C.persist_ls;
    if (*f && ((sim_now_ns() - *t) >= T_MUTE_NS)) {
        if (persist) {
            *t = sim_now_ns(); /* hard short: re-trips at once, a new SC event */
        } else {
            *f = false;
        }
    }
}

void sim_chain_eval(void)
{
    const uint64_t now = sim_now_ns();
    for (uint32_t pass = 0u; pass < 6u; pass++) {
        bool changed = false;
        changed |= fire(&C.fltok_d, &C.fltok);
        changed |= fire(&C.rdy_hs_d, &C.rdy_hs);
        changed |= fire(&C.rdy_ls_d, &C.rdy_ls);
        if (C.oneshot && (C.oneshot_end <= now)) {
            C.oneshot = false;
            C.fault_latch = !no_flt();
            changed = true;
        }
        /* fault latch: PRE sets it outside the one-shot; during CLR low /Q is high */
        if (!C.oneshot && !no_flt() && !C.fault_latch) {
            C.fault_latch = true;
            changed = true;
        }
        const bool fltok_target = C.oneshot ? true : !C.fault_latch;
        schedule(&C.fltok_d, C.fltok, fltok_target, fltok_target ? T_FLTOK_RISE_NS : T_FLTOK_FALL_NS);
        /* ASC latch: FS1B preset dominates, then clear */
        if (sim_fs26_fs1b_pin_low() && !has(SIM_STUCK_FS1B_PRESET)) {
            C.asc_latch = true;
        } else if (!C.out[HAL_DO_ASC_CLR_N] && !has(SIM_STUCK_ASC_CLR_DEAD)) {
            C.asc_latch = false;
        } else {
            /* hold */
        }
        schedule(&C.rdy_hs_d, C.rdy_hs, flyback_on(true), flyback_on(true) ? C.rise_ns : C.fall_ns);
        schedule(&C.rdy_ls_d, C.rdy_ls, flyback_on(false), flyback_on(false) ? C.rise_ns : C.fall_ns);
        const bool rdy_hs = C.rdy_hs && !supply_reads_low();
        const bool rdy_ls = C.rdy_ls && !supply_reads_low();
        const bool en = !has(SIM_STUCK_CHAIN_LOW) &&
                        (!sim_fs26_fs0b_asserted() || has(SIM_STUCK_FS0B_TERM)) &&
                        (C.out[HAL_DO_MCU_GATE_EN] || has(SIM_STUCK_MCU_EN_TERM)) &&
                        (rdy_hs || has(SIM_STUCK_RDY_HS_TERM)) && (rdy_ls || has(SIM_STUCK_RDY_LS_TERM)) &&
                        (C.fltok || has(SIM_STUCK_FLTOK_TERM));
        if (en && !C.drv_en) {
            release_on_en_edge(true);
            release_on_en_edge(false);
            changed = true;
        }
        if (en != C.drv_en) {
            C.drv_en = en;
            changed = true;
        }
        if (!changed) {
            break;
        }
    }
    sim_pwm_fault_inputs_changed();
}

void sim_chain_on_output(hal_do_t pin, bool level)
{
    const bool prev = C.out[pin];
    C.out[pin] = level;
    if ((pin == HAL_DO_ASC_REQ) && level && !prev && C.out[HAL_DO_ASC_CLR_N] && !has(SIM_STUCK_ASC_CLK_DEAD)) {
        C.asc_latch = true;
    }
    if ((pin == HAL_DO_FLT_CLR) && !level && prev) {
        C.oneshot = true;
        C.oneshot_end = sim_now_ns() + T_ONESHOT_NS;
    }
    sim_chain_eval();
}

void sim_chain_pad(hal_di_t pin, bool drive_low)
{
    if (pin == HAL_DI_FLT_HS_N) {
        C.pad_hs = drive_low;
    } else if (pin == HAL_DI_FLT_LS_N) {
        C.pad_ls = drive_low;
    } else {
        return;
    }
    sim_chain_eval();
}

static bool asc_cmd(void) { return C.asc_latch && (no_flt() || has(SIM_STUCK_UASCG)); }

bool sim_chain_read(hal_di_t pin)
{
    sim_chain_eval();
    switch (pin) {
    case HAL_DI_DRV_EN_RB: return C.drv_en;
    case HAL_DI_ASC_CMD_RB: return asc_cmd();
    case HAL_DI_RDY_HS: return C.rdy_hs && !supply_reads_low();
    case HAL_DI_RDY_LS: return C.rdy_ls && !supply_reads_low();
    case HAL_DI_FLT_HS_N: return flt_n(true);
    case HAL_DI_FLT_LS_N: return flt_n(false);
    case HAL_DI_SBC_INTB: return true;
    default: return false;
    }
}

/* eFlexPWM fault inputs as seen through the SIUL2 input mux (active = FLT low). */
uint8_t sim_chain_pwm_fault_pins(void)
{
    uint8_t f = 0u;
    if (!has(SIM_STUCK_PWM_FAULT_ROUTE) && sim_pwm_route_active()) { /* unbound IMCR: no route */
        f |= flt_n(true) ? 0u : HAL_PWM_FAULT_FLT_HS;
        f |= flt_n(false) ? 0u : HAL_PWM_FAULT_FLT_LS;
    }
    return f;
}

uint64_t sim_chain_next_event_ns(void)
{
    uint64_t t = UINT64_MAX;
    const delayed_t *d[3] = {&C.fltok_d, &C.rdy_hs_d, &C.rdy_ls_d};
    for (uint32_t i = 0u; i < 3u; i++) {
        if (d[i]->pend && (d[i]->due < t)) {
            t = d[i]->due;
        }
    }
    if (C.oneshot && (C.oneshot_end < t)) {
        t = C.oneshot_end;
    }
    return t;
}

void sim_chain_desat(bool hs, bool persist)
{
    if (hs) {
        C.desat_hs = true;
        C.persist_hs = persist;
        C.desat_t_hs = sim_now_ns();
    } else {
        C.desat_ls = true;
        C.persist_ls = persist;
        C.desat_t_ls = sim_now_ns();
    }
    sim_chain_eval();
}

void sim_chain_set_v5gd(float v)
{
    C.v5gd = v;
    sim_chain_eval();
}

bool sim_chain_drv_en(void)
{
    sim_chain_eval();
    return C.drv_en;
}

bool sim_chain_asc_latch(void) { return C.asc_latch; }
bool sim_chain_fault_latch(void) { return C.fault_latch; }

bool sim_chain_ls_on(void)
{
    sim_chain_eval();
    const bool via_asc = asc_cmd();
    const bool via_pwm = C.drv_en && sim_pwm_ls_cmd();
    return (via_asc || via_pwm) && !C.desat_ls;
}

bool sim_chain_hs_on(void)
{
    sim_chain_eval();
    return C.drv_en && sim_pwm_hs_cmd() && !C.desat_hs;
}
