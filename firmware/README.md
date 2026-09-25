# Traction inverter firmware (S32K396 + FS26)

This is the application firmware for the 220 kW / 850 V traction inverter. It covers four SKUs:
8XX SiC, 8XX IGBT, 4XX IGBT and 4XX SiC. It implements `docs/firmware-contract.md` (rev A.15),
§8/§8a of `docs/design-basis.md`, the round-12 disposition, the ball map
`calculations/mcu-ballmap.json`, the round-14 review of commit cfd35a7 (see "Round 14"), the
round-15 rechecks of commit a8c75eb (see "Round 15"), the round-16 rechecks of commit 32214be (see
"Round 16") and the round-17 closure of every open item (see "Round 17": each one is now a decision in the
contract or a row of the target checklist `docs/target-bringup.md`). It is C11 with no dynamic memory and no
recursion, and every loop is bounded. It uses fixed-width types and single-precision float only.

The Wolfspeed CRD200 package was used only to check which structure is usual for such a
firmware. No code was taken from it.

## Build and test

```
make test          # host build of the firmware + simulation + tests, then run (cc, warnings = errors)
make target-check  # syntax-check src/platform/s32k396 on the host with the RTD calls compiled out, and check
                   # that every TODO(<kind>) marker has its row in docs/target-bringup.md (and back)
make target        # S32K396 build: needs S32K3_RTD=<path> and arm-none-eabi-gcc (see Makefile)
make params        # regenerate include/params_<sku>.h + cal_ranges.h (node tools/gen-params.mjs)
make board-map     # regenerate src/platform/s32k396/board_pins.h from the ball map (node)
make clean
```

`make test` compiles with `-std=c11 -Wall -Wextra -Werror -Wshadow -Wdouble-promotion
-Wmissing-prototypes -Wstrict-prototypes -Wundef -Wpointer-arith -Wcast-qual -Wvla`. Test files
alone get `-Wno-double-promotion`, because their reference arithmetic is done in double. Current
result: **256 tests, 2310 checks, 0 failures**, also with `-fsanitize=address,undefined` and at
`-O2` (`make BUILD=build/asan test CFLAGS="-O1 -g -fsanitize=address,undefined"`,
`make BUILD=build/o2 test CFLAGS=-O2`).

For the target build you need these, which are not in this repository:

- The NXP S32K3 RTD for S32K39x, with the IP drivers FlexPwm_Ip, Adc_Sar_Ip, Bctu_Ip, Sdadc_Ip,
  Dma_Ip, Trgmux_Ip, Lcu_Ip, Siul2_Port_Ip, Siul2_Dio_Ip, Lpspi_Ip, FlexCAN_Ip, Stm_Ip, Swt_Ip,
  Fee/Fls (C40), Fccu_Ip, Clock_Ip and IntCtrl_Ip.
- The S32K39 device headers.
- An S32 Config Tools project that generates the configuration symbols named in the RTD markers
  (`docs/target-bringup.md` lists every one).
- A linker script with two sections: `.ti_retained` (no-init, survives an MCU reset) and
  `.ti_nocache` (for the eDMA buffers).

The SKU is picked at build time with `SKU=TI_SKU_8XX_IGBT` or similar. At boot, HW_ID and the
calibration record must agree with it (FW-01/02/20).

**Arming is blocked until everything below is provisioned — fail closed, nothing is assumed.**
1. `cal_fs26_prog_id` = the M_PROGID of the FS26 OTP variant that was procured (0xFFFF = unbound).
2. The calibration record sealed to the device UID (FW-20).
3. The arming evidence (`src/safety/arm_evidence.h`, round 14), all five items:
   - **ROUTE_BOUND**: `src/platform/s32k396/s32k396_board_cfg.h` filled from the S32K39 RM IMCR table
     (checklist T-01). Until then a target build stops with `#error`; filled values must be non-zero with
     two different IMCR indices (`_Static_assert`). The IMCRs are read back at run time.
   - **CONFIG_MATCHES**: the eFlexPWM fault lock-down image reads back.
   - **PROTECTION_LOCKED**: the REG_PROT soft-lock bits and the hard lock read back set
     (offsets in `s32k396_cfg.h`, checklist T-03). Never true by default; false in `target-check` builds.
   - **FAULT_ROUTE_VALIDATED** and **OVP_ROUTE_VALIDATED**: an EOL/HIL validation record in NVM
     (`NV_REC_VALIDATION`), CRC-sealed and bound to `TI_FW_ID`, the SKU and the device UID: the pad →
     PWM fault injection passed, and the FW-06 chain was measured within 15.6 µs (checklist T-05; this image
     is `TI_FW_ID` 0x0A0F0011).

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
 machine,   FOC, SVPWM  V_DC        UDS DTC,   precharge     calib FW-20 a driver needs one
 §6 matrix, MTPA/FW     temps       UDS 0x27/  FW-19, τ      NVM log     (fs26, bridge,
 fault mgr, gains,      HVIL, IGN   0x31 FW-32               queue       gate power/self-test)
 FS26, bridge, dclink   HW_ID, VSUP FW-11
 gate power/self-test
   └──────────┴──────────┴───────────┴──────────┴─────────────┴─────────┘
            hal/*.h     10 interfaces: pwm adc sdadc swg gpio spi_fs26 can nvm timer wdog
                        + sdadc_ring.c: the resolver frame protocol both platforms run (round 16)
   ┌──────────────────────────────┬──────────────────────────────────────┐
 platform/s32k396                  platform/host
 ball-map tables (generated),      simulation of the card: DRV_EN chain, fault latch +
 register images, RTD bodies       one-shot, ASC latch, drivers, FS26, ADC watchdog, the
 (RTD markers where the SDK binds) resolver excitation chain + per-channel eDMA, CAN, NVM with
                                   power loss; injection + time control
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
  timers). Round 16: a phase-current triplet counts only when all three channels of one trigger arrived,
  a resolver frame only when all three channels of one epoch did, and the resolver angle expires
  `cal_rslv_hold_us` after its newest frame whether or not a new one arrives.
- **One parameter set per SKU.** `include/params_<sku>.h` is generated from the contract tables
  and has 174 fields. 75 of them are `cal_*` values the contract does not fix, mostly hardware
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
| `include/` | 1149 | types/units, parameter struct, 4 generated SKU sets, CAL ranges |
| `src/util/` | 104 | math helpers, CRC-8 (0x1D, SAE J1850) and CRC-32 |
| `src/hal/` | 458 | the 10 HAL interfaces (timer: the 64-bit time base), the shared resolver frame protocol (round 16) |
| `src/sense/` | 676 | current (FW-05, stuck-channel check, lost triplets), V_DC (FW-07/18), temperatures (FW-13), HVIL (FW-09), HW_ID (FW-01/02), IGN, the LV supply (FW-33) |
| `src/control/` | 923 | resolver (FW-10: bounded hold, amplitude planes, SWG ramp), FOC/SVPWM, MTPA/field weakening/limits and the voltage witness (FW-03/04), gains, the DC-link trim (FW-08: a regen limiter in RUN) |
| `src/safety/` | 2102 | state machine, §6 matrix, fault manager (FW-15), FS26 (FW-12, + its AMUX for FW-33), bridge sequences + DESAT hold and the ASC-exit release wait, gate power (FW-14), self-test (FW-16), arming evidence |
| `src/comms/` | 684 | CAN command/status with E2E (FW-11), UDS DTC store, UDS SecurityAccess + the service-lock routine (FW-32) |
| `src/discharge/` | 279 | FW-17/18/19, FW-02 τ, unexpected discharge |
| `src/nvm/` | 520 | parameter sets (+ the exciter plane checks), calibration (FW-20), NVM log and queue (validation and service records) |
| `src/app/` | 1196 | integration (+ the current-loop liveness check FW-31, the diagnostic bus) |
| `src/platform/s32k396/` | 1914 | generated ball-map tables, the ADC map and its derived schedule, register images, REG_PROT layout, the board configuration the RM fills, drivers with RTD bodies (three SDADC DMA interrupts) |
| `src/platform/host/` | 1937 | simulation of the card, FS26 (its oscillator tolerance and AMUX) and MCU peripherals (REG_PROT, route binding, MCU reset, per-channel eDMA, the excitation chain) |
| `tests/` | 6718 | 31 suites (one file per module + time + sdadc + uds + scenarios), 256 tests, harness (two exact clocks, ADC-level noise) |
| `tools/` | 365 | parameter and board-map generators |

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
  PWM-ASC, the first HS pulse after ASC_CLR (`cal_asc_release_ns` + the dead time from its falling edge), and
  FW-15's ≥ 1.5 ms low;
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
  OPEN/INVALID/stale report (row, same-invocation outputs, targets, PWM/ASC, CAN status);
- round 16: the resolver angle's expiry at the hold (standstill, low speed across the µs wrap, 10 000 rpm)
  with the §6 response and a controlled re-acquisition, empty reads at 10/20 kHz that never fault; the frame
  protocol on a per-channel eDMA model (a frozen or late channel, a completion between channel copies, a
  preempted reader, interrupts held off, the epoch wrap); every combination of missing phase channels, a
  stopped current loop; the exciter planes (three SWG corners, a 25 Ω resolver, a tripped PTC);
- round 17: zero current (id = iq = 0) under the battery-lost row at 1000 rpm and in field weakening at
  7000 rpm, reported as 0 Nm, with the DC-link trim never engaged; the trim as a regen limiter in RUN with the
  battery present (idle in range, taking regen back above it, handing it back); the UDS service routine
  refused without a key, with HV present, with the bridge armed, and accepted with a (test) key — the clear
  taking effect at the next power-up; the SecurityAccess protocol (seed/key, one key per seed, the attempt
  lockout, malformed requests); the FS26 answered every 2 ms on the target's exact tick grid at its oscillator's
  −5 / 0 / +5 %; the first high-side pulse after an ASC exit behind the release deadline (SiC and IGBT); KL30
  at 35 V for 400 ms and a 24 V (and 26.5 V) jump start for 60 s as information, a longer overvoltage taking
  the orderly ramp and recovering.

**Needs the target, HIL, EOL or the bench:** everything is in **[`docs/target-bringup.md`](docs/target-bringup.md)**
— one row per open marker in the sources (RTD, RM, HW-RM, HW, EOL, REL), with the acceptance check and what
holds the image closed until then, plus the WCET of `docs/timing.md`, the commissioning values (`cal_*`) and
the vehicle-integration items. `make target-check` keeps the markers and the rows in step.

## FW-xx mapping

The full matrix, down to function and test name, is in [`docs/traceability.md`](docs/traceability.md).
It also maps every edge case from the task to its test.

| FW | Code | Tests | Host | Target item (`docs/target-bringup.md`) |
|---|---|---|---|---|
| 01, 02 | sense/hwid.c, app.c (slow list before HW_ID), discharge.c (τ) | hwid, discharge, scenarios | yes | T-14 (HW_ID on real cards), T-37 (τ per bank) |
| 03, 04 | control/torque.c (+ voltage witness), sense/temp.c | torque, state_machine, scenarios | yes | T-37 (dyno, thermal, `cal_peak_recovery_s`, `cal_vdyn_reserve_frac`) |
| 05 | sense/current.c (+ stuck channel, lost triplets), app.c, s32k396_adc/pwm.c | current, params, platform_cfg, scenarios | logic + modelled compare | T-08…T-10 (ADC WD → FAULT1), T-12 (BCTU list) |
| 06, 06a | sense/vdc.c, app.c, safety/bridge.c (the ASC-exit release wait) | vdc, bridge, scenarios | yes, modelled timing | T-05 (15.6 µs on HIL), T-08, T-11, T-36 (the exit edge) |
| 07 | sense/vdc.c | vdc, scenarios | yes | T-07 (EOL gains) |
| 08, 08b | control/dclink.c (the RUN-only regen trim), safe_state.c, fault_mgr.c, can_cmd.c, app.c (battery path at every speed; zero current under the row), state_machine.c | dclink, safe_state, fault_mgr, state_machine, scenarios | yes | T-37 (trim gains on the real bank), T-38 (rule (b), BMS) |
| 09 | sense/hvil.c | hvil, scenarios | yes | T-38 (harness signatures) |
| 10 | control/resolver.c, hal/sdadc_ring.c, s32k396_resolver.c, app.c | resolver, sdadc, params, calib, scenarios | yes | T-28…T-31 (SDADC/SWG/eDMA), T-07 (EOL planes), T-37 (latency) |
| 11 | comms/can_cmd.c, torque.c | can_cmd, torque, scenarios | yes | T-38 (vehicle DBC) |
| 12 | safety/fs26.c (+ the answer cadence), app.c (the answer first in the task) | fs26, scenarios | yes (model of the FS26, its oscillator tolerance) | T-32…T-34 (silicon: answer, spacing, MCU reset; OTP) |
| 13 | sense/temp.c | temp | yes | T-38 (sensor parts) |
| 14 | safety/gate_power.c | gate_power | yes | T-37 (RDY timings) |
| 15 | app.c, fault_mgr.c, bridge.c (+ DESAT hold), arm_evidence.c, s32k396_pwm.c | fault_mgr, bridge, safe_state, gate_selftest, calib, scenarios, platform_cfg | yes | T-01…T-05, T-15 |
| 16 | safety/gate_selftest.c | gate_selftest, state_machine | yes | T-16, T-17, T-37 (chain timings) |
| 17, 18, 19 | discharge/discharge.c, app.c (service lock) | discharge, state_machine, scenarios | yes | T-37 |
| 20 | nvm/calib.c, nvm/nvlog.c | calib, nvlog, scenarios | yes | T-07, T-25 (Fee), T-27 (UID) |
| 21 | the bootloader + HSE deliverable, not the application image (contract FW-21) | none | no | — |
| 31 | app.c (`app_task_1ms`: current-loop liveness) | scenarios | yes | T-22 (ISR rate) |
| 32 | comms/uds.c, app.c (`service_clear`, `diag`) | uds, scenarios | yes | T-26 (diagnostic RX), T-35 (the key) |
| 33 | sense/vsup.c, safety/fs26.c (the AMUX), app.c (`sense_slow`, `detect`) | scenarios | yes (the FS26 AMUX modelled) | T-39 (the reading, the profiles on the bench), T-37 (the bands) |

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

## Round 16 (rechecks of commit 32214be)

Four confirmed acquisition defects, same rule: **fail closed**. Each change has regression tests that fail on
the pre-fix source and pass now (method and per-test results: `docs/traceability.md`, "Round 16").

| ID | Defect | Change | Tests |
|---|---|---|---|
| A14-R01 (rev. 2) | Resolver validity never expired. `sense_fast()` updated the resolver only when all three blocks were read, and nothing invalidated it when no new tuple came (`GAP_MAX_BLOCKS` ran only when a later block was consumed): valid stayed 1 a second later. | `rslv_age()` runs on every current-loop tick, frame or not: the newest coherent frame (its block start) may be at most `cal_rslv_hold_us` old (500 µs, range 250–800). Derivation (`gen-params.mjs`): the hold may add at most the angle error the observer already carries at the acceleration envelope, α/ω_n²; extrapolating adds e_ω·t + α·t²/2, with the critically damped observer's peak speed lag e_ω = α/(e·ω_n); the two are equal at t = 1.09/ω_n = 580 µs (300 Hz), whatever α; 500 µs adds 0.26° el at 2·10⁴ rad/s². Floor: two carrier periods + jitter (a loop faster than the frames reads nothing new every other tick — not a fault); ceiling: the observer's 8-block re-acquisition gap. Past the hold the angle is withdrawn (`stale`, DTC_RSLV_STALE) and the existing "resolver invalid" path takes the bridge (no ordinary FOC; SPO or PWM-ASC per §6); returning frames are acquired from scratch (priming + SETTLE_BLOCKS), the latched row needs a VCU fault reset below n_x. The last valid speed is held only for the §6 column (`cal_speed_hold_ms`, now independent of the resolver's lock: `rslv_seen`). | resolver: `validity_expires_without_new_frames_and_reacquires_from_scratch` (standstill and 3000 rpm, each also across the µs wrap); scenarios: `resolver_frames_stopping_withdraws_the_angle_at_the_hold` (0 rpm/200 Nm, 1000 rpm/100 Nm across the wrap, 10 000 rpm), `temporary_empty_reads_never_fault` (SiC 20 kHz, IGBT 10 kHz, COS 30 µs late); params: `round16_cal_defaults_and_ranges` |
| A14-R02 (rev. 2) | One SIN-DMA heartbeat vouched for three channels: the SIN completion published one count and one stamp for every reader, so a frozen EXC/COS buffer read as fresh, and a completion between the EXC read and the SIN/COS reads gave a mixed-epoch tuple carrying only the last stamp. | A frame protocol both platforms run (`src/hal/sdadc_ring.c`). Each SDADC's DMA has its own completion interrupt (three IRQs, `s32k_sdadc_dma_irq`) and a 4-slot ring; an epoch is published only once every channel has completed it, stamped by its FIRST completion; each count is checked against the channel's DMA write position (TCD destination address). A channel a block behind another, an interrupt that finds its DMA two blocks on, or lost samples break the block-to-epoch mapping: the ring stops publishing until re-init and the resolver ages out. `hal_sdadc_read_frame()` copies EXC + SIN + COS + epoch + stamp together under a seqlock, with every DMA position checked before (≤ 1 block past the epoch) and after (≤ 2) the copy and the copy shorter than a carrier period: the slot cannot have been rewritten even with the completion interrupts held off. Otherwise false, output untouched — a missing frame feeds A14-R01's age, never a fresh stamp. | sdadc (new suite): `every_frame_is_one_epoch_of_all_three_channels`, `a_frozen_channel_yields_no_frame`, `a_late_channel_holds_the_frame_back_and_the_stamp_is_the_first`, `a_completion_between_channel_reads_never_mixes_epochs`, `held_off_completion_interrupts_never_yield_a_fresh_frame`, `the_epoch_counter_wraps`; scenarios: `a_frozen_resolver_channel_is_never_read_as_fresh` |
| A14-R03/R04/R01 | Failed phase-current acquisition: the target `hal_adc_read_phase()` zero-filled a missing channel and left `*t_us` unwritten, and `sense_fast()` ignored the result and passed its uninitialised `t` to `isns_update()`. | HAL contract (`hal/adc.h`): all three new conversions of one trigger, or false with nothing written (the target driver fetches all three and writes only a complete triplet). The caller consumes only a complete triplet; otherwise `isns_lost()`: invalid, not fresh, no channel valid (the OC backstop and the activity check skip it), the stamp stays the last complete triplet's → the existing current-sensor path (§6 "control lost", DTC_ISNS_STALE — not an open wire); V_DC and the resolver are serviced in the same tick. A stopped BCTU raises no current-loop interrupt at all, so the 1 ms task now checks the loop's liveness: last ISR entry older than `cal_isns_stale_us` ⇒ currents lost, resolver aged. | current: `lost_sample_is_invalid_and_keeps_its_last_stamp`; scenarios: `lost_phase_current_triplets_take_the_failure_path` (all 7 combinations, repeated, recovery), `lost_triplets_across_the_microsecond_wrap_keep_a_defined_stamp`, `a_stopped_current_loop_is_caught_by_the_task` |
| A14-N01 / review 3 R04 | Amplitude planes: the excitation monitor taps the protected node (after RSX, before the PTC and the harness), not the resolver terminals, and the FW-10 target was an implicit EOL code count. | `cal_rslv_exc_target_vpp` = 7.2 V pp AT THE MONITOR PLANE, the trim setpoint; the FW-20 record carries the monitor chain's gain (`exc_code_per_vpp`, codes per V pp; layout 2). Planes (`gen-params.mjs`, A.15 exciter 13 k/28 k): amplifier = monitor × 77/72.6 = 7.64 V pp (1.91 V pk per output, under the 2.07 V pk −40 °C slew ceiling); SWG = 7.64/(2 × 2.072) = 1.843 V pp, 2.2 % under the MAXAPP low corner (1.884); winding = monitor × `cal_rslv_wind_per_mon` (70/72.6, PTCs cold) = 6.94 V pp; a PTC at 5 Ω for an hour after a trip gives 6.3 V pp, which the monitor cannot see. FW-10 now judges the WINDING — monitor × allowance × the resolver's own ratiometric output, which does see the PTC — against the 6.5 V pp floor: the post-trip state is flagged (DTC_RSLV_EXCITATION). `ti_params_validate()` refuses a setpoint beyond the low corner, the slew ceiling or the cold floor. The SWG starts at `cal_swg_code_init` (8: ≈ 1.45 V pp at the max corner) and ramps up one code per 5 ms, never from the register maximum; the excitation checks and validity wait for the ramp; `DTC_RSLV_SWG_SAT` when the trim sits at code 15 below its band. | params: `exciter_planes_and_trim_headroom`; resolver: `winding_plane_flags_what_the_monitor_cannot_see`, `swg_trim_ramps_readies_and_saturates`; calib: `each_failure_detected` (layout 1 refused); scenarios: `swg_trim_is_written_to_the_generator` (rewritten: three corners), `a_low_impedance_resolver_saturates_the_trim_with_a_dtc`, `ptc_post_trip_is_flagged_at_the_winding_and_a_cool_restart_recovers` |
| incidental | A never-acquired resolver raised the latched "control lost" row in FAULT (straight from INIT), and the unknown-speed §6 decision then raised MCU_GATE_EN for PWM-ASC in a no-arm state — latent: the resolver used to acquire in ≈ 2 ms, before the first FAULT tick; the SWG ramp now takes 20–35 ms. | The row needs a resolver that was valid once (`rslv_seen`); before its first acquisition nothing can arm (SENSOR_SELFTEST needs it). | the five no-arm scenarios (`hwid_wrong_open_short_never_arm`, `arming_refused_*`, `unbound_fault_route_never_arms`, `brownout_during_nvm_write_never_blocks`, `stuck_on_qdis_latches_service_required_and_never_rearms`) |
| incidental | FW-15 "no sooner than 1 s": the retry gate compared floored ms stamps (1000 counts can be 999.001 ms); a shifted sub-ms phase made `desat_retry_waits_1s_across_the_microsecond_wrap` reset at 999.76 ms. | One more count (`desat_retry_min_ms + 1`). | fault_mgr: `one_authorised_retry_after_1s_then_latch` (2000 refused, 2001 allowed); scenarios: `desat_retry_waits_1s_across_the_microsecond_wrap` |

Test infrastructure: the host simulation's resolver path is a per-channel eDMA model (each SDADC's DMA completes into
its own 4-slot ring and raises its own interrupt; `sim_sdadc_freeze/delay_ns/complete_now/irq_latency_ns/read_hook/
count_base/tag`) behind the card's excitation chain (SWG corner `sim_swg_maxapp`, MFB + bridge, RSX, PTC and primary
`sim_resolver_load`); `sim_adc_phase_stop()` stops phase channels. The harness's `exc_scale` is gone. Existing tests
changed: `swg_trim_is_written_to_the_generator` (rewritten for the ramp and the planes), `one_authorised_retry_after_1s_then_latch`
(boundary 2000 → 2001 ms), `each_failure_detected` (layout 2), the resolver unit tests' calibration (monitor gain) and
`feed()` (marks the excitation ready: the trim is tested on its own). `TI_FW_ID` 0x0A0F0010 and FW-20 layout 2: this
image needs a new EOL/HIL validation record and a new calibration record before it arms.

## Round 17 (closure of the open items)

Every item of the former list of "contract contradictions and open items" is closed: the contract now states
the implemented decision, or the item is a row of the target checklist (the list below says which). Two
decisions changed the image's behaviour, and three more changes came in the same round: the FS26 answer cadence
(found on the way), the ASC-exit release wait and the LV supply supervision (both from the qualification-plan
review). Each has regression tests that fail on the pre-fix source and pass now (method and per-test results:
`docs/traceability.md`, "Round 17").

| Item | Before | Change | Tests |
|---|---|---|---|
| 26 — FW-08 DC-link trim | Under the battery-lost row below n_x, iq was held at 0 at the current-loop rate while `t_cmd_nm` carried the DC-link PI output (V_ref = the normal-range maximum): the trim never reached iq, id still took the MTPA/field-weakening value of that output (−52 A at 7000 rpm), and INV_STATUS reported it as the torque (−50 Nm with the link at 750 V; +17.5 Nm with the zero-torque bit clear at 870 V). RUN had no trim at all. | Decided: under the row the inverter applies zero current, id = iq = 0 at the current-loop rate (`SS_ACT_ZERO_CURRENT`), and INV_STATUS reports the 0 Nm applied. The trim (`dcl_trim`) runs only in normal RUN/DERATE — torque granted, so the battery path is proven — as a regen limiter: its reference is `vdc_max_v` by construction, it takes regen back while V_DC is above it (at most `cal_dcl_tmax_nm`), never adds motoring torque, and is reset outside RUN. | dclink: `trim_takes_back_regen_only_above_the_range_maximum` (rewritten); scenarios: `dc_link_trim_limits_regen_with_the_battery_present`, `battery_path_loss_below_n_x_applies_and_reports_zero_current` (1000 rpm with the link above the range, 7000 rpm in field weakening), `battery_path_loss_while_armed_at_every_speed` (tightened: t_cmd 0, id 0 below n_x) |
| T-32 — FS26 watchdog cadence (found in this round) | The answer came after the slow list and CAN, due at ≥ 2000 µs from a stamp taken after its SPI transfer: on the target's exact 1 ms grid that first holds on the third task, so the FS26 was answered every ≈ 3.0 ms — the end of its window (3 ms, 50 % closed, fail-safe oscillator 20 MHz ± 5 %: open until 2.857–3.158 ms). At a fast FS26 the answer is late and one late answer reaches WD_ERR_LIMIT = 2: FS0B, DRV_EN low. The host harness hid it: its relative time steps let every SPI transfer shift the later ticks (2.03 ms). | The answer first in the 1 ms task, due at ≥ 1500 µs (`fs26_wd_due`): every second task, 1890–2110 µs apart by design (an answer offset of 20–130 µs), inside the window with 311 µs / 747 µs margin at its two ends (contract §5, "FW-12 refresh cadence"; `docs/timing.md`). The harness keeps the target's two clocks; the FS26 model has the oscillator tolerance. | scenarios: `fs26_is_answered_every_2ms_inside_its_window_at_both_oscillator_corners` (−5 / 0 / +5 %) |
| 19 — service lock | "Service required" (stuck-on QDIS) survived key cycles in NVM with no way to clear it. | FW-32: a UDS server on the diagnostic bus (`comms/uds.c`, single-frame ISO-TP, request 0x7E1 / response 0x7E9): SecurityAccess 0x27 (4-byte seed/key; the key function is the build-time hook `TI_UDS_KEY_FN`, **none by default: every seed request NRC 0x22, the lock cannot be cleared**; one key per seed; three invalid keys lock SecurityAccess out until the MCU restarts) and RoutineControl 0x31 start of 0xF010 (NRC 0x33 without the unlock; NRC 0x22 with HV present or unknown, or the bridge armed). The routine rewrites the NVM record as CLEARED with the key cycle and sets `DTC_SERVICE_LOCK_CLEARED`; the clear takes effect at the next power-up (this key cycle keeps the lock). | uds: `security_access_refused_without_a_key_function`, `seed_key_unlocks_one_routine_run`, `wrong_keys_lock_out_and_malformed_requests_are_refused`; scenarios: `service_lock_clear_is_refused_without_a_key`, `service_lock_clear_is_refused_with_hv_present_or_armed`, `service_lock_clear_with_the_key_takes_effect_at_the_next_power_up` |
| FW-06a — ASC exit | After its 1 µs ASC_CLR pulse the firmware waited 1 µs, so the first high-side pulse came 2.0 µs after the clear's falling edge — before the low sides' ASC release (≤ 1.07 µs: design-verify Safety A.8, VOW3120 t_pHL 0.5 + DASCR 0.08 + NSI6611 t_ASC_f 0.48 µs + 11 ns of logic) plus their turn-off (the dead time, 1.0 µs SiC / 2.5 µs IGBT): deadlines 2.07 / 3.57 µs. The contract still quoted 7.5 / 0.75 µs for the ASC entry/release. | The first HS pulse waits `cal_asc_release_ns` (1.5 µs; range 1.07–5 µs, never below the release) + the SKU dead time from the falling edge, + one µs timer count: 4.0 µs SiC, 5.0 µs IGBT on the host. The contract's ASC figures follow the verifier row: LS start ≥ 4.42 µs, entry ≤ 7.56 µs, release ≤ 1.06 µs (1.07 µs with the logic). | bridge: `asc_exit_only_when_allowed_and_hs_after_the_release`; scenarios: `asc_exit_first_high_side_pulse_after_the_release_deadline` (after an MCU reset at 10 000 rpm, SiC and IGBT) |
| FW-33 — LV supply (let-through LV entry) | The firmware did not read VSUP: a load dump or a jump start left no record, and an overvoltage of any length was never acted on (the FS26's VSUPOV is only an interrupt, and INTB is unused). | VSUP through the FS26 AMUX (VSUP / 14, set at every boot), every 1 ms. Above 20 V is information — RUN and the torque unchanged (no derate), `DTC_LV_OVERVOLTAGE` stamped over the event — for `cal_vsup_ld_ms` (500 ms, range 400–1000) above `cal_vsup_jump_max_v` (27 V, range 24.5–30: IR-03 test B, 35 V / 400 ms) and for `cal_vsup_jump_ms` (65 s, range 60–120 s) at or below it (IR-02, 24 V / 60 s). Longer is `DTC_LV_OV_SUSTAINED` and the §6 command-lost ramp — the HVIL-open path — until KL30 is back. | scenarios: `lv_load_dump_35v_for_400ms_is_information_not_a_fault`, `lv_overvoltage_beyond_its_band_takes_the_orderly_ramp`, `lv_24v_jump_start_is_information_for_its_60s` (24 V and 26.5 V) |

`TI_FW_ID` is 0x0A0F0011: this image needs a new EOL/HIL validation record before it arms (checklist T-05);
the calibration record stays layout 2. It was introduced in this round and never validated, so the FS26,
ASC-exit and LV changes ship under the same identity. Also in this round: the markers of the platform code were consolidated to one per
bring-up item (47, each a row of `docs/target-bringup.md`; `make target-check` fails on drift either way), the
parameter sets regenerated (`make params`: 174 fields, 75 CAL rows — the DC-link trim's comments, the ASC
release and the three LV bands), the harness's plant given ADC-level noise (`docs/traceability.md`, "Round 17"),
and the ball-map test states the V5GD pin as the contract now does.

## Decisions recorded in the contract (round 17)

The former "contract contradictions and open items", by number. Each is now contract text (the section named,
in `docs/firmware-contract.md`; §10c indexes the round) or a checklist row (`docs/target-bringup.md`).

1. **V5GD pin** — FW-07: V5GD_SNS = V5GD/2 on PTD27 (ADC4_P6); PTB5 is RDY_LS (`test_board_map`).
2. **FW-05 fault path** — FW-05: analog watchdog → TRGMUX/LCU → eFlexPWM_1 FAULT1, high sides only; the eTPU
   wording is superseded. Silicon: T-08…T-10.
3. **Current-loop rate** — §2: 2·f_sw, double update (20/16 kHz SiC, 10 kHz IGBT).
4. **State order** — §9: GATE_SELFTEST before PRECHARGE_WAIT (the VCU precharges after "self-test done").
5. **FW-07 low-voltage floor** — FW-07: max(5 %, `cal_vdc_disagree_floor_v` = 18 V).
6. **FW-12 "OTP readback"** — §5 "FW-12 read-back and release": M_PROGID, OTP_CORRUPT, DBG_MODE, the INIT registers
   and complements at every boot; the per-field OTP comparison is EOL (T-34).
7. **FW-05 safe state** — FW-05: high sides off by FAULT1, then the §6 "control lost" row, cleared by a VCU reset
   below n_x.
8. **§6 gaps** — §6: the row "V_DC invalid with V5GD healthy" (SPO under the energy rule, else LS-ASC) and "Unknown
   speed" (the last valid speed held `cal_speed_hold_ms`, then the n ≥ n_x column, rule (a) at n_max).
9. **FW-04 recovery time** — FW-04: `cal_peak_recovery_s` = 180 s; unknown coolant temperature ⇒ no peak.
10. **FW-19 τ check** — FW-19: the plateau (97.5 % of the pack) is primary, τ the second signature.
11. **FS26 release** — §5 FW-12 paragraphs: release only at FLT_ERR_CNT = 0 within the self-test window; the answer
    cadence (fixed in round 17). Silicon: T-32 (answer arithmetic and spacing), T-33 (after an MCU reset).
12. **Lost DESAT record** — §7 step 1: A/B record; a brown-out that also loses retained RAM loses that one
    record — accepted, a short still present is recorded at the next DESAT.
13. **BMS timeout** — FW-11: zero regen only; motoring keeps the FW-03 envelope.
14. **FW-21** — §10 FW-21: the bootloader + HSE deliverable, not the application image's.
15. **FW-08b wording** — FW-08b as implemented: `keep_hv` at any speed while an SPO relies on the battery, until
    rule (a) holds or ASC; "no safe state proven" without the battery.
16. **Immediate MCU_GATE_EN low** — §4c (V5GD) and §7 step 1: with a FLT line low the drop waits
    `cal_desat_en_hold_us` (60–160 µs after the ASC_CLR); with FLT high it is immediate.
17. **IMCR "distinct" values** — FW-24: non-zero values, two different IMCR indices; the SSS values may be equal.
18. **`br_service(now_us)`** — FW-22: the hold starts from the bridge's own time read after the FLT lines.
19. **Service lock** — implemented (above): FW-32 (§10c); the product key: T-35.
20. **Dynamic voltage reserve** — FW-03/FW-25: `cal_vdyn_reserve_frac` = 5 % of the FOC voltage limit.
21. **REG_PROT layout** — checklist T-03 (layout, XRDC fallback), T-04 (the IMCRs, byte locks).
22. **Validation record producer** — checklist T-05; the contract's vehicle-interface paragraph (§10a) names it.
23. **Equal gain errors** — FW-05 addendum (§10a): bounded by the EOL record, the compare built from the same gains
    and DESAT; the coverage table is in `docs/traceability.md`.
24. **FW-11 vs §6** — FW-11 row and the §6 command-lost row: armed, a stale command is also the battery-lost row,
    which wins; the FW-11 ramp remains for a command lost with the battery path proven.
25. **When the battery-lost row applies** — FW-08: every 1 ms in ARMED_ZERO_TORQUE/RUN/DERATE; a FAULT only while
    its response still holds energy.
26. **DC-link trim** — implemented (above): FW-08.
27. **ADC1 injected chain** — FW-06 (the sample wait: 4 µs of the 5.0 µs row); measurement: T-11.
28. **BCTU list read-back** — checklist T-12.
29. **Image identity** — §10c: `TI_FW_ID` 0x0A0F0011, calibration layout 2; the records: T-05, T-06, T-07.
30. **Amplitude planes** — FW-30 (round 17): the setpoint at the monitor, the floor at the winding through
    `cal_rslv_wind_per_mon` and the EOL ratio; the EOL confirmation: T-07.
31. **Where RSX sits** — FW-30: on the amplifier side of the monitor tap (77/72.6 up, 70/72.6 down).
32. **Exciter gain** — FW-30: |H(10 kHz)| ≈ 2.07 (2.072) for the 28 k feedback, 1.843 V pp from the SWG, 1.91 V pk
    per output.
33. **SWG code law** — FW-30 and checklist T-31.
34. **First acquisition** — §9 step 3 and FW-30: 20/25/35 ms at the high/typical/low MAXAPP corner; after an MCU
    reset at speed the speed is unknown for that time and ASC is kept.
35. **Current-loop liveness** — FW-31 (§10c).

**Silicon / RM checklist → [`docs/target-bringup.md`](docs/target-bringup.md).**
