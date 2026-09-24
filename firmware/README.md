# Traction inverter firmware (S32K396 + FS26)

This is the application firmware for the 220 kW / 850 V traction inverter. It covers four SKUs:
8XX SiC, 8XX IGBT, 4XX IGBT and 4XX SiC. It implements `docs/firmware-contract.md` (rev A.12),
§8/§8a of `docs/design-basis.md`, the round-12 disposition, and the ball map
`calculations/mcu-ballmap.json`. It is C11 with no dynamic memory and no recursion, and every
loop is bounded. It uses fixed-width types and single-precision float only.

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
result: **178 tests, 1209 checks, 0 failures.**

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

**Arming is blocked until two things are provisioned.** First, set `cal_fs26_prog_id` to the
M_PROGID of the FS26 OTP variant that was procured; the default 0xFFFF means "unbound", and the
image will not arm. Second, the calibration record must be sealed to the device UID.

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
  and has 153 fields. 62 of them are `cal_*` values the contract does not fix, mostly hardware
  timings and tolerances. Each has its contract default and a `[min, max]` range
  (`include/cal_ranges.h`), checked at boot.
- **Units:** see `include/ti_types.h`. Time is `uint32_t` µs or ms, compared only through
  `ti_elapsed()`/`ti_age()`. ADC values are 12-bit codes. Speed is mechanical rpm. Angles are
  electrical rad in [0, 2π).
- **Safe states.** `safety/safe_state.c` is the §6 matrix as a pure function. It has speed
  columns (n < n_x, n ≥ n_x, unknown ⇒ high), the rule (a) winding-energy screen and the rule (b)
  release. `safety/fault_mgr.c` combines rows (forced SPO wins, otherwise the highest rank) and
  owns the FW-15 retained record and the one authorised retry.
- **Timing:** see `docs/timing.md`. The current loop runs at 2·f_sw on the BCTU trigger, the
  1 ms task handles supervision, and the fault ISR is the highest priority.

## Modules (lines, `wc -l`)

| Directory | Lines | Contents |
|---|---|---|
| `include/` | 1028 | types/units, parameter struct, 4 generated SKU sets, CAL ranges |
| `src/util/` | 104 | math helpers, CRC-8 (0x1D, SAE J1850) and CRC-32 |
| `src/hal/` | 256 | the 10 HAL interfaces |
| `src/sense/` | 581 | current (FW-05), V_DC (FW-07/18), temperatures (FW-13), HVIL (FW-09), HW_ID (FW-01/02), IGN |
| `src/control/` | 771 | resolver (FW-10), FOC/SVPWM, MTPA/field weakening/limits (FW-03/04), gains, DC-link (FW-08) |
| `src/safety/` | 1867 | state machine, §6 matrix, fault manager (FW-15), FS26 (FW-12), bridge sequences, gate power (FW-14), self-test (FW-16) |
| `src/comms/` | 462 | CAN command/status with E2E (FW-11), UDS DTC store |
| `src/discharge/` | 269 | FW-17/18/19, FW-02 τ |
| `src/nvm/` | 476 | parameter sets, calibration (FW-20), NVM log and queue |
| `src/app/` | 973 | integration |
| `src/platform/s32k396/` | 1573 | generated ball-map tables, register images, drivers with RTD bodies |
| `src/platform/host/` | 1503 | simulation of the card, FS26 and MCU peripherals |
| `tests/` | 3981 | 28 suites (one file per module + scenarios), 178 tests, harness |
| `tools/` | 325 | parameter and board-map generators |

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
  ADC indices and thresholds (`tests/test_platform_cfg.c`), plus the ball-map binding.

**Needs the target, HIL or the bench:**

- every `TODO(RTD)` binding, and whether the lock-down registers really lock (REG_PROT/XRDC);
- the IMCR routing of PTC26/PTC25 to FAULT0/2, and the ADC watchdog → TRGMUX/LCU → FAULT1 path;
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
| 01, 02 | sense/hwid.c, app.c, discharge.c (τ) | hwid, discharge, scenarios | yes | divider tolerance |
| 03, 04 | control/torque.c, sense/temp.c | torque, state_machine | yes | dyno, thermal |
| 05 | sense/current.c, app.c, s32k396_adc/pwm.c | current, params, platform_cfg, scenarios | logic + modelled compare | ADC WD → FAULT1 route |
| 06, 06a | sense/vdc.c, app.c, safety/bridge.c | vdc, bridge, scenarios | yes, modelled timing | 15.6 µs on HIL |
| 07 | sense/vdc.c | vdc, scenarios | yes | EOL gains |
| 08, 08b | control/dclink.c, safe_state.c, fault_mgr.c | dclink, safe_state, fault_mgr | yes | bank tuning |
| 09 | sense/hvil.c | hvil, scenarios | yes | harness |
| 10 | control/resolver.c | resolver, scenarios | yes | SDADC/SWG config, latency |
| 11 | comms/can_cmd.c, torque.c | can_cmd, torque, scenarios | yes | vehicle DBC |
| 12 | safety/fs26.c | fs26, scenarios | yes (model of the FS26) | OTP image, silicon |
| 13 | sense/temp.c | temp | yes | sensor parts |
| 14 | safety/gate_power.c | gate_power | yes | RDY timings |
| 15 | app.c, fault_mgr.c, bridge.c, s32k396_pwm.c | fault_mgr, bridge, safe_state, scenarios, platform_cfg | yes | IMCR route, REG_PROT |
| 16 | safety/gate_selftest.c | gate_selftest, state_machine | yes | chain timings |
| 17, 18, 19 | discharge/discharge.c | discharge, state_machine, scenarios | yes | resistor thermal |
| 20 | nvm/calib.c, nvm/nvlog.c | calib, nvlog, scenarios | yes | Fee config, UID |
| 21 | not implemented (bootloader + HSE) | none | no | all |

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
