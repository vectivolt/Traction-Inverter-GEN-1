# ISR and timing plan

Target: S32K396, a lockstep Cortex-M7. The priorities are in `src/platform/s32k396/s32k396.h`
and the binding is in `s32k396_main.c`. **WCET values are target measurements that have not been
made yet.** The columns are left empty on purpose. Fill them from a trace (SWO/ETM or GPIO toggle
plus scope) on the HIL rig, at the worst operating point: RUN at the field-weakening limit, CAN
at full load, and an NVM job in flight.

## Execution contexts

| Context | Trigger | Rate / period | NVIC prio | Work | Budget | WCET measured |
|---|---|---|---|---|---|---|
| eFlexPWM_1 fault ISR | FFLAG (FAULT0 FLT_HS, FAULT1 ADC watchdog, FAULT2 FLT_LS) | event | 0 | FW-06: ASC_REQ edge, dead-time wait, PWM-ASC. FW-15 step 1: bank to retained RAM, queue NVM, §6 decision. FW-05: OC row | FW-06 action ≤ 1.0 µs to the ASC_REQ edge (`fw06_action_us`); whole ISR ≤ 10 µs (proposed) | |
| SDADC block stamp | eDMA major loop, SDADC2 (SIN) | 10 kHz (resolver carrier) | 1 | time stamp and block counter only | ≤ 1 µs | |
| Current loop `app_isr_current` | BCTU end of list (PWM_1 SM0 VAL0/VAL1 triggers) | 2·f_sw: 20 kHz (SiC 10 kHz), 16 kHz (SiC 8 kHz), 10 kHz (IGBT 5 kHz) | 2 | phase currents, FW-05 software check, V_DC, resolver block, FOC, guards, duty write | must finish before the next half-cycle reload: < 0.5·T_sw minus the conversion time (≈ 23 µs at 20 kHz) | |
| 1 ms task `app_task_1ms` | STM_0 channel 0 | 1 kHz | 4 | slow ADC list, temperatures, HVIL, IGN, CAN RX/TX, FS26 watchdog (every 2 ms), fault manager, state machine, torque path, discharge, gate power, FW-16 steps | ≤ 400 µs (40 % CPU, proposed) | |
| FlexCAN RX | RTD FlexCAN ISR → callback | event (VCU frames every 10 ms) | 5 | copy into a 16-deep ring | ≤ 2 µs | |
| Background `app_idle` | main loop | continuous | none | NVM queue: Fee/Fls main functions | not time-critical, never on a safety path | |

The V_DC channels (ADC6_P4, ADC1_P6) run free at ≥ 200 kS/s per channel with **no ISR**. The
ADC analog watchdog acts in hardware through TRGMUX/LCU on eFlexPWM FAULT1, which takes the high
sides off. The only software involved is the fault ISR above.

### Double-update current loop, not 10 kHz

The task asked for a "10 kHz current loop". §2 of the contract sizes the current-loop ceilings
for double-update PWM, with a 0.75·T_sw sample-to-actuation delay (`s6_delay_tsw`). A
single-update 10 kHz loop at f_sw = 10 kHz leaves about 19° phase margin at the §2 ceiling. So
the loop runs at 2·f_sw. Only the IGBT SKUs (5 kHz) land exactly on 10 kHz. This is listed in
the README as a contradiction. The code does not silently change it.

## FW-06: over-voltage to ASC request

| Segment | Allocation | Source | Host model | Target |
|---|---|---|---|---|
| Divider + AMC1311B + receiver | 8.6 µs | `fw06_analog_us` | ramp lag 8.6 µs | measure |
| Sampling (free-running, 200 kS/s) | ≤ 5.0 µs | `fw06_sample_hz` | 5 sampling phases tested | ADC config |
| Conversion | 1.0 µs | `fw06_conv_us` | 1.0 µs | ADC config |
| Watchdog → FAULT1 → ISR → ASC_REQ edge | 1.0 µs | `fw06_action_us` | 0.3 µs ISR latency + code | **measure**: interrupt entry, PRIMASK sections, flash wait states |
| **Total** | **15.6 µs** | `fw06_budget_us` | `scenarios: fw06_ov_to_asc_request_within_15p6us` | HIL |

After the ASC_REQ edge, the ISR waits out the dead time before PWM-ASC: ≤ 1 µs for SiC and
≤ 3 µs for IGBT, busy-waiting inside the priority-0 ISR. This is the §4c "no sooner than the dead
time" rule. The hardware ASC latch itself engages ≥ 3.4 µs after the request, set by CASCD.

If ASC_REQ is already high, `br_enter_pwm_asc` drives it low and then high, back to back, to make
a new edge. That pulse must be at least the latch's minimum clock pulse width. Check it with a
scope on the target.

## Critical sections

`hal_crit_enter/exit` set PRIMASK. That masks the fault ISR too, which is necessary because
`nv_queue()` is called from the fault ISR and from the task. The only user is the `nv_queue`
record copy, which is ≤ 28 bytes (`nv_fault_t`), a few tens of cycles. That time adds directly to
the FW-06 action segment, so it is listed there.

## Other periodic deadlines

| Item | Period / deadline | Where |
|---|---|---|
| FS26 Q&A watchdog refresh | every 2 ms in a 3 ms window, 50 % closed (FW-12) | `fs26.c: fs26_wd_due` |
| INV_STATUS frame | 10 ms | `app.c: comms` |
| VCU command staleness | 20 ms ⇒ ramp to zero (FW-11) | `can_cmd.c: can_cmd_fresh` |
| BMS limit timeout | 100 ms ⇒ zero regen | `can_cmd.c: can_bms_fresh` |
| HVIL reaction | ≤ 100 ms (FW-09) | `hvil.c` |
| QDIS witness | 200 ms (FW-18) | `discharge.c` |
| FW-15 driver reset | ≥ 1.5 ms low, then one-shot 72–210 µs + DRV_EN RC (`cal_oneshot_wait_us`) | `bridge.c: br_rec_step` |
| SWT (MCU watchdog) | 50 ms, serviced by the task | `hal_wdog_kick` |
| Resolver | 10 kHz blocks; a gap of more than 8 blocks re-acquires (≈ 2.2 ms invalid) | `resolver.c` |
| Resolver chain latency | `cal_rslv_latency_us` (SDADC group delay + filter envelope), measured on HIL | `rslv_theta_e_at` |

Every hardware timing that the contract does not fix is a `cal_*` field. Each has its contract
default and a `[min, max]` range (`include/cal_ranges.h`, 62 items). None is invented as a
constant in the code.
