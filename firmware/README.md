# Traction inverter firmware (S32K396 + FS26)

This is the application firmware for the 220 kW / 850 V traction inverter. It covers four SKUs:
8XX SiC, 8XX IGBT, 4XX IGBT and 4XX SiC. It implements `docs/firmware-contract.md` (rev A.13),
§8/§8a of `docs/design-basis.md`, the round-12 disposition, the ball map
`calculations/mcu-ballmap.json`, the round-14 review of commit cfd35a7 (see "Round 14") and the
round-15 rechecks of commit a8c75eb (see "Round 15"). It is C11 with no dynamic memory and no
recursion, and every loop is bounded. It uses fixed-width types and single-precision float only.

The Wolfspeed CRD200 package was used only to check which structure is usual for such a
firmware. No code was taken from it.

## Build and test

```
make test          # host build of the firmware + simulation + tests, then run (cc, warnings = errors)
make target-check  # syntax-check src/platform/s32k396 on the host with the RTD calls compiled out
make target        # S32K396 build: needs S32K3_RTD=<path> and arm-none-eabi-gcc (see Makefile)
make params        # regenerate include/params_<sku>.h + cal_ranges.h (node tools/gen-params.mjs)
make board-map     # regenerate src/platform/s32k396/board_pins.h from the ball map (node)
make clean
```

`make test` compiles with `-std=c11 -Wall -Wextra -Werror -Wshadow -Wdouble-promotion
-Wmissing-prototypes -Wstrict-prototypes -Wundef -Wpointer-arith -Wcast-qual -Wvla`. Test files
alone get `-Wno-double-promotion`, because their reference arithmetic is done in double. Current
result: **223 tests, 1882 checks, 0 failures**, also with `-fsanitize=address,undefined` and at
`-O2` (`make BUILD=build/asan test CFLAGS="-O1 -g -fsanitize=address,undefined"`,
`make BUILD=build/o2 test CFLAGS=-O2`).

For the target build you need these, which are not in this repository:

- The NXP S32K3 RTD for S32K39x, with the IP drivers FlexPwm_Ip, Adc_Sar_Ip, Bctu_Ip, Sdadc_Ip,
  Dma_Ip, Trgmux_Ip, Lcu_Ip, Siul2_Port_Ip, Siul2_Dio_Ip, Lpspi_Ip, FlexCAN_Ip, Stm_Ip, Swt_Ip,
  Fee/Fls (C40), Fccu_Ip, Clock_Ip and IntCtrl_Ip.
- The S32K39 device headers.
- An S32 Config Tools project that generates the configuration symbols named in the `TODO(RTD)`
  comments.
- A linker script with two sections: `.ti_retained` (no-init, survives an MCU reset) and
  `.ti_nocache` (for the eDMA buffers).

The SKU is picked at build time with `SKU=TI_SKU_8XX_IGBT` or similar. At boot, HW_ID and the
calibration record must agree with it (FW-01/02/20).

**Arming is blocked until everything below is provisioned — fail closed, nothing is assumed.**
1. `cal_fs26_prog_id` = the M_PROGID of the FS26 OTP variant that was procured (0xFFFF = unbound).
2. The calibration record sealed to the device UID (FW-20).
3. The arming evidence (`src/safety/arm_evidence.h`, round 14), all five items:
   - **ROUTE_BOUND**: `src/platform/s32k396/s32k396_board_cfg.h` filled from the S32K39 RM IMCR table
     (`TODO(RM)`). Until then a target build stops with `#error`; filled values must be non-zero with
     two different IMCR indices (`_Static_assert`). The IMCRs are read back at run time.
   - **CONFIG_MATCHES**: the eFlexPWM fault lock-down image reads back.
   - **PROTECTION_LOCKED**: the REG_PROT soft-lock bits and the hard lock read back set
     (offsets `TODO(RM)` in `s32k396_cfg.h`). Never true by default; false in `target-check` builds.
   - **FAULT_ROUTE_VALIDATED** and **OVP_ROUTE_VALIDATED**: an EOL/HIL validation record in NVM
     (`NV_REC_VALIDATION`), CRC-sealed and bound to `TI_FW_ID`, the SKU and the device UID: the pad →
     PWM fault injection passed, and the FW-06 chain was measured within 15.6 µs.

   Anything missing is a DTC (`DTC_ARM_EVIDENCE` or `DTC_PWM_LOCK`), the state machine never leaves
   the inhibited state (no FS0B release, no FW-16 energisation, no `MCU_GATE_EN`), and INV_STATUS
   byte 15 names the missing items. The host default is "nothing validated": the test harness
   binds the route and stores a valid record the way the EOL/HIL rig would (`tests/harness.c`).

## Architecture

```
            app/app.c   integration: ISR bodies, 1 ms task, background; one g_app instance
   ┌──────────┬──────────┬───────────┬──────────┬─────────────┬─────────┐
 safety/    control/    sense/      comms/     discharge/    nvm/        pure logic: no HAL
 state      resolver    current     CAN E2E    QDIS FW-17/18 params      calls except where
 machine,   FOC, SVPWM  V_DC        UDS DTC    precharge     calib FW-20 a driver needs one
 §6 matrix, MTPA/FW     temps       FW-11      FW-19, τ      NVM log     (fs26, bridge,
 fault mgr, gains,      HVIL, IGN                            queue       gate power/self-test)
 FS26, bridge, dclink   HW_ID
 gate power/self-test
   └──────────┴──────────┴───────────┴──────────┴─────────────┴─────────┘
            hal/*.h     10 interfaces: pwm adc sdadc swg gpio spi_fs26 can nvm timer wdog
   ┌──────────────────────────────┬──────────────────────────────────────┐
 platform/s32k396                  platform/host
 ball-map tables (generated),      simulation of the card: DRV_EN chain, fault latch +
 register images, RTD bodies       one-shot, ASC latch, drivers, FS26, ADC watchdog,
 (TODO(RTD) where the SDK binds)   resolver, CAN, NVM with power loss; injection + time control
```

These rules shape the code:

- **Hardware first.** eFlexPWM_1 FAULT0 (FLT_HS_N) and FAULT2 (FLT_LS_N) force all six outputs
  low. FAULT1 is the ADC watchdog for FW-05 and FW-06 and forces the high sides only. All three
  are fail-safe with manual clear, locked at init and read back (`s32k396_pwm.c`,
  `s32k396_cfg.h`). The DRV_EN chain, the fault latch and the ASC latch act without code.
  Software only confirms, latches, logs and decides the §6 follow-up.
- **No torque without a fresh, valid command.** This needs an E2E CRC, an alive counter, age
  ≤ 20 ms, and finite values (NaN/Inf are rejected at the CAN decoder, in the torque path, in
  FOC and at the PWM write).
- **Every sensor value has a validity flag and a sample time** (`ti_meas_t`, per-channel stale
  timers).
- **One parameter set per SKU.** `include/params_<sku>.h` is generated from the contract tables
  and has 158 fields. 67 of them are `cal_*` values the contract does not fix, mostly hardware
  timings and tolerances. Each has its contract default and a `[min, max]` range
  (`include/cal_ranges.h`), checked at boot.
- **Units:** see `include/ti_types.h`. Time is `uint32_t` µs or ms, compared only through
  `ti_elapsed()`/`ti_age()`. One time domain (round 14, A12-R06, `src/hal/timer.h`): µs intervals use
  the raw counter `hal_time_us()`; every ms stamp comes from `hal_time_ms()` = (uint32)(us64 / 1000),
  where us64 is the counter extended to 64 bits (read at least once per 71.6 min wrap: the 1 ms task
  does), so ms wrap at 2^32 like any uint32. Never `hal_time_us() / 1000`. ADC values are 12-bit
  codes. Speed is mechanical rpm. Angles are electrical rad in [0, 2π).
- **Safe states.** `safety/safe_state.c` is the §6 matrix as a pure function. It has speed
  columns (n < n_x, n ≥ n_x, unknown ⇒ high), the rule (a) winding-energy screen and the rule (b)
  release. `safety/fault_mgr.c` combines rows (forced SPO wins, otherwise the highest rank) and
  owns the FW-15 retained record and the one authorised retry.
- **Timing:** see `docs/timing.md`. The current loop runs at 2·f_sw on the BCTU trigger, the
  1 ms task handles supervision, and the fault ISR is the highest priority.

## Modules (lines, `wc -l`)

| Directory | Lines | Contents |
|---|---|---|
| `include/` | 1058 | types/units, parameter struct, 4 generated SKU sets, CAL ranges |
| `src/util/` | 104 | math helpers, CRC-8 (0x1D, SAE J1850) and CRC-32 |
| `src/hal/` | 300 | the 10 HAL interfaces (timer: the 64-bit time base) |
| `src/sense/` | 611 | current (FW-05, stuck-channel check), V_DC (FW-07/18), temperatures (FW-13), HVIL (FW-09), HW_ID (FW-01/02), IGN |
| `src/control/` | 854 | resolver (FW-10), FOC/SVPWM, MTPA/field weakening/limits and the voltage witness (FW-03/04), gains, DC-link (FW-08) |
| `src/safety/` | 2077 | state machine, §6 matrix, fault manager (FW-15), FS26 (FW-12), bridge sequences + DESAT hold, gate power (FW-14), self-test (FW-16), arming evidence |
| `src/comms/` | 494 | CAN command/status with E2E (FW-11), UDS DTC store |
| `src/discharge/` | 279 | FW-17/18/19, FW-02 τ, unexpected discharge |
| `src/nvm/` | 506 | parameter sets, calibration (FW-20), NVM log and queue (validation and service records) |
| `src/app/` | 1091 | integration |
| `src/platform/s32k396/` | 1880 | generated ball-map tables, the ADC map and its derived schedule, register images, REG_PROT layout, TODO(RM) board configuration, drivers with RTD bodies |
| `src/platform/host/` | 1664 | simulation of the card, FS26 and MCU peripherals (REG_PROT, route binding, MCU reset) |
| `tests/` | 5358 | 29 suites (one file per module + time + scenarios), 223 tests, harness |
| `tools/` | 337 | parameter and board-map generators |

## What is verified where

**Host-verified** (`make test`, against `src/platform/host`):

- all decision logic: §6 rows and the energy rule on the screening motor, the fault manager,
  the state machine and §9 order, the FW-15 sequence including the one-shot/latch/ASC-latch
  model, FW-16 steps a–h with each chain term stuck permissive, the FS26 protocol (CRC, Q&A
  answer, release word from the datasheet example, error counter), CAN E2E, discharge and
  precharge plausibility, NVM A/B with torn writes, calibration and parameter validation;
- control: the current loop closed on an RL model of the screening motor, anti-windup, MTPA,
  field weakening, the resolver on synthesised SDADC blocks (−24° compensation, acquisition at
  speed, wrap, missed blocks);
- timing **as modelled**: the FW-06 chain with its allocated delays, the dead time before
  PWM-ASC, the ≥ 1 µs HS delay after ASC_CLR, and FW-15's ≥ 1.5 ms low;
- the S32K396 register images: the fault lock-down value, PWM counts and edges, mode images,
  ADC indices and thresholds (`tests/test_platform_cfg.c`), plus the ball-map binding;
- round 14: the DESAT hold on every EN-drop path (ISR, §6 decision, FW-15, FW-16), the time base
  across the 32-bit µs wrap (freshness, dwell timers, DTC stamps, the 1 s retry), keep-HV and
  "no safe state proven" at every speed, the host REG_PROT model (CPU and DMA writes rejected, a
  watchdog reset re-locks), the arming-evidence refusals, the torque→current voltage witness on a
  motor-map sweep, the stuck-channel check, and the stuck-on QDIS service lock;
- round 15: every ADC input's instance/subtype/channel against the ball map and the conversion schedule
  derived from it (the chain of every slow input, ADC1's injected chain, the V_DC ch2 gap), HW_ID read
  from a started conversion, and a battery-path loss while armed at zero/low/high/unknown speed ×
  OPEN/INVALID/stale report (row, same-invocation outputs, targets, PWM/ASC, CAN status).

**Needs the target, HIL or the bench:**

- every `TODO(RTD)` binding; the REG_PROT offsets and bits (`TODO(RM)`, `s32k396_cfg.h`) and whether
  REG_PROT covers eFlexPWM_1 and the SIUL2 IMCRs on the S32K39 (else XRDC);
- the IMCR values of PTC26/PTC25 → FAULT0/2 (`s32k396_board_cfg.h`, `TODO(RM)`), and the ADC
  watchdog → TRGMUX/LCU → FAULT1 path; the EOL/HIL rig that writes the validation record;
- the ADC chain read-back at init (NCMR/JCMR names `TODO(RTD)`), the BCTU list read-back (`TODO(RM)`), and
  the V_DC ch2 sample gap with ADC1's three injected conversions (round 15);
- real latencies and WCET (`docs/timing.md`);
- FS26 behaviour on silicon: OTP image, watchdog answer width, FS0B release after an RSTB,
  INIT_FS re-entry after an MCU reset;
- SDADC/SWG configuration, `cal_rslv_latency_us`, EOL resolver trim;
- sensor gains and offsets, thermal model, discharge τ;
- every `cal_*` value.

## FW-xx mapping

The full matrix, down to function and test name, is in [`docs/traceability.md`](docs/traceability.md).
It also maps every edge case from the task to its test.

| FW | Code | Tests | Host | Target item left |
|---|---|---|---|---|
| 01, 02 | sense/hwid.c, app.c (slow list before HW_ID), discharge.c (τ) | hwid, discharge, scenarios | yes | divider tolerance |
| 03, 04 | control/torque.c (+ voltage witness), sense/temp.c | torque, state_machine, scenarios | yes | dyno, thermal |
| 05 | sense/current.c (+ stuck channel), app.c, s32k396_adc/pwm.c | current, params, platform_cfg, scenarios | logic + modelled compare | ADC WD → FAULT1 route |
| 06, 06a | sense/vdc.c, app.c, safety/bridge.c | vdc, bridge, scenarios | yes, modelled timing | 15.6 µs on HIL |
| 07 | sense/vdc.c | vdc, scenarios | yes | EOL gains |
| 08, 08b | control/dclink.c, safe_state.c, fault_mgr.c, can_cmd.c, app.c (battery path at every speed), state_machine.c | dclink, safe_state, fault_mgr, state_machine, scenarios | yes | bank tuning |
| 09 | sense/hvil.c | hvil, scenarios | yes | harness |
| 10 | control/resolver.c | resolver, scenarios | yes | SDADC/SWG config, latency |
| 11 | comms/can_cmd.c, torque.c | can_cmd, torque, scenarios | yes | vehicle DBC |
| 12 | safety/fs26.c | fs26, scenarios | yes (model of the FS26) | OTP image, silicon |
| 13 | sense/temp.c | temp | yes | sensor parts |
| 14 | safety/gate_power.c | gate_power | yes | RDY timings |
| 15 | app.c, fault_mgr.c, bridge.c (+ DESAT hold), arm_evidence.c, s32k396_pwm.c | fault_mgr, bridge, safe_state, gate_selftest, calib, scenarios, platform_cfg | yes | IMCR values, REG_PROT offsets, validation rig |
| 16 | safety/gate_selftest.c | gate_selftest, state_machine | yes | chain timings |
| 17, 18, 19 | discharge/discharge.c, app.c (service lock) | discharge, state_machine, scenarios | yes | resistor thermal |
| 20 | nvm/calib.c, nvm/nvlog.c | calib, nvlog, scenarios | yes | Fee config, UID |
| 21 | not implemented (bootloader + HSE) | none | no | all |

## Round 14 (three reviews of commit cfd35a7)

**Ball map rev A.13 (regenerated `board_pins.h`).** The A.12 map had H5 on the 5 V reference (it is V15, 1.5 V)
and J7 grounded (it is V25); the map was re-derived against NXP's GEN3 net report and four signals moved to
netlist-confirmed balls: HW_ID → B5 (PTE0, ADC3_P0), NTC_A → T15 (PTC11, ADC5_S11), MT2_SIG → D5 (PTE26,
ADC1_P0), ASC_REQ → U4 (PTD7, MSCR 103). `make board-map` after any ball-map change; `tests/test_board_map.c`
pins the safety GPIO MSCR indices.

Rule for every item: **fail closed** — when evidence is missing the gates stay inhibited. Each
change has regression tests that fail on the pre-fix source and pass now (the before/after run is
described in `docs/traceability.md`).

| ID | Defect | Change | Tests |
|---|---|---|---|
| A12-R05 | The fault ISR's FLT branch, and every SPO/FW-15/FW-16 path, lowered `MCU_GATE_EN` at once. It is an undelayed input of the DRV_EN AND gate, while the fault latch delays FLT → DRV_EN 22–53 µs so the NSI6611 finishes its soft turn-off (RST/EN during it is unspecified). | The PWM inhibit stays immediate. Once a FLT line is seen low, no software path lowers `MCU_GATE_EN` until `cal_desat_en_hold_us` (60 µs, range 55–250) after that first sight. The hold lives in `bridge.c`: every drop goes through `en_low()` (br_spo, br_rec_start, the FW-15 failure path, FW-16 through br_spo), which only records a pending drop; `br_service()` (current-loop ISR, 1 ms task) carries it out. The hold start is a fresh time read after the FLT lines were read. Without a FLT line low the drop is immediate. Arming and PWM-ASC entry are refused while a drop is pending. | bridge: `desat_hold_keeps_en_until_the_hold_then_drops_it`, `desat_hold_covers_br_rec_start_and_recovery_still_works`, `non_desat_spo_drops_en_at_once`; gate_selftest: `failed_test_with_a_desat_drops_en_only_after_the_hold`; scenarios: `desat_at_speed_holds_en_through_the_hold_then_reset_and_pwm_asc`, `desat_at_low_speed_spo_waits_for_the_hold`; params: `round14_cal_defaults_and_ranges` |
| A12-R06 | ms = `hal_time_us()/1000` jumped 4294967 → 0 at the 32-bit µs wrap: an 8 ms-old frame read 4 290 672 337 ms old (false VCU/BMS staleness), dwell timers expired, a DESAT retry could start ≈100 ms after the event. | One time domain (`hal/timer.h`): `hal_time_us64()` extends the counter (the same `ti_time64_extend` on both platforms, inside a critical section), `hal_time_ms()` = (uint32)(us64/1000). Every ms producer converted (app.c, nvlog.c; the 1 ms task derives µs and ms from one 64-bit read). DTC first-occurrence stamp no longer treats 0 as "unset". The simulation can start next to the wrap (`sim_reset_at_us`). | time: `us64_extension_survives_repeated_wraps`, `sim_clock_across_the_microsecond_wrap`, `can_freshness_across_the_microsecond_wrap`, `dtc_time_stamps_across_the_microsecond_wrap`; scenarios: `running_across_the_microsecond_wrap_keeps_fresh_frames_fresh`, `boot_across_the_microsecond_wrap_reaches_armed`, `desat_retry_waits_1s_across_the_microsecond_wrap`, `dtc_time_stamps_across_the_microsecond_wrap_in_the_application` |
| A12-R08 | `keep_hv` only after a DESAT at n ≥ n_x; a 0 rpm, 340 A rms SPO relying on rule (b) reported keep_hv = 0. | `keep_hv` = the action holds SPO, rule (a) fails and the battery is present, at any speed, for every row whose premise is not a lost battery; re-evaluated each update, so it holds until rule (a) is met for the present current/speed, and ASC clears it. Battery absent: keep_hv = 0 and "no safe state proven" (fault_mgr `no_safe_state`, INV_STATUS b14.0, live) plus DTC_SPO_ENERGY. | safe_state: `keep_hv_follows_the_battery_as_the_sink_at_every_speed` (48-case cross product); fault_mgr: `keep_hv_held_until_rule_a_and_no_safe_state_without_battery`; scenarios: `standstill_desat_at_rated_current_reports_keep_hv_on_can`; `row_flt_ls_is_spo_only` updated |
| F01 | `TI_IMCR_*` placeholders 0u, written by `lock_faults()` (IMCR[0] = 0). | `s32k396_board_cfg.h` (TODO(RM)) `#error`s in a target build until filled; filled values must be non-zero with two different IMCR indices (`_Static_assert`); nothing is written while unbound; `hal_pwm_fault_route_bound()` reads the IMCRs back. Host: UNBOUND, and an unbound route does not carry FLT to the PWM. | platform_cfg: `fault_route_unbound_by_default`; scenarios: `unbound_fault_route_never_arms` |
| F02 | `hal_pwm_config_locked()` true from an image read-back; REG_PROT was a TODO. | `hal_pwm_config_matches()` (image) and `hal_pwm_protection_locked()` (REG_PROT SLBR bits + GCR.HLB read back, generic S32K3 layout, offsets TODO(RM); false unless every bit reads set, false without hardware). `lock_faults()` sets the soft locks, then the hard lock. CTRL2 is not locked (its FORCE bit is written at every mode change); its INDEP bit is read back every tick instead. Host: a REG_PROT model rejects writes from any master after lock; an MCU reset unlocks until re-init. | platform_cfg: `regprot_lock_decision_needs_every_bit_read_back`, `protected_registers_reject_writes_from_cpu_and_dma`; scenarios: `watchdog_reset_relocks_before_rearming` |
| F06 | No arming evidence. | `safety/arm_evidence.c`: ROUTE_BOUND, CONFIG_MATCHES, PROTECTION_LOCKED, FAULT_ROUTE_VALIDATED, OVP_ROUTE_VALIDATED; the last two from `NV_REC_VALIDATION` (CRC-32, `TI_FW_ID`, SKU, device UID, measured FW-06 chain ≤ 15.6 µs). Incomplete ⇒ init FAIL (no FS0B release, no FW-16, no EN); read back every tick (a loss while armed goes through the §6 "control lost" row); INV_STATUS b15 = missing items. | calib: `validation_record_binds_image_card_and_crc`; scenarios: `arming_refused_without_evidence_and_the_status_names_it`, `arming_permitted_with_a_valid_record`, `arming_refused_on_record_identity_or_crc_mismatch`, `evidence_lost_while_armed_goes_through_section6` |
| F23 | Demag and circle clamps after field weakening, no voltage check: a finite infeasible (id, iq). | A final witness `torque_v_required()` ≤ `torque_v_available()` (Rs, Ld, Lq, ψ, ω_e, `cal_vdyn_reserve_frac` 5 %): reduce iq, then id toward the demag limit; `TQ_INFEASIBLE` if not even iq = 0 fits ⇒ zero torque, speed-limit request (b14.3), DTC_TORQUE_INFEASIBLE. | torque: `witness_motor_map_sweep_never_returns_an_infeasible_pair`, `field_weakening_holds_voltage_ellipse_and_demag_clamp` (tightened); scenarios: `infeasible_current_gives_zero_torque_speed_limit_and_dtc` |
| F24 | KCL misses a channel stuck at zero at zero current, below its tolerance, or all three stuck. | Activity check while modulating (`cal_isns_act_min_a` 20 A, `cal_isns_act_frac` 0.2, `cal_isns_act_debounce` 20), per channel and angle-aware; latched `stuck_fault` ⇒ currents invalid ⇒ "control lost" row, DTC_ISNS_STUCK. Window/range/stale checks unchanged. Coverage per operating state: `docs/traceability.md`. | current: `activity_catches_a_stuck_channel_where_current_is_asked`; scenarios: `all_three_current_channels_stuck_detected_under_command`, `one_current_channel_stuck_below_the_kcl_tolerance_detected` |
| 9 | A QDIS shorted with the battery connected bypasses the 5 s release (4 × 96 W) and showed only as a DTC at the next opening. | The unexpected-discharge verdict (QDIS not commanded, contactors open, bridge not modulating — new) latches "service required": no arming, INV_STATUS b14.1 (do not re-energise) and b14.2 (open the contactors), kept in NVM (`NV_REC_DTC`) and re-applied at every boot. | discharge: `unexpected_discharge_judged_only_with_nothing_drawing_on_the_link`; scenarios: `stuck_on_qdis_latches_service_required_and_never_rearms` |

Test infrastructure changed with it: the harness now generates phase currents per current-loop
sample as an ideal current loop in the controller's dq frame (it used to inject 0 A or a fixed
d-axis amplitude at the mechanical angle, which left the FOC saturated and the FW-10 rate check
never reached), provisions the route binding and the validation record, and INV_STATUS grew bytes
14–15 (`can_cmd.h`). Existing tests changed only in signatures (`br_init`, `st_step`, `dis_step`,
`hal_time_ms`) except `row_flt_ls_is_spo_only` and `field_weakening_holds_voltage_ellipse_and_demag_clamp`,
whose expectations follow A12-R08 and F23.

## Round 15 (rechecks of commit a8c75eb)

Two confirmed defects, same rule: **fail closed**. Each change has regression tests that fail on the
pre-fix source and pass now (method and per-test results: `docs/traceability.md`, "Round 15").

| ID | Defect | Change | Tests |
|---|---|---|---|
| A13-R04 | ADC channel class not propagated. Round 14 moved NTC_A to T15 = PTC11 = ADC5_S11 (a standard input), but `s32k396_adc.c` MAP[] kept a hand-written `'P'`: `S32K3_ADC_CH('P', 11)` = 11 selected PCDR11 of an 8-entry array (UBSan: out of bounds; the read returned the register after PCDR7) instead of ICDR11 (= 43). The schedule was hand-listed too: `hal_adc_start_slow()` started the normal chains of ADC0/3/4/5, so ADC1's injected chain — INTRLOK_N, TMOD_W and now MT2_SIG (moved to ADC1_P0, next to the continuous V_DC ch2) — never ran; and HW_ID (ADC3_P0, slow list) was classified in `app_init` before any slow list had run, from a "never converted" 0 (a false "HW_ID short" on the target). | `gen-board-map.mjs` emits the full triple `BP_<NET>_INST/_SUB/_CHAN` for every peripheral signal and refuses an ADC input that does not exist (P8+, S24+). MAP[] = `TI_ADC_MAP_INIT` (`s32k396.h`), one `TI_ADC_ROW(net, group)` per HAL input with all three from `board_pins.h`: no per-pin letter left. `S32K3_ADC_CH()` is strict (any other pair is `TI_ADC_CH_INVALID`), and `cdr()` reads only PCDR0–7 / ICDR0–23 (else "never converted"). The schedule is derived from MAP (`s32k396_cfg.h`: `adc_slow_chain`, `adc_chain_mask`): the 1 ms list starts each instance's chain (normal on ADC0/3/4/5, injected on ADC1); `hal_adc_init()` reads every chain's NCMR/JCMR back against the ball map and refuses a mismatch (a stale Config Tools project) or a mapped input on an instance it leaves off. `init_identity()` starts the slow list before each HW_ID sample. ADC1's ch2 gap is (1 + 3) conversions ≤ the 5 µs FW-06 allocation (checked from the params). `TI_FW_ID` 0x0A0D000F: a round-14 validation record measured the FW-06 chain with the old ADC1 schedule. | platform_cfg: `adc_map_matches_the_ball_map`, `adc_schedule_follows_the_ball_map`; scenarios: `hw_id_is_converted_before_it_is_classified` |
| A13-R02 (review 3) | Low-speed contactor loss. `detect()` raised the battery-lost row only for `(contactors != CLOSED) && (ti_absf(speed) >= n_x)`: at a known low speed or standstill an OPEN or INVALID report — and at any speed a stale one, which keeps the last CLOSED — raised nothing unless V_DC disagreed with the pack. `st_run()` set `arm` and `torque_enable` before its exits, so the invocation that left RUN still permitted the requested torque; the next ticks disarmed through PRECHARGE_WAIT with EN low: all gates off with whatever current the winding held. | Armed (ARMED_ZERO_TORQUE/RUN/DERATE) the battery path must be proven at every speed: contactors reported CLOSED in a fresh VCU frame; OPEN, PRECHARGE, INVALID and a stale report are all "lost" (and V_DC off the pack, as before). The §6 row decides: below n_x zero torque at the current-loop rate under current control while the winding current is ≥ `cal_spo_release_a` — a §6 permission that does not depend on the state's arm; FW-06 stays the OV backstop — then SPO with EN low; at n ≥ n_x or unknown speed LS-ASC; without current control or ASC the other rows and the energy rule apply as before. The row holds while that response energises the bridge, then clears: unarmed, open contactors are the precharge sequence. It is the FAULT state while the response has energy to manage; a loss met with nothing to manage (SPO at once, V_DC at the pack: the FW-08 zero-torque opening) is not (`fm_needs_fault_state(f, done)`). State-machine outputs describe the state an invocation ends in: leaving RUN/DERATE grants no torque (to FAULT, DISCHARGE or SAFE_POWERDOWN no arm either); an active battery-path row (`sm_in_t.battery_lost`) leaves RUN like an open contactor and blocks re-entry. | scenarios: `low_speed_open_contactor_is_a_battery_path_loss` (the reviewer's reproduction), `battery_path_loss_while_armed_at_every_speed` (zero/low/high/unknown × OPEN/INVALID/stale, 340 A rms, regenerating), `zero_torque_opening_at_standstill_disarms_without_fault` (guard), `stale_can_takes_torque_to_zero_not_held`; state_machine: `leaving_run_grants_no_torque_in_the_same_invocation`, `battery_path_row_leaves_run_and_blocks_reentry`; fault_mgr: `battery_lost_is_a_fault_until_its_response_is_done`; safe_state: `unknown_speed_takes_high_column` (battery row added) |

Test infrastructure: the host sim can model the target's slow list (`sim_adc_require_slow_start()`: only
the phase currents and V_DC are converted before the list is first started; off by default). Existing
tests changed: `stale_can_ramps_to_zero_not_held` became `stale_can_takes_torque_to_zero_not_held` (a stale
report under torque is now a battery-path loss: zero torque at the current-loop rate instead of the FW-11
ramp; it also checks the re-arm through precharge); `dtc_time_stamps_across_the_microsecond_wrap_in_the_application`
runs at zero torque (under torque the loss disarms and the timeout DTC stops being re-stamped); the
`fm_needs_fault_state()` call sites (signature).

## Contract contradictions and open items (not silently changed)

1. **V5GD pin.** The contract names PTB5. Ball map rev A.12 puts V5GD_SNS on PTD27 (ADC4_P6),
   and PTB5 is RDY_LS. The code follows the ball map (`test_board_map`).
2. **FW-05 fault path.** FW-05 routes the OC compare to an "eTPU fault input". §4c and FW-15 use
   the eFlexPWM FAULT inputs. The code uses eFlexPWM_1 FAULT1 via TRGMUX/LCU, high sides only.
   This route has to be confirmed on the S32K39.
3. **Current loop rate.** The task says a "10 kHz current loop", but §2 assumes double update.
   The loop runs at 2·f_sw (`docs/timing.md`).
4. **State order.** The task lists PRECHARGE_WAIT before GATE_SELFTEST. §9 needs the self-test
   first. The code follows §9.
5. **FW-07 low-voltage floor.** The 5 % disagreement check has no floor, so
   `cal_vdc_disagree_floor_v` (18 V) was added.
6. **FW-12 "OTP readback".** The FS26 exposes the OTP bank only in debug/OTP mode. The firmware
   checks PROG_ID, OTP_CORRUPT, DBG_MODE, the INIT registers and their complements. A per-field
   OTP comparison belongs at EOL.
7. **FW-05 safe state.** FW-05 does not say which outputs the compare disables or what the safe
   state is afterwards. The code uses high sides off plus the "control lost" row.
8. **§6 gaps.** §6 has no row for V_DC invalid with V5GD healthy (the code uses SPO with the
   energy rule, falling back to LS-ASC). It also has no rule for unknown speed (the code holds
   the last valid speed for `cal_speed_hold_ms`, then takes the n ≥ n_x column).
9. **FW-04 recovery time.** FW-04 gives no peak recovery time. It is `cal_peak_recovery_s` =
   180 s. Unknown coolant temperature means no peak.
10. **FW-19 τ check.** With a 5 % low plateau the τ signature is weak, so the plateau check is
    the primary one.
11. **FS26 release.** FS0B release needs FLT_ERR_CNT = 0, which takes several good refreshes
    after a reset. The watchdog answer width and FS26 behaviour after an MCU reset must be
    checked on silicon.
12. **Lost DESAT record.** A brown-out that also loses retained RAM during a DESAT loses that
    DESAT record. The A/B scheme keeps the previous one.
13. **BMS timeout.** On a BMS timeout the code zeroes regen only, as the contract says.
14. **FW-21** (signed images, rollback) is out of scope for the application image.
15. **FW-08b wording (round 14).** The contract reports "keep HV connected" after a DESAT at
    n ≥ n_x until ASC or n < n_x. Following §6 rule (b) (A.12), the firmware reports it whenever an
    SPO relies on the battery — rule (a) fails and the battery is present — at any speed and for any
    row that ends in SPO, until rule (a) holds or ASC is active; with the battery absent it reports
    "no safe state proven" instead. The contract text should say this.
16. **Immediate MCU_GATE_EN low (§4c V5GD, §7 steps 1–2).** With a FLT line low, the firmware now
    keeps `MCU_GATE_EN` for `cal_desat_en_hold_us` before lowering it (the latch path holds DRV_EN
    22–53 µs anyway). A dead V5GD reads FLT low, so the V5GD drop can come 60–160 µs later than the
    ASC_CLR; the hovering case (FLT high) is still immediate.
17. **IMCR "distinct" values.** The review asked for four distinct non-zero values. The two SSS
    values select inside two different IMCRs and may legitimately be equal, so the check requires
    non-zero values and two different IMCR indices.
18. **`br_service(now_us)`.** The review sketched a time argument. `br_service()` reads its own
    time after reading the FLT lines: a caller's stamp can predate the FLT edge and would end the
    hold early.
19. **Service lock.** "Service required" (stuck-on QDIS) survives key cycles in NVM. Clearing it
    needs a service routine (UDS) that is not implemented here, like FW-21.
20. **Dynamic voltage reserve.** The contract fixes no reserve for the torque reference;
    `cal_vdyn_reserve_frac` (5 % of the FOC voltage limit) is new.
21. **REG_PROT layout.** The generic S32K3 layout (SLBR at module + 0x1800, GCR at + 0x1FFC, HLB
    bit 31, SLB = low nibble) is assumed until checked against the S32K39 RM (`TODO(RM)`). If
    REG_PROT does not cover eFlexPWM_1 or the SIUL2 IMCRs, XRDC is the fallback and
    `hal_pwm_protection_locked()` must read that instead.
22. **Validation record producer.** The EOL/HIL rig that injects FLT at the pads, measures the
    FW-06 chain and writes `NV_REC_VALIDATION` (`arm_validation_t`) is not in this repository.
23. **Equal gain errors on all three current channels** are not observable with three sensors in
    closed loop (F24); the coverage table in `docs/traceability.md` says what bounds them.
24. **FW-11 vs §6 (round 15).** FW-11 ramps a stale command to zero. A stale report also leaves the
    contactor state unknown, so while armed it is the §6 battery-lost row ("contactor/precharge feedback
    invalid"), and that row wins: zero torque at the current-loop rate below n_x, LS-ASC above. The FW-11
    ramp remains for a command lost with the battery path still proven (HVIL open). The contract should
    say so.
25. **When the battery-lost row applies (round 15).** §6 names the row but not when it is evaluated.
    The firmware evaluates it whenever armed, at every speed, with OPEN, PRECHARGE, INVALID and a stale
    report as "lost"; it is a FAULT only while its response still holds energy (current control or
    ASC); the FW-08 zero-torque opening below n_x with no winding current is a normal disarm.
26. **DC-link trim (FW-08), observed, not changed.** Under that row below n_x, `zero_now` holds iq at 0
    at the current-loop rate while `t_cmd_nm` carries the DC-link PI output (`dcl_step`, V_ref = the
    normal-range maximum): the trim never reaches the current references, and INV_STATUS reports it as
    the torque (−50 Nm with the link at a 750 V pack) while zero torque is applied. Either the trim is
    meant to act (then its V_ref must not drive the isolated link toward the OV trip) or the status and
    `t_cmd_nm` should read zero; FW-08's wording decides.
27. **ADC1 injected chain (round 15).** The FW-06 sample wait (≤ 5 µs) now includes ADC1's three
    injected conversions (MT2_SIG, INTRLOK_N, TMOD_W): (1 + 3) × 1 µs = 4 µs at the allocated
    conversion time — to be measured on the target (or MT2_SIG moved off ADC1).
28. **BCTU list read-back.** The ADC chain masks are read back at init; the BCTU list is not yet
    (LISTCHR layout, `TODO(RM)`).
29. **Image identity.** `TI_FW_ID` is 0x0A0D000F: a new EOL/HIL validation record is needed for this
    image before it arms.
