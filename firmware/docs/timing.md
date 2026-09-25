# ISR and timing plan

Target: S32K396, a lockstep Cortex-M7. The priorities are in `src/platform/s32k396/s32k396.h`
and the binding is in `s32k396_main.c`. **WCET values are target measurements that have not been
made yet** (checklist T-36, `docs/target-bringup.md`). The columns are left empty on purpose. Fill
them from a trace (SWO/ETM or GPIO toggle plus scope) on the HIL rig, at the worst operating point:
RUN at the field-weakening limit, CAN at full load, and an NVM job in flight.

## Execution contexts

| Context | Trigger | Rate / period | NVIC prio | Work | Budget | WCET measured |
|---|---|---|---|---|---|---|
| eFlexPWM_1 fault ISR | FFLAG (FAULT0 FLT_HS, FAULT1 ADC watchdog, FAULT2 FLT_LS) | event | 0 | FW-06: ASC_REQ edge, dead-time wait, PWM-ASC. FW-15 step 1: PWM off, DESAT hold started (the MCU_GATE_EN drop is deferred), bank to retained RAM, queue NVM, §6 decision. FW-05: OC row | FW-06 action ≤ 1.0 µs to the ASC_REQ edge (`fw06_action_us`); whole ISR ≤ 10 µs (proposed) | |
| SDADC frame protocol (round 16) | eDMA major loop of EACH SDADC channel: three interrupts (EXC, SIN, COS) | 3 × 10 kHz (resolver carrier) | 1 | that channel's block count checked against its DMA destination address; the first completion of a block stamps it; the last publishes the epoch (`sdadc_ring.c`) | ≤ 1 µs each | |
| Current loop `app_isr_current` | BCTU end of list (PWM_1 SM0 VAL0/VAL1 triggers) | 2·f_sw: 20 kHz (SiC 10 kHz), 16 kHz (SiC 8 kHz), 10 kHz (IGBT 5 kHz) | 2 | `br_service` (a pending MCU_GATE_EN drop), phase currents (a complete triplet or a lost sample), FW-05 software check, V_DC, resolver frame (seqlock copy of 3 × 16 samples, DMA positions before/after) and its age check, stuck-channel check (F24), FOC, guards, duty write | must finish before the next half-cycle reload: < 0.5·T_sw minus the conversion time (≈ 23 µs at 20 kHz) | |
| 1 ms task `app_task_1ms` | STM_0 channel 0 | 1 kHz | 4 | 64-bit time read (keeps the extension alive), the FS26 watchdog answer first (every second task, below), `br_service`, current-loop liveness (FW-31), slow ADC list, SWG trim (every 5 ms), temperatures, HVIL, IGN, VSUP (FW-33), CAN RX/TX, UDS requests on the diagnostic bus (≤ 4 per tick, FW-32), fault manager, arming-evidence read-back, state machine, torque path (voltage witness, the RUN-only DC-link trim), discharge + service lock, gate power, FW-16 steps | ≤ 400 µs (40 % CPU, proposed) | |
| FlexCAN RX | RTD FlexCAN ISR → callback | event (VCU frames every 10 ms) | 5 | copy into a 16-deep ring | ≤ 2 µs | |
| Background `app_idle` | main loop | continuous | none | NVM queue: Fee/Fls main functions | not time-critical, never on a safety path | |

The V_DC channels (ADC6_P4, ADC1_P6) run free at ≥ 200 kS/s per channel with **no ISR**. The
ADC analog watchdog acts in hardware through TRGMUX/LCU on eFlexPWM FAULT1, which takes the high
sides off. The only software involved is the fault ISR above.
ADC1 also converts its slow inputs (MT2_SIG P0, INTRLOK_N P7, TMOD_W S8) as injected conversions the
1 ms task starts (round 15: before, nothing started them): V_DC ch2 waits at most (1 + 3) conversions,
4 µs at the allocated 1 µs, inside the 5 µs sample wait below (`platform_cfg:
adc_schedule_follows_the_ball_map` checks it from the parameters; the measurement on the target is checklist
T-11 of `docs/target-bringup.md`).

### Double-update current loop, not 10 kHz

The task asked for a "10 kHz current loop". §2 of the contract sizes the current-loop ceilings
for double-update PWM, with a 0.75·T_sw sample-to-actuation delay (`s6_delay_tsw`). A
single-update 10 kHz loop at f_sw = 10 kHz leaves about 19° phase margin at the §2 ceiling. So
the loop runs at 2·f_sw. Only the IGBT SKUs (5 kHz) land exactly on 10 kHz. Since round 17 the
contract states this (§2): it is a decision, no longer an open item.

## FW-06: over-voltage to ASC request

| Segment | Allocation | Source | Host model | Target (checklist T-05, T-08, T-11) |
|---|---|---|---|---|
| Divider + AMC1311B + receiver | 8.6 µs | `fw06_analog_us` | ramp lag 8.6 µs | measure |
| Sampling (free-running, 200 kS/s) | ≤ 5.0 µs | `fw06_sample_hz` | 5 sampling phases tested | ADC config |
| Conversion | 1.0 µs | `fw06_conv_us` | 1.0 µs | ADC config |
| Watchdog → FAULT1 → ISR → ASC_REQ edge | 1.0 µs | `fw06_action_us` | 0.3 µs ISR latency + code | **measure**: interrupt entry, PRIMASK sections, flash wait states |
| **Total** | **15.6 µs** | `fw06_budget_us` | `scenarios: fw06_ov_to_asc_request_within_15p6us` | HIL |

After the ASC_REQ edge, the ISR waits out the dead time before PWM-ASC: ≤ 1 µs for SiC and
≤ 3 µs for IGBT, busy-waiting inside the priority-0 ISR. This is the §4c "no sooner than the dead
time" rule. The hardware ASC latch itself engages ≥ 4.42 µs after the request, set by CASCD (design-verify,
Safety A.8; ≤ 7.56 µs to the low-side gates).

If ASC_REQ is already high, `br_enter_pwm_asc` drives it low and then high, back to back, to make
a new edge. That pulse must be at least the latch's minimum clock pulse width: a scope check on the
target (checklist T-36).

## FS26 watchdog: answer inside the window (round 17, T-32)

| Quantity | Value | Source |
|---|---|---|
| Window, restarted by every answer | 3 ms, first half closed (FS_WDW_DURATION: WDW_PERIOD 0011, WDW_DC 010) | FS26 DS Rev.3 §22.6, Tables 82, 144, 145 |
| Fail-safe oscillator (times the window) | 20 MHz ± 5 % | Table 143 (FFSOSC_ACC) |
| Closed window ends | 1.5 ms / (1 ± 0.05) = 1.429–1.579 ms after the answer | |
| Open window ends | 3.0 ms / (1 ± 0.05) = 2.857–3.158 ms after the answer | |
| Task tick | STM compare, exact 1 ms grid (the MCU crystal: ppm) | `s32k396_io.c: s32k_timer_init` |
| Answer offset from its tick | 20–130 µs: task-start latency and preemption by the priority-0–2 interrupts ≤ 100 µs (assumed; measured with the WCET, T-36), two 32-bit SPI frames at 4 MHz ≤ 10 µs each (token read, answer), one retry frame for a CRC error | `app.c: app_task_1ms` (the answer first) |
| Due rule | ≥ 1500 µs since the previous answer: the second task (2000 − offset), never the first (1000 − offset), for any offset < 500 µs | `fs26.c: fs26_wd_due` |
| Answer spacing | 2000 ± 110 µs = 1890–2110 µs | |
| Margins | 311 µs after the closed window's latest end; 747 µs before the open window's earliest end | |
| Host simulation (both clocks exact, a fixed 20 µs offset) | 2000 µs on all four SKUs; `scenarios: fs26_is_answered_every_2ms_inside_its_window_at_both_oscillator_corners` at −5 / 0 / +5 % | `tests/harness.c: h_tick, isrs_until` |

Before round 17 the due rule was ≥ 2000 µs from the post-answer stamp and the answer came after the slow
list and CAN: on the exact grid that first holds on the third task, every ≈ 3.0 ms — the open window's end,
late at a fast FS26 oscillator (its window then expires at 2.857 ms) and on any tick carrying more work; one
late answer reaches WD_ERR_LIMIT = 2 (FS0B). The host harness had advanced time in relative steps, so every
FS26 SPI transfer shifted all later ticks and the answers read 2.03 ms apart. It now keeps the target's two
clocks — the current-loop trigger and the 1 ms tick, each on its own exact grid, run in time order; ISR triggers
that fall inside a long task (FW-16 step h, the FW-15 one-shot wait) are dropped, since on the target they
preempt it, and neither grid moves.

## Critical sections

`hal_crit_enter/exit` set PRIMASK. That masks the fault ISR too, which is necessary because each
user is reached from the fault ISR and from lower contexts. The users are:
- the `nv_queue` record copy, ≤ 28 bytes (`nv_fault_t`), a few tens of cycles;
- `hal_time_us64()`: one STM read and the high-word update (A12-R06), a handful of cycles;
- the bridge's DESAT-hold bookkeeping (`en_low`, `br_service`): two GPIO reads, one time read and
  at most one GPIO write (A12-R05).
Whichever of these happens to be running when the V_DC compare trips adds directly to the FW-06
action segment, so they are listed there; the `nv_queue` copy stays the longest.

## Other periodic deadlines

| Item | Period / deadline | Where |
|---|---|---|
| ASC exit, first high-side pulse (FW-06a step 3, round 17) | ≥ `cal_asc_release_ns` (1.5 µs: the ASC pins release ≤ 1.07 µs after the clear, design-verify Safety A.8) + the dead time (the low sides' turn-off), from the ASC_CLR falling edge, + one µs timer count: 3–4 µs SiC, 4–5 µs IGBT, against deadlines of 2.07 / 3.57 µs; a busy wait of at most that in the current-loop ISR, once per ASC exit | `bridge.c: br_exit_asc, br_modulate` |
| FS26 Q&A watchdog answer | every second 1 ms task, 1890–2110 µs apart, inside the 1.579–2.857 ms open window (FW-12; the budget below) | `fs26.c: fs26_wd_due`, `app.c: app_task_1ms` |
| INV_STATUS frame | 10 ms | `app.c: comms` |
| VCU command staleness | 20 ms ⇒ ramp to zero (FW-11) | `can_cmd.c: can_cmd_fresh` |
| BMS limit timeout | 100 ms ⇒ zero regen | `can_cmd.c: can_bms_fresh` |
| HVIL reaction | ≤ 100 ms (FW-09) | `hvil.c` |
| LV supply supervision (FW-33, round 17) | VSUP read through the FS26 AMUX every 1 ms (slow list). An overvoltage (> 20 V, ends < 19.5 V) is information: tolerated for `cal_vsup_ld_ms` = 500 ms (400–1000) of continuous time above `cal_vsup_jump_max_v` = 27 V (a test-B pulse: 35 V, ≤ 400 ms) and for `cal_vsup_jump_ms` = 65 s (60–120 s) per event at or below it (a 24 V / 60 s jump start); in the next millisecond after either it is sustained and takes the §6 command-lost ramp (the FW-11/FW-09 ramp) | `vsup.c: vsup_update`, `app.c: sense_slow, detect` |
| QDIS witness | 200 ms (FW-18) | `discharge.c` |
| FW-15 driver reset | ≥ 1.5 ms low, then one-shot 72–210 µs + DRV_EN RC (`cal_oneshot_wait_us`) | `bridge.c: br_rec_step` |
| DESAT hold (A12-R05) | no software MCU_GATE_EN drop for `cal_desat_en_hold_us` = 60 µs after a FLT line is first seen low (the fault latch drops DRV_EN at 22–53 µs by itself); the drop happens at the first current-loop ISR after it, so 60 µs + ≤ one ISR period (≤ 160 µs at 10 kHz) | `bridge.c: en_low, br_service` |
| 64-bit time base (A12-R06) | `hal_time_us64()` must be read at least once per 32-bit wrap (71.6 min); the 1 ms task reads it every tick. All ms stamps derive from it and wrap at 2^32 ms (49.7 days) | `hal/timer.h`, `s32k396_io.c`, `app.c: app_task_1ms` |
| SWT (MCU watchdog) | 50 ms, serviced by the task | `hal_wdog_kick` |
| Resolver | 10 kHz frames. Round 16: the angle is withdrawn when the newest coherent frame (its block start) is `cal_rslv_hold_us` = 500 µs old, checked on every current-loop tick whether or not a frame arrived (so at most one tick late: 550 µs at 20 kHz, 600 µs at 10 kHz); returning frames re-acquire (priming + 20 blocks ≈ 2.2 ms). A gap of more than 8 blocks between consumed frames also re-acquires | `resolver.c: rslv_age`, `app.c: sense_fast` |
| Current-loop liveness (round 16) | the 1 ms task declares the phase currents lost (and ages the resolver) when the last current-loop ISR entry is older than `cal_isns_stale_us` = 200 µs: a stopped BCTU is seen within one task period | `app.c: app_task_1ms` |
| SWG ramp (round 16) | from `cal_swg_code_init` one IOAMPL code per 5 ms until the monitor plane is within ±5 % of 7.2 V pp: resolver valid 20 / 25 / 35 ms after init (high / typical / low MAXAPP corner, host model) | `resolver.c: rslv_swg_trim`, `app.c: sense_slow` |
| Resolver chain latency | `cal_rslv_latency_us` (SDADC group delay + filter envelope), measured on HIL (checklist T-37) | `rslv_theta_e_at` |

Every hardware timing that the contract does not fix is a `cal_*` field. Each has its contract
default and a `[min, max]` range (`include/cal_ranges.h`, 75 items). None is invented as a
constant in the code.
