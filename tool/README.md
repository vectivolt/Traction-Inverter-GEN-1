# Traction Tool — simulator bridge and protocol exports

The language-neutral core of the Traction Tool, the bench/service application for the 220 kW / 800 V traction
inverter. The desktop application itself is a Qt 6 (C++) program; this folder gives it three things:

| Path | What it is |
|---|---|
| `bridge/` | `sim_bridge`: the inverter firmware (`firmware/src`, unmodified) running on its host simulation with a physical plant — a PMSM on a dyno, the DC link and pack, temperatures, a bench VCU/BMS — as a child process speaking newline-delimited JSON on stdin/stdout. Its own self-test (`make check`) and example scenario scripts. |
| `PROTOCOL.md` | The interface specification: every bridge command and output field (types, units, ranges, examples), and the vehicle CAN-FD / UDS frame codec derived from `docs/firmware-contract.md` and `firmware/src/comms` (IDs, byte layouts, scaling, clamps, CRC/E2E rules). Written for a C++ implementer who has not read the C. |
| `protocol/` | Machine-readable exports, regenerated from the firmware sources by `generate.mjs`: `params.json` (the calibration parameters from `firmware/tools/gen-params.mjs`), `dtcs.json` (every DTC with its description and the firmware's response), `can-frames.json` (the codec tables and 21 test vectors from the firmware's own encoders). |

Nothing here modifies `firmware/`. `qt/` (if present) is the Qt application, developed separately.
The earlier web-UI/Bun-server plan was dropped before any of it was committed to the tree.

## Build and run the bridge

Requirements: a C11 compiler (clang or gcc) and `make`; for the exports, Node ≥ 18 (the repository already uses it
for `firmware/tools/*.mjs`) or Bun. No network access and no third-party libraries.

```
make -C tool/bridge            # builds tool/bridge/sim_bridge (objects in tool/bridge/build/)
make -C tool/bridge check      # the stdio self-test (below), ~0.2 s once built
cd tool/bridge && ./sim_bridge --rate 200          # telemetry on stdout, commands on stdin
./sim_bridge --time 0 --script scenarios/drive-cycle.jsonl   # a scenario, as fast as possible
./sim_bridge --golden          # the codec test vectors, then exit
```

The bridge compiles the firmware with the firmware Makefile's host flags (`-std=c11 -Wall -Wextra -Werror -Wshadow
-Wdouble-promotion -Wmissing-prototypes -Wstrict-prototypes -Wundef -Wpointer-arith -Wcast-qual -Wvla`; its own
files too). The DTC name table is generated from `firmware/src/comms/dtc.h` at build time, and a compile-time check
fails the build if the parse ever misses an entry. Because another round of firmware work may be in progress in
`firmware/`, rebuild the bridge after firmware changes (make tracks the dependencies).

Typical session (details in `PROTOCOL.md` A.9):

```
{"cmd":"arm"}                                         precharge, contactors, ARMED_ZERO_TORQUE
{"cmd":"speed","rpm":3000}                            the dyno
{"cmd":"torque","nm":150,"hold_ms":250}               re-send every <= 100 ms while the operator holds it
{"cmd":"inject","fault":"desat_hs"}                   DTC_DESAT_HS, FAULT, section 6
{"cmd":"param_set","name":"cal_torque_ramp_nm_s","value":2500}
{"cmd":"disarm"}
```

## What `make check` proves

`bridge/check.c` starts `sim_bridge` as a child process and uses only the documented protocol (it parses the JSON
lines; it never links the firmware). It fails on the first expectation that does not hold within its budget of
simulated time:

1. `hello` carries the firmware ID and the DTC name table (DTC ids looked up by name, not hard-coded).
2. The firmware boots through section 9 to PRECHARGE_WAIT (FS26 read-back and release, identity, calibration,
   sensor self-test, gate power, the FW-16 gate self-test) within 5 s.
3. `arm`: the bench VCU precharges and closes the contactors; the firmware reaches ARMED_ZERO_TORQUE with
   MCU_GATE_EN and DRV_EN high — i.e. the fail-closed arming evidence (FW-24), the calibration (FW-20) and the FS26
   OTP binding (FW-12) are all satisfied by the provisioning.
4. The dyno reaches 1 500 rpm and the firmware's resolver speed follows (the SDADC/eDMA resolver chain works).
5. 120 N·m: RUN, modulating, and both the firmware's torque estimate and the plant's torque within 10 % — the
   current loop is closed through the voltage-driven motor model, not an ideal current source.
6. A parameter write in range is accepted; `cal_ign_off_v` = `cal_ign_on_v` is refused by the firmware's own
   `ti_params_validate()` (a cross-field rule) and nothing is written.
7. Round 23 — FW-39 service mode from the tool (paused and stepped, so the tool's heartbeat is exact): SecurityAccess
   with the bench key; a start refused while the bench VCU reports 5 km/h (NRC 0x22, reason "vehicle speed"); the Rs
   routine on the locked rotor (attestation `LK`) run to its end under the heartbeat, Rs within 2 % of the plant's.
8. Round 23 (firmware item 2): 7 500 rpm on the 750 V link at zero torque — the bridge modulates (field current),
   the pack current stays within 5 A and the shaft sees no braking torque (before the fix: idle, −23 A, −20 N·m).
9. A high-side DESAT injection: DTC_DESAT_HS active and the FAULT state within 500 ms; the VCU's FW-15 retry
   authorisation 1.1 s later (RUN again) and a second DESAT: DTC_DESAT_REPEAT.
10. Round 23 — the FW-40 services through the bridge's ISO 15765-2 tester: `19 04` returns both DESAT snapshots
    (112 bytes: a first frame, the tester's flow control, a consecutive frame), `19 0A` all DTCs (6 frames), the
    `2A` stream of DID 0xF200 on 0x6E9 at 10 ms (`periodic` lines) started and stopped; `dtc_clear` (the image's
    `14`) refused with HV present.
11. `disarm`: the contactors open and MCU_GATE_EN goes low; the dyno stopped, `discharge` to HV safe; `dtc_clear`
    accepted through SecurityAccess and `14`, the DESAT DTCs kept (a clear releases no latch).
12. `provision` without the bench key (`sa_key` false): the power-up blocks on the DESAT record of the previous key cycle (FW-15) —
    FAULT, now naming DTC_DESAT_HS — and UDS `27 01` answers NRC 0x22 (fail closed).
13. Closing stdin ends the bridge with status 0.

Run: `make -C tool/bridge check` → `PASS: 0 failure(s)` (33 expectations), about 0.2 s of wall time once built (8 s simulated).

The exports verify themselves: `node tool/protocol/generate.mjs --check` (0.3 s) re-derives all three files from the
firmware sources and the compiled bridge and fails if anything drifted (see `PROTOCOL.md` part C).

## What the bridge exercises — and what it does not

**Exercised (real firmware code, unmodified):** everything `app.c` integrates, on the host simulation of the card
that the firmware's own 402 tests use: the section 9 start-up and state machine; FS26 protocol, Q&A watchdog and
safety-output release (FW-12); HW_ID/SKU identity (FW-01/02); calibration record and parameter validation
(FW-20); the arming-evidence gate (FW-24); gate power and the FW-16 gate self-test; the current loop (FOC, SVPWM,
dead-time compensation, delay compensation), torque → current with MTPA/field weakening and the voltage-feasibility
witness (FW-25), the power envelope, derating and peak budget (FW-03/04); the resolver chain (SWG ramp, SDADC blocks
through the per-channel eDMA model, the frame protocol, the observer, FW-10/28–36); phase-current and V_DC sensing
with their plausibility checks (FW-05/07); the hardware compares and fault ISR (FW-05/06); the section 6 safe-state
matrix and fault manager, FW-15 DESAT recovery and the DESAT hold (FW-22); PWM-ASC entry/exit (section 4c,
FW-06a); precharge and discharge supervision (FW-17–19, FW-26); HVIL, KL15, the LV supply (FW-09, FW-33); CAN E2E
receive, the direction interlock, INV_STATUS; UDS SecurityAccess/RoutineControl (FW-32), the FW-40 diagnostic
services over ISO 15765-2 (0x19, 0x22, 0x2A, 0x14), FW-39's service mode and FW-41's capture; NVM records across
power cycles. The VCU/BMS talk to it through real encoded CAN frames; the torque, currents and link voltage it sees
come from a plant that responds to what the switches do.

**Not exercised:**
- The S32K396 platform layer (`firmware/src/platform/s32k396`: RTD drivers, register access, DMA, interrupt
  priorities, real preemption) and anything on silicon — the checklist `firmware/docs/target-bringup.md`.
- Timing on the target: the bridge runs the ISR and the task on exact grids; a trigger inside a long task is dropped
  rather than preempting it (the harness rule); WCET, jitter and FS26 answer spacing on silicon are not modelled.
- The hardware itself: the gate drivers, DESAT detection, the fault latch and ASC latch are the firmware repository's
  behavioural card model (`sim_chain.c`, `sim_fs26.c`), not a circuit simulation; switching is averaged per
  current-loop period (no PWM ripple, no switching transients, no EMC).
- The motor is the S6 screening motor with linear inductances (no saturation, no cross-coupling, no iron loss,
  sinusoidal back-EMF, Ld = Lq); the diode-bridge (SPO) model uses the mean inductance. Real motor data would come
  from the calibration record.
- The dyno holds speed ideally; there is no vehicle inertia or road load. Thermal models are first-order
  approximations tuned to the contract's figures, not measured coldplate data.
- A power-up while the rotor turns: the card model applies the ASC latch that FS1B presets at power-up although no
  gate supply is up yet (finding 8 below) — reboot the bridge with the dyno at 0 rpm.
- The EOL/HIL rig, the target bootloader and HSE (FW-38's update path runs on the host model only), an OEM
  SecurityAccess key (the bench key is the bridge's, `provision`), and a real VCU or BMS.
- Test hooks that the vehicle interface does not have (`asc`) are marked as such; `dtc_clear` runs the image's own
  `14` since round 23.

## How the Qt application consumes this

- **Simulator transport.** Start `sim_bridge` with `QProcess` (arguments: `--rate`, `--time`, `--sku`), write one
  JSON command per line to its stdin, read stdout line by line (`QJsonDocument::fromJson` per line) and dispatch on
  `type`: `hello` (identity, name tables, parameters), `tel` (telemetry — nested objects; flatten to dotted channel
  names), `ack` (correlate by `id`; a `uds` ack carries the reassembled `msg`), `log`, `can`, `periodic` (the `2A`
  stream). Closing the write channel stops the bridge (exit status 0).
  Implement hold-to-apply torque with `hold_ms` (the bridge zeroes the torque itself if the GUI stops refreshing).
  Staleness: telemetry arrives every `1000/rate` ms of simulated time; if no `tel` line arrives for several periods
  (paused, stopped, or crashed), show the data as stale.
- **Parameters page.** `protocol/params.json`: names, types, units, ranges, defaults, groups, descriptions and the
  cross-field rules for validation before writing; `param_set` then applies the firmware's own validation and
  reports the verdict. Diff against the device with `params`/`hello.params`.
- **Faults page.** `protocol/dtcs.json` for descriptions, classes and the firmware's response; `tel.dtc` for the live
  status; `hello.dtcs` maps ids to names for the image actually running.
- **Commissioning page.** FW-39 over `uds` (the ack's `msg`): SecurityAccess with the bench key (`provision` `sa_key`),
  RoutineControl 0xF020 start / results / stop and 0xF021 commit (`PROTOCOL.md` B.6), the results poll every 50 ms as
  the heartbeat; the preconditions from `tel` (`state`, `vcu.enable`, `vcu.vspeed_*`, `safety`, `dtc`,
  `sim.time_factor`), `enable` and `vspeed` for the bench VCU, `hello.motor` beside the identified values.
- **Firmware page.** FW-38 over `uds` (A.4.5 has one frame per `uds`, so the Qt transports segment long requests themselves: the first frame as `uds` — its ack is the ECU's flow control — the consecutive frames as `can_rx` on bus 1), checked against the bench card's boot record (A.1, `hello.boot_rec`); ECUReset restarts the card.
- **Round 23 (FW-45, FW-46, FW-38's root of trust).** The Commissioning page sweeps the Ld/Lq map (`31 01 F0 20 02 4C 4B k`, k 0…5, results 0x40+k/0x50+k/0x02; the active record's map from DID 0xFD26) and writes the ripple table (`2E FD 46`, segmented; `22 FD 46` read back); the Dashboard and Firmware pages show `hello.root` and the ripple import converts with `hello.motor`'s ψ and pole pairs (over CAN DIDs 0xFD23 and 0xFD25); the update pre-check reads DID 0xFD24 on both transports; and the Session sends one `uds` at a time per transport (the firmware drops a pending response for a new request).
- **CAN transport (real inverter).** Implement Part B of `PROTOCOL.md` (or generate code from
  `protocol/can-frames.json`) and use the `vectors` as unit tests of the C++ codec: every vector is a frame produced by
  the firmware's encoder. The adapter layer is Qt's QCanBus (plugins `peakcan`, `socketcan`, `vectorcan`, … —
  CAN FD on the ones that support it). Before the bench, the same codec can be run against the real firmware in the
  simulator: `vcu_model` + `can_rx` + `can_tap` put an external VCU on the simulated bus (`PROTOCOL.md` A.6).
- **Scenarios.** `bridge/scenarios/*.jsonl` are command lists with `in_ms`; pass one with `--script` or send its lines.

### The firmware on a virtual CAN bus (`BridgeCanGateway`)

`tool/qt`'s `BridgeCanGateway` runs that external-VCU path over a real `QCanBusDevice`, so `CanTransport` meets the real
firmware without hardware: `vcu_model` silences the built-in VCU and BMS, every `can` line (`can_tap`) is written to the
device as the target sends it (CAN FD with bit-rate switch), and every frame from the device goes back with `can_rx`
(0x7E1, 0x7E9 and 0x6E9 on bus 1, the rest on bus 0). With Qt's `virtualcan` plugin the bus is a loopback TCP server
(`can0`…`can9`, port 35468). The VCU on the bus reports the contactors — precharge, then closed once V_DC is up — and
the plant's contactors follow that field (A.4.2). Interactive: Traction Tool ▸ Tools ▸ Simulator on virtual CAN….
Test (in `tool/qt`): `ctest --test-dir build/dev -R tst_can_bridge --output-on-failure`; details and limits in
`tool/qt/README.md` (CAN adapters).

## Findings in the firmware (surfaced by the simulator)

These come from running the unmodified firmware against the plant. They are reported for the firmware owners; the
bridge does not work around any of them. **Round 23: findings 1–7 are fixed in the firmware** (`firmware/README.md`
"Round 23 — Fixes", `docs/firmware-contract.md` §10k: 1 a windowed rate with a deadband, 2 field weakening from the
measured link against the back-EMF, 3 the ASC-entry transient classified as information (the low sides proven to
hold), 4 a speed bound instead of forgetting the speed, 5 every declared DTC set where its condition is detected,
6 a torque-command slew, 7 the demodulator's own reference instant); the text below is what was found.

1. **FW-13 temperature rate check is below one ADC step** (`src/sense/temp.c`, `update_one`). Consecutive 1 ms
   samples are compared against `cal_temp_rate_c_s` (20 °C/s; its maximum 100 °C/s). One ADC LSB of a module NTC is
   ≈ 0.04 °C at 50 °C (≈ 38 °C/s over 1 ms) and ≈ 0.13 °C at 100 °C, so any one-code change invalidates the channel
   for at least one sample and sets DTC_TEMP_MODULE (which is never passed); above ≈ 70 °C the three consecutive
   rejections latch TEMP_RATE for the key cycle. With all three module NTCs latched, derating falls to the continuous
   rating and the 90 → 115 °C derating curve is never used. Reproduced with a noise-free, slowly rising temperature
   (`scenarios/thermal-derate.jsonl`); real ADC noise makes it worse.
2. **Uncontrolled rectification below n_x at zero torque** (`src/app/app.c`, `torque_path`: `fw_needed` uses n_x,
   which is defined at the 880 V OV trip, not the measured V_DC). With the screening motor on a 750 V link the
   back-EMF exceeds V_DC from ≈ 6 900 rpm, but the bridge stays idle at zero torque until 8 086 rpm: the diodes brake
   the shaft by ≈ 50–65 N·m and push ≈ 40 kW into the pack without any torque request.
3. **Every LS-ASC entry at speed trips FW-05.** The short-circuit transient of the screening motor peaks at
   ≈ 2 × ψ/L (≈ 730–900 A) against the 601 A FW-05 compare, so each ASC entry (FW-06 over-voltage, battery loss at
   speed, DESAT_HS above n_x after the reset, unknown speed) also latches DTC_OVERCURRENT. The contract's commissioning
   check (R8X-13) compares the ASC entry peak with DESAT only.
4. **Unknown speed turns a low-speed resolver fault into ASC.** At 2 000 rpm a resolver fault gives SPO while the
   speed is held; `cal_speed_hold_ms` (200 ms) later the speed is unknown, the n ≥ n_x column applies and PWM-ASC is
   entered at 2 000 rpm (−32 N·m braking, plus finding 3). This is the contract's stated rule (section 6, "Unknown
   speed"); it is listed because its effect on a bench is large.
5. **Six DTCs are declared but never set**: DTC_SENSOR_SELFTEST, DTC_ISNS_OFFSET and DTC_DESAT_PENDING_BOOT (a sensor
   self-test timeout and a DESAT pending at boot reach FAULT with no DTC at all), DTC_TEMP_BOARD, DTC_TEMP_MOTOR,
   DTC_OVERTEMP (`protocol/dtcs.json` `never_set`, computed from the sources).
6. **No slew on fresh torque commands.** An unslewed +200 → −150 N·m step at 10 000 rpm (deep field weakening) trips
   FW-05: the current loop runs out of voltage during the reversal. The firmware ramps only stale commands; the bench
   VCU model therefore slews at 3 000 N·m/s by default (`slew_nm_s` 0 reproduces it).
7. **Observation on the host resolver model**: the firmware's angle leads the true rotor angle by ≈ 7.7 µs
   (+1.85° el at 10 000 rpm, `tel.plant.theta_err_deg`), about 6 % torque error at 10 000 rpm; `cal_rslv_latency_us`
   (range 0–200 µs, positive = lag) cannot compensate a lead. This concerns the host SDADC model's timing and should
   be checked against the T-37 measurement on the target.
8. **Round 23 — a power-up while the rotor turns** (host card model, not the firmware): `sim_chain_ls_on()` applies
   the ASC latch that FS1B presets at power-up without the low-side gate supply (the flybacks are off until section 9
   step 6), so the plant shorts the winding at INIT: at 1 500 rpm ≈ 700 A, DTC_OVERCURRENT, then
   DTC_SENSOR_SELFTEST and FAULT. On the hardware no switch conducts without its gate supply. Open (reported for the
   card-model owner); the self-test stops the dyno before its power cycle.
9. **Round 23 — the FW-15 DESAT block at a power-up named nothing** (fixed): a DESAT recorded in the previous key
   cycle blocks arming at the next power-up (FAULT), but the DTC store is RAM and nothing named the cause; the
   recorded bank's DESAT DTC is now raised again at the power-up (check step 12).

## Offline and licence

No network access is needed to build or run anything here: the bridge is plain C with the C library only, and the
exports use Node's standard library. The bridge opens no sockets; its only I/O is stdin/stdout.

All code in this folder is original and belongs to this project (no third-party code; nothing from VESC Tool, which is
GPL-3.0 and was used only as a feature reference for the application).
