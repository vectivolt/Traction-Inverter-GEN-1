# Traction inverter firmware (S32K396 + FS26)

This is the application firmware for the 220 kW / 850 V traction inverter. It covers four SKUs:
8XX SiC, 8XX IGBT, 4XX IGBT and 4XX SiC. It implements `docs/firmware-contract.md` (rev A.18),
§8/§8a of `docs/design-basis.md`, the round-12 disposition, the ball map
`calculations/mcu-ballmap.json`, the round-14 review of commit cfd35a7 (see "Round 14"), the
round-15 rechecks of commit a8c75eb (see "Round 15"), the round-16 rechecks of commit 32214be (see
"Round 16"), the round-17 closure of every open item (see "Round 17": each one is now a decision in the
contract or a row of the target checklist `docs/target-bringup.md`), the round-18 rechecks of commit 4425af9
(see "Round 18") and the round-19 rechecks of commit e315bf1 (see "Round 19"). It is C11 with no dynamic memory
and no recursion, and every loop is bounded. It uses fixed-width types and single-precision float only.

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
make qemu-test     # the same suite built by arm-none-eabi-gcc for the Cortex-M7, run on QEMU (below)
make target-size   # text/data/bss of src/ + the s32k396 platform for the M7 (RTD calls compiled out)
make clean
```

`make test` compiles with `-std=c11 -Wall -Wextra -Werror -Wshadow -Wdouble-promotion
-Wmissing-prototypes -Wstrict-prototypes -Wundef -Wpointer-arith -Wcast-qual -Wvla`. Test files
alone get `-Wno-double-promotion`, because their reference arithmetic is done in double. Current
result (round 23, with its fixes and FW-45/FW-46): **421 tests, 4937 checks, 0 failures**, also with `-fsanitize=address,undefined` and at
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
     is `TI_FW_ID` 0x0A0F0015, round 23 — the second round-23 image; 0x0A0F0014 was the torque-solver image).

   Anything missing is a DTC (`DTC_ARM_EVIDENCE` or `DTC_PWM_LOCK`), the state machine never leaves
   the inhibited state (no FS0B release, no FW-16 energisation, no `MCU_GATE_EN`), and INV_STATUS
   byte 15 names the missing items. The host default is "nothing validated": the test harness
   binds the route and stores a valid record the way the EOL/HIL rig would (`tests/harness.c`).

### QEMU (target ISA) run

`make qemu-test` (`qemu.mk`) builds exactly the `make test` file set — the firmware, the host simulation platform and
the tests, with the same warnings as errors — with arm-none-eabi-gcc for the S32K396's core (`-mcpu=cortex-m7
-mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -O2`, newlib), links it for QEMU's `mps2-an500` board (a Cortex-M7;
`qemu/startup.c`, `qemu/mps2-an500.ld`) and runs it there with semihosted output and files. It passes only on QEMU
exit 0 and "N tests, M checks, 0 failed". Result: **402 tests, 4707 checks, 0 failed** — the host's counts and output —
in 1089 s under TCG (about 125 times the host binary; QEMU 11.1.1, Arm GNU Toolchain 14.3.Rel1). It needs
`qemu-system-arm`, node (the capture decoder test runs it on the host through semihosting) and an arm-none-eabi-gcc
with newlib (`ARM_CC=…`: Homebrew's `arm-none-eabi-gcc` formula has none, the Arm GNU Toolchain has).

It proves the firmware logic on the target's instruction set, FPU and C ABI: Thumb-2 code from the target compiler,
single-precision hard float with doubles in soft-float (the S32K396's FPU is single precision, data sheet §3.2),
32-bit `long`, `size_t` and pointers, newlib's libm, the M7's alignment faults. And it is the run that executes the
target's rounding: the host build fuses `a*b + c` into one multiply-add (Apple clang's default contraction), GCC
under `-std=c11` does not (a host build with `-ffp-contract=off` prints what the M7 prints). It does not prove
peripherals (the host simulation platform runs, not `src/platform/s32k396`), the RTD, interrupts or preemption (the
simulation calls the ISRs in turn), timing (TCG is not cycle-accurate; the one host-timing check reads 0 here, see
`qemu.mk`), the memory map, caches, TCM, ECC or the lockstep core — `docs/target-bringup.md` T-59 says what it
pre-empts.

GCC for the M7 found test-code defects that clang on arm64 does not flag; fixed, no assertion changed: `%u` for
`uint32_t` (an `unsigned long` on arm-none-eabi) in four failure messages, two CAN frames read after `last_status()`
may have left them unwritten, a 256-byte buffer for a command of up to 300, a sign-compare (`tests/test_scenarios.c`,
`tests/test_capture.c`).

`make target-size`: src/ with the s32k396 platform (RTD calls compiled out) for the same core — text 83 921, data 33
(32 of them `.ti_retained`), bss 127 747 bytes at -O2 (text 71 665 at the `make target` -Os). The libraries, the RTD,
the start-up and the stacks come on top. The S32K396 has 6 MB program flash and 800 KB RAM, 288 KB of it TCM
(`docs/datasheets/S32K39.pdf`, Table 1).

Last run on the final round-23 tree: **421 tests  4937 checks  0 failed** on the Cortex-M7 under QEMU (fpv5-sp-d16)  1 412 s wall — the host's exact counts, byte-identical output with `-ffp-contract=off`.

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
  `cal_rslv_hold_us` after its newest frame whether or not a new one arrives. Round 18: freshness is judged at
  a time read after the sample was read, with a signed age (`ti_stale`), and a resolver frame is stamped with
  its block start on the SDADC cadence, whatever its interrupt's latency. Round 19: that cadence is dated by the
  SWG start, never by a completion, and a ring whose DMA lost the carrier phase is restarted, not re-guessed.
- **One parameter set per SKU.** `include/params_<sku>.h` is generated from the contract tables
  and has 190 fields (round 23). 91 of them are `cal_*` values the contract does not fix, mostly hardware
  timings and tolerances. Each has its contract default and a `[min, max]` range
  (`include/cal_ranges.h`), checked at boot.
- **Units:** see `include/ti_types.h`. Time is `uint32_t` µs or ms, compared only through
  `ti_elapsed()`/`ti_age()` — and a sensor stamp's freshness through `ti_stale()` (round 18: signed, the stamp
  may postdate the check). One time domain (round 14, A12-R06, `src/hal/timer.h`): µs intervals use
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
| `include/` | 1180 | types/units (+ `ti_stale`), parameter struct, 4 generated SKU sets, CAL ranges |
| `src/util/` | 104 | math helpers, CRC-8 (0x1D, SAE J1850) and CRC-32 |
| `src/hal/` | 632 | the 10 HAL interfaces (timer: the 64-bit time base), the shared resolver frame protocol (round 16; round 18: the cadence stamp, the servicing deadlines, re-acquisition; round 19: the SWG-start origin, the re-sync from the clock, `lost` and the producer restart) |
| `src/sense/` | 680 | current (FW-05, stuck-channel check, lost triplets), V_DC (FW-07/18), temperatures (FW-13), HVIL (FW-09), HW_ID (FW-01/02), IGN, the LV supply (FW-33) |
| `src/control/` | 931 | resolver (FW-10: bounded hold, amplitude planes, SWG ramp, the latency's sign), FOC/SVPWM, MTPA/field weakening/limits and the voltage witness (FW-03/04), gains, the DC-link trim (FW-08: a regen limiter in RUN) |
| `src/safety/` | 2134 | state machine, §6 matrix, fault manager (FW-15), FS26 (FW-12, + its AMUX for FW-33), bridge sequences + DESAT hold and the ASC-exit release wait (round 18: the FW-15 low wait on the bridge's own clock), gate power (FW-14), self-test (FW-16), arming evidence |
| `src/comms/` | 687 | CAN command/status with E2E (FW-11), UDS DTC store, UDS SecurityAccess + the service-lock routine (FW-32) |
| `src/discharge/` | 279 | FW-17/18/19, FW-02 τ, unexpected discharge |
| `src/nvm/` | 520 | parameter sets (+ the exciter plane checks), calibration (FW-20), NVM log and queue (validation and service records) |
| `src/app/` | 1229 | integration (+ the current-loop liveness check FW-31, the diagnostic bus, the check times of FW-34, the resolver producer restart of FW-35) |
| `src/platform/s32k396/` | 1961 | generated ball-map tables, the ADC map and its derived schedule, register images, REG_PROT layout, the board configuration the RM fills, drivers with RTD bodies (three SDADC DMA interrupts) |
| `src/platform/host/` | 2044 | simulation of the card, FS26 (its oscillator tolerance and AMUX) and MCU peripherals (REG_PROT, route binding, MCU reset, per-channel eDMA, the excitation chain; round 18: ADC reads that take time, per-channel interrupt hold-off, the overrun flag, a preempting ISR; round 19: the DMA blocks triggered by the SWG start, its start latency) |
| `tests/` | 7893 | 31 suites (one file per module + time + sdadc + uds + scenarios), 290 tests, harness (two exact clocks, ADC-level noise) |
| `tools/` | 368 | parameter and board-map generators |

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
  the orderly ramp and recovering;
- round 18: every ADC read taking 1, 5 or 50 µs before it stamps (the target's order), also straddling the 32-bit
  µs wrap, and the current-loop ISR preempting the 1 ms task — currents, V_DC and the resolver stay fresh; a DESAT
  that preempts the task keeps FW-15's ≥ 1.5 ms low (also across the wrap); SDADC
  completion interrupts held off within the deadline (stamps exact), past it (rejected, re-acquired), for exactly
  a lap (never fresh), one channel lapping, all three stalled, a late anchor, both wraps, lost samples; the
  resolver latency against an independent rotor at ± 3000 / 10 000 rpm and 25 / 50 µs;
- round 19: the resolver cadence dated by the SWG start — first completions 0–800 µs late (accepted within the deadline
  + the 3 µs uncertainty with exact stamps, rejected beyond), a SWG start latency of 1–3 µs inside the declared
  uncertainty, a constant 40 / 60 µs delay never published, a delay rejected once never absorbed after the break (also
  as the FOC angle at 10 000 rpm), a channel paused one period, a DMA stall and 2^32 µs of silence against the clock,
  an early completion, an interrupt late inside the origin's uncertainty (no false loss), the restart limit per key
  cycle across an MCU reset.

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
| 10 | control/resolver.c, hal/sdadc_ring.c, s32k396_resolver.c, app.c | resolver, sdadc, params, calib, scenarios | yes | T-28…T-31 (SDADC/SWG/eDMA), T-07 (EOL planes), T-37 (latency), T-40 (completion latency) |
| 11 | comms/can_cmd.c, torque.c | can_cmd, torque, scenarios | yes | T-38 (vehicle DBC) |
| 12 | safety/fs26.c (+ the answer cadence), app.c (the answer first in the task) | fs26, scenarios | yes (model of the FS26, its oscillator tolerance) | T-32…T-34 (silicon: answer, spacing, MCU reset; OTP) |
| 13 | sense/temp.c | temp | yes | T-38 (sensor parts) |
| 14 | safety/gate_power.c | gate_power | yes | T-37 (RDY timings) |
| 15 | app.c, fault_mgr.c, bridge.c (+ DESAT hold), arm_evidence.c, s32k396_pwm.c | fault_mgr, bridge, safe_state, gate_selftest, calib, scenarios, platform_cfg | yes | T-01…T-05, T-15 |
| 16 | safety/gate_selftest.c | gate_selftest, state_machine | yes | T-16, T-17, T-37 (chain timings) |
| 17, 18, 19 | discharge/discharge.c, app.c (service lock) | discharge, state_machine, scenarios | yes | T-37 |
| 20 | nvm/calib.c, nvm/nvlog.c | calib, nvlog, scenarios | yes | T-07, T-25 (Fee), T-27 (UID) |
| 21 | implemented as FW-38 (round 23, contract §10f): boot/verify.c, update.c, uds_update.c, boot.c; app.c (`update_conditions`, `update_enter`) — see "FW-38" below | update, uds_diag | yes (the check, the update state, the bootloader decision under power cuts) | T-44…T-50 (flash driver, key, reset, bootloader binary, HSE) |
| 31 | app.c (`app_task_1ms`: current-loop liveness) | scenarios | yes | T-22 (ISR rate) |
| 32 | comms/uds.c, app.c (`service_clear`, `diag`) | uds, scenarios | yes | T-26 (diagnostic RX), T-35 (the key) |
| 33 | sense/vsup.c, safety/fs26.c (the AMUX), app.c (`sense_slow`, `detect`) | scenarios | yes (the FS26 AMUX modelled) | T-39 (the reading, the profiles on the bench), T-37 (the bands) |
| 34 | ti_types.h (`ti_stale`), app.c (`sense_fast`, `app_task_1ms`, `recovery`), current.c, vdc.c, resolver.c (`rslv_age`), bridge.c (`br_rec_step`) | time, current, vdc, resolver, bridge, scenarios | yes (the target's read-then-stamp order modelled) | T-36 |
| 35 | hal/sdadc_ring.c, s32k396_resolver.c (`hal_swg_start`: the origin; `hal_sdadc_restart`), app.c (the restart, the DTC) | sdadc, params, scenarios | yes (per-channel eDMA and interrupt model, triggered by the SWG) | T-40 (latency from the carrier boundary, cadence), T-41 (the flags wired to `lost`), T-42 (the SWG start latency), T-30, T-28 |
| 36 | control/resolver.c (`rslv_theta_e_at`) | resolver | yes | T-37 (the latency's sign on HIL) |

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

`TI_FW_ID` was 0x0A0F0011 in round 17 (0x0A0F0012 round 18, 0x0A0F0013 round 19, 0x0A0F0014 the round-23 torque-solver image, **0x0A0F0015 since the round-23 gap closure** — the current image): every change of the ID needs a new EOL/HIL validation record before it arms (checklist T-05);
the calibration record stays layout 2. It was introduced in this round and never validated, so the FS26,
ASC-exit and LV changes ship under the same identity. Also in this round: the markers of the platform code were consolidated to one per
bring-up item (47, each a row of `docs/target-bringup.md`; `make target-check` fails on drift either way), the
parameter sets regenerated (`make params`: 174 fields, 75 CAL rows — the DC-link trim's comments, the ASC
release and the three LV bands), the harness's plant given ADC-level noise (`docs/traceability.md`, "Round 17"),
and the ball-map test states the V5GD pin as the contract now does.

## Round 18 (rechecks of commit 4425af9)

Three confirmed defects, verified against the source before the change, same rule: **fail closed**. Each change has
regression tests that fail on the pre-fix source and pass now; each fix was also mutated once and the suite caught it
(method, per-test results and the mutations: `docs/traceability.md`, "Round 18"). Contract §10d: FW-34…FW-36.

| ID | Defect | Change | Tests |
|---|---|---|---|
| A16-R01 (FW-34) | Fresh samples declared stale on the target. `app_isr_current()` read `now_us` at entry; the target's `hal_adc_read_phase()` and `hal_adc_read()` stamp a sample with `hal_time_us()` when they read it — later — and `isns_update()`/`vdc_update()` computed the age `now_us − stamp` unsigned: one tick newer read 4.29e9 µs old, the currents and V_DC stale, the control lost. `rslv_age()` the same for a frame an SDADC interrupt published inside a long ISR. The host hid it: its reads stamped with the frozen clock, the ISR's entry. Found at another caller of the same comparison: the 1 ms task's FW-31 liveness check compared its start time with `t_isr_us`, which the current-loop ISR rewrites when it preempts the task (after the task read its time, during its FS26 transfers): "loop dead", the currents lost, the control-lost row. | Each freshness check reads its own time after its acquisition reads (`sense_fast`: the triplet; VOFS, V5GD, then V_DC — the checked channels last; the frame). A signed-safe helper `ti_stale(now, stamp, hold)` (`ti_types.h`): stale when the stamp is `hold` or more before now — or `hold` or more after it (no ISR's execution reaches that: a corrupt stamp is refused either way); wrap-safe; used in `isns_update`, `vdc_update`, `rslv_age` and the task's liveness check. `ti_elapsed()` stays unsigned for timers. The ISR's entry time stays its own: `t_isr_us`, the WCET reference, the FOC angle, the bridge. The same class at the FW-15 recovery timer: `br_rec_step()` timed the ≥ 1.5 ms low with the task's `now_us` against the fault ISR's newer stamp — with the DESAT hold over before the recovery started in that tick, the age wrapped and the reset pulse came ≈ 0.1 ms after the fault (the driver not reset: `DTC_FLT_RECOVERY_FAIL`); it now takes no time argument and reads its own (the FW-22 rule). The host models the target's order: `sim_adc_read_delay_ns()` (each read takes simulated time, events included, before it stamps) and `sim_fs26_xfer_hook()` (an interrupt inside the task's FS26 transfer). | time: `sensor_stamps_are_judged_with_a_signed_age`; current: `a_triplet_stamped_after_the_check_time_is_fresh`; vdc: `a_channel_stamped_after_the_check_time_is_fresh`; resolver: `a_frame_newer_than_the_check_time_is_not_aged_out`; bridge: `fw15_low_wait_runs_on_the_bridges_own_clock` (also across the µs wrap); scenarios: `samples_stamped_after_the_isr_entry_stay_fresh` (1 / 5 / 50 µs per read, across the µs wrap), `a_run_at_speed_with_the_adc_reads_taking_time`, `a_current_loop_preempting_the_task_is_not_a_dead_loop`, `fw15_low_wait_counts_from_a_fault_that_preempted_the_task` |
| A16-R02 (FW-35) | The resolver stamp came from the completion interrupt's execution time: `t_start = now_us − period` in `hal_sd_ring_complete()`, so an interrupt served late stamped old data too new (100 µs at 10 kHz is 24° el at 10 000 rpm, 4 pole pairs); blocks since the last count were `(hw − done) % 4`, so an interrupt exactly one lap (4 periods) late read as a repeated one, and the reader's DMA-position checks were modulo 4 as well: a lap was invisible. A broken ring stayed down until reset. | The stamp is cadence-locked: block k starts at `t_org + (k − k_org)·100 µs` (unsigned, wrap-safe), the origin anchored at the first completion after (re)acquisition and moved back by any completion that comes before its block's end (a completion is never early). A block's first completion must be served within `cal_sd_irq_lat_max_us` (new CAL: 30 µs, range 5–45 µs, below half the carrier period; `docs/timing.md`), a later channel's within half a period, else the timing is ambiguous: the block is not published and the ring breaks. The reader refuses a slot the cadence says may have been rewritten (a copy ending ≥ 3 periods after the block start). A broken ring re-acquires by itself: at a completion that finds all three DMAs in one slot (TCD destination addresses on the target, `s_dma[].hw` on the host) it takes that count, numbered past every published epoch, and anchors a fresh origin at the next completion; the resolver bridges a short gap or re-primes through FW-28; each re-acquisition is one occurrence of the information DTC `DTC_RSLV_REACQUIRED` (no §6 row). Lost samples (`TI_SD_LOST`) keep the ring down until re-init — positions cannot restore carrier phase 0. The SDADC data rate and the STM share the PLL (assumption, `docs/timing.md`; T-40). | sdadc: `interrupts_held_off_within_the_deadline_keep_every_stamp_exact`, `a_late_anchor_is_moved_back_by_the_first_prompt_completion`, `an_interrupt_held_off_past_the_deadline_is_rejected_then_reacquired` (40, 60 µs), `interrupts_held_off_for_a_lap_are_detected_never_fresh`, `one_channel_lapping_while_the_others_do_not`, `all_three_stalled_together_are_detected_and_reacquired`, `the_reader_preempted_across_a_lap_never_returns_the_frame`, `stamps_are_exact_across_the_microsecond_and_epoch_wraps`, `lost_samples_keep_the_ring_down_until_init`; params: `round18_cal_defaults_and_ranges`; scenarios: `a_late_resolver_interrupt_at_speed_is_counted_and_reacquired` (SiC, IGBT) |
| A16-R03 (FW-36) | `rslv_theta_e_at()` subtracted the chain latency: `(now − t_ref) − t_mid − cal_rslv_latency_us`, while the parameter is a positive delay (the block's angle is the rotor's that long before its mid-block reference): −12 / −24° el at 10 000 rpm, 4 pole pairs, 25 / 50 µs. The unit test encoded the wrong sign. | The latency is added: θ(now) = θ_block + ω·((now − t_ref) − t_mid + L); the header comment, the generator's description ("positive = the sample represents an earlier instant; add"), `docs/timing.md` and the T-37 row (the HIL measurement's sign convention) say so. | resolver: `latency_compensation_matches_the_true_angle_at_now` (an independent rotor model: both directions, 3000 / 10 000 rpm, 25 / 50 µs), `acquires_at_speed_after_a_reset` (corrected) |

`TI_FW_ID` was 0x0A0F0012 for this round-18 image (0x0A0F0013 round 19, 0x0A0F0014 then 0x0A0F0015 in round 23): each image needs a new EOL/HIL validation record before it arms (checklist T-05); the
calibration record stays layout 2. The parameter sets were regenerated (`make params`: 175 fields, 76 CAL rows);
the target checklist gained T-40 (the completion-interrupt latency distribution and the cadence against the STM),
and T-37 states the latency's sign convention. Existing tests changed: `resolver: acquires_at_speed_after_a_reset`
(the corrected sign); `bridge:` the three FW-15 tests' calls (`br_rec_step()` lost its time argument); `sdadc: a_frozen_channel_yields_no_frame`, `held_off_completion_interrupts_never_yield_a_fresh_frame`
and `scenarios: a_frozen_resolver_channel_is_never_read_as_fresh` (a ring that broke now re-acquires once its
channels are in step — it used to stay down until reset; a channel resuming out of step still never does);
`sdadc: the_epoch_counter_wraps` (37 frames: the ring takes the DMA positions at its first completion and anchors
at the second); every `sdadc` test now also requires each frame's stamp to be its carrier period's true start
(± 1 µs), not only its three tags to agree.

## Round 19 (rechecks of commit e315bf1)

One confirmed defect — all three rechecks reproduced it (A17-R01, register F201) — verified against the source before the
change, same rule: **fail closed**. The regression tests fail on the pre-fix source (108 checks in 17 tests) and pass now;
every mechanism was mutated once and the suite caught it (method, per-test results and the mutations:
`docs/traceability.md`, "Round 19"). Contract §10d: FW-35 rewritten.

| ID | Defect | Change | Tests |
|---|---|---|---|
| A17-R01 (FW-35) | The resolver ring dated its cadence from a completion callback's own execution time (`sdadc_ring.c` 147–150: `t_org = now_us − period_us` at the first completion after (re)acquisition, moved back by an earlier one). A late anchoring callback became the reference: block 1 stamped 29–800 µs too new with the ring unbroken (80 µs = 19.2° el at 10 000 rpm, 4 pole pairs); a constant 40 / 60 µs delay passed the 30 µs deadline forever (every frame 40 / 60 µs too new); after a break the re-anchor absorbed the very delay just rejected (at the application: the FOC angle 5.3° el behind at 10 000 rpm); `resync()` took its counts from equal DMA slots modulo 4, never the acquisition phase; and an early completion moved the origin to itself. | The origin is the **SWG start**: the SDADCs are triggered by the SWG period start, so `hal_swg_start()` (both platforms) brackets the generator enable with two `hal_time_us64()` reads inside PRIMASK and anchors the ring (`hal_sd_ring_anchor`: t_org the later read, k_org the first carrier period's block, uncertainty u = the bracket + 1 µs + `cal_swg_start_lat_us`, new CAL 2 µs [0, 20], T-42; u ≥ a quarter period is `lost`); running, it only changes the amplitude. The ring never anchors itself (an unanchored ring counts nothing) and judges EVERY completion against the absolute cadence, u added on both sides: a block's first completion within [−u, `cal_sd_irq_lat_max_us` + u], a later channel's within [−u, T/2 + u]; early or late breaks the ring. `t_org += late` is gone. A broken ring **re-syncs from the clock** — the counts are the block that ended within [−u, T/2 + u] of the completion — the DMA positions only confirming them (every DMA past it; a later channel still on it waits for its own completion); the completing DMA behind one past the block, a DMA elsewhere, or four periods without agreement is a DMA out of phase: `lost`, as are the platform's DMA error, FIFO-overrun and trigger-miss flags (T-41). A lost ring stays down until the **synchronized producer restart** `hal_sdadc_restart()` (DMA rings re-armed, the SWG restarted at its present code and re-anchored), which the 1 ms task requests at most `cal_rslv_restart_max` (new CAL: 3 [0, 10]) times per key cycle — counted in the retained session, so an MCU reset inside the key cycle does not refill it — each one occurrence of `DTC_RSLV_REACQUIRED`; beyond it the resolver stays invalid (FW-28). Times are 64-bit: the clock-derived index holds across the 32-bit wrap and any silence; the origin is re-based by whole periods at each publication. The host simulation triggers the DMA blocks from the SWG start and anchors as the target does (`sim_swg_start_latency_ns()`). | sdadc: `the_first_completion_is_judged_against_the_swg_start` (0, 29, 30, 31, 33, 34, 40, 60, 80, 800 µs), `the_swg_start_latency_stays_inside_the_declared_uncertainty` (1–3 µs), `a_constant_completion_delay_is_never_published` (40, 60 µs), `a_rejected_delay_is_never_absorbed_after_a_break`, `a_channel_paused_one_period_is_lost_not_taken_for_a_late_one`, `a_late_interrupt_inside_the_origins_uncertainty_is_no_phase_loss`, `an_early_completion_breaks_the_ring`, `the_origin_and_the_clock_index_hold_across_the_32bit_wrap`, `the_anchor_refuses_an_origin_too_uncertain`, `nothing_counts_before_the_swg_start`; params: `round19_cal_defaults_and_ranges`; scenarios: `a_late_completion_after_a_break_never_dates_the_angle`, `resolver_producer_restarts_are_bounded_per_key_cycle` |

`TI_FW_ID` was 0x0A0F0013 for this round-19 image (0x0A0F0014 then 0x0A0F0015 in round 23): each image needs a new EOL/HIL validation record before it arms (checklist T-05); the
calibration record stays layout 2. The parameter sets were regenerated (`make params`: 177 fields, 78 CAL rows; the
`cal_sd_irq_lat_max_us` description now says its reference is the carrier boundary, so the SDADC's output latency counts
in it). The target checklist gained T-41 (every SDADC/eDMA flag of lost or shifted samples wired to `lost`, and the
restart path of `hal_sdadc_init()`) and T-42 (the SWG start latency, the first block's phase 0, LDOS updates keeping the
boundaries, the SGEN flags); T-40 now measures the completion latency from the carrier boundary; T-28's lost samples wait
for the restart. Existing tests changed: `sdadc: a_frozen_channel_yields_no_frame` (a frozen channel is `lost` — found
behind the clock during the freeze — and nothing comes back without the restart; round 18 re-acquired the in-step
resume), `all_three_stalled_together_are_detected_and_reacquired` (the 5-period DMA stall is `lost`, then the restart; the
8-period one and the held interrupts re-sync), `a_completion_between_channel_reads_never_mixes_epochs` (the DMAs moving
on during the copy are now a 2 µs preemption across a boundary with the interrupts pending — the forced early completion
it used is refused now), `a_late_channel_holds_the_frame_back_and_the_stamp_is_the_first` (the stamp exactly the block
start), `the_epoch_counter_wraps` (+ the re-base invariant), `lost_samples_keep_the_ring_down_until_init` → `…until_a_restart`;
`a_late_anchor_is_moved_back_by_the_first_prompt_completion` removed — it proved the round-18 origin correction, which is
gone (its case, a 25 µs latency from the start, is inside the first-completion sweep: exact stamps from the first frame);
every `sdadc` test now requires each frame's stamp within ± 3 µs of its period's true start on the SWG's cadence;
`scenarios: a_frozen_resolver_channel_is_never_read_as_fresh` (lost, one restart, one DTC occurrence, no ring
re-acquisition; each iteration a cold start), `resolver_frames_stopping_withdraws_the_angle_at_the_hold` (comment: the
re-sync or the restart). Test infrastructure: `h_rotor_theta_e()` in the harness.

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
   speed" (the last valid speed held `cal_speed_hold_ms`, then the n ≥ n_x column, rule (a) at n_max) — round 23
   (fix 6): replaced by a physical bound, |n_last| + `cal_speed_accel_max_rpm_s`·t, unknown past n_max (contract §6).
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
29. **Image identity** — §10c: `TI_FW_ID` 0x0A0F0015 (round 23, second image — 0x0A0F0014 the round-23 torque-solver image; 0x0A0F0011 round 17, 0x0A0F0012 round 18, 0x0A0F0013 round 19), calibration layout 4 (round 23's FW-45/FW-46: the L_d/L_q saturation maps and the ripple table; 190 fields / 91 rows; layout 3 since the gap closure, layout 2 before); the records: T-05, T-06, T-07 (round 18:
    0x0A0F0012, round 19: 0x0A0F0013, §10d; round 23: 0x0A0F0014, FW-37, §10e; then 0x0A0F0015, FW-38…FW-44 and §10k).
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

## Round 23 — VESC gap closure (FW-38 … FW-44)

The comparison in `docs/firmware-vs-vesc.md` (round 23) ranked the capabilities the VESC firmware has and ours lacked that a
traction inverter genuinely needs. Each is a numbered contract item with its own section below (the agents that implemented
them append here; counts are updated once at the end of the round).

### FW-38 — Firmware update: signed images, anti-rollback, the no-torque update state
FW-21's requirement implemented (contract §10f): nothing is installed or started unless an Ed25519 signature by the
release key, the payload's SHA-256, the target SKU and the anti-rollback security version all hold — the opposite of
VESC's CRC-only loader (`docs/firmware-vs-vesc.md`, gap 1).

| Part | Files | What it does |
|---|---|---|
| Container | `src/boot/image.h`, `tools/sign-image.mjs` | 128-byte header: magic "TIFW", format, target (SKU), length, `TI_FW_ID`, security version, SHA-256 of the payload, Ed25519 signature over the first 64 bytes. The tool builds (`--in --out --key --target --fw-id --sec-ver`), verifies (`--verify`) and prints a public key as a C list (`--pubkey`); Node's standard library only |
| Check | `src/boot/verify.c`, `sha256.c`, `ed25519_tweetnacl.c` | structure → signature → target → security version ≥ the counter → hash (read back from flash), each refusal its own `IMG_ERR_*`. Ed25519 vendored from TweetNaCl (public domain; origin and every change in its header), RFC 8032's S < L added, no UB (the negative shifts are multiplications); verification only in a target build. The key: a `const` table (host: the TEST key), overridden by the OTP/HSE copy (`hal_flash_pubkey`); none or all-zero ⇒ nothing verifies |
| Update state | `src/boot/update.c`, `uds_update.c`; one line in `src/comms/uds.c`; `src/app/app.c: update_conditions, update_enter` | 0x10 0x02 only disarmed, discharged, at standstill (else NRC 0x22, nothing changes); entering withdraws `ARM_EV_VALIDATED` and forbids arming until the next power-up (`DTC_FW_UPDATE`): no arming, no torque. 0x34 (needs the 0x27 unlock), 0x36 (counter, repeat, length, size, busy), 0x37, 0x31 0xFF01 verify / 0xF038 activate, 0x11 — flash work in `app_idle`, never in the 1 ms task |
| Bootloader decision | `src/boot/boot.c`, `nvm/nvlog.h: NV_REC_BOOT` | a write-ahead boot record (card SKU, anti-rollback counter, last-known-good flag, last outcome): ACTIVATE → verify STAGE → INSTALL → EXEC verified → TRIAL; the image confirms itself after 5 s (`UPD_CONFIRM_MS`) → the next reset commits it to LKG and raises the counter; an unconfirmed trial, an interrupted install or an invalid EXEC restores LKG; nothing valid ⇒ nothing starts |
| Flash | `src/hal/flash.h`, `platform/host/sim_flash.c` (+ `sim_flash.h`), `platform/s32k396/s32k396_flash.c` | three regions (EXEC, STAGE, LKG), page erase, word program into erased bytes only; the host model cuts power after any number of writes (the torn operation half-done, every later one failing); the target is a stub that fails every operation (T-44…T-47) |

**Keys.** The host tests sign with a TEST key pair derived from a public string (seed = SHA-256("TI FW-38 TEST KEY - NOT
FOR RELEASE"), `sign-image.mjs --key test`); its public half is the host build's key table and a target build can never
contain it. The release key pair is generated offline (an HSM or an air-gapped signing station); its private half never
enters this repository or a build machine — the release build receives only the public half (`-DTI_FW38_PUBKEY=` the
`--pubkey` output) or reads the HSE/OTP copy (T-46). An HSM signs the same 64 bytes the tool signs.

**Tests** (`tests/test_update.c`, 16 tests, 351 checks): SHA-256 (FIPS 180-4, one million 'a' incrementally) and Ed25519
(RFC 8032 test 1: key, signature, verification; the message, R, S, the key each flipped; S + L refused); the tool's
container verified and reproduced byte for byte; every refusal with its reason (signature, tampered payload and its last
byte, a signed field changed, another key, wrong target, rollback, each structural field, a read failure); 3000 bounded
random headers (the parser accepts exactly the structurally valid ones, nothing unsigned verifies) and one bit flipped in
each header byte (always refused); install → trial → commit with the counter (and the old image then refused as a
rollback); refusals that leave EXEC untouched; a power cut at every write of the swap (the last known good boots), a
second cut during the restore, the INSTALL record write torn (nothing copied yet) and the TRIAL write torn (abandoned);
an unconfirmed first boot rolled back; a cut at every other write of the commit (resumed, the counter at 6); a torn
record write never takes the counter back; EXEC corrupted (restored), both copies corrupted and an unsigned EXEC with no
record (nothing starts); the programming services' protocol; refused while moving, with HV present, armed, and armed
with the link reading 20 V; the update state never arms under a VCU enable and 100 N·m (INV_STATUS b15); an update end to
end over UDS (download, verify, activate, ECUReset, the bootloader, the self-confirmation, the commit, then arming), and
one whose first boot never confirms (rolled back, `DTC_FW_FALLBACK`). **Mutations** (14, each caught): the rollback, hash,
signature and target checks removed; an interrupted install rolled forward; the copy before the INSTALL record; the
arming not withdrawn; standstill not required; the entry conditions not checked; the block counter not checked; S < L
removed; the record ending in its own CRC-32 again; the counter not raised; the trial never confirmed.

**Found on the way.** A record that ends in its own CRC-32 makes the NVM slot's CRC-32 blind to it (CRC-32 over
M ‖ CRC-32(M) is the same for every M): a torn write validated with the stale bytes left in the slot, the record from two
writes back — an anti-rollback counter could go back. The boot record has no CRC of its own. **Open, outside FW-38:**
`calib_t` (FW-20) and `arm_validation_t` (FW-24) have that layout (contract §10f, open 1); an updated image needs its
own FW-24 validation record before it arms (open 2). The NVM's 32 slots are now all used. (Round 23 fixes 9 and 10 close
open 1 and the slot count; fix 11 carries TransferData over ISO 15765-2 — see "Fixes" below.)

**Host vs target.** The host proves the logic end to end: the check, every refusal, the UDS services, the update state's
interlocks through the application's arming evidence, and the bootloader's journal under power cuts on page-accurate
flash and A/B NVM models. Only the target proves the C40 driver and the region map (read-while-write, no ISR stall), the
release key in the HSE/OTP, the reset, the bootloader binary with its FS26 handling and timing, the HSE secure boot, the
debug lock and a monotonic counter in the HSE, and the EOL provisioning (T-44…T-50).

### FW-39 — Motor self-commissioning in an interlocked service mode

The service tool identifies R_s, L_d, L_q, ψ and the resolver's electrical zero (with the resolver's direction) on the
vehicle's own inverter — the capability the VESC firmware has — but through the safety chain instead of around it, and
nothing it measures is used before the next key cycle has validated it (contract §10g).

| Part | What it does | Where |
|---|---|---|
| Entry | the FW-32 SecurityAccess unlock (one start per unlock) + the tool's rig attestation (`LK` locked rotor / `DF`, `DR` dyno direction) + armed through the normal path (ARMED_ZERO_TORQUE, bridge armed idle, FW-24 evidence and the FW-20 record behind it) + no §6 row, no active DTC, a fresh VCU command without enable, the VCU's vehicle speed valid and zero, V_DC in the SKU window with the battery path proven, the motor speed the routine needs, every service CAL in range | `src/app/commission.c: start, preconditions` |
| Watch (1 ms) | the same list + the tool's heartbeat (200 ms), the locked rotor's motion (2° el), the dyno's steadiness (5 %), a current bound, no voltage saturation, the schedule; any loss: abort to the normal safe state (no service modulation, the bridge armed idle, or the §6 decision's), `DTC_MC_ABORTED` | `mc_task, watch, mc_abort` |
| No torque | the state machine does not see the VCU's enable from a start until the VCU withdraws it; an enable ends the routine and is never executed | `mc_torque_barred`; `app.c: gather` |
| R_s | 30 / 60 A DC along phase U's axis, locked rotor; ΔV/ΔI from the duties (the inverter error cancels) | `plan, mc_isr_*, est_rs` |
| L_d, L_q | 20 A sinusoidal **current** reference at f_isr/40 along d then q on a 50 A bias (no phase current crosses zero); 2 × 2 impedance from both runs, actuation delay and hold undone, eigenvalues in the frame of the zero used; AXES when the saliency axis disagrees with it | `est_ldq, ldq` |
| ψ, zero, direction | i_d = i_q = 0 on the dyno: ψ = \|e\|/\|ω_e\|, zero = the record's + atan2(e_d, e_q); the voltage vector's turn against the resolver (DIR_PHASES), the resolver against the attested dyno (DIR_DYNO) | `est_psi, emf` |
| Staging | VALID + inside the FW-20 class limits (`calib_check`'s ranges) + inside its band of the record (10 / 10 / 5 % / 2° el) ⇒ staged; beyond ⇒ a second agreeing run (3 σ) | `judge, class_ok` |
| Commit | RoutineControl 0xF021 with a fresh unlock and the bridge not switching (the record's NVM copy is a PRIMASK section): the active record + the staged values, `calib_seal` + `calib_check` (layout, CRC, ranges, SKU, UID, motor ID), a new NV_REC_CALIB version, `DTC_MC_CAL_WRITTEN`; this key cycle keeps its record, the next init validates the new one | `commit` |
| UDS | `31 01 F0 20 [routine] [attest]` start, `31 02 F0 20` stop, `31 03 F0 20 [index]` results (the heartbeat), `31 01 F0 21` commit | `mc_uds_handle` (pre-dispatched from `app.c: diag`) |
| CALs | 31 values with ranges (`mc_cal_t`), checked at every start: a bad one refuses the service mode, never driving | `MC_CAL_DEFAULT, RANGES` |
| Host plant | a virtual PMSM — L_d ≠ L_q, R_s, ψ, the true electrical zero, the resolver (reversible), a dead time differing from the FOC's compensation, the target's actuation delay, the rotor locked / dyno-driven / free | `src/platform/host/sim_pmsm.c`; `tests/harness.h: H.plant` |

**Tolerances on the host plant** (L_d 0.30 / L_q 0.55 mH, R_s 21 mΩ, ψ 0.128 Wb, the zero 20° el off the record): stated
R_s, L_d, L_q 1 %, ψ 0.5 %, the zero 3.5 mrad; achieved on the 8XX SiC set (20 kHz loop, 500 Hz injection, 750 V) R_s
+0.11 %, L_d −0.10 %, L_q −0.01 %, ψ −0.10 %, the zero +0.54 mrad (+0.03° el), on the 4XX IGBT set (10 kHz, 250 Hz,
400 V) −0.05 %, −0.13 %, −0.17 %, −0.15 %, −0.75 mrad (−0.04° el) — round 23 fix 7 (the resolver angle dated at the
demodulator's own centroid): the zero −0.50 / −1.72 mrad, the rest within 0.1 % of these. Two estimator effects the plant exposed are designed
out: the FOC's own dead-time compensation acts on a current sampled 1.5 periods earlier (at 500 Hz it read as +45 µH on
both axes: the voltage is taken from the duties), and the inverter error differs between the d and q runs unless both
share one DC operating point (−9 % saliency without the bias).

**Tests** (`tests/test_commission.c`, suite `commission`, 10 tests): the service CALs' ranges; the three routines recover
the plant on SiC and IGBT, read over UDS with their uncertainties; L_d/L_q AXES with the record's zero 20° off, VALID with
it right; confirmation (pending → a disagreeing second run replaces it → an agreeing third stages; a value inside its band
stages at once; ψ and the zero confirmed across the two dyno directions); the record written only through FW-20 after
confirmation — refused with nothing staged, without a fresh unlock or while the bridge switches, the running cycle's record untouched, the
NVM record sealed and `calib_check`-clean, the next power-up arms with it, another card refuses it; every precondition
refuses the start with its reason (unlock, routine, length, attestation, CAL, evidence, record, §6 row, DTC, VCU enable,
vehicle speed invalid / moving, V_DC below the window, the dyno not turning, a "locked" rotor turning, above the dyno
window, RUN, disarmed; the service records are not active DTCs); a running routine aborts on vehicle speed, a DTC, the
heartbeat, V_DC, a §6 row (HVIL), a VCU enable, the tool's stop, a free rotor (light: by its speed; heavy: by its angle,
within the 2° el band) and a dyno speed step — each within its bound, the modulation off, nothing staged; no torque
command accepted in service mode; the resolver direction (DIR_PHASES with sin/cos swapped, DIR_DYNO against the attested
direction); a value outside the FW-20 class never staged (and, at the default margin, the start transient of a record ψ
far from the machine's aborts with CURRENT). Counts: 10 tests, 266 checks of the suite; on the round's starting tree
300 → 310 tests, 2802 → 3068 checks, 0 failures, also with ASan/UBSan and at −O2; `make target-check` 52 → 55
markers, each with its row (this item's three `TODO(HW)`: T-51…T-53). 21 mutations of the checks, each undone once, all
caught (`docs/traceability.md`, FW-39).

**Hooks elsewhere** (minimal insertions): `app.c` — the include, `mc_init()` in `app_init`, the UDS pre-dispatch in
`diag`, the references before and the sample after `foc_step`, `mc_task` before `gather`, the enable bar in `gather`, the
service modulation in `torque_path`; `dtc.h` — `DTC_MC_ABORTED`, `DTC_MC_CAL_WRITTEN`; `can_cmd.h/.c` — the VCU's vehicle
speed (VCU_CMD b4 [5], b6–7) and `can_vcu_cmd_vspeed()`; `tests/harness.h/.c` — `H.plant`, `H.veh_speed_*`.

### FW-40 — Diagnostic services: fault history / DTC export and read-only telemetry over UDS
The data the firmware already keeps — the DTC store, the NVM fault ring (`nv_fault_t`, `nv_read_fault()`, which had no
caller) and the live state — made readable by a service tool over ISO 14229-1 on the diagnostic bus (contract §10h;
`docs/firmware-vs-vesc.md` gaps 3 and 6). Read-only for control: nothing it writes is read by a control decision (0x14
keeps the one DTC control reads, `DTC_DESAT_REPEAT`).

| Part | What it does | Where |
|---|---|---|
| 0x19 | `01` count and `02` list by status mask (AND the availability mask 0x7F), `0A` every DTC, `04` the snapshot records of a DTC — the fault ring newest first, record n = its n-th newest event, 0xFF all; two ring records per task, restarted once if a record lands meanwhile | `src/comms/uds_diag.c: read_dtc_info, scan_step` |
| DTC numbers | 0xD10000 \| the position in `dtc_id_t` (`dtc_code()`, format 0x01); name, number and description generated from `dtc.h`: JSON for the tool, the C table the unit test checks, the contract's Markdown | `tools/dtc-table.mjs` (`--c`, `--md`, `--check`), `src/comms/dtc_table.h` (generated) |
| Status byte | the bits the store tracks (TF, TFTOC, pending, confirmed, TNCSLC, TFSLC, TNCTOC), as it tracks them; warningIndicator not tracked | `src/comms/dtc.c` (unchanged but `dtc_clear`) |
| Snapshot | DID 0xFD2F, 49 bytes: the stored record — key cycle, time, row, action, speed, i_d, i_q, V_DC — and the operating context FW-40 appended to `nv_fault_t`: state, torque command and applied, hottest module NTC, the two motor sensors | `nvlog.h: nv_fault_ctx_t`; `app.c: ctx_now`; `fault_mgr.c: fm_desat` |
| 0x22 | every 0x22 reaching it (the other items' DIDs are answered before it; unknown: NRC 0x31); 12 DIDs (round 23, FW-46: a 13th, 0xFD46 the ripple table, written by 0x2E), up to 8 per request: 0xF200 state, 0xF201 arming and the FW-24 evidence item by item, 0xF202 speed and torque (command and applied, FW-37), 0xF203 currents, 0xF204 both V_DC channels, 0xF205 temperatures, 0xF206 limits in force, 0xF207 watchdog and heartbeat, 0xF208 uptime and DTC counts, 0xFD20 identity (`TI_FW_ID`), 0xFD21 calibration layout/version, 0xFD22 the validation record | `DIDS`, `did_*`; each copied between two current-loop ISRs (`did_copy`) |
| 0x2A | the 0xF2xx DIDs as a stream at 100 / 10 / 1 ms (slow / medium / fast; 1 kHz the ceiling), four at most, UUDT frames on 0x6E9; one frame per task at most, none beside a response frame, round robin; a busy mailbox drops it (counted) | `periodic_rq`, `periodic_step` |
| 0x14 | gated as the FW-32 routine: SecurityAccess (one clear per unlock), HV absent (unknown = present), the bridge disarmed; the DESAT class, the service lock and the key cycle's no-arming failures are **kept** — a clear releases no latch, so it must not hide one; the fault ring is not erased | `clear_dtcs`, `kept`; `dtc.c: dtc_clear` |
| Transport | ISO 15765-2 on CAN-FD (TX_DL 64): requests in single frames (classic or escape) — round 23 fix 11: segmented requests for the FW-38 programming services under this ECU's flow control (BS 4, STmin 0, N_Cr 1 s); responses classic / escape single frames or first + consecutive frames under the tester's BS, STmin, WAIT, overflow; N_As, N_Bs 1 s; retried while the mailbox is busy | `tp_step`, `send_usdt`, `flow_control`; `rx_first`, `rx_consecutive`, `rx_flow_control`, `rx_dispatch` |
| Wiring | one dispatch line in `uds_handle()` (after FW-41's), `uds_diag_tick()` at the end of `app.c: diag`, `app_t.udsd` | `uds.c`, `app.c`, `app.h` |

**Tests** (`tests/test_uds_diag.c`, suite `uds_diag`, 11 tests, 649 checks): every DTC has its number and description;
0x19 01/02/0A against the simulation's injected faults (HVIL open, V_DC channels apart, a DTC failed then passed) equal to
the store bit for bit for six masks; 0x19 04 after two DESATs (the FW-15 retry path) byte for byte the stored records,
records 1/2/0xFF, empty, out of range, the scan over tasks; a record written during a scan (restart; twice: NRC 0x22);
every DID against the live state at the same instant (floats bit-exact), eight DIDs in one escape request = eight single
reads; 0x14 refused without the unlock (0x33), with HV present, unknown, and armed (also armed with the link reading
20 V) (0x22), the clear and the unlock consumed; the DESAT class and the service lock kept (status, occurrences, times),
the retry still refused; 30 malformed requests with their NRCs, invalid frames unanswered, a 4000-frame fixed-seed fuzz
with every answer well-formed and the store unchanged; the flow control (STmin, BS, WAIT, a busy mailbox, N_Bs,
overflow, abort by a new request); the stream (1 kHz, round robin, rates, re-rate, stop, capacity, drops, content =
the DID); a 400-task run at 3000 rpm with four DIDs at 1 kHz bit-identical in every control output and task end time to
one without, the layer taking no simulated time. Counts: the tree as FW-40 landed 358 tests / 3792 checks, with it 369 /
4441, 0 failures, also with ASan/UBSan and at −O2 (other agents' suites in progress at the same time: see the round's final
counts). 21 mutations, each undone, all caught — the three 0x14 gates, `DTC_DESAT_REPEAT` and the service lock made
clearable, the unlock not consumed, the status mask ignored, i_d/i_q swapped, records of every DTC, no scan restart, the V_DC
channels swapped, a malformed 0x22 length accepted, every due DID in one task, periodic beside a response, block size,
STmin, N_Bs, WAIT, a busy mailbox losing the frame, the layer waiting 5 µs, the torque command not recorded
(`docs/traceability.md`, FW-40). `make target-check`: no marker added or moved by FW-40. `TI_FW_ID` unchanged (the image
and the `nv_fault_t` layout changed: the round's ID bump covers it; old ring records read ext = 0).

**Hooks elsewhere** (minimal insertions): `uds.c` — the include and one dispatch line; `app.c` — `uds_diag_tick()` in
`diag`, the operating context in `ctx_now`; `app.h` — the include (after `TI_FW_ID`, so the T-06 marker keeps its line)
and `app_t.udsd`; `dtc.h/.c` — `dtc_clear()`; `nvlog.h` — `nv_fault_ctx_t`; `fault_mgr.h/.c` — `fm_ctx_t.op` into the
DESAT record; `platform/host/sim.h`, `sim_hal.c` — `sim_can_tx_busy()` (the target's one TX mailbox busy);
`tests/test.h`, `test_main.c` — the suite.

### FW-41 — Sampled-waveform capture with fault trigger
A read-only oscilloscope-style capture (contract §10i; `docs/firmware-vs-vesc.md` gap 4): nothing in control reads it.

| Part | Implementation | Tests (`tests/test_capture.c`, suite `capture`) |
|---|---|---|
| Ring | `src/diag/capture.c/.h`: `CAP_N` = 2048 records × 32 B = 64 KiB (build-time, `-DCAP_N=…`; 12.5 % of the S32K396's 512 KB of system SRAM), one per current-loop ISR: its entry time, flags (the §6 rows, bridge mode, state, the validities, the §6 action, the trigger mark) and 12 int16 channels — i_a/i_b/i_c, i_d/i_q and their references, v_d/v_q, V_DC (0.05 A / 0.05 V), the resolver angle (2π/32768 rad) and speed (1 rpm). `cap_isr()` is the last statement of `app_isr_current()`: a bounded copy of values the ISR holds, no conversion, no wait, no loop (not a number → −32768, saturation at ±32767, the angle wrapped) | `records_every_channel_at_the_documented_scales` |
| Triggers | a new DTC occurrence (`dtc_events()`, a count added to `dtc.c`) or a new §6 row; the command; a level on one channel with hysteresis; a sources mask; the pre-/post-trigger split set with every arm (default `CAP_N`/2); frozen until re-armed; armed at every start-up | `a_fault_trigger_lands_at_the_configured_split` (splits 0, 1, 777, 2047; frozen means frozen), `an_early_trigger_keeps_only_the_records_it_has`, `the_command_trigger`, `the_level_trigger_needs_its_hysteresis`, `the_sources_mask_selects_the_triggers`, `the_ring_never_overruns_and_never_blocks` (60 random captures), `the_isr_copy_is_a_bounded_copy_on_the_host` |
| End to end | the whole firmware on the simulated card in RUN at 1000 rpm / 100 Nm, pre 1500: the fault's record at index 1500, the 160 current-loop ISRs around it equal record by record to what the ISR computed, the currents before the fault the plant's (Σi ≈ 0, on the reference's circle) | `a_desat_is_captured_at_the_split_…` (the first ISR after the fault ISR), `an_overcurrent_…` (the first sample beyond 601 A), `an_overvoltage_…` (the first ISR after the ADC-watchdog fault), `a_resolver_loss_…` (the first tick at which the newest frame is `cal_rslv_hold_us` old) |
| Read-out | `src/diag/uds_capture.c/.h`, called first in `uds_handle()` (one dispatcher line): `22 FD 40` status, `22 FD 41` the next 56-byte block with its number and the capture id, `2E FD 41` seek, `31 01 F0 41` (re-)arm with an optional configuration (a CAN-FD single frame), `31 01 F0 42` trigger; a response over 7 bytes is a CAN-FD single frame. FW-40's DID table can call `cap_status()` / `cap_image_read()` / `cap_arm()` / `cap_trigger()` instead | `uds_read_out_sequence_integrity_and_rearm`, `the_capture_reads_out_over_the_diagnostic_bus_while_the_inverter_runs` (1176 blocks through the 1 ms task under torque; the ISR grid and the frozen capture unchanged) |
| Decoder | `tools/capture-decode.mjs` (Node, no dependencies): the blocks (hex lines, any order) → CSV; refuses a missing block, two captures, a conflicting repeat, an image its header does not account for | `the_blocks_decode_to_the_waveform_through_the_node_decoder` (the C test writes the blocks, runs the decoder, reads its CSV back; three refusals) |

Cost (`docs/timing.md`): 12–15 ns per call on the host reference; ≈ 300 Thumb-2 instructions on the M7's common path, ≤ 1 µs at
320 MHz estimated (≤ 4 % of the budget at 20 kHz); on silicon: checklist T-43. The existing timing assertions (the FW-06 15.6 µs
chain, the FS26 cadence, FW-31 liveness, the ASC-exit edge, FW-34's read delays) pass with the capture armed from boot in every
scenario. Twelve mutations — the split off by one, no hysteresis, a row's level taken for its edge, writing while frozen, a
read-out with an arm pending, the cursor kept across captures, the id not advanced, a non-number recorded as a value, every
`dtc_set` counted, the angle saturated instead of wrapped, a command surviving a re-arm, an arm without its configuration — and
the hook removed from the ISR: each caught (2 to 62 failed checks). Suite: 15 tests, 180 checks, in the host, −O2 and
ASan/UBSan builds. `TI_FW_ID` unchanged.

### FW-42 / FW-43 / FW-44 — Overspeed protection, run-time statistics, key-on current-offset refresh
Gaps 5, 7 and 9 of `docs/firmware-vs-vesc.md`; contract §10j. Code: `src/safety/overspeed.c`, `src/nvm/runstats.c`,
`src/sense/offtrack.c`, the 1 ms task in `src/app/app.c` (`overspeed`, `offset_refresh`, `run_stats`, `init_runtime`);
tests: `tests/test_fw42_44.c` (suite `fw42_44`).

| Item | Before | Change | Tests |
|---|---|---|---|
| FW-42 — overspeed | No direct check: a speed above the motor's `n_max_rpm` was caught only when its back-EMF tripped the FW-06 DC-link compare. | The resolver-valid speed (never a held one), both directions, against the calibration record's `n_max_rpm` every 1 ms, before `detect()`. **Warning** from `cal_ovs_warn_frac` × n_max (1.00): the §6 command-lost row (FW-09/FW-33's orderly ramp: torque to zero, then SPO below n_x, current control kept above it with the battery), the speed-limit request (b14.3), no arming (MCU_GATE_EN not raised from DISARMED). **Trip** from `cal_ovs_trip_frac` × n_max (1.05): also the §6 "Resolver invalid, or control lost" row, latched — SPO below n_x under the energy rule, LS-ASC at or above it; no ASC of its own. `cal_ovs_debounce_ms` (10) consecutive samples to enter or leave a band, `cal_ovs_hyst_frac` (0.02 × n_max) hysteresis; `DTC_OVERSPEED` one occurrence per event. | `overspeed_warning_trip_recovery_with_hysteresis`, `overspeed_debounce_in_ms_and_no_evidence_without_a_valid_resolver`, `overspeed_warning_zeroes_the_torque_and_requests_a_speed_limit` (n_max above and below n_x), `overspeed_trip_takes_the_control_lost_row_at_high_and_low_speed` (LS-ASC at 17.2 krpm, n_x 8086 rpm; SPO at 6.5 krpm, n_x 24 258 rpm; the VCU reset only below n_x), `a_single_bad_resolver_sample_never_trips_overspeed`, `overspeed_refuses_arming_while_active` |
| FW-43 — run-time statistics | None. | Energy drawn / returned (motoring / regenerating) as ∫ V_DC·I_DC with I_DC the bridge's DC current from the loop's own v_dq·i_dq (no DC shunt), while modulating; key-on time, time in RUN/DERATE; the highest module, coolant and motor temperatures; DTC occurrences per class (8 classes); key cycles (the existing counter). **No distance: the contract has no vehicle-speed or wheel signal.** The run-time record (NVM A/B + CRC, layout version) every `cal_rs_save_s` (600 s) and once on entering SAFE_POWERDOWN; **an unclean shutdown loses the interval since the last record** (VESC's caveat, now ours, stated). Read-only DID 0xFE43 (0x22, one CAN-FD single frame). | `runstats_energy_time_and_maxima_integrate_exactly`, `runstats_dtc_occurrences_count_per_class`, `runstats_record_is_saved_at_a_bounded_rate_and_once_at_shutdown`, `runstats_accumulate_a_scripted_drive` (2 s at +100 N m and 1 s at −100 N m at 3000 rpm against T·ω to 1 %; the DID end to end), `runstats_persist_across_a_key_cycle_and_lose_the_last_interval_on_power_loss`, `runstats_did_answers_only_its_own_read` |
| FW-44 — offset refresh | The key-on check refused to arm on offset drift beyond `cal_isns_offset_tol_v`; a drift inside it was never corrected. | The check is unchanged and still against the EOL record (never modified). When it passes, the working offset (the conversion's and the FW-05 compare's) steps toward the fresh mean by at most `cal_isns_ofs_step_v` (2 mV): once per key cycle (kept across an MCU reset), disarmed, PWM off, at standstill (< n_ss) — never armed, moving or mid-run. Persisted in the run-time record, bound to the calibration record's CRC; a tracked value outside the EOL tolerance is ignored. | `offset_tracker_adopts_a_bounded_step_inside_the_tolerance_only`, `offset_refresh_at_key_on_rate_limited_per_key_cycle` (EOL untouched, the FW-05 compare follows, an MCU reset adopts nothing more, a new calibration restarts), `offset_outside_the_tolerance_still_refuses_and_adopts_nothing`, `no_offset_adoption_while_moving_or_armed` |

Also: six CALs (`cal_ovs_warn_frac`, `cal_ovs_trip_frac`, `cal_ovs_hyst_frac`, `cal_ovs_debounce_ms`, `cal_rs_save_s`,
`cal_isns_ofs_step_v`; `round23_cal_defaults_and_ranges`), `DTC_OVERSPEED`, `NV_REC_RUNTIME` (slots 28/29: records after the
fault ring take A/B pairs in enum order — FW-38's boot record followed at 30/31, which fills the 32 slots; FW-43 and FW-44
share one record for that reason; round 23 fix 10: 64 slots). **Mutations: 19, all caught** (each applied alone to a private copy of the tree): FW-42 —
debounce removed, hysteresis removed, the trip raised as the overvoltage row (an ASC at any speed), the trip raising no row,
the arming refusal removed, the warning not the command-lost row, the signed speed, a held speed counted as measured, no
speed-limit request; FW-43 — regen counted as motoring, no record at controlled shutdown, no periodic record; FW-44 —
adoption outside the tolerance, no per-key-cycle limit, adoption while moving, adoption while armed, an unbounded step, the
FW-05 compare not re-programmed, tracked offsets not bound to the calibration. Suite `fw42_44`: 17 tests, 199 checks,
0 failed in all three flavours (`make test`, `-fsanitize=address,undefined`, `-O2`); `TI_FW_ID` unchanged.
Found on the way (not changed, existing FW-10/FW-28 behaviour): one resolver frame outside the amplitude window, or one
angle error of a few degrees, withdraws the resolver for good in the key cycle — the frame makes `valid` false for that
tick and the current loop raises the latched control-lost row at once, and the acceleration plausibility latches within
three frames at ≈ 0.5 rad/s of speed change per frame — while the amplitude count (`cal_rslv_debounce`) suggests a
debounce. Whether that is the intended sensitivity is a question for the FW-10 owner. (Round 23 fix 8: it was a bypass
of FW-10's debounce, now closed — see "Fixes" below.)

### Fixes
Defects found by the closed-loop simulator (`tool/README.md`, "Findings"), the FW-38 and FW-42 work and the bridge
self-test; contract §10k, traceability "Round 23 — Fixes". Each has a test that failed on the code before it (the
"before" run: the old code restored, the suite run, the listed checks failed) and mutation checks (each applied alone to
the live tree, the suite run, restored). Counts: 369 tests / 4447 checks before, **402 / 4707** after, 0 failed in all
three flavours; `make target-check` 61 markers before and after; `TI_FW_ID` unchanged.

| # | Defect (evidence) | Change | Tests (failing before) | Mutations (each caught) |
|---|---|---|---|---|
| 1 | FW-13's rate compared 1 ms samples with 20 °C/s: one code toggling at 50/100/125 °C invalidated the channel; a 5 °C/s rise latched TEMP_RATE at 70.6 °C; 60 → 112 °C at 8 °C/s left all three NTCs latched, derating at the fallback 0.544 instead of 0.4 at 100 °C (13 checks) | `sense/temp.c`: the mean over `cal_temp_rate_win_ms` (200) against the last accepted mean, past `cal_temp_rate_db_codes` (4); latch, open, short unchanged | temp: `one_code_steps_and_a_slow_rise_never_trip_the_rate_check`, `a_fast_ramp_a_step_open_and_short_still_trip`, `the_deadband_holds_a_slow_swing_of_a_few_codes`; r23: `module_temperatures_rising_through_the_derating_band_keep_their_channels` | no deadband; the rate over 1 ms; no latch; no rate check |
| 2 | At 750 V, 7 500 rpm, 0 N·m the bridge idled with √3·ω_e·ψ = 816 V above the link (3 checks); on the bridge plant −23 A into the pack, −20 N·m | `app.c: emf_needs_control`: ω_e·ψ ≥ (1 − `cal_fw_emf_margin_frac`)·v_available(V_DC) also needs current control; n_x stays the floor and the §6 column | r23: `zero_torque_below_n_x_takes_the_back_emf_off_the_diodes` (id_ref −72.9 A, 1 N·m, +1 kW losses), `the_take_over_follows_the_measured_link` (5 909 / 5 121 rpm at 750 / 650 V); bridge check step 8 (+0.5 A) | the diodes' point V_DC/√3; no margin; the OV trip for the link |
| 3 | +200 → −150 N·m at 10 000 rpm on the voltage-driven plant: DTC_OVERCURRENT, LS-ASC (slewed at 350 000 N·m/s; 3 checks) | `app.c: torque_path`: a fresh command moves at `cal_torque_slew_nm_s` (2000) both ways; §6 zeroing and solver refusals immediate; FW-11's ramp unchanged | r23: `a_torque_reversal_at_10000_rpm_is_slewed_and_never_trips`, `the_fault_paths_still_zero_the_torque_at_once`; nine scenario waits lengthened for the slew | §6 zeroing slewed; the slew at the FW-11 constant; decreases unslewed; the HVIL ramp at the slew |
| 4 | Six DTCs referenced only by dtc.h (9 checks); the §9 step-4 timeout had none either; the bridge found the FW-15 DESAT block at a power-up in FAULT with no DTC | `app.c: selftest_dtcs, temp_dtcs`, the offset check, `init`: each set where detected (temperature DTCs pass); a blocking DESAT record raises its `DTC_DESAT_HS`/`_LS` (`fm_t.desat_bank`) | r23: `a_desat_pending_at_boot_is_recorded`, `a_desat_recorded_before_the_power_up_names_the_fault_it_blocks`, `a_sensor_self_test_timeout_is_recorded`, `a_current_offset_beyond_its_tolerance_is_recorded`, `an_fs0b_release_timeout_is_recorded`, `board_motor_and_over_temperatures_are_recorded_and_pass` | nine: each DTC unset, timeouts swapped, one board NTC watched, no pass, over-temperature at the derating start, the wrong bank, the retained bank only |
| 5 | Every ASC entry at speed latched DTC_OVERCURRENT and the control-lost row (peaks 730 / 687 A at 8 500 / 10 000 rpm; 9 checks). Analysis: FAULT1 opens the high sides only; the low sides held on every sample — no safety defect | `app.c: over_current, asc_oc_rearm`, `bridge.c: t_asc_us`: inside `cal_asc_oc_window_ms` (20) of an LS-ASC entry `DTC_ASC_OC_TRANSIENT`, no row; the compare re-armed after it (FAULT1 kept with an OV flag) | r23: `an_asc_entry_transient_is_information_and_the_low_sides_hold`, `an_over_current_outside_the_asc_window_is_still_the_fault`, `an_over_voltage_asc_keeps_its_fault_flag_through_the_transients_rearm` | the window ×10; no window; never re-armed; FAULT1 cleared beside an OV |
| 6 | A resolver loss at 2 000 rpm: LS-ASC 200 ms later (`cal_speed_hold_ms`; 4 checks) | `app.c: app_speed_hi_rpm`: the bound \|n_last\| + `cal_speed_accel_max_rpm_s`·t (2 500 rpm/s, replaces the hold) in every n < n_x decision; unknown past n_max (§6) | r23: `a_resolver_loss_is_decided_on_the_speed_bound` (2 000 rpm SPO for 1.5 s; 7 000 rpm ASC at 434 ms), `the_speed_is_unknown_once_the_bound_passes_n_max` | the bound constant; n_max ignored; the §6 context on the last speed |
| 7 | The angle led the rotor by 7.7 µs (1.85° el at 10 000 rpm): the demodulator's weights sin²(φ_k + ref) have their centroid 7.698 µs after the samples' mean at ref −24° — the firmware's own arithmetic, not the host model (19 checks) | `control/resolver.c: centroid_us`: the block dated at the centroid | resolver: `the_block_angle_is_referred_to_the_demodulators_own_centroid`; r23: `the_angle_the_current_loop_uses_is_the_rotors_at_speed` | the shift's sign; the nominal phase for the block's |
| 8 | One out-of-window frame made the resolver invalid (the control-lost row at once); one 3–5° frame latched the acceleration plausibility — FW-10's 3-frame debounce bypassed (71 checks) | `control/resolver.c`: validity no longer drops on one frame; a frame beyond the tracking limit or `cal_rslv_debounce` × the acceleration threshold is kept out of the observer, one count | resolver: four tests; r23: `a_single_corrupt_resolver_frame_does_not_withdraw_the_resolver` | every frame taken; one frame invalidates; the gate at the tracking limit only; kept-out frames renew the hold; stale named first |
| 9 | A torn write of `calib_t` read back as the record from two writes back (the slot CRC-32 over a payload ending in its own CRC-32 depends on the header only) | `nvm/nvlog.c`: the slot check a CRC-32C over header + payload ("TIN2"); "TINV" slots readable, replaced by the next write | nvlog: a torn calib write, 3000 random tears, the CRC-32C, the legacy slots | CRC-32 again; new slots in the old format; legacy refused |
| 10 | 32 of 32 NVM slots used | `hal/nvm.h`: 64 slots (32 KiB Fee on the target, T-25) | nvlog: `every_record_has_its_own_slots_and_the_map_has_room` | 32 slots; overlapping A/B pairs |
| 11 | TransferData moved 5 bytes per block (≈ 210 s per MiB) | `comms/uds_diag.c`: an ISO 15765-2 receiver (flow control BS 4, STmin 0, N_Cr 1 s) for the programming services; `UPD_BLOCK_MAX` 4095 | update: `a_1_mib_image_downloads_over_iso_tp_in_seconds` (4.6 s host-simulated, 257 blocks), `segmented_requests_are_received_under_iso_15765_2`; every power-loss and sequence test unchanged | out-of-sequence frames taken; no flow control after a block; no N_Cr; an oversized first frame ignored; another service dispatched |
| 12 | FW-21's rows said "out of scope" | this table's FW-21 row, traceability, contract §10 | — | — |
| 13 | The bridge paired one single-frame reply per request; `dtc_clear` bypassed the image; no vehicle speed | `tool/bridge`: an ISO 15765-2 tester, `periodic` lines (0x6E9), `dtc_clear` through 27/14 with the bench key (`provision` `sa_key`), `vspeed`; exports regenerated | `make -C tool/bridge check`: 33 expectations (service mode, 0x19 04 segmented, 0x19 0A, 0x2A, 0x14 gates, the DESAT block named) | no flow control; periodic paired as responses; no vehicle speed; the old hook; no key (a sequence-check removal is not observable: the image never sends one) |
| 14 | Counts | parameter sets 188 fields, 89 CAL rows; DTCs 80; this README, T-37, `docs/timing.md` | — | — |

**Open (reported, not changed):** the host card model applies the ASC latch that FS1B presets at power-up without the
gate supply (`platform/host/sim_chain.c: sim_chain_ls_on`), so a power-up while the rotor turns shorts the winding in the
plant (≈ 700 A at 1 500 rpm, DTC_OVERCURRENT, DTC_SENSOR_SELFTEST); on the card no switch conducts before §9 step 6.
`docs/qualification-plan.md` (QP-FW-07) still names `cal_speed_hold_ms`, now `cal_speed_accel_max_rpm_s`.

## Round 23 — FW-45/FW-46

Gaps 8 and 10 of `docs/firmware-vs-vesc.md`: saturation-dependent L_d/L_q in the torque solve, the current loop and FW-39's
commissioning (FW-45, contract §10l), and a torque-ripple (cogging) feed-forward loaded by the service tool (FW-46, contract
§10m). Code: `src/control/motor.h` (the maps, `motor_sat`), `torque.c/.h`, `foc.c/.h`, `src/nvm/calib.c/.h`,
`src/app/commission.c/.h`, `src/app/app.c/.h`, `src/comms/uds_diag.c/.h`, `tools/gen-params.mjs`; the host plant
`src/platform/host/sim_pmsm.c/.h` (saturation, cogging); tests `tests/test_fw45_46.c` (suite `fw45_46`, 13 tests).

"Before" is the old behaviour restored in the new tree — the new tests cannot compile against the old sources, which have
neither the maps nor the table — the suite run, the failed checks counted; each is also one of the mutations below. The
suite of the previous tree (402 tests, 4707 checks) runs unchanged and passes: a flat map (the default) is the scalar model
bit for bit.

| Item | Before | Change | Tests (fail before → pass after) |
|---|---|---|---|
| FW-45 record | layout 3, no maps | layout 4: `motor_t` gains the L_d, L_q maps (apparent inductance at 0, 0.2 … 1.0 × the SKU's current limit) and their full scale (bound to `i_crest_a`); flat at the scalars by default; `calib_check`: class range, non-increasing, last ≥ 0.3 × first; `calib_t` the ripple table | `the_record_is_layout_4_and_a_bad_map_is_refused` (layout 3 kept: 2 checks) |
| FW-45 torque solve | the scalar model: on a saturated machine TQ_OK is > 0.1 % off the torque in 1 271 of 4 733 cases (worst 217 N·m); the plant at 80 % current gets −9.8 % | λ_d = ψ + L_d(\|i_d\|)·i_d, λ_q = L_q(\|i_q\|)·i_q in the torque, ellipse, circle, demagnetisation limit and postcondition; the solve on the maps' contour (exact contour, slope from the differential inductances, a five-point sign scan per argmin, the ψ_e guard from the maps' extremes); flat maps take FW-37's path | `the_saturated_solve_meets_…` (20 000 cases), `a_flat_map_is_the_scalar_solve_bit_for_bit` (20 000), `the_psi_e_guard_keeps_…` (20 000), `the_map_delivers_the_torque_…` (the maps ignored: 12 checks in 5 tests; the scalar contour: 8) |
| FW-45 current loop | one fixed gain: at 0.37 of the unsaturated differential inductance a 20 A step overshoots 93 % and rings | k_p per axis × the map's differential inductance over the unsaturated one, in [0.3, 1]; the speed voltages and FW-10's speed model from the maps' flux | `gain_scheduling_keeps_…` (no scheduling: 3 checks) |
| FW-45 commissioning | one bias (50 A along phase U) | FW-39's L_d/L_q routine at bias index k (0…5, the 8-byte start as a CAN-FD escape frame), the bias on the true axes; results 0x40+k / 0x50+k, 0x02, the flags' bias bit; a whole map committed (trapezoids from the differential points) | `the_routine_at_six_biases_…`, `the_bias_byte_is_checked_…`, `the_bias_runs_on_the_true_axes` (the old 7-byte parser: 56 checks in 2 tests) |
| FW-46 ripple feed-forward | none | the record's 36-point table; applied in the current-loop ISR at the FOC's angle (less its mean, clamped to `cal_ripple_ff_max_a`), gated by 6·f_e < `cal_ripple_ff_fmax_hz`, scaled to the solved margin; DID 0xFD46 write/read, staged and committed through FW-39 | `the_ripple_feed_forward_cancels_…` (never applied: 1 check; the DID not dispatched: 16 checks in 2 tests), `the_ripple_table_write_…`, `the_feed_forward_never_leaves_…`, `with_the_default_cal_…` |

**The plant's numbers** (8XX SiC, 750 V; the FW-39 PMSM — L_d 0.30 / L_q 0.55 mH, 21 mΩ, 0.128 Wb, 4 pole pairs — made to
saturate, the flux L₀·I_s·atan(i/I_s) with I_s 600 A on d and 320 A on q, or to cog, 4.0 N·m at the 6th and 1.5 N·m at the
12th harmonic):
- at 80 % of the current limit (384.7 A; the plant's MTPA torque there 310.9 N·m) at 1000 rpm: the flat map delivers
  **−9.8 %** (at 347 A), the plant's map **−0.1 %** (at 384 A); the map measured through FW-39 at the six biases and
  committed: **+0.05 %**;
- the sweep: ten runs (four points confirmed by a second run); each differential inductance within 0.2 % of the plant's;
  the committed apparent map within 2.7 % (point 0 is the differential at 50 A, FW-39's own convention: 2.5 % below L(0)
  on this strongly saturating q axis);
- the ripple at 100 rpm, 30 N·m: **3.02 → 0.51 N·m rms (−83 %)** — the 6th 3.99 → 0.40, the 12th 1.50 → 0.49 N·m: linear
  interpolation of 36 points keeps sinc² of a harmonic, 0.91 of the 6th and 0.68 of the 12th; at 600 rpm (6·f_e 240 Hz)
  nothing is applied, 3.01 N·m with and without the table.

**Convexity and the postcondition.** FW-37's bisections relied on |i|² and |v|² being convex along the torque hyperbola.
On a map they need not be (the randomised reference found contours with two local minima of |i|: a saturating L_q below
L_d turns the reluctance torque over at high i_q; a saturating L_d on an SPM creates one), so the solve now reads the
slope's sign at five points before bisecting and takes the least bracketed minimum; every search keeps its bracket and its
step bound (it terminates), and the postcondition still checks every returned vector. On 20 000 random saturating maps:
no postcondition failure, TQ_OK within 0.1 %, no TQ_LIMITED where the request fits (before the sign scan: one), and eight
TQ_LIMITED of 8 507 valid but more than 0.1 N·m below the most that fits — five where the ψ_e guard from the maps'
extremes excludes i_d that fits (weak magnets with L_q saturating below L_d, reverse-salient machines with a saturating
L_d; worst 91.5 of 127.5 N·m on a 0.02 Wb machine with L_q down to 46 %), three where the reduction's feasibility is not
monotone on the map (0.1–2.4 N·m). The guard is kept on purpose: from the scalars it lets the searches onto a contour that
does not exist — 25 TQ_POSTCOND (the §6 row, driving) in 20 000 weak-magnet cases, none with it.

**Gain scheduling is needed** (the number): with a fixed gain the q loop's crossover rises by 1 / (the differential
inductance's ratio) — at 0.37 from 1.08 kHz to ≈ 2.9 kHz, phase margin from ≈ 61° to ≈ 12° (90° − 360°·f_c·75 µs): 93 %
overshoot, 87 % ring-back on a 20 A step; scheduled 6 %, none. The floor 0.3 holds where a steep or non-physical segment's
L + i·dL/di goes to zero or below.

**Decisions** (with the line relied on):
- The map is used as its shape (each point over point 0) at the level of the scalar (`motor.h: motor_sat`): point 0 is the
  scalar in every record `calib_nominal` and the commit write, a flat map is the scalar model bit for bit — also for tests
  that change `ld_h` after the nominal record (`test_scenarios.c: a_salient_motor_in_field_weakening_…`) — and FW-39's scalar
  alone moves the map's level. A map whose point 0 is 0 is none (a `motor_t` built outside a record).
- The full scale `i_map_a` is stored in `motor_t` (a 13th value, bound to `p->i_crest_a` by `calib_check`) so
  `torque_from_current(id, iq, m)` and `torque_v_required(…, m)` keep their signatures: the old suite compiles and runs
  unchanged.
- The ripple table is int16 in 0.01 A, not float: 36 floats make the record 548 bytes, the NVM slot holds 496 (`nvm.h:
  HAL_NVM_SLOT_SIZE` 512 − the 16-byte header); as counts it is 476.
- The gate is the cogging fundamental 6·f_e (six per electrical period, a three-phase machine's), not the table's lowest
  harmonic: no analysis of the table at run time, and a table of lower harmonics is still gated where the loop follows it.
- The feed-forward never carries the table's mean (a ripple has none; the mean torque stays the solved one) and is scaled
  down, never the solved vector, until the vector with the table's extremes fits the circle and the ellipse; the
  postcondition's torque band stays on the solved vector.
- The commissioning measures the differential inductance (the HF impedance at a DC bias); the commit converts a whole axis
  to the apparent map by trapezoids and refuses a partial one (NRC 0x22). The bias runs on the true axes (the d run at
  −i_d, the q run at +i_q); at low bias a phase current can sit inside the HF swing (on the host 3–19 A for k = 0…3, the
  results still within 0.14 %) — T-57.
- The §6 energy screen and the back-EMF bound keep the scalars (the unsaturated inductance bounds the saturated one on a
  non-increasing map: the screen stays conservative).

**Cost**: `foc_step` +8 ns on the host with maps (29.9 → 38.2 ns), the table +2 ns — ≈ +0.7 µs on the M7; the solve with a
saturating map 3.0× FW-37's mean and 2.8× its slowest case on the host — ≈ 180 µs at 320 MHz by FW-37's estimate
(`docs/timing.md`); a flat map costs nothing more. T-57 measures it.

**Counts**: 402 → **415 tests, 4707 → 4875 checks, 0 failed** (421 / 4937 after the second pass below) in all three flavours (`make test`; `-fsanitize=address,undefined`;
`-O2`), also at `-O2 -ffp-contract=off` and on the Cortex-M7 under QEMU (`make BUILD=build/qemu_fw4546 qemu-test`: GCC 14.3,
newlib, no contraction — the same counts, the same printed numbers to the last digit or two, 1397 s wall); parameter sets 188 → **190 fields**, 89 → **91 CAL rows** (`cal_ripple_ff_max_a` 0 A [0, 30], `cal_ripple_ff_fmax_hz`
200 Hz [0, 500]; `make params`); DTCs **80** (none added); `make target-check` 61 → **63 markers** (T-57
`src/app/commission.c:283`, T-58 `:947`; T-52/T-53's citations moved with the file). `TI_FW_ID` unchanged (0x0A0F0015; the
manager's bump covers the image and the layout-4 record). The protocol exports regenerated (`tool/protocol/*.json`: the two
CALs, the bias byte, 0xFD46; `generate.mjs --check` clean) and `make -C ../tool/bridge check` passes; the bridge needs no new
command (its `uds` line carries the escape frame and the segmented write) — its plant has neither saturation nor cogging.

**Mutations** (`docs/traceability.md`, "Round 23 — FW-45/FW-46"): 41, each applied alone to the live tree, its objects
deleted, the suite run, the source restored — all caught (M4, the ψ_e guard, only after the weak-magnet test was added; M19,
a partial map, only after R_s was staged beside it). Not host-testable: the ordering of the feed-forward's scale around
the references against a current-loop ISR preempting the task (T-58).


## Round 23 — second pass (after the QEMU run and the tool's bench work)

| Item | What | Proof |
|---|---|---|
| ISR 64-bit divide (T-36) | the current-loop ISR stamped a DTC time with `hal_time_ms()` and the SDADC completion ISR located blocks with `since / per` — both 64-bit divisions, which the M7 has only as libgcc's `__aeabi_uldivmod` loop (the QEMU build showed the references). `ti_udiv64_16` (hal/timer.h): four 16-bit digits, one hardware UDIV each, exact for divisors 1..65535; `ti_ms_from_us64` and `resync()` use it | tests/test_time.c `the_64_by_16_bit_division_is_exact_without_a_64_bit_divide` (20 000 random operands + the edges + the ms wraps against the 64-bit `/`); mutation `(r << 15)` caught (3 checks); `make target-size` now disassembles every M7 object and fails on `__aeabi_(u)ldivmod` outside commission/update/uds_diag/runstats/nvlog and `hal_pwm_init` |
| Host rounding = target rounding (T-59) | Apple clang fuses `a*b + c`; GCC for the M7 under -std=c11 does not, so the host suite never ran the target's float rounding. `-ffp-contract=off` in the host CFLAGS and the bridge's | the suite unchanged (421 / 4937 / 0 at the end of the pass); the QEMU run prints byte-identical results |
| Root of trust reported | DID 0xFD23: kind (0 none, 1 TEST, 2 build key, 3 OTP/HSE) + key id (SHA-256(public key)[0..3]); the simulator's hello `root`; not a DTC (it would fail every "no active DTC" precondition on development units) | tests/test_uds_diag.c `the_root_of_trust_is_reported_with_the_test_key_id` (kind 1 on the host, key id 0xD51131AD pinned) |
| Bridge: a committed record at the next key cycle | `power_up()` handed `app_init()` the bench's sealed default record, so a FW-39 commit was never loaded after `reboot` (found by the tool's commissioning test) → the NVM record wins once it exists | tool/qt tst_gui_smoke: the calibration CRC changes across the reboot, the routines re-run on the new record |
| Map tolerance + motor DID | FW-45's record check refused every map commit on a machine that does not saturate (the six measured points scatter by ≈ 0.1 %, and the rule was never-rising; found by the tool's sweep) → a rise ≤ 2 % between neighbours passes (MOTOR_MAP_RISE_TOL); DID 0xFD25 carries the record's motor data (ψ, pp, the scalars) for the tool's ripple import over CAN; DID 0xFD26 the active record's maps (a committed map read back after the key cycle) | tests/test_fw45_46.c (a 1 % rise passes, 3 % refused), tests/test_uds_diag.c the_motor_did_mirrors_the_record |
| Checklist | T-57 (the map on the dyno) and T-58 (the ripple table) from FW-45/46; T-59 the QEMU run (renumbered from a duplicate T-57); T-43's instruction count from the M7 build (458, not ≈ 300) | `make target-check` 63 markers |
