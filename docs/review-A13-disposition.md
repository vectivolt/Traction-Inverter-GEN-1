# Review round 14 — three independent reviews of `cfd35a7` (rev A.13)

**Inputs.** (1) A whole-system engineering review with a 24-item finding register (F01–F24), readiness
matrix and an independent calculation script; (2) an A.12 recheck (A12-R01…R08, notes N01–N03) that executed
SHA-matched firmware files; (3) a second recheck (A12-R01…R07) of the same commit. The instruction: implement
every improvement end to end and make the system fail-safe.

**Method.** Every claim was verified against the source first. The two MCU supply-ball findings were checked
against the S32K39 datasheet figure AND against NXP's own GEN3 control-card net report on disk
(`NET-91122_C.net`, MCU refdes U513, 244 connected balls), which then served as an independent oracle for the
whole ball map. The firmware findings were reproduced by reading the cited functions. The fixes were made by
this session (schematic, exporter, verifier, BOM, docs), an Opus agent (firmware: `make test` 178/1209 →
**214 tests / 1622 checks / 0 failed**, also under ASan/UBSan and −O2; every new test was run against the
extracted `cfd35a7` tree through a compatibility shim — 33 fail there and pass now, 3 are guards/positive
controls, 2 need interfaces the old code lacks; `firmware/docs/traceability.md`) and a Sonnet agent (certificate
and datasheet archive).

## Verdict

All three reviews are right on every defect they name. **The A.12 "pin freeze" was not closed**: H5 carried
5 V on the 1.5 V V15 ball and J7 grounded the V25 regulator output — a control card built from A.12 would not
have started. The firmware bypassed the hardware DESAT delay, mis-derived its millisecond clock and did not
request battery retention where its own safe-state decision relied on it; its "locked" witness was a readback.
The excitation monitor and the exciter outputs were exposed to harness faults. All of it is fixed at small
cost (four capacitors/resistors, two TVS, two PTC, two ballast resistors, one clamp change; +₹ see the BOM),
and the design is now fail-closed: gates cannot be enabled until the routing, the register lock and the
validation records exist. What remains is measurement and vendor/vehicle evidence, listed at the end.

**"Already Fixed" for a gate means the gate exists and is open** — never that the physical qualification is
complete.

## Review 1 — whole-system register (F01–F24)

| ID | Review said | Class | Verification | Action in rev A.13 |
|---|---|---|---|---|
| F01 | CRITICAL — IMCR/SSS placeholders (0u) for PTC26/PTC25 → PWM_1 FAULT0/2; the fault hold is not demonstrated | **Confirmed** | `s32k396.h` lines 89–92 literal 0u, written by lock_faults() | placeholder mappings are a build error; the host build reports the route unbound and arming is refused; the physical route test is an arming-evidence item. **F150** |
| F02 | CRITICAL — hal_pwm_config_locked() returns true from a readback; REG_PROT/XRDC is a TODO | **Confirmed** | source | config_matches() / protection_locked() separated; the lock witness reads the lock bits or returns false; arming-evidence record (route, config, lock, validated fault/OVP routes in NVM with CRC) gates gate-enable. **F151** |
| F03 | MAJOR — REXM 5.1 k lets 5.45–5.66 mA into the SDADC pads at a 35 V fault (3 mA limit) | **Confirmed** | reproduced (5.45 mA at 5.3 V clamp; unpowered worse) | REXM1/3 18 k, REXM2 42.2 k, REXM4 84.5 k (1.96 mA at 35 V, 2.81 mA at 50 V, same ratios); verifier row; ERC values. **F152** |
| F04 | MAJOR — ALM2402 outputs reach the connector; a 24/35 V terminal fault exceeds the 18 V output rating whatever the supply does | **Confirmed** | schematic | SMCJ8.5CA at each connector node (clamps at 10–11 V, below the 12.1 V rail + a diode: no back-drive) + MF-MSMF020 PTC (0.2 A, 30 V, AEC-Q200) per line; fault currents, pulse budget and amplitude loss in the register; bench gate ㉘. **F153** |
| F05 | MODERATE — the monitor pair has no C_AAF (180 pF min) | **Confirmed** | schematic | CEXM 220 pF C0G across VREXM_P/N; Thevenin 12.6/14.8 k per leg ≤ 20 k; the 3.0° fixed reference offset is absorbed by FW-20. **F152** |
| F06 | CRITICAL — the ADC-watchdog → PWM-fault OVP route is not demonstrated on the target; a sampled watchdog is not a comparator | **Already Fixed** (gate) | target-integration item since A.12 | the OVP route is an arming-evidence flag (OVP_ROUTE_VALIDATED from the EOL/HIL record); an external comparator only if the measured chain misses the 15.6 µs budget |
| F07 | CRITICAL — no independently closed worst-case SC current-extinction budget | **Improvement Recommended** | the DESAT rows carried detection + soft-off only | explicit budget row (detection + soft-off + fall/tail) per silicon, WARN against the 6 µs/800 V IGBT withstand and the unpublished SiC figure — gate ③ unchanged |
| F08 | CRITICAL — forced SPO / gate-power loss vs the real motor | **Already Fixed** (gate ⑥) | contract §6 + energy rule present | keep_hv now follows the energy dependence (R08); the motor dataset, ASC qualification and contactor retention stay the vehicle-level gate |
| F09 | MAJOR — discharge resistor pulse/fail-open behaviour not established; a shorted QDIS bypasses the timeout | **Confirmed** (detection missing) | reproduced 96 W/part | firmware detects an unexpected discharge at the next contactor opening → latched no-re-energise DTC + contactor-open request; verifier rows per SKU; the benign-failure test stays gate ㉖. **F157** |
| F10 | MAJOR — the 6.2 k divider suits a bounded 850 V contract only; tolerances must be frozen | **Already Fixed** / **Improvement Recommended** | worst-corner row (893 V) existed; bottoms 0.1 %, tops 1 % thin-film in the BOM | INFO row: an extended 920 V variant is a separate release (5.1 k → 1108 V FS, thresholds, calibration, can hot-voltage) |
| F11 | MAJOR — LV clamp/PTC/semiconductor stress needs one source-impedance model | **Already Fixed** (gate ㉗) | — | unchanged; stale "34 V / Ri ≥ 4 Ω covered" BOM texts removed (N03) |
| F12 | MAJOR — production BOM/OTP configuration not fully frozen | **Improvement Recommended** | — | UCC12051QDVERQ1 promoted to the primary MPN (UCC12050 = proto fit); the FS26 OTP image remains §8a; AVL columns are the BOM |
| F13 | MAJOR — sensor sleeve insulation system unreleased | **Already Fixed** (gate ⑩) | — | unchanged |
| F14 | MAJOR — capacitor hot-voltage/ripple/sharing qualification | **Already Fixed** (gate ④) | — | unchanged; bank count kept |
| F15 | MAJOR — cold-plate model unvalidated | **Already Fixed** (gate ④) | — | unchanged |
| F16 | MAJOR — gate-power transformer corner qualification | **Already Fixed** (gate ⑤/⑬) | — | unchanged |
| F17 | MODERATE — CAN termination must follow the vehicle position | **Improvement Recommended** | BOM already marked the endpoint option | variants.md states the population rule; ERC unchanged |
| F18 | MAJOR — motor-temperature line fuse/clamp coordination | **Confirmed** | PESD5V0L1BA is ESD-class | SMAJ5.0A (400 W) carries the ≈ 16 A until FMT opens; fuse MPN bound (EXTRACTED §31); the 1 k limits the buffer to 3.9 mA. **F154** |
| F19 | MODERATE — shared model inputs are not independent verification | **Improvement Recommended** | — | the ball map is now checked against NXP's netlist (a manufacturer-derived golden source); kicad5-verify fails on a mutated pin number; the mutation practice stays |
| F20 | MAJOR — real-time timing not established | **Already Fixed** (target gate) | — | WCET/jitter remain HIL items (firmware README) |
| F21 | MODERATE — safety readiness overstated ("paperwork") | **Confirmed** (wording) | cost-rollup §2 | wording corrected: the safety work products are engineering that can change requirements; "ASIL-D-capable" kept |
| F22 | MODERATE — common 1200 V module not proven lowest-cost for 4XX | **Improvement Recommended** | — | variants.md: 750 V RFQ item and the conduction-loss delta; D3 stays for prototypes |
| F23 | MAJOR — torque_to_current() lacks a final voltage-feasibility witness | **Confirmed** | source order | final |v| check after the clamps; iq then id reduced; infeasible → zero torque + speed-limit request + DTC; motor-map sweep test. **F155** |
| F24 | MODERATE — KCL does not detect a stuck-at-zero channel at zero current | **Confirmed** (wording + coverage) | logical counter-example holds | per-channel activity check above a current threshold; coverage table per operating state in the firmware docs; no fourth sensor. **F156** |

## Review 2 — A.12 recheck (A12-R01…R08, N01–N03)

| ID | Review said | Class | Verification | Action in rev A.13 |
|---|---|---|---|---|
| A12-R01 | CRITICAL — H5 is V15 (1.5 V, 2.75 V abs max), not a SAR reference; A.12 put VREF5 on it | **Confirmed** | DS figure: H5 V15; GEN3 net VCORE | H5 → V15S; manifest, JSON, symbol, ERC lock. **F143** |
| A12-R02 | CRITICAL — J7 is V25 (regulator output, 140–220 nF), A.12 grounded it | **Confirmed** | DS figure: J7 V25; GEN3 net V25 with C79/C80 | J7 → V25 + CV25 220 nF X7R 10 V; ERC lock. **F143** |
| A12-R03 | MAJOR — the symbol's pin numbers are 65/77/94, the ball only in the name | **Confirmed** | `traction-r1.lib` X records | pages.mjs emits the ball as the pin number; kicad5-verify checks every MCU record against the manifest (mutation-detecting). **F144** |
| A12-R04 | MODERATE — CNMOS (1 nF) missing from the ballast network | **Confirmed** | DS Table 13 | CBAL 1 nF C0G. **F145** |
| A12-R05 | CRITICAL — the DESAT ISR drops MCU_GATE_EN at once, bypassing the 22–53 µs hardware delay | **Confirmed** | app.c:390 → br_spo(true) | DESAT hold inside the bridge module for every caller (CAL 60 µs ≥ the RC upper corner + FLT); PWM inhibit immediate; regression tests. **F146** |
| A12-R06 | MAJOR — now_ms = us/1000 of a wrapping counter | **Confirmed** | app.c:33 | 64-bit monotonic clock, ms derived consistently; every /1000 producer audited; wrap tests. **F147** |
| A12-R07 | MAJOR — 76 mA does not cover the production UCC12051-Q1 (80 mA max no-load) | **Confirmed** | SNVSBY2A §6.9 | 96 mA budget; R5L 47 Ω ballast per LDO (LDO ≈ 0.5 W, Tj ≈ 116 °C); UCC12051-Q1 primary. **F149** |
| A12-R08 | MAJOR — keep_hv speed-only while rule (b) applies at every speed | **Confirmed** | safe_state.c:63 | keep_hv from actual reliance on rule (b), held until rule (a) or ASC; through the CAN status. **F148** |
| N01 | the UCC14141-Q1 VDE certificate is issued (40058888) | **Confirmed** | archived (EXTRACTED §32) | gate ㉔ residual closed at component level |
| N02 | the 1 µs ASC-exit parameter alone is not a shoot-through defect | **False Finding** (agreed) | br_exit_asc timestamps after the pulse | no change; physical non-overlap stays a measurement |
| N03 | stale DTVH/DTVL text (34 V, "Ri ≥ 4 Ω covered") | **Confirmed** (docs) | BOM | texts corrected. **F158** |
| lock witness | "everything left is measurement" was too strong | **Confirmed** | — | arming criteria distinguish configured / protected / host-tested / hardware-validated (F151) |
| pin-map completeness | re-derive against an independent source; J6 label ambiguity | **Confirmed** | — | GEN3 net report used as the oracle; J6 = VREFL_SAR_0123, J5 = VSS_DCDC per the netlist |

## Review 3 — second recheck (A12-R01…R07)

| ID | Review said | Class | Verification | Action in rev A.13 |
|---|---|---|---|---|
| A12-R01 | H5 wrong voltage domain | **Confirmed** | as above | **F143** |
| A12-R02 | J7 is V25 | **Confirmed** | as above | **F143** |
| A12-R03 | CAD pin numbers not physical | **Confirmed** | as above | **F144** |
| A12-R04 | DESAT ISR bypass; audit apply_decision/br_rec_start | **Confirmed** | all br_spo(true) callers listed | hold enforced inside the bridge module, so every caller is covered. **F146** |
| A12-R05 | low-speed retention missing from keep_hv | **Confirmed** | as above | **F148** |
| A12-R06 | timebase modulus mismatch | **Confirmed** | as above | **F147** |
| A12-R07 | production bias current | **Confirmed** | as above | **F149** |

## Firmware decisions worth knowing (from the implementation)

- `br_service()` takes no time argument: the DESAT hold starts from a time read AFTER the FLT lines are read, so
  a caller's older timestamp cannot end it early. CTRL2 is not write-locked (every PWM mode change writes its
  FORCE bit, so locking it would block PWM-ASC on silicon); its INDEP bit is read back every 1 ms instead.
- IMCR guards: non-zero values and two different IMCR indices (the two select values sit in different IMCRs
  and may legitimately be equal); placeholders `#error` the target build; REG_PROT offsets/bits and the IMCR
  values stay TODO(RM) until read from the reference manual.
- Missing arming evidence fails INIT: FS0B is never released, FW-16 never runs, MCU_GATE_EN never rises; status
  byte 15 names the missing item; evidence lost while armed goes through the §6 "control lost" row.
- Not implemented: the UDS routine that clears the service lock; the EOL/HIL rig that writes the validation
  record (outside this repository).

## Self-found in this round

| Item | Fix |
|---|---|
| The A.12 verification report rendered the register rows F141/F142 inside every margin-table section (37 copies) — a round-13 script had spliced them into the report's loop template | template restored, register rows placed in the findings log; each row once (F159 in the register text is not needed — tooling) |
| The SPF-91122 text parse disagrees with the GEN3 netlist on one ball (G6: PTB30 vs PTE4) | G6 unused; noted in the manifest; every signal ball now sits on a netlist-confirmed ball |

## Marine

The A.13 deltas were ported into `marine/` (shared card and power PCB: pin map, protection, LDO ballast; shared
firmware behaviours; the UCC14141-Q1 certificate). `npm run marine`: 86 PASS · 22 WARN · 0 FAIL — unchanged
counts, as no sizing formula moved. M8 forks at Road rev A.13.

## What stays open (bench, vendor, OEM, target) — with acceptance criteria

Unchanged gates ②③④⑤⑥⑦⑨⑩⑫⑬⑭⑮⑰⑱⑲ ⑳㉑㉒㉓㉕㉖㉗ (README). New/re-scoped: **㉘** exciter and
motor-temperature terminal-fault bench test (24 V/60 s and 35 V/400 ms on each line, MCU on/off/standby:
PTC trip time, TVS temperature, amplifier and buffer unharmed); **target evidence** for the arming record —
fault-route injection with the CPU halted, OVP chain ≤ 15.6 µs, REG_PROT lock readback, WCET — the firmware
refuses to arm without it. The 4XX/8XX power-stage populations are qualified separately (DPT, SC, thermal).

## Register

F143–F158 in `calculations/design-verify.mjs` (verification-report.md §Findings); the CSV beside this file
lists every review ID with its class.
