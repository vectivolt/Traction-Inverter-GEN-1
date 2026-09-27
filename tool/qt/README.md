# Traction Tool

A desktop bench and service application for the 220 kW / 800 V traction inverter of this repository — in the spirit
of VESC Tool: live data, controls, configuration, analysis, faults, a terminal and a firmware page. Qt 6 Widgets,
C++17, offline (no network at run time). It talks to:

- **the simulator bridge** (`tool/bridge/sim_bridge`: the firmware's host build, unmodified, with a PMSM on a dyno, the
  DC link, a bench VCU/BMS and fault injection — `tool/PROTOCOL.md` Part A);
- **a recorded log** (replay, with its own timing or scaled);
- **the inverter on a real CAN-FD bus** through Qt SerialBus (`tool/PROTOCOL.md` Part B).

It is a tool for people who know the contract (`docs/firmware-contract.md`): it shows what the firmware decides and
why, and asks the firmware for things; it never decides for it.

## The pages

| Page | What it does |
|---|---|
| **Dashboard** | The §9 state track, arming (evidence missing, MCU_GATE_EN/DRV_EN/ASC latch), HV state, speed and link-voltage gauges with their bands (n_x, the normal range, the OV trip), torque **applied** (INV_STATUS b4–5) against the command (b16–17) and the request, id/iq, phase currents, power, temperatures against the FW-04 derating band in force, the limits in force, the watchdog/heartbeat picture (frame age, ISR age, FS26, CAN freshness and rejects), the §6 decision (rows, action, rules (a)/(b), keep-HV, no-safe-state, service lock, HVIL). Under the header, the image verifier's **root of trust** (the bridge's hello `root`; over CAN DID 0xFD23, read once the service key lets the tool transmit): "TEST root of trust — development unit, not for delivery" for the TEST key, "Root of trust: no key — every image refused" for none, the key id for a build or OTP/HSE key. |
| **Live plots** | Multi-trace time plot of any channel (tree with search and presets), pause, follow, rubber-band zoom (right-click steps back), wheel magnify, middle-drag pan, two cursors with value/Δ/1/Δt readout, window length, the simulator's telemetry rate; the **id/iq plane** with the current circle (the allowance in force) and the voltage ellipse at the present speed; the **spectrum** (Hann-windowed FFT) of any channel; PNG/CSV export. |
| **Controls** | Arm/disarm behind the firmware's own checklist (CAL, VAL, OTP, platform evidence, identity, parameters, HVIL, no fault … as the bridge's `arm` object reports it); torque with a **hold-to-apply deadman** (mouse or Space; re-sent every 100 ms with a 300 ms wall-clock `hold_ms`, released on key/mouse release, focus loss or a stale link); gear; slew; the dyno speed; PWM-ASC and discharge behind a **typed confirmation**; fault injection (DESAT HS/LS, over-current, over-temperature, V_DC sensing loss, battery loss, resolver loss, CAN loss, HVIL open, V5GD loss, LV overvoltage), BMS limits, fault reset, DESAT retry authorisation, key on/off, power cycle, EOL provisioning. |
| **Commissioning** | FW-39 motor self-commissioning in the firmware's interlocked service mode, the way VESC Tool's motor detection works but never a bypass: the preconditions as a checklist (service key, link, heartbeat pacing, ARMED_ZERO_TORQUE, VCU enable withdrawn, vehicle speed valid and zero, no fault or DTC) with Arm / withdraw enable / vehicle speed 0; the operator's rig attestation (LK rotor locked, DF/DR dyno) behind a **typed confirmation**; one routine at a time — Rs, Ld/Lq, ψ with the electrical zero and the resolver direction — through SecurityAccess (bench key) and RoutineControl 0xF020, the results poll every 50 ms as the heartbeat, stop; every quantity decoded (value ± uncertainty in mΩ, µH, mWb, mrad, verdict, pending / staged / beyond the band) with the staged mask; the commit into a new calibration record version (0xF021, typed confirmation, validated by the next key cycle); JSON/CSV export. Round 23: **Map L_d/L_q (6 bias points)** (FW-45, typed) — the Ld/Lq routine at bias indices 0 … 5 in sequence (`31 01 F0 20 02 4C 4B k`, an escape single frame), each run with its own unlock, LK attestation and heartbeat, a point beyond the band of the record's map given one confirming run; the twelve differential inductances (results 0x40+k, 0x50+k, 0.1 µH) with their verdicts and staging in two tables against the bias the firmware runs (k/5 of the SKU's `i_crest_a`, at least 50 A, at most the limit — the d run also the demagnetisation limit — less 20 A; the d run at −b, the q run at +b), the staged points (index 0x02) and, in a Record column next to them, the active record's twelve points (DID 0xFD26 on both transports, read by the Session at the link-up and at each power-up — after a commit, the next key cycle's map); and the **torque-ripple table** (FW-46) — **Import ripple map…** (the CSV below) shows the 36 points and the peak, **Write + read back** unlocks, writes `2E FD 46` (75 bytes, one segmented request) and compares `22 FD 46` value by value, **Read table** reads what the next commit writes; the map and the table are committed with the rest. A refusal shows the NRC and the firmware's reason; nothing is retried and no torque is sent. |
| **Parameters** | The calibration table (`tool/protocol/params.json`, i.e. `firmware/tools/gen-params.mjs`): grouped, searchable, units, ranges, defaults, the device's value beside the edited one, dirty state, a diff filter (edited vs device, device vs default, value vs default), validation before any write (ranges, types and every `ti_params_validate()` cross-field rule, evaluated for the connected SKU), write with confirmation (one `param_set` each; the device's verdict is final), import/export JSON, change log. |
| **Faults & alerts** | Active DTCs with their UDS code, class, status bits, occurrences and first/last stamps, what each means and **what the firmware does** (`tool/protocol/dtcs.json`); the session's fault history on a timeline; user **alert rules** on any channel (threshold, hysteresis, hold time, severity → a toast and the log; persisted); the **bug report** (one JSON: state, the last N seconds of every channel, parameters with the change log, firmware identity, DTCs and history, alert rules and log, events). |
| **Analysis** | Open a recorded log (JSONL from the recorder, or CSV) or take the session buffer: per-channel statistics, a time plot with event markers (state changes, DTC raise/clear), energy and efficiency integration, temperature against load with a least-squares line, CSV/PNG export. |
| **Terminal** | A console on the active transport with history and completion: `arm`, `torque 50 hold_ms=500`, `inject desat_hs`, `uds 02 27 01`, `param_set cal_torque_max_nm 300`, or a raw `{"cmd":…}` object; every acknowledgement (ok / refused + why) is shown. |
| **Firmware** | TI_FW_ID of the running image against this build's protocol exports, the FW-20 calibration record and its byte layout (`calib_t`), the FW-24 EOL/HIL validation record (and whether this image accepts it), the arming-evidence checklist, the FW-16 self-test record, the provisioning; and the **FW-38 field update**: a signed `.tifw` container selected (`firmware/tools/sign-image.mjs`), its header shown (target SKU, TI_FW_ID, security version, length, the payload's SHA-256 checked, signature present — the firmware verifies it; the release key never enters this tool) and compared with the running image (DID 0xFD20/0xF208, and 0xFD24 — the FW-38 boot record with its anti-rollback counter — on both transports) **before anything is sent** — another SKU, the same or an older TI_FW_ID, a security version below the counter (over CAN a refusal: the update reads DID 0xFD24 again and ends before the unlock, having sent nothing but that read; on the simulator a warning, and the firmware's own verification refuses the image, ROLLBACK); a **typed confirmation**; then SecurityAccess, the programming session (no arming until the next power-up), RequestDownload, TransferData in the blocks the ECU grants (ISO 15765-2 segmented on both transports), RequestTransferExit, verify (0xFF01, polled), activate (0xF038), ECUReset and the identity read again until the ECU has restarted — with a progress bar, blocks, bytes, elapsed time, the flow control the ECU asked for and every refusal's NRC with its meaning; only NRC 0x21 busyRepeatRequest repeats a request (counted and shown). The root-of-trust banner as on the Dashboard. |

**Ripple map CSV** (Commissioning ▸ Import ripple map…, FW-46): one row per sample — the electrical angle in degrees (the
firmware's θ_e, 0 on the d axis; any range, wrapped into one period) and the torque ripple measured on the shaft in N·m
with no table applied, in the firmware's sign (positive motors in the positive direction); comma, semicolon, tab or space
separated, `#` comments and one header line allowed, at least one sample every 10° el. The tool resamples it to the
firmware's 36 points (0, 10 … 350° el, periodic linear interpolation), removes the mean and writes the i_q that cancels
it, `i_q = −(T − T̄) / (1.5·pp·ψ)` — `torque.c`'s constant at i_d = 0 with the record's ψ and pole pairs, the firmware's
own convention (`test_fw45_46.c` builds its table so) — in 0.01 A, ±30 A at most. The record's ψ and pp come from the
simulator's hello on the bridge and from DID 0xFD25 over CAN (the Session reads it with DID 0xFD23 once the service key
lets the tool transmit, and again whenever the link comes back; before that the import says so). The firmware refuses any value beyond
`cal_ripple_ff_max_a` (default 0 A: only a zero table; NRC 0x31) and applies the table from the next key cycle after the
commit. Example: `resources/ripple-example.csv` (2.0 N·m at the 6th and 0.8 N·m at the 12th harmonic on a 0.5 N·m mean,
every 5° el).

The connection bar is on every page: link state (Live, Stale, Paused, Replay, Connecting, Failed, Disconnected), the
transport, the heartbeat age and frame rate, recording, the simulation/replay time factor and pause, the service lock. **A stale frame never looks live**: a page
whose data stopped says so in a banner with the age, tiles and gauges grey out with a STALE badge, plots show an
overlay, and every command control is disabled until frames are live again. "Live" means the transport is open and a
frame arrived within 4 expected frame periods (at least 250 ms) by the host's monotonic clock; a paused simulator is
"Paused", not "Stale".

## Build, test, run

Requirements: CMake ≥ 3.25, Ninja, a C++17 compiler, **Qt ≥ 6.5** (Core, Gui, Widgets, SerialBus, Svg, Test; tested
6.11.2) and **Qwt ≥ 6.2** (tested 6.3.0). No Node, no network: the protocol exports are read from `tool/protocol/`.

macOS (Homebrew), as used here:

```sh
brew install qtbase qtserialbus qtsvg qttools qwt     # the component formulae; `brew install qt` also works
cd tool/qt
cmake --preset dev          # Debug, -Wall -Wextra -Wpedantic -Werror (tests without -Wpedantic: QtTest's macros)
cmake --build --preset dev
ctest --preset dev          # 8 suites; the bridge suites need tool/bridge/sim_bridge (make -C tool/bridge), tst_update Node
./build/dev/TractionTool.app/Contents/MacOS/TractionTool --sim     # start with the simulator
```

Presets: `dev` (Debug), `release`, `macos-homebrew` (Release with `CMAKE_PREFIX_PATH=/opt/homebrew`), build preset
`package` (the macOS .dmg). Elsewhere pass `-DCMAKE_PREFIX_PATH=<Qt>;<Qwt>`; Qwt is found as a library or a framework.
`TT_SIM_BRIDGE` (cache variable and environment variable) points at another bridge executable; `TT_PROTOCOL_DIR` at
another copy of the exports.

Command line: `--sim [--sku 8xx_sic|8xx_igbt|4xx_igbt|4xx_sic] [--rate HZ] [--time FACTOR]`, `--replay FILE`,
`--light`. The bridge is looked up in `$TT_SIM_BRIDGE`, next to the executable (the packaged app), then the build's
`tool/bridge/sim_bridge`.

Tests (QtTest, `ctest`):

| Suite | Covers |
|---|---|
| `tst_codec` | the CAN-FD codec against the **18 firmware-encoder vectors of `tool/protocol/can-frames.json`** (encode byte for byte, decode against the firmware's receiver), the CRC-8/SAE-J1850 check value, rejection of corrupted/short/cross-ID frames, the legacy 16-byte INV_STATUS, the alive-counter rule, and `CanTransport` on a `MockCanDevice` (E2E and frozen-counter rejection, the service lock, VCU emulation every 10 ms with valid counters, the torque deadman, the **ISO 15765-2 transmit** of a long UDS request: the first frame alone, then block size, STmin, Wait, Overflow and N_Bs; classic and escape single frames, padding 0xAA); the **Session's UDS queue** on the same mock: two overlapping requests put one frame on the bus, then the other once the first is answered, each callback gets its own response in order with the id `command()` returned, and an unanswered request holds the slot until the transport's 100 ms timeout (without the queue the test fails: two frames on the bus) |
| `tst_bridgeparser` | the JSON-lines parser and command encoder (flat, ≤ 24 keys, ≤ 4095 bytes), acks, telemetry flattening (null, the DTC array, the INV_STATUS hex), the terminal shorthand, and a **live check against the running bridge** that every field the application reads is present |
| `tst_params` | every row of `params.json`, validation (ranges, types, the cross-field rule evaluator and the rules themselves), the device table, dirty state, writes, import/export, the DTC catalogue of `dtcs.json` |
| `tst_alerts` | hold time, hysteresis, missing/NaN readings, operators, persistence; the DTC tracker (raise, clear, occurrence, the CAN summary) |
| `tst_replay` | JSONL/CSV load and save, the recorder, time going backwards, real-time/scaled/paused/seek playback, read-only refusals, the FFT, statistics and energy integration; the FW-46 ripple CSV (resampling, the mean removed, the i_q conversion, the angle wrapped, and every refusal: a gap over 10° el, two samples at one angle, a bad row, beyond ±30 A, no ψ) |
| `tst_gui_smoke` | offscreen (`QT_QPA_PLATFORM=offscreen`), against the real bridge: live telemetry, **Arm** through the page's button once the firmware's checklist allows it, **hold-to-apply torque** (command and applied torque follow, return on release), **inject DESAT** and find `DESAT_HS` in the Faults table, **Disarm**, the light theme, pause → "Paused" with every control disabled, then the recording analysed and replayed. Also the **Commissioning** sequencer end to end against the bridge: arm, enable withdrawn, vehicle speed 0, SecurityAccess, the Rs routine with the locked-rotor attestation polled to DONE (Rs within 2 % of the plant, verdict VALID), a start refused at 5 km/h with NRC 0x22 reason 8, the commit accepted, both exports read back. And round 23 on a third bridge: the root-of-trust banner on the Dashboard and the Firmware page; the example ripple CSV imported through the page, refused at the default `cal_ripple_ff_max_a` (NRC 0x31), written and read back equal, committed, the card rebooted and the active table read back equal; the FW-45 sweep — all twelve points VALID, within 5 % of the plant's inductance (the plant does not saturate: each point is the scalar, measured within ≈ 0.1 %) and staged — the commit accepted (the staged mask → committed, 0x80), the card rebooted and the record's maps read back (DID 0xFD26): all twelve points within 0.2 % of the staged ones — and within 0.06 µH of the firmware's own construction from them (trapezoids, L_0 = D_0) — i_map the SKU's limit (√2 × `hello.limits.i_pk_rms_a`, 0.1 A), and the page's Record column showing them. |

| `tst_can_bridge` | `CanTransport` **against the real firmware over a real `QCanBusDevice`**: the bridge on Qt's virtual CAN-FD bus through `BridgeCanGateway` (below). Locked, nothing is transmitted; unlocked, the emulated VCU arms the firmware — its contactor report, precharge then closed, is what the plant's contactors follow — and ARMED is decoded from INV_STATUS; 1500 rpm on the dyno through the bridge; 50 N·m → RUN with the applied torque within 10 %; the 0x6E9 periodic stream at its 10 ms period; faults injected, then `19 02 FF`: a segmented response (first frame, the tool's flow control, consecutive frames) reassembled, its confirmed DTCs exactly the simulator's; a corrupted INV_STATUS from a third node rejected by the E2E check; relocked, nothing leaves the tool. And FW-46 over CAN through the Session and the Commissioning page: no motor data before the service key (the import refuses), then DIDs 0xFD23/0xFD25/0xFD26 read in one request (an 83-byte segmented response) — ψ, pole pairs and the scalars equal to the bridge's hello within the DID's resolution, the record's twelve map points and i_map — the example ripple CSV imported with them (the same table the hello gives), written (`2E FD 46`: the tool's first and consecutive frame under the firmware's flow control), read back equal (`22 FD 46`: the firmware's segmented response under the tool's) and read again on its own. About 4 s; skips without the bridge or the `virtualcan` plugin |

| `tst_update` | **FW-38 against the real firmware**: images built and signed at test time with the firmware's own tool (`node firmware/tools/sign-image.mjs --key test`); the container parser (every field, every structural refusal); through `BridgeTransport` a security version below the card's counter named before anything is sent, transferred and refused at verification (ROLLBACK) with the activation refused (NRC 0x24), a signature bit refused at verification (SIGNATURE), the good image verified, activated, reset and the card restarted; the Firmware page's Update… enabled by a usable image only; the same update through `CanTransport` on the virtual CAN-FD bus (`BridgeCanGateway`: the tool's segmented requests under the firmware's own flow control, BS 4, STmin 0), where first the root of trust is read (DID 0xFD23, equal to the bridge's hello) and the lower security version is refused by the pre-check against DID 0xFD24 with nothing but that read on the bus (no 0x27, 0x10 or 0x34). ≈ 3 s; skips without the bridge, Node or `virtualcan` |

No known firmware gap is pinned at present. Closed, each reported from here and fixed in the firmware: the FW-45 commit
of a sweep on a plant that does not saturate was refused (`31 01 F0 21` → `7F 31 22`: the trapezoids turned the points'
≈ 0.1 % scatter into a map rising by up to ≈ 0.1 µH, and `calib.c` `map_ok` required it never to rise) — the record check
now tolerates a rise of up to 2 % between neighbouring points (`MOTOR_MAP_RISE_TOL`); the committed map could not be read
back (the results `31 03 F0 20 40+k / 50+k` belong to the running key cycle: `mc_init()` clears them, and no DID carried
the map) — DID 0xFD26 carries the active record's maps now, and the smoke test reads all twelve points after the key
cycle; over CAN the ripple import had no ψ or pole pairs — DID 0xFD25 carries them; and a programming request of 8–62
bytes (an escape single frame) got no answer — `tst_update` asserts that `36 01 …` so framed answers `7F 36 7F`.

`TT_SCREENSHOTS=<dir>` makes the GUI test save a PNG of every page.

## Packaging

macOS, run here: `cmake --preset macos-homebrew && cmake --build --preset package` stages `Traction Tool.app` in
`build/macos-homebrew/package`, copies in `sim_bridge`, runs **macdeployqt** (Qt
frameworks and plugins, `qwt.framework`, and the Homebrew libraries Qt links), points every bundled library's install
id into the bundle, signs ad hoc, and writes `TractionTool-<version>-macos-arm64.dmg` (with an /Applications link,
NOTICES.md and Licenses/). The bundle references nothing outside itself or the system. It is not notarised: for
distribution, sign with a Developer ID and notarise (`codesign --options runtime`, `xcrun notarytool`). The same target then runs `scripts/licences.mjs sources` (downloads the exact Qt submodule and Qwt source archives the
Homebrew frameworks were built from — url and sha256 from the installed formulae — plus the formula recipes and every
`qt_attribution.json` of those modules → `licenses/QT-THIRD-PARTY.md` and `licenses/third-party/qt/`) and
`scripts/licences.mjs table --app …` (regenerates the bundled-library table of `NOTICES.md` from what is actually in
`Contents/Frameworks`, copying each licence text from its keg; an unattributed library fails the package), copies
`NOTICES.md` + `licenses/` into the bundle and beside it, and writes `TractionTool-<version>-corresponding-source.zip`
next to the .dmg — the LGPL corresponding source, shipped rather than offered (NOTICES.md).

Not run here (documented for the other platforms):
- **Windows**: build with MSVC and a Qt/Qwt of the same compiler, then
  `windeployqt --release --no-translations build\release\TractionTool.exe`, copy `qwt.dll`, `sim_bridge.exe` (build
  the bridge with MinGW/clang: it is POSIX C using `poll`) and `NOTICES.md` + `licenses\` beside it, and package with
  an installer of your choice. The canbus plugins for PEAK/Vector/SYS TEC/PassThru come with windeployqt.
- **Linux**: `cmake --install build/release --prefix AppDir/usr`, then
  `linuxdeploy --appdir AppDir --plugin qt --output appimage` (with `QMAKE` pointing at the Qt used), adding
  `sim_bridge`, `NOTICES.md` and `licenses/`. SocketCAN needs no vendor library.

## Architecture

```
 transports (ITransport)                         Session (the one state)                 pages
 ┌────────────────────┐  frames (TelemetryFrame) ┌──────────────────────────┐  signals  ┌──────────────┐
 │ BridgeTransport    │ ───────────────────────▶ │ liveness (host clock)    │ ───────▶ │ Dashboard     │
 │  QProcess, JSONL   │  info (hello, params)    │ TelemetryStore (ring)    │          │ Live plots    │
 │ ReplayTransport    │  acks (id-matched)       │ DtcTracker, AlertEngine  │          │ Controls      │
 │  JSONL/CSV, timed  │  log, traffic            │ ParamModel (params.json) │          │ Parameters    │
 │ CanTransport       │ ◀─────────────────────── │ Recorder, event log      │ ◀─────── │ Faults & …    │
 │  QCanBus + codec   │  send(cmd, flat args)    │ service lock             │ command  │ Analysis …    │
 └────────────────────┘                          └──────────────────────────┘          └──────────────┘
          ▲ ProtocolInfo: tool/protocol/{params,dtcs,can-frames}.json embedded at build time (:/protocol/*)
```

- **One vocabulary.** Every transport produces `TelemetryFrame`s whose channel names are the bridge's JSON paths
  flattened with dots (`motion.speed_rpm`, `foc.id_a`, …) plus the decoded INV_STATUS as `inv.*`
  (`inv.torque_applied_nm`, `inv.torque_cmd_nm`, …). A CAN session and a simulator session therefore drive the same
  pages; what a source cannot provide stays "—".
- **One command path.** `Session::command(name, flat args, callback)` → `ITransport::send` → exactly one `Ack` with its
  id (a transport refuses what it cannot do, with the reason). The bridge's own command set (PROTOCOL.md A.4) is the
  vocabulary; `CanTransport` implements the vehicle subset as the VCU and refuses the rest. UDS requests (`uds`, and
  the bridge's `dtc_clear`, which runs the image's own UDS) go **one at a time per transport**: the firmware drops a
  pending diagnostic response when a new request arrives, so the Session keeps any further request in a queue until the
  outstanding one is answered or times out — the Terminal, Commissioning, Firmware and Faults pages never pair
  responses crosswise, and each page's API is unchanged (the ack carries the number `command()` returned).
- **The single source of truth.** `ProtocolInfo` loads the exports that `tool/protocol/generate.mjs` makes from the
  firmware (the parameter table of `gen-params.mjs`, the DTCs of `dtc.h` with their responses, the frame layouts and
  test vectors). The enumerations PROTOCOL.md A.7 documents but no export carries (calibration error bits, HVIL states,
  §6 row/action names) are in `ProtocolInfo.cpp`; at run time the bridge's `hello` names win. After a firmware change:
  `node tool/protocol/generate.mjs`, rebuild.
- **Storage.** A columnar ring store (200 s at 100 Hz) feeds the plots with min/max decimation to the canvas width;
  the bug report reads its last N seconds.

Source layout: `src/core` (Session, telemetry store, parameters, alerts, DTCs, logs, DSP, bug report, ProtocolInfo, the
commissioning and firmware-update sequencers), `src/transport` (the three transports, the CAN codec, the ISO 15765-2
transmit side, the bridge protocol, `BridgeCanGateway`, `MockCanDevice`), `src/ui` (theme, window,
dialogs), `src/ui/widgets` (tiles, gauges, banded bars, state track, checklist, timeline, Qwt plots), `src/ui/pages`.
The theme is `resources/theme.qss` (one stylesheet, `@tokens` filled from the dark or light palette in `Theme.cpp`)
plus a QPalette on Fusion; spacing scale 4/8/12/16/24/32 px.

## What the simulator exercises — and what it does not

Exercised, because it is the firmware's own code: the §9 start-up and the state machine, fail-closed arming (FW-24
evidence, FW-20 calibration, FW-12 PROG_ID — withdraw any through the provisioning controls), the FW-16 gate
self-test on the card model, precharge plausibility (FW-19), the §6 safe-state decisions and FW-15 recovery, the
FW-05/FW-06 compare paths, CAN E2E and alive counters (through the firmware's encoders and receiver), torque → current
(FW-37) against the voltage and current limits, derating, parameter validation (`ti_params_validate`), the DTC store,
UDS SecurityAccess and routine 0xF010 as the default build answers them.

Not exercised: timing on the S32K39 (the ISRs run on a simulated clock; WCET and latencies are target measurements),
real analog front ends and noise (the plant adds one current LSB), switching transients and the power stage (an
averaged model), accurate thermal behaviour (first order), a real CAN bus (bit timing, errors, bus-off), the bootloader
(FW-38: the simulated card verifies and activates a staged image and restarts on ECUReset, but no bootloader installs or
runs the new image — its boot record stays ACTIVATE), the EOL/HIL rig itself (provisioning is simulated), and the OEM DBC (the frame layouts are this
repository's definition until it binds them).

## CAN adapters

File ▸ Connect CAN adapter lists every adapter plugin Qt SerialBus knows — socketcan (Linux), peakcan, vectorcan,
systeccan, tinycan, passthrucan (J2534), virtualcan — marked "not on this system" when absent, plus **kvaser**, for
which Qt has no plugin (reach a Kvaser adapter through its SocketCAN driver on Linux or its J2534 driver through
passthrucan on Windows). Choose the interface (or type its name), the nominal bit rate and CAN FD with the data bit
rate: INV_STATUS is 20 bytes, so the bus must be CAN FD. Nothing is assumed present; a missing vendor library is
reported, not fatal.

On a real bus the tool **listens**: INV_STATUS (0x201) with the firmware's own E2E checks — CRC-8/SAE-J1850 over the
DataID and the payload, the alive-counter rule (a repeated counter is a frozen sender and is not shown as fresh data) —
and, if a VCU is on the bus, its VCU_CMD/VCU_BMS. **Transmitting needs the service key** (Tools ▸ Service key,
Ctrl+Shift+K): then the Controls page can run the VCU emulation (VCU_CMD + VCU_BMS every 10 ms, the bench VCU's role),
arm/disarm (the enable bit), torque with the deadman, gear, fault reset, DESAT retry, discharge, the BMS limits, and
the terminal can send raw frames and UDS requests (0x7E1: `hex` one single frame as given; `msg` a request of any length
framed per ISO 15765-2 with TX_DL 64 and 0xAA padding — a classic or escape single frame, or a first frame and
consecutive frames under the ECU's flow control: block size, STmin, Wait, Overflow, N_Bs 1 s; the response on 0x7E9 is
reassembled — a first frame gets the tool's flow control, ContinueToSend, BS 0, STmin 0 — and acknowledged with `msg`,
`frames` and `rsp` like the bridge's, plus `tx_frames`, `fc_bs`, `fc_stmin` for a segmented request). The contactor state the emulated VCU
**reports** is set by the operator to what the bench hardware actually does; the tool never assumes it. Relocking or
disconnecting stops the emulation, which the firmware sees as a stale command (FW-11: ramp to zero).

The service key is `traction-service`. It is an interlock against a slip on a live bench, not a security boundary
(its SHA-256 is in `src/core/Session.cpp`; change both together).

### The simulator on a virtual CAN bus

`BridgeCanGateway` puts the simulator bridge — the real firmware — on a `QCanBusDevice`: every frame the firmware
transmits (`can_tap`) goes onto the bus as the target sends it (CAN FD with bit-rate switch), every data frame on the
bus goes into the firmware (`can_rx`: 0x7E1/0x7E9/0x6E9 onto its diagnostic bus, the rest onto the vehicle bus). The
bridge's own VCU and BMS relay are silenced (`vcu_model`), so the VCU on the bus drives the firmware — and, as in the
vehicle, the contactor field of its VCU_CMD drives the plant's contactors: that VCU reports *precharge*, then *closed*
once V_DC holds 98 % of the pack after ≥ 300 ms (the bench VCU's sequence, `tool/PROTOCOL.md` A.6). The bridge runs in
real time: the VCU sends on the wall clock and the firmware judges the age of its frames (FW-11, 20 ms).

Qt's `virtualcan` plugin makes the bus virtual (verified with Qt 6.11.2): interfaces `can0` … `can9`; a TCP server on
127.0.0.1, port 35468, or `tcp://127.0.0.1:<port>/canN` (a host name such as `localhost` never connects), started inside
the first process that opens a device — one server per process, so a process's devices all use one port; CAN FD frames
up to 64 bytes pass with their FD and BRS flags, but a device writes them only with `CanFdKey` set
(`CanTransport::createDevice` sets it); frames are not echoed to their sender; the bit-rate keys are ignored and no
frame's validity is checked (the gateway checks).

**Tools ▸ Simulator on virtual CAN…** starts the bridge on `can0` and switches the session to `CanTransport` on the same
bus — the tool as on a bench: service key, VCU on, Arm, report the contactors precharge, then closed. The dyno, the
plant and fault injection have no path from the pages in this mode (they are bridge commands, and `CanTransport` refuses
simulator commands): use File ▸ Start simulator for those, or `BridgeCanGateway::bridge()` from code, as the test does.
Other programs can join `can0` (a second Traction Tool as a listener); two gateways on one bus would mix two firmwares.

Test: `ctest --test-dir build/dev -R tst_can_bridge --output-on-failure` (≈ 4 s; `-V` prints the measured torque, the
0x6E9 period and the ISO-TP sizes). It takes a free port of its own, so it never joins a running session's bus.

Limits. The bridge's own ISO 15765-2 tester answers every first frame the firmware sends with flow control (not
switchable from outside the bridge): in the simulator that releases the consecutive frames; the tool's flow control is
on the bus too (the test checks it) and arrives second, which the firmware ignores. Requests from the bus pass that tester
untouched (it only answers first frames the firmware sends): the flow control the firmware sends for the tool's own
first frames reaches the bus through `can_tap`, and the tester merely prints it as an unpaired `uds` ack (`tst_update`). A host too loaded to schedule either
process for ~10 ms makes a VCU frame late, and the firmware reacts as in a vehicle (CMD_LOST, DTC_CAN_TIMEOUT, the torque
ramped out and back); the test judges the torque on its median and reports such events.

## Keyboard

| Keys | Action |
|---|---|
| Ctrl+1 … Ctrl+9 | the pages |
| Ctrl+Shift+S / Ctrl+O / Ctrl+Shift+C / Ctrl+Shift+D | start the simulator / open a replay / connect CAN / disconnect |
| Ctrl+R / Ctrl+B | record / bug report |
| Space (held, on the deadman button) | apply torque while held |
| Esc (Controls) / Ctrl+0 (anywhere) | zero torque |
| Ctrl+Shift+K | service key (again: lock) |
| Ctrl+Shift+L | light/dark theme |
| P / F / C (Live plots) | pause / follow / cursor mode |
| Ctrl+F (Parameters) | search |
| Up / Down (Terminal) | history |

Every control is reachable with Tab; the typed confirmations take their word from the keyboard.

## Licences

Proprietary application; Qt 6 under **LGPL-3.0** (dynamically linked, replaceable frameworks), **Qwt** under
**LGPL-2.1 with the Qwt exception**. Qt Charts, Qt Graphs, Qt Data Visualization and QCustomPlot are not used.
Nothing is taken from VESC Tool (GPL-3.0): its public feature set only inspired the page structure. See [NOTICES.md](NOTICES.md) for the
bundled components, their licences and the release obligations, and `licenses/` for the texts.
