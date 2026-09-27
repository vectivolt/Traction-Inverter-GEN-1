# Our firmware against the VESC firmware and VESC Tool — feature and control matrix (round 23, 2026-09-26)

Prepared from the archived VESC sources (`docs/reference/vesc/`: bldc master 4fd8279 = FW 7.01 dev, bldc 6.00, vesc_tool master dc53c65 = VESC Tool 7.01) and from our `firmware/` at rev A.22 (TI_FW_ID 0x0A0F0014, after the round-23 torque rework FW-37) by a research agent, then read by the manager. It is deliberately two-sided: where VESC's breadth is real it says so; where ours is ahead it cites the evidence. The ranked gap list at the end drove the round-23 gap closure (FW-38 … FW-44, `firmware/README.md`). Note: the `tool/` mentioned in the OURS inventory is now the Qt application (`tool/qt/`) with the C simulator bridge (`tool/bridge/`) — see `docs/tool-stack-decision.md`.

# VESC vs. traction-inverter firmware: feature and control comparison

## Sources and method

- **VESC firmware "7.01-dev"**: `docs/reference/vesc/src/bldc-master-4fd8279/` (GitHub `vedderb/bldc`,
  `master` @ `4fd8279`, archived 2026-09-17; `conf_general.h` `FW_VERSION_MAJOR/MINOR` = 7/01).
- **VESC firmware 6.00 (last tagged release)**: `docs/reference/vesc/src/bldc-6.00/` (tag `6.00`) — used
  only where the comparison needed to know what changed between releases.
- **VESC Tool "7.01"**: `docs/reference/vesc/src/vesc_tool-master-dc53c65/` (GitHub `vedderb/vesc_tool`,
  `master` @ `dc53c65`, archived 2026-09-04; `vesc_tool.pro` `VT_VERSION`).
  Provenance for all three: `docs/reference/vesc/README.md`. All three are GPL-3.0 (VESC Tool's licence
  text sits beside the archives as `docs/reference/vesc/LICENSE-vesc_tool.txt`); nothing from them is
  linked into or copied into either OURS or `tool/` (Traction Tool) — they are read-only reference copies,
  and this report paraphrases/cites rather than reproduces their source.
- **OURS**: `firmware/` (C11, S32K396 + FS26, 220 kW/850 V traction inverter, 4 SKUs), read against
  `firmware/README.md`, `docs/firmware-contract.md` (rev A.18 at the start of this session; FW-01..FW-37 by
  the end — see the live-edit note below), `firmware/docs/traceability.md`, `firmware/docs/target-bringup.md`,
  `firmware/docs/timing.md`, and the full `firmware/src/{app,control,safety,hal,comms,discharge,nvm,sense,
  platform,util}` and `firmware/tests` trees.
- Every claim below was verified by reading the cited file (not from general knowledge of either project).
  Four parallel research passes inventoried VESC (motor control/sensors; limits/protections/sampling/
  terminal; app layer/CAN/comms/ancillary; VESC Tool pages); the author did the rest directly, including all
  of the OURS inventory, the matrix, and the synthesis below.

### A live repository, not a frozen snapshot
While this research was in progress, `firmware/src/control/torque.{c,h}`, `app.{c,h}`, `comms/can_cmd.{c,h}`,
`comms/dtc.h`, three test files, `docs/firmware-contract.md`, `firmware/README.md` and
`firmware/docs/target-bringup.md` all changed on disk — a real, live "round 23" rework of the torque -> current
path landing during this session (confirmed against `git status`/`git diff` at each step, and cross-checked
against the newly-appeared `docs/review-A22-disposition.md`, rev A.22). This report describes the **final,
settled state** re-read at the end of the session: `TI_FW_ID` 0x0A0F0014, contract §10e, **FW-37**. The
round-23 defect it fixed is documented first-hand in three independent, mutually-corroborating places —
`torque.c`'s own comments, `docs/review-A22-disposition.md` (finding F217/F218), and `docs/firmware-contract.md`
§10e — all agreeing on the same reproduction case (a salient motor, 400 V / 2000 rad/s-el / 250 A rms: a
requested ±100 N*m came out +195.8 / -205.1 N*m). This live edit is itself first-hand evidence for the task's
framing that the torque path is "being reworked this week," and it is discussed on its own merits in the
matrix below (OURS AHEAD on the resulting joint-solve-plus-postcondition design) rather than treated as noise.
Also newly appeared during this session, and material to the "host tool" question: an untracked, uncommitted
`tool/` directory (`git status`: `?? tool/`) — `package.json` names it `"traction-tool"`, *"bench/service GUI
for the 220 kW / 800 V traction inverter (simulator, replay, CAN-FD codec)"* (preact + uplot), and
`docs/reference/vesc/README.md` states outright that the VESC/VESC-Tool archives were kept *"as design
reference for `tool/` (Traction Tool)"*. Today it contains only a generic newline-delimited-JSON stdio helper
(`tool/bridge/json.{c,h}`, 336 lines) — no server, client, CAN codec, or simulator hookup yet. This report's
tooling-related gaps are best read as "what `tool/` and OURS' comms surface will need to grow into," not as
an unrecognised blind spot.

## OURS inventory (firmware/; contract started this session at rev A.18 / TI_FW_ID 0x0A0F0013 and, through
the live "round 23" rework documented in "Sources and method" above and in the caveat below, ended it at
FW-37 / TI_FW_ID 0x0A0F0014 — the descriptions below already reflect the final, settled state)

### Control modes
- Torque control only, commanded over CAN (VCU_CMD 0x101, `firmware/src/comms/can_cmd.h`). No speed-loop,
  position-hold/creep, duty-cycle, or "handbrake" control mode exists in the inverter itself.
- Torque -> current (this module was rewritten *during* this research session — see below): MTPA and field
  weakening are solved **jointly** on the motor's torque hyperbola (iq = t / (kt * psi_e(id))) against the
  voltage ellipse, current circle and demagnetization limit, by bisection on a proven-convex objective (not
  a sampled/iterative guess): the least-current point is tried first (true MTPA), and only if its voltage
  is infeasible does the search move along the hyperbola toward the least-voltage point. If no point of the
  full-torque hyperbola fits, the torque itself is reduced (bisection on a scale factor) to the largest
  value whose hyperbola still has a feasible point, rather than silently clamping id/iq independently.
  Every returned (id, iq) is checked against a formal **postcondition** before it is ever returned — same
  sign as the request, magnitude never exceeding the request (within 0.1%+1mN*m), inside the current circle,
  inside the voltage ellipse (except the explicit-infeasible case), inside the demagnetization limit — and a
  postcondition failure is its own outcome (`TQ_POSTCOND`) that zeroes the output rather than ever handing
  FOC a vector that doesn't provably satisfy its own limits.
  `firmware/src/control/torque.c` (`torque_to_current`, `hyp_point`, `hyp_argmin`, `postcondition`),
  `firmware/src/control/torque.h`. Contract: FW-03, FW-04, FW-25 (`docs/firmware-contract.md` §3, §10a);
  the rewrite itself (labelled "round 23, FW-37" in the source comments) is not yet reflected in
  `docs/firmware-contract.md` or `firmware/docs/traceability.md` as of this reading — see live-caveat note.
  The source comment documents *why* it changed: "Until round 22 the MTPA pair was moved by field weakening
  (id more negative) and by the current circle with iq kept: with Lq > Ld the extra -id ADDS reluctance
  torque at the same iq, and the witness looked at the voltage only — twice the requested torque on a
  salient motor" — i.e. the previous version (shown further below, as it read for most of this research
  session) could command roughly double the requested torque on a salient (Lq > Ld) motor under field
  weakening. This is exactly the kind of defect a formal postcondition check is designed to catch.
- Current loop: amplitude-invariant Clarke/Park, PI per axis with conditional-integration anti-windup and
  speed-voltage decoupling (feedforward), voltage-circle limit (d-priority), inverse Park advanced by the
  sample-to-actuation delay, min-max SVPWM, linear current-sign dead-time compensation, and a finite/range
  guard on every input/output (`firmware/src/control/foc.c`: `foc_step`, `pi_axes`, `foc_svpwm`, `foc_dt_comp`).
  Runs at 2x f_sw (double update): 20/16 kHz (SiC) or 10 kHz (IGBT) (`docs/firmware-contract.md` §2).
- Gain scheduling: one PI gain set per SKU x f_sw, computed by pole cancellation (Kp=L*wc, Ki=R*wc) against
  a phase-margin ceiling derived from an S6 stability screen; a request above the ceiling or at an unlisted
  f_sw is refused outright (no gain set -> no torque). `firmware/src/control/gains.c/.h`.
- DC-link trim: a regen-only PI limiter active solely in normal RUN/DERATE with the battery path proven;
  never adds motoring torque, resets outside RUN. `firmware/src/control/dclink.c/.h` (FW-08, round 17).
- No sensorless observer, no HFI, no open-loop/startup ramp mode: the motor is always resolver-fed: this is
  a design given (resolver-equipped automotive traction motor), not an omission — see matrix.
- No cogging-torque or torque-ripple compensation found anywhere in the tree (`grep -ri cogging|ripple`
  across firmware/ and docs/ returns nothing but capacitor "ripple current" in design-basis.md).
- Motor model (`firmware/src/control/motor.h`: `motor_t`) is constant Ld, Lq, Rs, psi_f — no
  saturation-dependent Ld(i)/Lq(i) table is consulted by the real-time controller (only by the offline §6
  rule-(a) energy screen in `safety/safe_state.c`, which does allow L_d(i), L_q(i) conceptually per the
  contract prose even though the shipped `motor_t` carries fixed values).

### Sensing
- Rotor position/speed: resolver only, demodulated **in software** (no dedicated R/D-converter IC): SWG1
  sine-wave generator excites at 10 kHz, three SDADC channels (excitation monitor, SIN, COS) are correlated
  against the carrier, ratiometric amplitude/phase correction, type-II tracking observer, multi-plane
  validity checks (amplitude window, two-plane excitation window, tracking error, acceleration plausibility,
  angle-rate vs. current-model cross-check). `firmware/src/control/resolver.c/.h`, `firmware/src/hal/sdadc.h`,
  `firmware/src/hal/sdadc_ring.c`, `firmware/src/platform/s32k396/s32k396_resolver.c`. Extremely detailed
  timing/latency contract (FW-10, FW-28, FW-29, FW-30, FW-35, FW-36 in `docs/firmware-contract.md`).
- Phase currents: 3x LEM HC5FW Hall-effect current transducers, ADC-sampled twice per PWM period
  (all-or-nothing triplet contract, FW-27); per-channel validity window, sum-of-currents (KCL) plausibility,
  latent-stuck-channel activity detector, hardware analog-watchdog overcurrent trip independent of the ISR.
  `firmware/src/sense/current.c/.h` (FW-05).
- DC-link voltage: two independently isolated channels (AMC1311B), cross-checked against each other and
  (with contactors closed) against the reported pack voltage; explicit UNKNOWN vs SAFE HV-state distinction.
  `firmware/src/sense/vdc.c/.h` (FW-07, FW-18).
- Temperatures: module NTCs (both power modules), board NTCs, 1-2 motor temperature sensors (PT1000 or NTC,
  selectable), each with open/short/rate-of-change plausibility. `firmware/src/sense/temp.c/.h` (FW-13).
- HVIL loop (software-judged signature ladder, no hardware comparator), LV supply supervision (KL30 via the
  FS26's own AMUX, distinguishing a load-dump/jump-start transient from a sustained fault),
  ignition (KL15) sense, SKU-identity resistor sense. `firmware/src/sense/{hvil,vsup,ign,hwid}.c/.h`
  (FW-09, FW-33, FW-01/02).
- No Hall-effect **position** sensors, no incremental/absolute SPI/sin-cos encoders, no PWM/servo position
  input anywhere in the tree — resolver is the only rotor-position sensing modality supported.

### Limits, derating, protections
- Torque/power envelope follows V_DC (`torque_p_max_w`), current limits per SKU, a 30 s peak energy budget
  with a 180 s (3x) recovery time constant that only refills after full recovery. `torque.c` (FW-03, FW-04).
- Thermal derating from module NTC and coolant temperature (hysteresis-based ramp); invalid NTC derates to
  continuous rating (never up); unknown coolant blocks peak entirely.
- BMS charge/discharge power limits relayed by the VCU over CAN, with their own staleness timeout (zero
  regen only on BMS timeout; motoring keeps the FW-03 envelope) (FW-11).
- Fast hardware protections (independent of the CPU): phase overcurrent at 1.25 x sqrt(2) x I_pk,rms via ADC
  analog watchdog -> eFlexPWM fault input (<=2 PWM periods); DC-link overvoltage via both V_DC channels'
  hardware compare -> ASC request (<=15.6 us to the request, <=7.56 us ASC entry, budget derived from a
  regen-into-lost-battery energy calculation); DESAT per switch with retry-limited recovery (one
  VCU-authorised retry per key cycle, second DESAT latches). `docs/firmware-contract.md` §1, §4, §7.
- A full safe-state decision matrix (10 rows x 2 speed columns, `firmware/src/safety/safe_state.c/.h`)
  choosing SPO / LS-ASC / ramp-to-zero / zero-current per fault type and speed, gated by a formal
  winding-energy-into-the-DC-link screening equation (rule a) or a proven vehicle-integration release
  (rule b) — never an assumption.
- Dead-time compensation is present (`foc_dt_comp`), a linear current-sign-based scheme.

### Faults / DTCs
- `firmware/src/comms/dtc.h`: ~65 distinct DTC IDs (ISO 14229-1 style: status byte with test-failed /
  confirmed / pending / test-not-completed bits, occurrence counter, first/last timestamp) covering SBC,
  identity, calibration, DESAT, overcurrent/overvoltage, every sense-channel failure mode, resolver
  amplitude/excitation/tracking/accel/rate, temperature, HVIL, CAN/BMS timeout and E2E, QDIS/discharge,
  precharge, self-test, gate power, arming evidence, torque infeasibility, LV supply, resolver re-acquisition.
- Fault-event history: a 16-slot NVM ring buffer, each entry timestamped (`t_ms`) with key cycle, fault row,
  action taken, and the electrical context at the moment of the fault (speed, id, iq, vdc).
  `firmware/src/nvm/nvlog.h`/`.c` (`nv_fault_t`, `nv_read_fault`). **This function has zero callers anywhere
  in the firmware or tests except its own unit test** (`grep -rn nv_read_fault firmware/` only matches
  `nvlog.c`, `nvlog.h`, and `tests/test_nvlog.c`) — the data is captured on-target but nothing (no CAN
  service, no UDS service) exposes it to a bench/service tool today.
- DESAT record is A/B redundant in retained RAM + queued NVM write (never blocking on a safety path).

### Comms (vehicle interface)
- CAN-FD, two buses: vehicle (`HAL_CAN_VEHICLE`) and diagnostic (`HAL_CAN_DIAG`). `firmware/src/hal/can.h`.
- Vehicle protocol (this repo's own definition until an OEM DBC binds it): VCU_CMD 0x101 (torque, gear,
  enable, fault-reset, contactor state, DESAT-retry authorization, discharge/shutdown requests, coolant
  temp), VCU_BMS 0x102 (pack voltage, charge/discharge power limits), INV_STATUS 0x201 (state, bridge mode,
  HV state, torque, speed, V_DC, module temp, DTC count/first-DTC, "keep HV connected", "no safe state
  proven", "service required", "open the contactors", "speed limit requested", missing arming-evidence bits).
  Every frame is E2E-protected: CRC-8 (SAE J1850) + 4-bit alive counter, staleness and jump-detection.
  `firmware/src/comms/can_cmd.h/.c`.
- Diagnostic bus: a minimal UDS server (ISO 14229-1 over single-frame ISO-TP) implementing exactly two
  services: 0x27 SecurityAccess (seed/key, build-time key-hook, fails closed with no product key configured)
  and 0x31 RoutineControl (exactly one routine: 0xF010, clear the stuck-on-QDIS service lock).
  `firmware/src/comms/uds.c/.h`. **No ReadDataByIdentifier (0x22), no ReadDTCInformation (0x19), no
  live-data/telemetry-streaming service, no routine for anything else (no waveform capture, no
  self-test-on-demand, no calibration write) is implemented.** Any other SID gets NRC 0x11.
- No multi-node/multi-inverter addressing scheme (a single traction inverter per vehicle is assumed; no
  analogue to VESC's CAN "controller_id" fleet).

### Calibration / EOL / arming evidence
- FW-20 calibration record (`firmware/src/nvm/calib.h/.c`): versioned (layout 2), CRC-32, range-checked,
  bound to hardware serial + SKU + motor ID; holds current-sensor offset/gain/sign per phase, V_DC
  gain/offset per channel, resolver gains/offsets/phase-trim/electrical-zero/pole-pairs and the excitation
  monitor gain, motor L_d/L_q/R_s/psi/n_max/`rule_b_released`, motor-temp-sensor type, an optional MTPA LUT
  and the selected f_sw. Any failure (missing/wrong version/CRC/range/SKU/serial/motor-ID) -> no torque.
- No on-target self-commissioning/parameter-identification routine exists anywhere in the firmware: R_s,
  L_d/L_q, flux linkage, resolver electrical-zero and phase-trim are all **calibration-record inputs**
  produced by an EOL/HIL rig that is explicitly and repeatedly stated to be **outside this repository**
  (`firmware/src/nvm/calib.h`: "TODO(EOL): the EOL station ... is not in this repository"; same pattern in
  `firmware/src/safety/arm_evidence.h` for the FAULT_ROUTE_VALIDATED/OVP_ROUTE_VALIDATED records, and in
  `firmware/docs/target-bringup.md` T-05/T-07/T-34/T-35). Grepping the whole tree for
  detect/wizard/auto-tune/self-commission/identif* (`firmware/src`) turns up nothing resembling an
  on-target measurement routine — only the word "identity" (SKU identity, FW-01/02) and "detect()"
  (the app.c fault-detection dispatcher, unrelated to motor-parameter detection).
- Arming evidence (`firmware/src/safety/arm_evidence.h`, FW-24): five fail-closed items (route bound, config
  read-back matches, protection-lock read-back set, and two EOL/HIL-measured, CRC-sealed, image+SKU+UID-bound
  NVM records: the fault-route injection and the <=15.6 us OVP-chain timing). Nothing is ever assumed true.
- Firmware identity/update: `TI_FW_ID` gates the two EOL/HIL records; the actual bootloader/signed-image/
  rollback mechanism (FW-21) is explicitly **a separate deliverable, not in this repository**
  (`docs/firmware-contract.md` §10, "FW-21 is the bootloader + HSE deliverable, not the application image's").

### Host / bench / service tooling available today
- None beyond: (a) the two-service UDS stub above, (b) the CAN status broadcast (INV_STATUS), and (c) a
  host-build test harness (`firmware/tests/`, `firmware/src/platform/host/sim.h`/`sim_chain.c`/`sim_fs26.c`/
  `sim_hal.c`) that is CI/simulation infrastructure, not a field/bench tool. It is a genuinely deep,
  nanosecond-clock-controlled simulation of the whole card (the DRV_EN AND-chain with real RC delays, the
  fault latch + one-shot, the ASC latch, the NSI6611 FLT mute/release rule, "stuck permissive" fault
  injection per shutdown term for FW-16, a full FS26 SPI/watchdog/OTP model, ADC/analog-watchdog models,
  a per-channel SDADC/eDMA resolver model, CAN, and NVM with power-loss injection) and runs 290 tests /
  2705 checks (`make test`), also under ASan/UBSan and at -O2. This is a real engineering asset (it is how
  every FW-xx requirement in the traceability matrix is proven end to end pre-silicon) but it is not
  reachable from outside a developer's build tree: there is no terminal, no live-telemetry viewer, no
  waveform/scope capture, no PC/mobile GUI, and no scripting layer of any kind for this firmware in the
  field or on a bench.
- Firmware self-description: `firmware/README.md`, `docs/firmware-contract.md` (887 lines, FW-01..FW-36 at
  the time of writing, rev A.18), `firmware/docs/traceability.md` (577 lines, requirement -> code -> test
  matrix), `firmware/docs/target-bringup.md` (63 lines, T-01..T-42 silicon/RM/EOL checklist),
  `firmware/docs/timing.md` (WCET/latency budgets).
- **A bench tool has just been started, and is explicitly modelled on VESC Tool.** A `tool/` directory
  exists at the repo root, completely new and **not yet committed to git** (`git status` shows `?? tool/`;
  `git log -- tool/` is empty) — this appears to postdate even the round-23 rework above.
  `tool/package.json` names it `"traction-tool"`, described as *"Traction Tool: bench/service GUI for the
  220 kW / 800 V traction inverter (simulator, replay, CAN-FD codec)"*, built on `preact` (UI) and `uplot`
  (time-series charting — the same job VESC Tool's real-time/sampled-data plots do). Its `scripts` already
  name the intended shape: a `bridge` (native, built by `make -C bridge`), a `dev`/`start` server
  (`src/server/main.ts`), and a client bundle (`src/client/index.html`) — but **`src/` does not exist yet**:
  the only code present is `tool/bridge/json.{c,h}` (294 + 42 lines), a generic newline-delimited-JSON
  stdio protocol helper (`jin_parse`/`jo_*`) with a documentation example of one possible future command,
  `{"cmd":"torque","nm":120}` — no CAN-FD codec, no simulator hookup, no replay, no server, and no UI code
  exist yet. The VESC reference archive's own README (`docs/reference/vesc/README.md`) confirms the intent
  directly: it says the VESC/VESC-Tool sources were archived *"for the feature and control comparison of
  round 23 ... and as design reference for `tool/` (Traction Tool)"` — i.e. this exact comparison task, and
  VESC Tool specifically, is the acknowledged design reference for that new tool. **This materially changes
  how the "no host tool" gap should be read: it is not an unrecognised gap, it is a gap the team has just
  started closing**, and this report's tooling-related GAP entries below are best read as "what `tool/`
  and the firmware's own comms surface will need to grow into," not as a surprise finding.
- Also newly appeared during this session: `docs/review-A22-disposition.md` (round 23, rev A.22, TI_FW_ID
  now **0x0A0F0014**) is the disposition record for exactly the torque-solver defect this report already
  found by reading `torque.c`'s own comments — it independently corroborates that finding with a named
  defect ID and a reproduction case: **F217**, *"the torque solver could return a current vector
  representing about twice the requested torque in field weakening"* (its worked example: 400 V / 2000
  rad/s / 250 A, +100 N*m requested, +195.8 N*m delivered), plus **F218**, the old no-LUT MTPA fallback (an
  8-step fixed-point iteration with no convergence check) landing on 267.4 A against a 246.3 A optimum at
  200 N*m. Both are fixed by the round-23 `torque.c` rewrite described above. `docs/firmware-contract.md`,
  `firmware/README.md` and `firmware/docs/traceability.md` still read `TI_FW_ID` 0x0A0F0013 as of this
  session — i.e. the contract/traceability docs are one image revision behind the source and the review
  disposition, consistent with the "documentation hasn't caught up with a live rework" pattern noted above.

### Live caveat observed during this research
`firmware/src/control/torque.h` and `.c` changed on disk twice while this research was in progress (this is
a live repository, not a frozen snapshot): first the header alone was rewritten to describe a "round 23,
FW-37" MTPA/field-weakening rework while the .c file still held the prior (round-17-era) implementation,
and shortly after the .c file was rewritten to match. The description above is of the **final, landed**
state of both files as re-read at the end of this session. `docs/firmware-contract.md` and
`firmware/docs/traceability.md` still had no mention of "FW-37" or "round 23" as of the last check in this
session, so the contract/traceability documentation has not yet caught up with the source change — worth
the team's attention, but not something this read-only research corrects. This live edit is also direct,
first-hand evidence for the task's framing that the torque-to-current path is "being reworked this week":
it was, verifiably, while this report was being written, and it fixed a real correctness defect (the prior
version could produce roughly 2x the requested torque on a salient motor in field weakening, per the
developer's own commit-comment admission in the source).

---

## VESC inventory — part 1 of 3: app layer, CAN, IMU, BMS, LispBM, config storage, hardware breadth
(source: `docs/reference/vesc/src/bldc-master-4fd8279/`, "7.01-dev"; comparisons to `bldc-6.00` noted)

### Application layer (hobby/light-EV input methods)
`applications/app.c` dispatches one active app (`app_use` enum, `datatypes.h:598-609`): PPM, ADC, UART,
PPM+UART, ADC+UART, Nunchuk, NRF, Custom, PAS, ADC+PAS.
- `applications/app_ppm.c` — RC PPM receiver decode; beyond current/duty it supports **PID speed and PID
  position control (180 deg/360 deg)** (`PPM_CTRL_TYPE_*`, `datatypes.h:629-640`), plus CAN-forwarding of
  the decoded throttle to other VESCs.
- `applications/app_adc.c` — analog throttle/twist-grip with reverse-via-button/center-detent/brake-ADC
  variants (`ADC_CTRL_TYPE_*`) — the classic e-scooter/e-bike input.
- `applications/app_uartcomm.c` — runs the full VESC binary command protocol over UART (up to 3 ports).
- `applications/app_nunchuk.c` — Wii Nunchuk joystick over I2C.
- `applications/app_pas.c` — e-bike pedal-assist (cadence or torque sensing).
- `applications/app_custom.c` + `app_custom_template.c` — user-supplied C control app hook (build-time
  file selection, up to 5 slots), with an example that registers a PWM callback and a VESC-Tool terminal
  command.
- Plus niche apps: `app_skypuff.c` (winch), `app_dpv.c` (dive propulsion vehicle), `applications/er/`,
  `applications/finn/` (OEM-specific apps with their own QML config UI).
- **`app_balance.c` (self-balancing board) does NOT exist in the 7.01-dev tree** — `CHANGELOG.md:112`:
  "Removed built-in balance app. The balance-package can be used instead" (moved to an external LispBM
  package). It DOES exist in `bldc-6.00/applications/app_balance.c` (uses `imu/ahrs.h`), so this is a
  recent architectural move (core firmware -> Lisp package) for that one app, not an omission.
- Default shipped app: `APP_UART` (`applications/appconf_default.h:82`).

### CAN bus (`comm/comm_can.c/.h`)
- Addressing: 8-bit `controller_id` in the low byte of a 29-bit extended CAN ID; command type in the upper
  byte(s) (e.g. `comm_can_set_current()`, `comm/comm_can.c:515-521`).
- Status broadcast set (`CAN_PACKET_ID` enum, `datatypes.h:1157-1227`, 70 entries total): `CAN_PACKET_STATUS`
  (rpm/current/duty), `_STATUS_2` (Ah/Ah-charged), `_STATUS_3` (Wh/Wh-charged), `_STATUS_4` (FET
  temp/motor temp/input current/PID position), `_STATUS_5` (V_in/tachometer), `_STATUS_6` (adc1-3/ppm). No
  single frame carries a fault code as a status broadcast — fault state travels through the generic
  request/reply protocol, not a periodic status message (unlike OURS' INV_STATUS, which packs fault/DTC
  bits into the periodic frame itself).
- Multi-VESC command set (`comm/comm_can.h:33-52`): set duty/current/current-off-delay/current-brake/
  rpm/pos/current-rel/handbrake by `controller_id`; `comm_can_ping`/PING-PONG presence detection;
  `comm_can_detect_apply_all_foc` (bus-wide FOC auto-detect); remote-configure-and-persist calls
  (current limits, FOC eRPM, battery cutoff); `comm_can_shutdown`.
- Generic packet forwarding/bridging (`CAN_PACKET_FILL_RX_BUFFER(_LONG)`/`_PROCESS_(RX|SHORT)_BUFFER`,
  `comm/comm_can.c:1642-1809`): fragments the *same* command protocol used over USB/UART to a target
  `controller_id` and routes the reply back — this is how VESC Tool configures/flashes an entire CAN bus
  of controllers through one USB connection to one of them.
- Also on the same packet ID space: an "IO Board" I/O-expansion protocol, a power-switch/contactor
  protocol, GNSS telemetry relay, bus-wide baud-rate change, and the BMS bridging sub-protocol (next).

### IMU (`imu/`)
Multiple selectable sensor drivers (`IMU_TYPE` enum, `datatypes.h:836-843`): MPU9150, ICM20948, BMI160
(Bosch's own driver + wrapper), LSM6DS3, LSM6DSV32X, behind a transport abstraction (bit-banged I2C/SPI,
hardware SPI) with a dedicated sampling thread (DRDY-interrupt or polling fallback). AHRS: `imu/ahrs.c`
(Madgwick/Mahony, adapted from x-io) or `imu/Fusion/` (x-io's newer Fusion library), switchable at runtime
(`AHRS_MODE`, `datatypes.h:845-849`). Feeds roll/pitch/yaw/quaternion to the (now external) balance package
and to LispBM (`get-imu-*` extensions).

### BMS interface (`bms.c`/`bms.h`)
First-party "VESC BMS" support, explicitly architected to extend to others (`BMS_TYPE` enum currently only
`NONE`/`VESC`, `datatypes.h:283-285`). `bms_values` (`datatypes.h:311-343`): pack V, charge V, input
current, session+lifetime Ah/Wh, **per-cell voltage and balancing state for up to 50 cells**, per-channel
temps, humidity/pressure, SOC, SOH, charging/balancing/charge-allowed flags. Wired directly into the CAN RX
path (`comm/comm_can.c:1345,1359` call `bms_process_can_frame()` on every frame) via a dozen+ dedicated
`CAN_PACKET_BMS_*` IDs, and exposed to LispBM (`get-bms-val`, `bms-force-balance`, `bms-zero-offset`).
`bms_update_limits()` folds BMS charge/discharge limits directly into the controller's own input-current
limits — architecturally similar in intent to OURS' VCU_BMS CAN message, but BMS-native here vs.
VCU-mediated in OURS.

### LispBM scripting (`lispBM/`)
An embedded, sandboxed Lisp interpreter (`lispBM/README.md`: "runs lisp-programs in a sandboxed
environment... a runaway script can't crash the firmware... runs standalone... stored in flash and
auto-started on boot"). `lispBM/lispif_vesc_extensions.c` (218 KB) registers **244 built-in extensions**
exposing motor telemetry, IMU, CAN (including raw frame send/scan), BMS, app overrides, configuration
(including `conf-detect-foc`, `conf-detect-hall` — motor detection *from a script*), and raw EEPROM
key/value storage. `lispif_c_lib.c` additionally loads separately-compiled native C libraries at runtime
(`lispBM/c_libs/`, with a stable ABI header `vesc_c_if.h` and example libraries for WS2812 LEDs, a custom
binary data channel, an SSD1306 OLED driver, etc). This is a full user-programmable control/telemetry
layer with no equivalent whatsoever in OURS (nor should it have one — see matrix).

### Configuration storage
`mc_configuration`/`app_configuration` persist in flash-emulated EEPROM (ST's reference two-page emulation,
`driver/eeprom.c`, wrapped by `conf_general.c:335-511`), each with a CRC field checked on read
(`mc_configuration.crc`; a mismatch logs `FAULT_CODE_FLASH_CORRUPTION_MC_CFG` and falls back to firmware
defaults). Separate address ranges for MC config, app config, per-hardware variables, per-custom-app
variables, and a second-motor config (dual-motor boards). LispBM scripts get their own slice of the same
mechanism via `eeprom-read/store-f/-i`.

### Hardware-target breadth (`hwconf/`)
**23 vendor/family directories, 314 distinct `hw_*.h` board headers** in the 7.01-dev tree (`trampa` alone
has 14 sub-boards; the `vesc` reference family has 10) — versus **10 vendor directories** in the 6.00
release, i.e. roughly doubled. Shared gate-driver ICs (`drv8301/8305/8316/8320s/8323s.c`) are factored out
once and reused across boards. This is the clearest structural signal of what VESC optimizes for
(maximum hardware reuse across a hobbyist/prosumer market) versus OURS (one fixed board, four SKU
parameter sets, `calculations/mcu-ballmap.json` pin-frozen).

## VESC Tool inventory (source: `docs/reference/vesc/src/vesc_tool-master-dc53c65/`, desktop Qt Widgets +
mobile QML, sharing one C++ backend: `vescinterface.cpp`, `commands.cpp`, `configparams.cpp`)

Desktop nav tree (`mainwindow.cpp`, `addPageItem` calls): Welcome & Wizards, Connection, Firmware, VESC
Packages, Motor Settings (General/BLDC/DC/FOC/GPDrive/PID/Additional Info/Experiments), App Settings
(General/PPM/ADC/UART/VESC Remote/Nrf/PAS/IMU/Config0-2), Data Analysis (Realtime Data/Sampled Data/
Experiment Plot/IMU Data/BMS Data/Log Analysis/Motor Analysis), VESC Dev Tools (Terminal/QML Scripting/
LispBM Scripting/CAN Tools/Display Tool/Debug Console/SWD Programmer/ESP Programmer). Mobile (`mobile/`,
QML, one `SwipeView`) shares the same backend with a reduced page set.

- **Real-time dashboards**: `pages/pagertdata.cpp` — 5 live `QCustomPlot` graphs (current/duty, ERPM,
  FOC dq current+voltage, temperatures, rotor-position with observer/encoder/hall overlay traces) on a
  20 ms timer, with an RT-data logging toggle; `pages/pageimu.cpp`, `pages/pagebms.cpp` (live per-cell
  voltage/temperature bars + balance/charge control). Mobile: `RtData.qml` (gauges), `RtDataSetup.qml`
  (user-configurable live view), `Vesc3DView.qml` (IMU-driven 3D model), `StatPage.qml` (session/trip
  Ah/Wh/distance/efficiency stats).
- **Sampled/logged data & analysis**: `pages/pagesampleddata.cpp` — raw ADC waveform capture (phase
  currents/voltages/virtual ground/switching frequency) on manual or **fault trigger**, CSV export/import;
  `pages/pageloganalysis.cpp` (88 KB) — a full ride-log analysis suite: GPS route on an embedded OSM map,
  timeline plot of any logged channel synced to map playback, PDF/PNG/CSV export, three log sources
  (on-device, downloaded, live session); `pages/pageexperiments.cpp` — automated open-loop sweep-and-log
  bench tool with optional external Victron BMV-700 ground-truth cross-check; `pages/pagemotorcomparison.cpp`
  — overlay/compare multiple detected motor parameter sets. Mobile: `LogBox.qml` records motor telemetry +
  phone GPS in the background, its own help text says to move the file to desktop Log Analysis.
- **Motor/app configuration wizards**: `setupwizardmotor.cpp` (`QWizard`): Intro -> Connection -> Firmware
  -> Motor Type -> Currents -> Voltages -> Sensors (**Sensorless, Hall, ABI encoder, AS5047, Resolver
  AD2S1205, Sin/Cos, BiSS, AS5x47U**) -> a sensor-specific detection page (BLDC/FOC/FOC-encoder/FOC-hall,
  each embedding a live spin-the-motor detection widget) -> Conclusion. `setupwizardapp.cpp`: Intro ->
  Connection -> Firmware -> input-type choice -> General/Nunchuk/PPM-map/PPM/ADC-map/ADC -> Conclusion.
  `startupwizard.cpp`: a disclaimer/warranty first-run wizard. `widgets/detectallfocdialog.cpp`: batch
  FOC auto-detect across a *list* of motors, runnable **fleet-wide over CAN** in one pass. A condensed
  QML alternative (`mobile/SetupWizardFoc.qml`, shared by desktop and mobile) offers usage presets
  (Generic/E-Skate/Balance/Propeller) before detection.
- **Terminal**: `pages/pageterminal.cpp` / `mobile/Terminal.qml` — free-text command box calling
  `sendTerminalCmd()`, streaming text back; mobile adds a quick menu (`faults`, `threads`, `help`,
  `Reboot`, `Shutdown`, `Restart LispBM`).
- **Firmware update**: `pages/pagefirmware.cpp` — bundled hardware-matched list, downloadable archive, or
  a custom local file; single-device upload or **upload-to-every-device-on-the-CAN-bus** in one action
  (with an explicit "only if every device shares the same hardware" warning on the mobile equivalent,
  `FwUpdate.qml`). Separate low-level tools: `pageswdprog.cpp` (bare-MCU/nRF5x SWD/JTAG flashing),
  `pageespprog.cpp` (WiFi/BLE co-processor flashing).
- **CAN device list / forwarding**: `pages/pageconnection.cpp` (`scanCANbus()`, per-ID `"VESC %1"` list,
  forwarding toggle) and a direct SocketCAN connection option; mobile's `CanScreen.qml` unifies this into
  one drawer.
- **Custom GUI / scripting**: extensive and first-class on both platforms — `res/qml/DynamicLoader.qml`
  injects arbitrary QML text live (`Qt.createQmlObject`); `pages/pagescripting.cpp` is a full QML code
  editor/IDE with 29 ready-made examples (`res/qml/Examples/`, e.g. `BrakeBench.qml`, `CanDebugger.qml`,
  `PositionControl.qml`) and can compile+flash the QML into the controller's own storage so it becomes a
  persistent on-device custom GUI; `pages/pagelisp.cpp` is the matching LispBM editor/IDE with a live
  variable-binding plot; `pages/pagecustomconfig.cpp` generically renders whatever parameter set a Lisp
  script defines on the target (up to 3 slots); `pages/pagevescpackage.cpp` bundles a script + GUI +
  metadata into an installable `.vescpkg`.
- **Mobile app**: shares most pages (scaled down: e.g. one `ConfigPageMotor.qml` instead of desktop's
  per-topic pages) and adds mobile-only features — background ride+GPS logging, savable "Profiles", a
  direct manual drive-test panel (`Controls.qml`: buttons bound to set-current/duty/rpm/brake for
  bench testing from the phone), TCP/relay-hub connection. It explicitly defers Log Analysis/map view,
  Sampled Data, Experiment/Motor Analysis, CAN Tools, Debug Console and the SWD/ESP programmers to desktop.
- **Developer/debug pages**: `pages/pagecananalyzer.cpp` (raw CAN frame monitor/injector + bus bitrate
  change), `pages/pagedebugprint.cpp` (captures the app's own debug console), `display_tool/` (bitmap/icon
  editor for controllers with a small status LCD).

**Structural point for the matrix**: every one of the above is a GUI feature backed by a documented
serial/BLE/CAN command protocol (`comm/commands.c`, `datatypes.h`) that treats the controller as a rich,
introspectable peripheral. OURS' on-target comms surface (two UDS services + one periodic status frame)
could not drive most of this even if the `tool/` bench app existed today in finished form.

## VESC inventory — part 2 of 3: limits/protections, fault handling, sampling, terminal, run-time stats,
firmware update (source: `docs/reference/vesc/src/bldc-master-4fd8279/`, line numbers from that checkout)

### Limits and protections
`mc_configuration` (`datatypes.h:395-597`): motor current max/min + hard `l_abs_current_max`; battery/input
current max/min with a map-based rolloff; duty min/max with a rolloff-start fraction; DC-link `l_min_vin`/
`l_max_vin` plus separate soft battery-cutoff and regen-overvoltage rolloff bands; FET-temp and motor-temp
derate curves (start/end), with a **separate, less-aggressive curve for braking vs. driving current**
(`l_temp_accel_dec`, so braking authority survives longer under heat than motoring current does — a
nuance OURS' single derate curve, `torque.c: torque_derate_update`, does not distinguish); RPM
rolloff/min/max plus optional hard OVERSPEED/UNDERSPEED/ABS_OVERSPEED faults; battery-side watt limits
(no separate motor-side watt limit).
Enforcement: `update_override_limits()`, `motor/mc_interface.c:2245-2547`, combines every candidate ceiling
with `utils_min_abs()` each control cycle (`bms_update_limits()` further intersects with BMS-relayed
limits). **FOC hard-clamps** the commanded iq/duty to the computed limits every cycle
(`motor/mcpwm_foc.c:3329,3390-3392,3661-3672`); **BLDC instead ramps** toward the limit at a rate
proportional to the overshoot (`m_current_backoff_gain`, `motor/mcpwm.c:2075-2089`) — i.e. VESC's own two
control schemes disagree on instant-clamp vs. gradual-backoff.
DC-link over/under-voltage is **not an instant trip**: `mc_interface_mc_timer_isr()` (`mc_interface.c:
1900-1920`) integrates `|Vin-limit|` every ISR tick and only faults once the integral exceeds
`l_max_vin*0.05` (with anti-windup). The absolute-current hard fault (`mc_interface.c:1986-1993`,
`FAULT_CODE_ABS_OVER_CURRENT`) is likewise a **software check running in the motor-control ISR**, not an
independent hardware comparator.
A literal "safe start" exists (`SAFE_START_MODE`, `datatypes.h:621-625`) but lives in the **app layer**
(`app_ppm.c`/`app_adc.c`: throttle must sit at zero for a minimum time/pulse-count after a fault or at
boot), not in `mc_interface`/`mcpwm_foc`.

### Fault handling
`mc_fault_code` enum (`datatypes.h:144-179`): 33 values — OVER/UNDER_VOLTAGE, DRV, ABS_OVER_CURRENT,
OVER_TEMP_FET/MOTOR, GATE_DRIVER_OVER/UNDER_VOLTAGE, MCU_UNDER_VOLTAGE, BOOTING_FROM_WATCHDOG_RESET,
several ENCODER_*/RESOLVER_* codes, FLASH_CORRUPTION[_APP/_MC_CFG], HIGH_OFFSET_CURRENT_SENSOR_1/2/3,
UNBALANCED_CURRENTS, BRK, PHASE_FILTER, LV_OUTPUT_FAULT, OVERSPEED/UNDERSPEED/ABS_OVERSPEED.
Raised via `mc_interface_fault_stop()` (`mc_interface.c:1864`, ISR-safe), latched by a dedicated thread
(`fault_stop_thread`, `mc_interface.c:2959-3053`) which builds a rich single-event snapshot (`fault_data`:
current, filtered current, voltage, gate-driver voltage, duty, RPM, tachometer, `cycles_running`, three raw
timer-register captures, comm step, MOSFET temperature, DRV-chip fault bits) and pushes it into a 25-slot
array (`fault_vec[FAULT_VEC_LEN=25]`, `terminal.c:50,66` — a simple wraparound index, not true ring
semantics), retrievable with the `fault`/`faults` terminal command. **`fault_data` has no timestamp field
at all** (confirmed by reading the full struct, `datatypes.h:1242-1263`) and `fault_vec` is a plain RAM
array — zeroed on every reset, never written to EEPROM/flash: it does not survive a reset, let alone a
power cycle. Clearing is **automatic and silent**: `run_timer_tasks()` (`mc_interface.c:2557,2585-2603`)
resets `m_fault_now` to NONE once the fault has not re-fired for `m_fault_stop_time_ms` and the live DRV
fault pin is not asserted — there is no manual "clear fault" command and no authorization gate of any kind
on a fault clearing and modulation resuming.
Adjacent MCU-level crash diagnostics (distinct system): `CrashInfo` in `.noinit` RAM (`main.c:96`) —
boot count, raw reset-cause register, consecutive-crash streak, a brown-out-dip counter with a
seconds-since-boot timestamp (the only timestamp found anywhere in this fault infrastructure), and on an
actual firmware crash a full Cortex-M fault-frame capture — survives a warm reset (so the previous crash
can be inspected after reboot) but not a power cycle; printed via `crash_diag [clear]`.

### Data sampling / waveform capture (debugging)
A full oscilloscope-style capture mechanism, `motor/mc_interface.c` (not `mcpwm_foc.c`, which only supplies
the raw values). Modes (`debug_sampling_mode`, `datatypes.h:246-255`): OFF/NOW/START/TRIGGER_START/
**TRIGGER_FAULT**/(NOSEND variants)/SEND_LAST_SAMPLES/SEND_SINGLE_SAMPLE. Armed by
`mc_interface_sample_print_data(mode, len, decimation, raw, reply_func)` (`mc_interface.c:1506-1530`, up to
`ADC_SAMPLE_MAX_LEN=1000` samples). Captured inside the control-ISR itself
(`mc_interface_mc_timer_isr()`, ~lines 2040-2215), decimated by `m_sample_int`; `TRIGGER_FAULT` arms **the
instant `m_fault_now != FAULT_CODE_NONE`** — i.e. a fault-triggered black-box recorder. Ten parallel
1000-entry arrays capture, per sample: electrical angle, three phase currents, three phase voltages,
DC-link/zero reference, filtered total current, instantaneous switching-frequency estimate, and a packed
commutation/hall status byte. Retrieval protocol: `COMM_SAMPLE_PRINT` (`datatypes.h:984`), request parsed
in `comm/commands.c:677-692`, reply streamed **one packet per sample index** via `send_sample_block()`
(`mc_interface.c:2876-2910`). This is precisely the capability named in the task's gap candidates
("sampled-waveform capture for debugging... trigger on fault") and it is **confirmed entirely absent** in
OURS (no ring buffer, no capture mode, no retrieval protocol anywhere in `firmware/src`).

### Terminal commands
`terminal.c`: 33 hardcoded built-ins (`terminal_process_string()`, `terminal.c:167-1206`) plus a dynamic
registry (`terminal_register_command_callback()`) used at **90 call sites across ~39 files**, roughly 120
possible commands (most hardware-specific, one board compiled per image). Representative themes: status/
faults (`fault`/`faults`, `hw_status` — fw version/HW name/UUID/odometer/runtime, `uptime`, `crash_diag`,
`drv_reset_faults`); measurement/detection (`param_detect`, `measure_res`, `measure_ind`,
`measure_linkage[_foc/_openloop]`, `measure_res_ind`, `rotor_lock_openloop`, `foc_detect_apply_all[_can]`,
`hall_analyze`); FOC tuning (`foc_state`, `foc_dc_cal`, `foc_openloop[_duty]`, `foc_encoder_detect`,
`foc_sensors_detect_apply`); encoder status/error-clear; gate-driver register-level access per DRV chip
(`drvXXXX_read_reg`/`write_reg`/`set_oc_adj`/`print_faults`); a BlackMagic SWD-probe passthrough
(`bm_swdp_scan`/`bm_attach`/`bm_flash_erase`/... — lets a VESC act as a debug probe for another chip, e.g.
its own BLE module); system/misc (`mem`, `threads`, `crc`, `rebootwdt`, `connect_virtual_motor`, `events`).
**OURS has, in total, two diagnostic services** (SecurityAccess + one RoutineControl) and zero terminal-
style commands of any kind.

### Run-time statistics (energy, distance, max temperatures)
Two tiers, cleanly separated:
- **Session-only, RAM, lost every reboot**: `m_amp_seconds`/`_charged`, `m_watt_seconds`/`_charged`
  (`mc_interface.c:80-83`, integrated every ISR tick), surfaced in `mc_values` as `amp_hours`,
  `watt_hours`, `tachometer`, `position`; and `setup_stats` (`datatypes.h:1439-1452`) — a session
  accumulator of **max recorded temperature (motor and MOSFET), max speed, max power, max current**,
  reset via `mc_interface_stat_reset()`.
- **Lifetime, flash-persisted (with a caveat)**: `backup_data` (`datatypes.h:1456-1478`) — `uint64_t
  odometer` (metres) and `uint64_t runtime` (seconds), continuously accumulated in RAM
  (`run_timer_tasks()`) but **flushed to emulated EEPROM only on a clean shutdown or `COMM_REBOOT`**
  (`conf_general.c:156-186`, comment: doing it on every event risked losing config mid page-swap) — so an
  abrupt power loss loses odometer/runtime progress since the last clean shutdown. This maps directly to
  the task's "run-time statistics" gap candidate: **confirmed present (with the above caveat) in VESC,
  confirmed entirely absent in OURS** (no odometer/Ah/Wh/max-temperature/lifetime field anywhere in
  `firmware/`, verified by grep).

### Firmware update / bootloader path
**No bootloader source exists in this tree** — only the "stage new firmware" and "jump into an
already-flashed bootloader" halves. `COMM_ERASE_NEW_APP`/`COMM_WRITE_NEW_APP_DATA` (optionally
LZO-compressed) stage an image into a fixed flash region (`flash_helper.c`); `COMM_JUMP_TO_BOOTLOADER`
does a bare Cortex-M cross-image jump to a hard-coded flash address. Fleet update:
`*_ALL_CAN` command variants (`datatypes.h:1024-1047`) rebroadcast the identical payload to every VESC on
the bus and apply it locally too — one USB/UART link updates a whole CAN-bus fleet in lockstep.
**No cryptographic signing exists anywhere in this path**: the packaging script
(`package_firmware.py`) has no sign/hash/sha step, and the only integrity mechanism,
`flash_helper_verify_flash_memory()` (`flash_helper.c:317-339`), is a CRC32 self-check of the
*already-running* app (a corruption detector) — not an authenticity check on an incoming image. **Any
correctly-CRC'd image can be flashed by anyone with USB/UART/CAN access.** This is directly relevant to
this report's safety-consideration language below: it is exactly the pattern that must never be carried
into an ASIL context. OURS' own `docs/firmware-contract.md` FW-21 already specifies the opposite (signed
images, rollback protection, a no-torque update state) at the requirement level, but — by the contract's
own words — "FW-21 is the bootloader + HSE deliverable, not the application image's": the mechanism that
would implement it is explicitly out of this repository, so it cannot be inventoried from source the way
VESC's (unsigned) mechanism can.

## VESC inventory — part 3 of 3: motor control algorithms, sensors, detection wizards, calibration
(source: `motor/mcpwm_foc.c` 5434 lines, `motor/foc_math.c` 799 lines, `motor/mcpwm.c` 3021 lines,
`encoder/*.c`, `conf_general.c`, `datatypes.h`, all under `bldc-master-4fd8279/`)

### Control modes (`mc_control_mode`, `datatypes.h:181-193`)
`CONTROL_MODE_DUTY, SPEED, CURRENT, CURRENT_BRAKE, POS, HANDBRAKE, OPENLOOP, OPENLOOP_PHASE,
OPENLOOP_DUTY, OPENLOOP_DUTY_PHASE, NONE`. Duty control is cascaded PI-on-duty-error feeding a
current-loop `iq` target (`timer_update()`, not a separate loop). Speed control
(`foc_run_pid_control_speed`, `foc_math.c:492-577`) has a selectable feedback source
(`S_PID_SPEED_SRC`: PLL vs. two faster/noisier estimators). Position control
(`foc_run_pid_control_pos`, `foc_math.c:385-491`) is a full PID with separate D-on-error/D-on-PV terms
and a gain-reduction zone near the setpoint. Handbrake simply forces electrical phase to zero with a
fixed current target (magnetic rotor lock at an arbitrary angle) — not a feedback loop. Openloop modes
integrate a commanded angle/speed with no feedback, used only by detection routines.

### Sensorless startup and observer
A 3-stage forced-commutation sequence (lock -> ramp -> constant speed, `timer_update()`) bridges to
closed loop whenever the observer's PLL speed stays below a threshold past a hysteresis timer. The
observer itself (`foc_observer_update()`, `foc_math.c:26-224`) is selectable among **7 variants**
(`mc_foc_observer_type`): Ortega-original (nonlinear adaptive flux observer, cites Lee/Hong/Nam/Ortega/
Praly/Astolfi IEEE TPEL 2010), Ortega+lambda-comp (also adapts estimated flux magnitude), MXLemming and
MXLemming+lambda-comp (a simpler direct back-EMF integrator with hard vector-magnitude clamping,
attributed to the MESC FOC project), and the MXV family (another back-EMF integrator, with either
error-driven or low-pass-filtered flux-magnitude adaptation) — all feeding a PLL
(`foc_pll_run()`) for smoothed angle/speed. A separate saturation-compensation stage adjusts the
observer's effective L/lambda as current rises (`SAT_COMP_MODE`: factor-based or lambda-based).

### HFI (High-Frequency Injection)
Present, 5 selectable variants (`FOC_SENSOR_MODE_HFI/HFI_START/HFI_V2/V3/V4/V5`,
`mcpwm_foc.c` control_current() dispatch, demodulation in `hfi_update()` L4218-4474). Base HFI
six-vector-injects a rotating high-frequency voltage and FFT-demodulates the current ripple (bin-1 =
saturation-based low-current signal, bin-2 = rotor angle). 180-degree polarity ambiguity is resolved
either by a bin1/bin2 voting scheme or by dedicated D-axis current-pulse injection modes.
`HFI_START` uses HFI only near standstill to align/bootstrap the sensorless observer, then hands off —
i.e. VESC's own "HFI + sensorless fallback" combination named in the task. Torque is explicitly withheld
until ambiguity resolution completes. The same injection/demodulation machinery is reused offline as the
actual inductance-measurement instrument (see detection wizards below).

### Field weakening — `foc_run_fw()`, `foc_math.c:708-765`
Duty-triggered (once filtered duty exceeds a start fraction of max duty), current linearly mapped from
duty up to a cap, rate-limited, subtracted from the d-axis current budget and also reducing iq by a
configurable factor; a backoff term reduces the weakening setpoint if measured iq starts lagging its
target (an explicit anti-current-starvation guard, per the code's own comment).

### MTPA — `mcpwm_foc.c:3624-3639`
Closed-form analytic solution only (no LUT, no iterative search): `id = (psi - sqrt(psi^2 + 8*(dL*iq)^2))
/ (4*dL)`, active only for salient motors (`foc_motor_ld_lq_diff != 0`); a mode flag chooses whether the
formula uses the commanded or the (min of commanded/measured) iq.

### Current-controller saturation / anti-windup — `control_current()`, `mcpwm_foc.c:4636-4650`
D-axis has priority: vd and its own integrator are clamped to a fraction of the voltage-circle limit;
the remaining q-axis budget is a circle-limit computed from the clamped vd, and vq plus its integrator
are clamped to that — a standard clamped-integrator anti-windup, applied independently again in the
position PID, the speed PID, and the duty-downramp PI (which also explicitly re-seeds its integrator for
bumpless transfer when leaving duty mode). Separate feed-forward decoupling terms (cross-coupling +
back-EMF) are available and are a distinct mechanism from the anti-windup despite sharing code location.

### Cogging-torque / torque-ripple compensation
**Confirmed absent.** An exhaustive case-insensitive search for "cogging"/"torque ripple" across the
entire tree (both bldc-master-4fd8279 and bldc-6.00) returns zero hits.

### Dead-time compensation — `update_valpha_vbeta()`, `mcpwm_foc.c:5054-5119`
Present, but with an important nuance the source itself states: the phase-current-sign-based correction
feeds only the internal voltage *estimate* used by the sensorless observer and telemetry — "these are
not used to control the switching times" (source comment). **I.e. VESC's dead-time compensation corrects
the control-loop's voltage model, not the PWM pulse widths themselves**, whereas OURS' `foc_dt_comp`
(`firmware/src/control/foc.c`) adds the compensation directly into the duty cycle that drives the PWM
registers — a real, verifiable difference in where the correction is applied, not just a naming overlap.

### Sensors (`sensor_port_mode`, 19 values, dispatched by `encoder/encoder.c`)
Hall (bypasses the encoder dispatcher entirely — read directly in `util/utils_sys.c`, oversampled/
majority-voted, blended with the observer via `foc_correct_hall()`, `foc_math.c:597-695`), ABI incremental
(`encoder/enc_abi.c`, hard hysteresis switch to/from the observer, no interpolation), AS504x and AS5x47U
SPI (`enc_as504x.c`, `enc_as5x47u.c`), MT6816 and MT6835 SPI (`enc_mt6816.c`, `enc_mt6835.c` — MT6835 not
in the 6.00 release), TLE5012 (`enc_tle5012.c`, bit-banged or hardware SSC), sin/cos analog
(`enc_sincos.c`, amplitude-fault-checked), **resolver AD2S1205** (`enc_ad2s1205.c` — bit-banged SPI
driving the chip's SAMPLE/RDVEL pins directly; fault bits surfaced as `FAULT_CODE_RESOLVER_LOT/DOS/LOS`
in `encoder/encoder.c` `encoder_check_faults()`), Tamagawa TS5700N8501 (single- and multi-turn), BiSS-C,
MA782, AMT22, PWM/servo-signal input (the last four not present in the 6.00 release), and a custom-callback
hook. All are unified behind generic accessors (`encoder_read_deg()` etc.) that `mcpwm_foc.c` calls without
knowing which physical driver is active.
**Contrast with OURS**: VESC supports resolver via a dedicated IC (AD2S1205) that does the R/D conversion
in hardware and is treated as just one more sensor option among 12+; OURS does R/D conversion **in
software** against a resolver that is the *only* sensing modality it will ever have, with an order of
magnitude more validation machinery around that one channel (amplitude/excitation/tracking/acceleration
planes, cadence-locked timestamping, FW-10/28/29/30/35/36) than VESC's resolver support needs, because
VESC can fall back to a different sensor or to sensorless if the resolver misbehaves and OURS cannot.

### Motor detection / self-commissioning wizards (`mcpwm_foc.c` primitives + `conf_general.c` orchestration)
- **R** — `mcpwm_foc_measure_resistance()`: DC-lock method (force phase to 0, ramp iq, average ADC
  samples, R = V/I).
- **L (and Ld-Lq saliency)** — `mcpwm_foc_measure_inductance()`: reuses the **HFI injection/demodulation
  machinery** as a measurement instrument (six-vector injection, FFT bin-0/bin-2 -> Ld, Lq, saliency),
  scaled by 0.9 as a deliberate under-estimation bias for observer stability.
  `mcpwm_foc_measure_inductance_current()` searches increasing test duty until a target current is hit.
- **Combined R+L wizard** — `mcpwm_foc_measure_res_ind()`: sweeps test current, measures R, then L at
  that current, with current-loop gains temporarily dropped to near-open-loop during the test.
- **Flux linkage (psi)** — `conf_general_measure_flux_linkage_openloop()` (the modern path): forces
  id=0, ramps iq to a target duty at a controlled acceleration, derives psi from steady-state back-EMF;
  **also auto-tunes the current-loop PI gains from the measured R/L** via pole placement (kp=L*bw,
  ki=R*bw); opportunistically returns encoder offset/ratio/inversion from the same forced rotation.
- **Hall table build** — `mcpwm_foc_hall_detect()`: force-lock, sweep electrical angle 360 deg forward
  and backward three times each while sampling the hall code, circular-mean sin/cos per code
  (forward+backward cancels timing/hysteresis bias).
- **Encoder offset/ratio/inversion** — `mcpwm_foc_encoder_detect()`: find the index pulse, rotate to
  determine electrical:mechanical ratio and sense inversion, then sweep a full mechanical rotation
  forward and backward to find the average commanded-vs-observed angle offset.
- **Combined "auto-detect everything"** — `conf_general_detect_apply_all_foc()` (+ a CAN-bus fan-out
  variant that runs it on every controller on the bus): current/voltage offset cal -> R/L measurement
  (also deriving a current limit from a caller-supplied power-loss budget) -> flux-linkage measurement at
  raised switching frequency -> write every result into the working config -> sensor auto-detect.
- **Sensor auto-detect scope is narrow**: `conf_general_autodetect_apply_sensors_foc()` only ever tries
  Hall, then AS5047-SPI — it does **not** auto-probe ABI, sin/cos, resolver, TLE5012, MT6816/6835, BiSS-C,
  etc.; those require manual selection. "Auto-detect" is bounded to what can be electrically
  self-verified with no prior configuration.

### Calibration
Current-sensor offset + phase-voltage offset — `mcpwm_foc_dc_cal()`: with the motor undriven, drives one
phase at a time at 50% duty (net zero torque even if already spinning) and averages 1000 samples per
phase for the current offsets; simultaneously offsets the phase-voltage sense lines relative to their own
3-phase average (the "driven" offset, valid at the SVM operating point), optionally also capturing a
separate "undriven" offset set with PWM fully stopped for the low-modulation/observer path. Phase-filter
cross-check (`foc_phase_filter_enable`) compares the hardware phase-voltage filter reading against the
modulation-implied voltage below a speed threshold and faults (`FAULT_CODE_PHASE_FILTER`) on disagreement.
**This runs automatically as the first step of every detection wizard — i.e. VESC re-zeros its current
sensors at the start of every "commission this motor" action**, though not on every ordinary key-on/boot
of an already-commissioned motor (that would restart the motor's own field/back-EMF).

### `mc_fault_code` (`datatypes.h:144-179`, 34 values — supersedes the "33" count from the other agent's
partial read; both agents independently transcribed the same enum, confirming NONE, OVER/UNDER_VOLTAGE,
DRV, ABS_OVER_CURRENT, OVER_TEMP_FET/MOTOR, GATE_DRIVER_OVER/UNDER_VOLTAGE, MCU_UNDER_VOLTAGE,
BOOTING_FROM_WATCHDOG_RESET, ENCODER_SPI, ENCODER_SINCOS_BELOW/ABOVE_*, FLASH_CORRUPTION[_APP/_MC_CFG],
HIGH_OFFSET_CURRENT_SENSOR_1/2/3, UNBALANCED_CURRENTS, BRK, RESOLVER_LOT/DOS/LOS, ENCODER_NO_MAGNET,
ENCODER_MAGNET_TOO_STRONG, PHASE_FILTER, ENCODER_FAULT, LV_OUTPUT_FAULT, ENCODER_SLIP, OVERSPEED,
UNDERSPEED, ABS_OVERSPEED).

---

# The comparison matrix

Legend: **AHEAD** = OURS AHEAD, **PAR** = PARITY, **N/A** = NOT APPLICABLE TO A TRACTION INVERTER,
**GAP** = GAP WORTH CLOSING. Sizes on GAP rows: **S**mall / **M**edium / **L**arge.

## 1. Motor control modes and algorithms

| # | Feature | VESC (what / where) | OURS (what / where) | Verdict |
|---|---|---|---|---|
|1.1| Closed-loop current control (core FOC loop) | `control_current()`, `motor/mcpwm_foc.c`; PI + decoupling feed-forward | `foc_step()`, `firmware/src/control/foc.c`; PI + speed-voltage decoupling feed-forward | **PAR** — same technique (PI + decoupling), each correct for its plant |
|1.2| Torque -> current reference solve (MTPA + field weakening + limits) | MTPA closed-form (`mcpwm_foc.c:3624-3639`, salient motors only) computed once, then a **separately** duty-triggered field-weakening current subtracted from id (`foc_run_fw()`), then a circle clamp — three decoupled steps, no torque-level cross-check tying the final (id,iq) back to the requested torque | **Round 23 rework (FW-37)**: MTPA and field weakening solved **jointly** on the motor's torque hyperbola against the voltage ellipse, current circle and demagnetisation limit by proven-convex bisection; every returned (id,iq) is checked against a formal postcondition (correct sign, magnitude <= request, inside every limit) before return, with its own failure mode (`TQ_POSTCOND` -> `DTC_TORQUE_POSTCOND` -> the §6 "control lost" row). `firmware/src/control/torque.c` | **OURS AHEAD** — the decoupled VESC pattern is exactly the shape of defect OURS's own round-22 code had (F217: a documented, reproduced ~2x torque-delivery error on a salient motor in field weakening, fixed in round 23) and self-corrected via a formal joint solve + runtime postcondition; VESC's architecture has no equivalent cross-check (its PI controller's own vd/vq voltage-circle clamp is a separate, real mitigating layer, but there is no torque-level feasibility proof on the reference itself) |
|1.3| Current-loop PI gain design | Pole-cancellation form `kp=L*bw, ki=R*bw`, `bw=1/tau` (tau ~ 1.5 ms assumed), computed from **measured** R/L during commissioning (`conf_general_measure_flux_linkage_openloop()`); no explicit stability-margin proof or refusal path | Same pole-cancellation form `kp=L*wc, ki=R*wc`, computed from the **EOL-measured** calibration record's R/Ld (`app.c:137` -> `gains_default()` -> `gains_compute()`, `firmware/src/control/gains.c`) at a bandwidth that is a calibrated fraction of a **per-SKU-per-f_sw phase-margin ceiling** derived from a documented stability screen (`docs/firmware-contract.md` §2, "S6 screen"); refuses to produce a gain set (no torque) outside the proven range | **OURS AHEAD** — same underlying control-design method on both sides (credit VESC for auto-tuning from measured plant data), but only OURS ties the bandwidth choice to a proven margin and fails closed outside it |
|1.4| Current-controller saturation / anti-windup | D-axis-priority clamped-integrator anti-windup + circle limit for q (`control_current()`, `mcpwm_foc.c:4636-4650`); separate instances for position/speed/duty PIs | D-axis-priority clamped-integrator (conditional integration) anti-windup + circle limit for q (`foc.c: integrate()`, `pi_axes()`) | **PAR** — the same standard technique |
|1.5| Dead-time compensation | Present (`update_valpha_vbeta()`, `mcpwm_foc.c:5054-5119`) but, **per the source's own comment**, feeds only the internal voltage *estimate* used by the sensorless observer/telemetry — "these are not used to control the switching times" | Present (`foc_dt_comp()`, `foc.c`), added directly into the duty cycle that is written to the PWM registers | **OURS AHEAD** — OURS actually compensates the switched voltage; VESC's, by its own admission, does not |
|1.6| Duty-cycle control mode | `CONTROL_MODE_DUTY`; cascaded PI-on-duty feeding the current loop | No duty-cycle control target exists (the SVPWM duty is an internal actuation detail, never a control input) | **N/A** — a vehicle traction inverter is torque-commanded by the VCU; "duty" as a rider-facing throttle concept has no automotive analogue |
|1.7| Speed control mode (closed loop inside the controller) | `CONTROL_MODE_SPEED`, PID with 3 selectable feedback-source estimators | None — OURS reports speed on CAN and can refuse/limit torque when no voltage-feasible current exists at the present speed (F23), but never closes a speed loop itself | **N/A** — in an automotive drivetrain the VCU is the speed/vehicle-motion control layer; the inverter is deliberately a torque slave. Adding an inverter-side speed loop would create two controllers fighting over the same actuator |
|1.8| Position / handbrake control | `CONTROL_MODE_POS` (full PID) and `CONTROL_MODE_HANDBRAKE` (fixed-angle magnetic lock) | None | **N/A** — no traction use case for locking the rotor at an arbitrary electrical angle; parking/hold is a friction-brake function at the vehicle level, and low-speed "creep" (if wanted) needs no inverter feature beyond the torque-follows-command path that already exists — the VCU simply requests a small positive torque |
|1.9| Sensorless startup / forced-commutation openloop modes | 3-stage lock->ramp->constant forced commutation bridging to the observer; explicit `OPENLOOP*` control modes for diagnostics | None | **N/A** — the resolver supplies absolute position from t=0; there is no "unknown rotor position at start" problem to solve |
|1.10| Sensorless back-EMF observer as the primary (or a fallback) position source | 7 selectable variants (Ortega original/+lambda-comp, MXLemming/+lambda-comp, MXV family/+lambda-comp/+lin), each feeding a PLL; a documented, citation-backed body of control theory | None as a position source. A minimal analogue exists only as a **plausibility cross-check**: `foc_omega_model()` (`foc.c`) derives a back-EMF-based speed estimate from (vq, iq, flux) purely to sanity-check the resolver's angle-rate (FW-10), never to command anything | **N/A as a primary sensor** — a resolver-equipped ASIL motor has no missing-sensor problem to solve with an observer; OURS' minimal cross-check use is the right-sized amount of the same idea for a plausibility role, not a capability gap |
|1.11| HFI (High-Frequency Injection) | 5 variants (`HFI`/`HFI_START`/`V2`-`V5`), six-vector or D-pulse injection, FFT demodulation, explicit "HFI then sensorless fallback" mode; also reused as the inductance-measurement instrument | None | **N/A** — HFI exists to find rotor position at standstill/low speed without a physical sensor; irrelevant with a resolver |
|1.12| Cogging-torque / torque-ripple compensation | **Confirmed absent** (exhaustive grep, both VESC trees) | **Confirmed absent** | **GAP WORTH CLOSING (M)** — not a "VESC has it, we don't" item (neither does), but a legitimate forward-looking ask for a traction product where torque ripple affects NVH and half-shaft/gearbox durability at sustained high torque; would need an EOL-calibrated feedforward table keyed to mechanical angle, validated against the specific motor's measured ripple signature |
|1.13| Saturation-dependent Ld(i)/Lq(i) in the real-time torque solve | VESC's `SAT_COMP_MODE` corrects the **sensorless observer's** internal L/lambda model as current rises (irrelevant to OURS, see 1.10); it is not a current-loop feedforward for control accuracy | `motor_t` (`control/motor.h`) carries fixed Ld, Lq, Rs, psi_f; the real-time MTPA/field-weakening solve and the FW-37 postcondition all use these constants. The offline §6 rule-(a) energy screen's prose allows Ld(i)/Lq(i), but the shipped calibration record and controller do not carry or consult such a table | **GAP WORTH CLOSING (M)** — at 220 kW a traction IPM runs well into saturation near rated torque; a calibrated 2-D Ld(i,iq)/Lq(i,iq) table feeding the existing hyperbola solve (already structured to take L as a parameter) would improve torque linearity/accuracy at the top of the envelope without touching the safety-relevant parts of FW-37 |

## 2. Sensor support and self-commissioning

| # | Feature | VESC | OURS | Verdict |
|---|---|---|---|---|
|2.1| Hall-effect rotor-position sensors | Direct GPIO read + majority vote + angle LUT blend (`util/utils_sys.c`, `foc_correct_hall()`) | None (the only "Hall" parts in OURS are LEM HC5FW **current** transducers, unrelated) | **N/A** — 3-Hall commutation gives ~60 deg resolution, unsuitable for FOC of a high-power traction motor and not how automotive traction position sensing is done |
|2.2| ABI incremental encoder | `encoder/enc_abi.c`, hard hysteresis switch to/from the observer | None | **N/A** — incremental encoders lose absolute position on power loss and are materially less robust to the underhood EMI/vibration/temperature environment than a resolver, which is exactly why traction drives use resolvers |
|2.3| SPI absolute encoders (AS504x/AS5x47U/MT6816/MT6835/TLE5012), sin/cos, BiSS-C, Tamagawa, MA782, AMT22 | 10 distinct chip-scale encoder drivers under `encoder/*.c` | None | **N/A** — same reasoning as 2.2; these are cost/complexity-optimised hobby-grade choices, not automotive-qualified for direct traction-shaft mounting |
|2.4| PWM/servo position input | `encoder/enc_pwm.c` | None | **N/A** |
|2.5| Resolver support | One driver among 12+ options, via the dedicated AD2S1205 R/D-converter **IC** (`encoder/enc_ad2s1205.c`, bit-banged SPI to the chip's SAMPLE/RDVEL pins); 3 resolver-specific faults (LOT/DOS/LOS) surfacing the IC's own status bits | The **only** sensing modality, R/D conversion done **in software** (SWG excitation + 3-channel SDADC correlation, `control/resolver.c`), with amplitude-window, two-plane excitation-window, tracking-error, acceleration-plausibility and angle-rate-vs-current-model checks, cadence-locked timestamping proven across MCU resets and DMA phase loss (FW-10/28/29/30/35/36) | **PAR at the capability level** (both correctly support resolver-based sensing) — OURS' validation depth is far beyond what a generic hobby driver needs, but that is the correct amount of engineering for a sensor with no fallback, not evidence of a gap on either side |
|2.6| Motor parameter identification / self-commissioning (R, Ld/Lq + saliency, flux linkage psi, hall table, encoder/resolver offset) | A complete, orchestrated on-target suite: DC-lock R (`mcpwm_foc_measure_resistance`), HFI-based L/saliency (`mcpwm_foc_measure_inductance`, reusing the HFI injection path), a combined R+L sweep, open-loop flux-linkage measurement that **also auto-tunes the current-loop PI gains** from the result (`conf_general_measure_flux_linkage_openloop`), a hall-table builder (forward+backward 360 deg sweeps), an encoder offset/ratio/inversion detector, and one top-level "detect everything" routine runnable **fleet-wide over CAN** (`conf_general_detect_apply_all_foc[_can]`) | **None on-target, anywhere.** R, Ld/Lq, Rs, psi, resolver gains/offsets/phase-trim/electrical-zero and pole-pairs are all **calibration-record inputs** (`nvm/calib.h`) produced by an EOL/HIL rig stated repeatedly and explicitly to be **outside this repository** (`calib.h` TODO(EOL); `safety/arm_evidence.h` for the two validation records; `target-bringup.md` T-05/T-07/T-34/T-35) | **GAP WORTH CLOSING (L)** — see the dedicated safety discussion below; this is the headline gap |
|2.7| Sensor auto-detect scope | Deliberately narrow even in VESC: only Hall then AS5047-SPI are auto-probed; everything else is manually selected | N/A (no auto-detect of any kind) | (context for 2.6, not a separate row) |
|2.8| Current-sensor offset calibration | `mcpwm_foc_dc_cal()`: drives one phase at a time at 50% duty (net-zero torque even if already spinning), averages 1000 samples/phase; runs automatically as the **first step of every commissioning/detection wizard** — not on an ordinary boot of an already-commissioned motor | `isns_offset_ok()` (`sense/current.c`) **checks** (does not recompute) the current-loop's mean pin voltage against the frozen EOL-calibrated offset at every key-on (§9 step 3, `SENSOR_SELFTEST`), refusing arming if it has drifted outside `cal_isns_offset_tol_v` | **PAR, with a shared small gap** — both treat true offset (re-)measurement as a commissioning-time activity, not an every-key-on activity; OURS' extra key-on drift *check* (fail-closed) is a real plus VESC's ordinary boot path does not appear to have. Neither system re-derives and adopts a fresh offset every key cycle to track thermal drift — see the ranked gap list (S) |
|2.9| Phase-voltage filter cross-check | `foc_phase_filter_enable`: compares the hardware phase-voltage filter reading against the modulation-implied voltage below a speed threshold; faults on >5% disagreement | No equivalent named check (OURS has no phase-voltage sense hardware in this design; phase current is the only per-phase measurement) | **N/A** — different hardware architecture (OURS' current sensing + dual-channel V_DC + resolver already gives the cross-checks its architecture needs; there is no phase-voltage-filter hardware to cross-check) |

## 3. Limits, derating and protections

| # | Feature | VESC | OURS | Verdict |
|---|---|---|---|---|
|3.1| Motor / battery current limits (motoring + braking, in + out) | `mc_configuration` fields (`l_current_max/min`, `l_in_current_max/min`, etc.), combined every cycle by `update_override_limits()` (`mc_interface.c:2245-2547`); **FOC hard-clamps** the reference, **BLDC instead ramps** toward the limit (`m_current_backoff_gain`) — the two VESC control schemes disagree with each other | `torque_limits()`/`torque_clamp()` (`control/torque.c`), current allowance tied to a **30 s peak-energy budget with a 180 s (3x) recovery time constant** (`torque_derate_update`) rather than an instantaneous limit alone | **PAR**, each with a nuance the other lacks: VESC's separate accel-vs-brake temperature-derate curves (`l_temp_accel_dec`, so braking authority survives heat longer) has no OURS analogue; OURS' explicit peak/continuous energy-budget model is more sophisticated thermal accounting than VESC's instantaneous-limit-plus-ramp |
|3.2| DC-link over/under-voltage protection | **Software, ISR-based**: `mc_interface_mc_timer_isr()` integrates \|Vin-limit\| every tick and faults only once the integral exceeds a threshold (tolerates transients; anti-windup clamped) — appropriate at a 12-100 V hobby-vehicle scale | **Independent analog hardware**: both V_DC channels' hardware comparators trip the SKU's OV threshold directly into the eFlexPWM fault input and an ASC request, proven to <=15.6 us to the ASC request and <=7.56 us to ASC entry (FW-06, `docs/firmware-contract.md` §4), entirely decoupled from CPU/software execution or timing | **OURS AHEAD** — and this must not be inverted: a software-ISR-timescale response to a lost battery path during regen at 800 V/220 kW would let the DC-link exceed component ratings within tens of microseconds (the contract's own physics: 0.9-1.65 V/us charge rates); VESC's approach is correct for its own voltage/energy class and must never be copied into this one |
|3.3| Absolute overcurrent hard cutoff | Software check in the motor-control ISR (`mc_interface.c:1986-1993`, `FAULT_CODE_ABS_OVER_CURRENT`) | Independent ADC analog-watchdog -> eFlexPWM fault input, <=2 PWM periods, active even with the CPU halted (FW-05) | **OURS AHEAD** — same reasoning as 3.2: a software/ISR-timed cutoff is inappropriate as the *only* line of defence at this voltage/current/energy class; OURS keeps VESC's style of software check too, as a backstop, but never as the sole path |
|3.4| Duty-cycle / modulation-index limit | `l_min_duty`/`l_max_duty`, `l_duty_start` rolloff | `cal_mod_index_max` (used throughout `torque_v_available()`) plus the FW-25 voltage-feasibility witness | **PAR** — same concept under different names |
|3.5| Watt limits | Explicit battery-side `l_watt_max`/`l_watt_min` | `torque_p_max_w()` (`torque.c`): P_max(V) = min(P_rated, sqrt(3/2)*0.95*V*I*0.85) (FW-03) | **PAR** — functionally equivalent power ceilings |
|3.6| Regenerative-braking current/watt limiting (steady state) | `l_battery_regen_cut_start/end` rolloff | BMS-relayed `p_chg_w` limit with its own staleness timeout -> zero regen (FW-11); a RUN-only DC-link trim (`dclink.c`) that takes back regen above the SKU's normal-range voltage maximum | **PAR** on the steady-state mechanism | 
|3.7| Regenerative braking with the energy sink lost (contactor/battery gone) | No analogue found — VESC's domain (a small, always-connected battery on a vehicle a rider is standing on/near) does not present this failure mode the way an 800 V pack with a contactor does | A full, formally derived treatment: zero-current below n_x, LS-ASC (winding-current recirculation) above n_x or when the link would exceed the OV trip with no sink, an explicit winding-energy screening equation (rule a) and a proven-vehicle-integration release gate (rule b), `keep_hv` reporting to the VCU/BMS (FW-06/08/08b, §6) | **OURS AHEAD** — this is a genuinely different problem at automotive voltage/energy scale that VESC's design space does not need to solve |
|3.8| Overspeed / underspeed protection | Explicit hard `FAULT_CODE_OVERSPEED`/`UNDERSPEED`/`ABS_OVERSPEED`, config-gated (`l_additional_faults`) | **No direct check found.** `n_max_rpm` (`control/motor.h`) is used only (a) as a calibration-record range bound (`nvm/calib.c:58`) and (b) as the assumed worst-case speed when the resolver is unknown/invalid (`safety/safe_state.c:64`) — grepped for "overspeed"/"n_max_rpm" across `firmware/src`, `firmware/include` and the contract with no direct "measured speed exceeds n_max -> fault/derate" logic found. An extreme overspeed is only caught **indirectly**, via the back-EMF it generates eventually tripping the V_DC hardware OV comparator (FW-06) | **GAP WORTH CLOSING (S)** — cheap to add (compare `rslv_speed_rpm()` against a calibrated ceiling in the existing 1 ms `detect()` dispatcher) and closes a real, independently-identified safety-coverage gap; sizing is small because the indirect OV backstop already exists, so this is a diagnosis/derate improvement, not a new hardware protection layer |

## 4. Fault codes and diagnostics

| # | Feature | VESC | OURS | Verdict |
|---|---|---|---|---|
|4.1| Fault taxonomy | `mc_fault_code`, 34 flat values (`datatypes.h:144-179`), one active fault at a time (`mc_interface_get_fault()`) | ~68 `dtc_id_t` values (`comms/dtc.h`), each an ISO 14229-1-style record: status byte (test-failed/confirmed/pending/test-not-completed bits), occurrence counter, first/last timestamp | **OURS AHEAD** on structure — a proper per-DTC state machine vs. a single flat "current fault" enum |
|4.2| Fault-event history (snapshot + persistence) | `fault_vec[25]` (`terminal.c:50,66`, an imperfect wraparound ring): a rich single-event snapshot (currents, voltage, duty, RPM, tachometer, `cycles_running`, 3 raw timer-register captures, comm step, temperature, DRV-chip fault bits) but **`fault_data` has no timestamp field at all**, and the array is plain RAM — lost on every reset, let alone a power cycle | `nv_fault_t` (`nvm/nvlog.h`), a 16-slot NVM ring: `key_cycle`, `t_ms` timestamp, fault row, action taken, and electrical context (speed, id, iq, vdc) at the fault instant — **A/B-redundant, survives a power cycle** | **Mixed, net OURS AHEAD**: OURS is ahead on persistence and timestamping (the things a warranty/field investigation actually needs); VESC's snapshot is richer in raw low-level detail (timer captures, DRV fault bits) that OURS' doesn't capture. Both findings matter for the ranked gap list — OURS' data is better *and* currently unreachable (4.4) |
|4.3| Fault clearing / retry policy | **Automatic and silent**: `run_timer_tasks()` resets the active fault to NONE once it has not re-fired for `m_fault_stop_time_ms` and the live hardware fault pin is deasserted — no operator/authority involved, no distinction between a nuisance fault and a short-circuit-class event | **Authorised-only, rate-limited**: one VCU-authorised retry per key cycle, no sooner than 1 s after the event, at reduced torque; a second DESAT-class event latches permanently until a key cycle (FW-15) | **OURS AHEAD, and explicitly not-to-be-copied**: silently auto-resuming modulation after a short-circuit-class event with no authorisation is the single clearest "must NOT be copied from a hobby controller into an ASIL context" item in this whole comparison |
|4.4| Exposing fault/DTC data to a host or vehicle tool | The `fault`/`faults` terminal command prints the current fault and the `fault_vec` history over USB/CAN-bridged serial; `crash_diag` for MCU-level crash info | INV_STATUS carries only a confirmed-DTC **count** and the **first** active DTC ID (`can_cmd.h`); there is no service to read the full DTC list or the fault-history ring — `nv_read_fault()` (the function that would serve it) has **zero callers anywhere except its own unit test** (verified by `grep -rn nv_read_fault firmware/`) | **GAP WORTH CLOSING (S)** — the data model is already better than VESC's (4.1/4.2); this is purely a missing export path, cheap to add on the existing UDS server (`comms/uds.c` already implements the ISO-TP/UDS framing) as a `ReadDTCInformation`(0x19)-style service, and a natural first real feature for `tool/` |

## 5. App layer, CAN comms, and ancillary subsystems

| # | Feature | VESC | OURS | Verdict |
|---|---|---|---|---|
|5.1| Hobby/light-EV input methods (RC PPM, analog throttle, UART pass-through, Nunchuk, PAS, custom-C hook) | 6+ selectable `app_*` modules, one active at a time (`applications/app.c`) | None — torque comes only from the VCU over CAN-FD (`can_cmd.h`) | **N/A** — a modern EV has no throttle-pedal-direct-to-inverter wiring; the VCU is the single input aggregator |
|5.2| Self-balancing-board app | Removed from core firmware after 6.00, moved to an external LispBM package (`CHANGELOG.md:112`); still present in `bldc-6.00/applications/app_balance.c` | None | **N/A** |
|5.3| CAN protocol integrity | Plain payloads (floats/ints packed into `CAN_PACKET_STATUS*`), no application-layer CRC/counter/staleness protection beyond the CAN controller's own transport-level CRC | Every frame (VCU_CMD/VCU_BMS/INV_STATUS) is E2E-protected: CRC-8 (SAE J1850) + 4-bit alive counter + staleness (20 ms) + jump-detection (`can_cmd.c`) | **OURS AHEAD** — appropriate to a safety-relevant vehicle bus; VESC's breadth of *data channels* (6 status frames, IO-board/GNSS/power-switch sub-protocols) exceeds OURS' 3-frame protocol, but breadth without E2E protection would be the wrong trade for this application |
|5.4| Multi-controller CAN addressing / fleet command-forwarding | 8-bit `controller_id` addressing, generic packet-forwarding/bridging so one USB link can configure/flash an entire CAN-bus fleet (`comm_can.c:1642-1809`) | None — a single traction inverter per drive unit | **N/A** — no peer fleet of identical controllers exists in this vehicle architecture to address |
|5.5| BMS interface | First-party "VESC BMS" bridge wired into the CAN RX path, per-cell voltage/balance/temperature for up to 50 cells (`bms.c`, `bms_values`) | VCU_BMS CAN message: pack voltage + charge/discharge power limits only, relayed by the VCU (`can_cmd.h`) | **N/A / correct-by-design** — cell-level BMS ownership belongs to the vehicle's BMS/VCU in an automotive architecture, not the traction inverter; VESC's first-party bridge reflects a hobby/prosumer context where the same small vendor often supplies both |
|5.6| IMU / AHRS | Multiple sensor drivers + 2 AHRS filters (Madgwick/Mahony, Fusion), for the balance package and telemetry | None | **N/A** — a traction inverter has no attitude-estimation need |
|5.7| User scripting (LispBM) | A sandboxed Lisp interpreter, 244 built-in extensions touching motor/IMU/CAN/BMS state and outputs, persisted to flash, auto-started on boot, plus loadable native-C libraries | None | **N/A, and this must never be added.** Arbitrary runtime-loaded/interpreted behaviour is fundamentally incompatible with a certified, fixed-behaviour ASIL-D control unit; this is the second clear "must NOT be copied" item in this comparison |
|5.8| Custom on-device GUI / installable packages | `res/qml/DynamicLoader.qml` injects live QML; a full script+GUI+metadata `.vescpkg` bundle mechanism | None | **N/A**, same reasoning as 5.7 |
|5.9| Configuration persistence philosophy | Flash-emulated EEPROM with a CRC per struct; **on CRC failure, silently reloads compiled-in defaults and keeps running** (`conf_general.c:455-461`) | FW-20 calibration record: versioned, CRC-32, range-checked, bound to hardware serial + SKU + motor ID; **on any failure, refuses torque entirely** (`nvm/calib.c`) | **OURS AHEAD, by design necessity**: VESC's choice is correct for a hobby device (don't brick over a bit-flip); OURS' fail-closed choice is the one an automotive drive unit must make — silently running a 220 kW motor on unbound/wrong parameters is not an acceptable failure mode under any circumstance |
|5.10| Hardware-target breadth | 23 vendor directories, 314 board headers (up from 10/~150 in the 6.00 release) | 1 fixed, pin-frozen board, 4 SKU parameter sets (`calculations/mcu-ballmap.json`) | **N/A** — different product model (a reusable community firmware vs. a single qualified automotive design); not a capability gap |

## 6. Calibration, EOL, and firmware update/service tooling

| # | Feature | VESC | OURS | Verdict |
|---|---|---|---|---|
|6.1| Calibration/config storage integrity | See 5.9 | See 5.9 | (see 5.9) |
|6.2| Arming discipline | None found analogous — VESC runs once configured; "safe start" (`SAFE_START_MODE`) is an app-layer "throttle must be zero after a fault/boot" debounce, not a system-wide gate | FW-24: five fail-closed, itemised arming-evidence checks (route bound, config read-back, protection-lock read-back, and two EOL/HIL-measured, CRC-sealed, image+SKU+UID-bound NVM records) — gate enable, self-test energisation and torque are ALL refused unless every item is present, missing items named on CAN (`safety/arm_evidence.h`) | **OURS AHEAD** — nothing in VESC resembles a formal, itemised, evidenced arming gate |
|6.3| Firmware update / bootloader mechanism | A complete (if unsigned) in-repo mechanism: stage-erase/write (optionally LZO-compressed) + a bare Cortex-M jump to a hard-coded bootloader address, with fleet-wide CAN-forwarded update in one action. **No cryptographic signing anywhere** — the only integrity check is a CRC32 self-check of the *already-running* app (a corruption detector, not an authenticity check); *"any correctly-CRC'd image can be flashed by anyone with USB/UART/CAN access"* | FW-21 *specifies* signed images, rollback protection and a no-torque update state — but explicitly states its own mechanism is **"the bootloader + HSE deliverable, not the application image's"** and is not in this repository | **GAP WORTH CLOSING (L)** — see the dedicated safety discussion below; VESC's unsigned mechanism is the concrete anti-pattern to never copy |
|6.4| Data sampling / oscilloscope-style waveform capture for debugging | A complete mechanism (`mc_interface.c`): `DEBUG_SAMPLING_*` modes including **fault-triggered** capture, 10 parallel 1000-sample buffers (angle, 3 phase currents, 3 phase voltages, V_DC/zero ref, filtered current, switching frequency, status byte), retrieved via `COMM_SAMPLE_PRINT` | **None** — no ring buffer, no capture mode, no retrieval protocol anywhere in `firmware/src` | **GAP WORTH CLOSING (M)** — exactly the capability named in the task's own gap candidates; core engineering debugging tool for bring-up and field-return analysis |
|6.5| On-target run-time statistics (energy, distance, max temperatures) | Two tiers: session RAM counters (Ah/Wh, max temp/speed/power/current, `setup_stats`) and lifetime flash-persisted odometer+runtime (`backup_data`/`g_backup`, flushed only on clean shutdown/reboot — an abrupt power loss loses progress since the last clean one) | **None** — confirmed by grep across `firmware/` for odometer/amp-hour/watt-hour/distance/max-temperature/lifetime fields: zero hits | **GAP WORTH CLOSING (S-M)** — low safety relevance, real warranty/predictive-maintenance value |
|6.6| Service/terminal protocol for a host tool | ~120 possible commands (33 built-in + a registry used at 90 call sites): status/faults, measurement/detection, FOC tuning, encoder status, CAN, gate-driver register access, even a BlackMagic SWD-probe passthrough | **Two** diagnostic services in total: UDS SecurityAccess (0x27) and one RoutineControl (0x31, clear the stuck-on-QDIS lock); no ReadDataByIdentifier, no ReadDTCInformation, no live-data streaming, zero terminal-style commands | **GAP WORTH CLOSING (S-M)** — the natural next feature for `tool/`'s bridge protocol (already sketched as newline-delimited JSON), gated behind the same SecurityAccess pattern already proven for the one existing routine |
|6.7| Bench/service GUI application | VESC Tool: a mature, two-platform (desktop Qt + mobile QML) application — real-time dashboards, fault/sampled-data capture with CSV export, ride-log analysis with GPS/map, detection wizards, terminal, single/fleet firmware flashing, CAN device list/forwarding, a full custom-GUI/scripting environment, and developer tools (raw CAN monitor, SWD/ESP programmers) | `tool/` ("Traction Tool"): **just started, uncommitted**, `package.json` describes the intent (simulator, replay, CAN-FD codec) using preact+uplot; only a generic JSON stdio helper exists so far, explicitly modelled on VESC Tool per `docs/reference/vesc/README.md` | **GAP WORTH CLOSING (M, in progress)** — sized as the sum of 6.4/6.5/6.6's firmware-side prerequisites plus the tool's own server/client/bridge code; GPS/ride-mapping-class features (VESC Tool's Log Analysis map view) are explicitly **out of scope/NOT APPLICABLE** for a fixed traction inverter bench tool |

---

# Ranked gaps (by value to the product)

1. **Firmware update / secure bootloader (L).** Today there is *no* field-update path for a shipped
   vehicle's inverter anywhere in this repository — FW-21 states the requirement (signed images, rollback,
   no-torque update state) but its implementation is explicitly out of scope. Without it, every bug fix or
   improvement requires physical ECU removal/bench reflash. **Safety consideration**: build this as the
   opposite of VESC's mechanism in every respect that matters — VESC's own source confirms it has no
   cryptographic signing at all ("any correctly-CRC'd image can be flashed by anyone with bus access"); an
   ASIL context needs signature verification before any flash write is accepted, anti-rollback (never accept
   an older signed image once a newer one has run, to close attack/regression windows), and the no-torque
   update state FW-21 already specifies. The fleet-wide "flash every CAN node in one action" convenience
   VESC offers should not be replicated verbatim for a safety-relevant ECU without the same per-node
   signature check applied individually.

2. **Motor parameter identification / self-commissioning, safely interlocked (L).** VESC's suite (R, L,
   flux linkage, hall table, encoder offset, one-button "detect everything," even fleet-wide over CAN) is
   the single largest capability difference found in this comparison, and closing even a subset of it (e.g.
   just resolver electrical-zero/phase-trim, since that is the one sensor OURS will always have) would cut
   EOL bench time and enable field service after a motor swap without a full HIL rig. **Safety
   consideration (the most important one in this report)**: VESC's routines work by forcing test currents
   and forced-commutation rotation into a motor whose parameters are not yet known, relying on generic
   conservative defaults and a human standing next to a physical kill switch. That precondition does not
   exist for a 220 kW/800 V traction motor mounted in a vehicle. Any on-target self-commissioning capability
   here must (a) run only inside an interlocked service/EOL mode equivalent to today's external rig's own
   preconditions (wheels off the ground / dyno-locked, HV isolated from producing vehicle motion), (b) go
   through the *same* fail-closed arming evidence (FW-24) as ordinary operation rather than a bypass path,
   and (c) write its result only into the existing sealed, CRC-bound, range-checked calibration-record path
   (FW-20) rather than being trusted live mid-routine. This is an addition to the arming/safety case, not a
   simple port of VESC's wizard.

3. **Fault-history and DTC export to a host/service tool (S).** The data OURS already captures
   (`nv_fault_t`) is structurally better than VESC's (persisted, timestamped, electrical-context-rich) but
   is completely unreachable today — `nv_read_fault()` has no caller outside its own test. Wiring a
   `ReadDTCInformation`-style UDS service on the existing diagnostic bus is cheap and is the highest
   value-per-effort item in this list; it is also the natural first real feature for `tool/`.

4. **Sampled-waveform capture with fault trigger (M).** VESC's `DEBUG_SAMPLING_*`/`COMM_SAMPLE_PRINT`
   mechanism is a well-proven pattern (capture phase currents/voltages/duty around a fault or on command,
   stream to a host). OURS has nothing comparable, and as more motors/SKUs are integrated this is exactly
   the tool engineering will keep needing by hand (scope probes on a HIL rig) without it. Low safety risk to
   add (a read-only RAM ring buffer fed from data the ISR already touches), gated the same way telemetry
   generally should be (no write path, no effect on control).

5. **Overspeed protection / DTC (S).** A specific, concretely-evidenced coverage gap: no direct
   "measured speed exceeds n_max" check exists; today an overspeed is only caught indirectly through the
   back-EMF it eventually generates tripping the DC-link OV hardware comparator. Cheap to add (one more
   check in the existing 1 ms `detect()` dispatcher against the calibration record's `n_max_rpm`), and it
   converts an indirect, voltage-mediated backstop into a direct, diagnosable one with its own DTC.

6. **Service/terminal diagnostic protocol for the host tool (S-M).** A natural extension of the existing
   UDS server and `tool/`'s own newline-delimited-JSON bridge design: read-only telemetry/status commands
   first (current state, live sensor values, DTC summary), gated the same way the one existing
   state-changing routine already is (SecurityAccess, refused with HV present or armed).

7. **On-target run-time statistics — energy, distance, max temperatures (S-M).** Real warranty/
   predictive-maintenance value (VESC persists an odometer and runtime counter, with a documented caveat
   about losing progress on an unclean shutdown); essentially zero safety relevance, so it is fine to build
   exactly this simply (a few accumulators plus an existing-style NVM record) rather than gold-plating it.

8. **Saturation-dependent Ld(i)/Lq(i) in the torque solve (M).** Torque-linearity/efficiency improvement at
   the top of a 220 kW envelope where the machine is genuinely saturated; the existing FW-37 hyperbola solve
   is already structured to take L as a parameter, so this is a calibration-table addition, not a control
   redesign.

9. **Key-on current-offset refresh beyond the existing drift check (S).** OURS already checks the offset
   at every key-on and fails closed on drift; actually adopting a fresh reading (bounded by the same
   tolerance) would track slow thermal drift over the vehicle's life without weakening the existing check.
   Low priority — the existing check is already the safety-relevant part.

10. **Cogging-torque / torque-ripple compensation (M).** Lowest priority: neither reference implements it,
    it is a longer-term NVH/durability refinement, and it should wait until the higher-leverage items above
    (especially self-commissioning and the saturation table, which a ripple-compensation calibration would
    build on) are in place.

# Closure of the ranked gaps (2026-09-26, the same round — written by the manager after the agents' work was verified)

| Gap | Closed by | Proof |
|---|---|---|
| 1 firmware update / secure bootloader | **FW-38**: signed image container (Ed25519 with vendored TweetNaCl, SHA-256), anti-rollback security version, A/B boot slots, UDS 0x34/0x36/0x37/0x31/0x11 over ISO-TP (4 095-byte blocks), `firmware/tools/sign-image.mjs`; the release key is never in the repository | T-44…T-50; power-loss and sequence tests; a 1 MB image in 4.6 s simulated |
| 2 self-commissioning, interlocked | **FW-39** (`app/commission.c`, contract §10g): Rs, Ld/Lq, ψ, electrical zero, direction; entry through SecurityAccess + rig attestation + ARMED_ZERO_TORQUE + vehicle speed valid and zero; results staged, sealed through FW-20 | T-51…T-54; the bridge check identifies Rs within 2 % of the plant; the tool's Commissioning page |
| 3 DTC export | **FW-40**: UDS 0x19 (01/02/0A/04) with the ISO 14229 status byte, DIDs 0xF200–F208 / 0xFD20–FD22, a gated 0x14 | T-55/T-56; `tools/dtc-table.mjs` → 80 DTCs in `dtc_table.h` and the protocol exports |
| 4 waveform capture with fault trigger | **FW-41**: 2048 × 32 B ring after the current ISR; triggers DTC / §6 row / command / level; UDS 22 FD40/FD41, 2E FD41, 31 01 F041/F042; `tools/capture-decode.mjs` | T-43; the tool's Analysis page reads it |
| 5 overspeed | **FW-42**: warn 1.00×, trip 1.05× n_max, hysteresis 0.02×, 10-sample debounce, §6 rows, DTC_OVERSPEED | tests + bridge |
| 6 service protocol | **FW-40** again: ISO 15765-2 over CAN-FD, periodic 0x2A (100/10/1 ms), the exports `tool/protocol/*.json` generated from the firmware and verified (`generate.mjs --check`) | `make check` of the bridge |
| 7 run-time statistics | **FW-43**: NV_REC_RUNTIME saved every 600 s and at shutdown; DID 0xFE43 | tests |
| 8 saturation-dependent Ld(i)/Lq(i) | **FW-45** (contract §10l): two six-point inductance tables per motor in the record (layout 4, flat by default = bit-identical to the scalar solve over 20 000 cases), used by the torque solve, every limit and the postcondition; the current-loop gain scheduled by the local slope (fixed gains at 0.37 × L: phase margin 61° → 12°, 93 % overshoot; scheduled: 6 %); FW-39's Ld/Lq routine at six bias indices fills the map (results 0x40+k / 0x50+k) | 415 → tests, 41 mutations; on the saturating host plant the flat solve mis-delivers −9.8 % at 80 % current, the map −0.1 %; the dyno map itself is T-57 |
| 9 key-on offset adoption | **FW-44**: ≤ 2 mV per key cycle, bound to the CAL CRC | tests |
| 10 ripple compensation | **FW-46** (contract §10m): a 36-point i_q feed-forward table over the electrical period in the record (int16, 0.01 A; default zero = off), mean removed, clamped to cal_ripple_ff_max_a, gated below cal_ripple_ff_fmax_hz (6 f_e), scaled down — never the base torque — so the compensated vector still fits the limits; loaded by DID 0xFD46 under FW-39's interlocks, committed by RID 0xF021 | ripple at 100 rpm / 30 N·m 3.02 → 0.51 N·m rms (−83 %) on the cogging host plant, unchanged at 600 rpm; the per-motor table is a dyno measurement (T-58) |

The closed-loop simulator bridge built for the tool also found twelve firmware defects on the way (register F221–F232, `docs/review-A22-disposition.md`), the worst being uncontrolled diode regeneration once the back-EMF passes the link below n_x. Final image `TI_FW_ID` 0x0A0F0015 (the 0x0A0F0014 references above are the state when this matrix was written).

---

# Where OURS is genuinely ahead (with evidence)

- **Fail-closed arming with itemised, named evidence** (FW-24, `safety/arm_evidence.h`) — five checks,
  every one refusing torque/gate-enable if missing, the CAN status naming exactly what's absent. No VESC
  analogue exists; VESC arms/runs once configured, with only an app-layer "throttle at zero" debounce.
- **Hardware-only fast electrical protection, decoupled from CPU/software timing** — DC overvoltage (FW-06,
  proven <=15.6 us to the ASC request) and phase overcurrent (FW-05, <=2 PWM periods) both act through
  independent analog-watchdog-to-PWM-fault-input hardware paths that work even with the CPU halted, versus
  VESC's software-ISR leaky-integrator (voltage) and ISR-checked threshold (current) — correct choices at
  VESC's voltage/energy scale, unsafe if copied verbatim into this one (matrix rows 3.2/3.3).
- **A formally derived safe-state framework for what happens to stored energy when the sink disappears**
  (the §6 decision matrix, the rule-(a) winding/link energy screen, the rule-(b) proven-integration release
  gate, `keep_hv` reporting) — matrix row 3.7. VESC's design space never needs to solve this problem.
- **The round-23 torque-to-current rework itself** (FW-37): a proven-convex joint MTPA/field-weakening
  solve with a runtime-checked postcondition on every returned current vector, replacing a decoupled
  three-step pattern whose VESC-equivalent shape had already caused OURS its own documented, reproduced
  ~2x torque-delivery defect (F217, fixed in the same rework). No comparable cross-check exists in VESC's
  current-reference generation.
- **Dead-time compensation that reaches the actual PWM duty cycle** — VESC's own source comment states its
  dead-time correction never reaches the switching times; OURS' does (matrix row 1.5).
- **E2E-protected vehicle CAN protocol** (CRC-8 + alive counter + staleness + jump detection on every
  frame) versus VESC's unprotected payloads (matrix row 5.3).
- **Fail-closed calibration and configuration**: any corruption, version mismatch, or identity mismatch in
  the FW-20 calibration record refuses torque outright, versus VESC's fail-to-compiled-defaults-and-keep-
  running EEPROM emulation (matrix row 5.9) — the correct choice for each domain, but a real difference in
  rigor for this one.
- **Authorised-only fault recovery** — one VCU-authorised, rate-limited retry, a second DESAT-class event
  latching permanently, versus VESC's silent, automatic, unauthorised fault self-clear (matrix row 4.3).
- **Depth of validation on the one sensor that exists** — the resolver chain's amplitude/excitation/
  tracking/acceleration-plausibility checks and cadence-locked timestamping proven across MCU resets and
  DMA phase loss (FW-10/28/29/30/35/36) go well beyond what any single VESC encoder driver needs, because
  OURS has no fallback sensor to fall back to (matrix row 2.5).
- **Requirement-to-code-to-test traceability and a from-scratch, fault-injecting host simulation**:
  `firmware/docs/traceability.md` ties every FW-xx requirement to its code and its tests; the host build
  (`firmware/src/platform/host/sim*.c`) models the entire card's safety chain (DRV_EN AND-gate with real RC
  delays, the fault latch and one-shot, the ASC latch, the NSI6611 mute/release rule, "stuck permissive"
  fault injection per shutdown term, a full FS26 model, ADC/SDADC/eDMA models, CAN, NVM-with-power-loss
  injection) and runs 290 tests / 2705 checks under ASan/UBSan and at -O2. VESC's own `tests/` directory
  (`bldc-master-4fd8279/tests/`: `angles`, `float_serialization`, `overvoltage_fault`, `packet_recovery`,
  `utils_math`) is narrower unit-level testing, not a closed-loop safety-chain simulation with this kind of
  fault-injection breadth. This is process rigor appropriate to a certified product, not a criticism of an
  open-source community project held to a different bar.

And, honestly stated the other way: **VESC's genuinely superior breadth** is real and should not be
undersold — 7 sensorless-observer variants and full HFI (matrix 1.10/1.11), 12+ position-sensor drivers
(2.1-2.4), a complete on-target self-commissioning suite (2.6, this report's #1 recommended gap to close),
a mature two-platform bench/service GUI with ride-log/GPS analysis and on-device user scripting (6.7, 5.7),
and a hardware-target ecosystem of 314 board variants (5.10) that no fixed-design automotive firmware would
ever need to match. Both facts are true at once: VESC is the richer *feature* surface for a
cost-constrained, sensor-flexible, human-supervised hobby/prosumer motor controller; OURS is the more
rigorous *safety* architecture for a certified, single-design, VCU-supervised automotive traction inverter,
and neither should trade its core strength for the other's.
