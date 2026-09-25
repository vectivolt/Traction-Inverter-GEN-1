# Round 17 — gap closure of rev A.15 → rev A.16

**Instruction.** "Close all gaps" (and "if required feel free to upgrade"): after sixteen review rounds the project
still carried WARN rows that named a bench or a vendor, OPEN rows, firmware items that said "the contract should say",
placeholder parts, and a KiCad hand-off verified only by its own placement rule. This round leaves nothing in that
state: every item below is either closed by a design or document change verified here, or it is one numbered request
to a vendor (`vendor-requests.md`, VR-nn), one numbered assumption about the vehicle, motor or harness
(`interface-requirements.md`, IR-nn) or one executable procedure with pass criteria (`qualification-plan.md`, QP-nn).

**Method.** Every gap was verified against its source before deciding (the datasheet texts in `docs/datasheets/`, the
verifier expressions, the card net list, the firmware source). Several "gaps" turned out to be stale rows or missing
citations rather than missing engineering; several were real and closed with hardware. Nothing was closed by
re-labelling: a row is PASS only where a datasheet statement, a computed margin or a documented decision covers it.

## Gap register

| ID | Gap (rev A.15 state) | Verified as | Closure | Rev A.16 state |
|---|---|---|---|---|
| G-01 | Exciter back-drive with VEXD absent — OPEN: the ALM2402 reverse diodes carry the 4.9 A / 59 µs charge and have no published envelope | real (DS §8.3.6: pulsed use only) | **Hardware**: DEXP/DEXN **Nexperia PMEG4050EP-Q** (AEC-Q101, SOD128, I_FSM 70 A, V_F 0.49 V max at 5 A) from each protected node to VEXD — takes the charge below the internal diodes' knee; NCV4276C output abs max 40 V tolerates the back-fed rail (DS Maximum Ratings) | PASS (row "rated diversion"); the measurement stays in QP as characterisation |
| G-02 | Exciter TVS energy CONDITIONAL on the PTC clearing time; source-impedance allocation ≥ 0.27 Ω | the round-16 rows mixed a single fault (line shorted to KL30 ≤ 24 V) with a load-dump-coincident 35 V case | **Analysis split + upgrade**: single fault — 3.4 J at the 20 ms bound and 37 A ≤ 40 A with zero external impedance; the TVS becomes the 3 kW **SMDJ8.5A-HRA** (AEC-Q101, same SMC pad: ≈ 9 J at 10 ms) so even the 35 V energy at the 20 ms bound (6.3 J) is inside the curve; what remains of the 35 V case is the PTC's 40 A I_max — a double event (a short *during* an ISO 16750-2 test-B pulse), IR-16 / IR-33 | single fault PASS; 35 V TVS energy PASS; 35 V PTC current INFO (accepted double event) |
| G-03 | PTC hold current vs "60 mA assumed maximum" | the 60 mA had no source | **Interface requirement** IR-13: resolver primary ≥ 60 Ω at 10 kHz → ≤ 41 mA rms at the 7.2 V pp setpoint (58 % of the 70 mA hot hold) | PASS |
| G-04 | Resolver drive at 9 V KL30: "≈ 6.5 V pp available vs 8 V pp target" | **stale** (A.4.3 single-ended model): since round 12 both outputs swing ±1.91 V pk around VMID_REX = V5A/2 = 2.5 V | **Recomputed**: the rail only has to exceed 4.6 V; VEXD ≈ 8.3 V at 9 V KL30 | PASS |
| G-05 | Stuck-ON QDIS — "resistor must fail open without flame" (gate ㉖) | the bound RDIS part's sheet states it: TT/Welwyn SQP "will not burn or emit incandescent particles under any condition of applied temperature or overload" | **Citation** + BOM note (the Yageo alternate lacks the statement) + VR-28 for the qualification data + QP fail-open characterisation | PASS (both rows) |
| G-06 | RFS4 at the 24 V jump start with FS1B held: 1.47× its nameplate for 60 s | real (0.48 W in a 0603 rated 0.33 W) | **Hardware**: 1206 anti-surge ESR18EZPF1001, 0.5 W at 70 °C (ROHM ESR series Rev.012 archived) — 0.96× for 60 s, element ≈ 107 °C | PASS |
| G-07 | Hall ratiometric reference (V5A) vs VREF5: ±1–2 % gain drift | bounded, not a defect | **Documented**: EOL gain calibration (FW-20) removes the static ratio; the residual sits inside the ±5 % torque allocation (IR-26); the safety checks do not depend on it | PASS |
| G-08 | KiCad hand-off "not yet verified by an export from a running KiCad instance" | KiCad 10.0.6 is installed: it loads the legacy sheets but resolves no symbols (with or without a sym-lib-table, cache library or project file) — its netlist export lists no components — so the legacy format cannot be CLI-verified in KiCad 9/10 (its JSON ERC report also divides every coordinate by 100 — a report quirk, not a loading error) | **Deliverable upgrade**: `kicad/traction/` in the KiCad 9 format (embedded symbols + project library), converted item by item from the freshly written native KiCad 5 sheets so the two sets cannot drift, and proved by `kicad-cli sch export netlist` against the built netlist on every board with mutation tests; `sym-lib-table` + cache library added to both legacy folders/zips | closed (KiCad section) |
| G-09 | 15 WARN rows that named "a bench" or "a vendor" without a procedure or a question | real (nothing executable behind them) | **Documents**: VR-01…28 with acceptance criteria, IR-01…33, QP procedures with pass criteria; the report's open-inputs section points to them | every remaining WARN names its VR/IR/QP |
| G-10 | Firmware README: 35 "contract contradictions and open items (not silently changed)" | real (decisions taken in code, contract text behind) | **Contract sync** (§10b/§10c), FW-08 decision implemented, UDS service routine for the service lock, `firmware/docs/target-bringup.md` for the silicon/RM items | see the firmware section |
| G-11 | LV input: ISO 16750-2 load dump test B (35 V / 400 ms) not survivable on paper by the SMC TVS + polyfuse | real — and worse than the row said: in the real alternator model (79–101 V source behind Ri with the 35 V central clamp in parallel) any local TVS clamping below 35 V takes the unsuppressed current | **Strategy change to let-through** (LV-input section): every TVS knee above 35 V, every downstream part shown rated for the plateau, a 100 µF hybrid capacitor for the fast pulses, an anti-series pair for the negative pulses | implemented and verified: ERC 965/0, 13 new LV A.16 rows (one WARN left: the FS26 "limited period" duration, VR-29); IR-03 = "central suppression, Us* ≤ 36 V at the pin", no Ri constraint |
| G-12 | Datasheets missing or unconfirmed (Murata BLM31 sheet, TDK automotive beads, Littelfuse SMDJ AEC status, Diodes DMP6023LEQ "Advance Information", Samtec pin numbering) | real | retrieval round: 13 sheets archived, three findings changed the BOM (see the datasheet section) | closed except VR-14 (Murata's literal AEC-Q200 line) and VR-20 (Diodes production status) |
| G-13 | (found by the KiCad proof) the legacy sets numbered the 40-way harness headers JIC/JICC P1–P40 — the MCU ball rule in `pages.mjs` also matched the harness labels P1_…P40_ — against pads 1–40 | real defect in the hand-off (netlist unaffected, the KiCad 5 symbol pin numbers were wrong) | **Generator fix**: the ball rule applies to UMCU only; the modern set takes its numbers from the built netlist | fixed, regenerated (F183) |
| G-14 | (found by the KiCad proof) after the SMDJ8.5A-HRA upgrade the legacy generator drew TVSEP/TVSEN as resistors (its diode list knew SMAJ/SMBJ/SMCJ, not SMDJ), and the new PMEG Schottky needed the same rule | real (symbol only; connections correct) | **Generator fix**: SMDJ / 5.0SMDJ / PMEG classed as diodes | fixed, regenerated (F184) |
| G-15 | (found by the LV study) ISO 7637-2 pulse 1 (−150 V, 10 Ω) avalanches the reverse Schottky DREVC: the TVS sat after it | real defect (654 W vs the STPS5L60S 144 W avalanche rating) | **Hardware**: the clamp moves to the pin node ahead of DREVC (as GEN3 has it) as an anti-series TPSMC33A-VR / TPSMC18A-VR pair — DREVC sees ≈ 38 V of 60 V | fixed |
| G-16 | (found by the LV study) ISO 16750-2:2023 raised the jump start to 26 V / 60 s at T_min; the TPSMC24CA-VR breaks down at 25.1 V at −40 °C and would conduct for the whole minute | real for the 2023 edition (IR-02 is the 2012 24 V) | the 33 V-knee parts are dark at 26 V (34.4 V at −40 °C); the 26 V case is an INFO row and the 24 V rows list what changes at 26 V (RFS4 would need the ESR25) | fixed for the TVS; the OEM's edition is IR-02 |
| G-17 | (found by the LV study) FLVC (MF-LSMF300/24X, 3 A hold at 23 °C = 1.5 A at 85 °C) carries the whole inverter's LV current: 1.73 A at 13.5 V, 2.54 A at 9 V | real defect (nuisance trip hot at low KL30; the old row checked one chain at 23 °C) | **Hardware**: Bel 0680L5000-05 5 A slow-blow fuse (125 V DC) — 2.54 A at 9 V is 0.65 of the 85 °C continuous limit (0.44 at 13.5 V); hot-plug inrush 0.59 A²s vs 36 A²s melting | fixed (F187) |
| G-18 | (found by the LV study) KL15 pulse 1 drives the FS26 WAKE1 pin (−0.3 V abs) far negative through the sense divider; DIGN's 75 V rating is exceeded | real | **Hardware**: DIGN → US1M (1000 V) with RIGN1 behind it, so WAKE1 and the ignition sense sit behind one blocking diode (pulse 1 on DIGN 164 V of 1000 V) | fixed (F188) |
| G-19 | (found by the QP review) the firmware waits ≥ 1 µs after ASC_CLR before the first high-side pulse, but since the A.12 opto change the ASC pins release in ≤ 1.06 µs — overlap possible on SiC | real (shoot-through window: the old firmware fired at 2.0 µs against 2.07 / 3.57 µs deadlines) | **Firmware**: `cal_asc_release_ns` 1500 ns + dead time from the ASC_CLR edge → 4.0 / 5.0 µs; scenario test fails before / passes after; contract §4c | fixed |
| G-20 | (found by the firmware agent) the FS26 watchdog answer lands at ≈ 3.0 ms on the target, at the edge of the 3 ms window (the due test waited ≥ 2000 µs from a stamp taken after the SPI transfer; the host sim's tick grid hid it) | real safety-timing defect | **Firmware**: refresh at the top of the task, due at ≥ 1500 µs (≈ 2.0 ms), exact-grid test that fails on the pre-fix tree | see the firmware section |
| G-21 | (found by the QP review) the sim S10 "ASC command path collapses within ≈ 1 ms" was modelled for the TLP152/QA01C parts and never re-derived | stale basis, same conclusion | **Re-derived** for VOW3120 + UCC14141-Q1 (8 V UVLO) and the round-17 LV bulk: ≈ 170 µF at ≈ 1.05 A gives 6 V/ms, the bias input reaches 8 V in ≈ 0.7 ms — the figure stands with its basis; IR-05/IR-32/IR-33 name the safety-concept assumption | closed |
| G-22 | (found by the QP review) README used gate number ㉔ twice; dfm.md still described the 0603 RFS4 and the MGJ2 purchasing gate; the report's open-inputs list still named the MGJ2 certificate | stale text | parking-drain gate renumbered ㉙ (QP references updated); dfm.md and the report corrected | closed |

## What this round changes in hardware

| Item | Was | Now |
|---|---|---|
| DEXP / DEXN | — | Nexperia PMEG4050EP-Q Schottky (AEC-Q101, 70 A I_FSM), protected node → VEXD (rated back-drive diversion) |
| TVSEP / TVSEN | SMCJ8.5A (1.5 kW; the plain SMDJ has no AEC statement) | **SMDJ8.5A-HRA** (3 kW, AEC-Q101, same SMC pad, I_FSM 300 A) |
| LFH1 / LFL1 / LFC | BLM31PG121SH1L, sheet not on file | same part, sheet archived; no automotive 120 Ω alternate exists (TDK's automotive MPZ2012 has 100/220 Ω only) |
| JIC / JICC pin numbering | "confirm interleaved odd/even" | confirmed **sequential per row** (row A 1–20, row B 21–40) from the Samtec drawing |
| RFS4 | ESR03EZPF1001 (0603, 0.33 W) | ESR18EZPF1001 (1206, 0.5 W) |
| DTVSC | TPSMC24CA-VR on NRC (after the reverse Schottky) | **TPSMC33A-VR at the pin node** (cathode FCO), ahead of DREVC |
| DTVSC2 | — | **TPSMC18A-VR** (anode TVSM, cathode DGND): the anti-series pair clamps pulse 1 / 3a and stays off at −14 V |
| CLVC3 | — | **Panasonic EEH-ZC1H101P** 100 µF / 50 V hybrid polymer on NRC (pulse 2a absorber; 10.5 mm tall — card envelope) |
| FLVC | MF-LSMF300/24X polyfuse (1.5 A hold at 85 °C for a 2.54 A load at 9 V) | **Bel 0680L5000-05** 5 A slow-blow 2410 fuse, 125 V DC (alternate Bourns SF-1206SA500W-2, AEC-Q200, 50 A breaking only) |
| DIGN | 75 V diode; WAKE1 exposed to pulse 1 | **US1M** (1000 V), RIGN1 behind it — WAKE1 and the ignition sense behind one blocking diode |
| DTVH / DTVL | TPSMC24CA-VR | **TPSMC33CA-VR** (dark at the 34 V plateau) |
| ULDO15 | NCV4276CDTADJRKG (DPAK-5: 156 °C at 35 V / 400 ms) | **NCV4276CDSADJR4G** (D2PAK-5: 141 °C) |
| RDIS alternate | Yageo SQP without a no-flame statement | alternate only with the same statement (BOM note, VR-28) |

## KiCad

Three sets now ship: `kicad5/traction` (EasyEDA import), `kicad5/traction-native` (KiCad 5–8; now with `sym-lib-table`
and a cache library) and **`kicad/traction` (KiCad 9/10 format, `Traction-Inverter-KiCad-modern.zip`)**. The modern set
is generated by `kicad-sch-gen.mjs` from the native sheets item by item and proved by `kicad-sch-verify.mjs` with the
installed KiCad 10.0.6 CLI, inside `npm run sheets`:

| Board | Symbols / labels / stubs / no-connects | KiCad netlist vs circuit.json | ERC (failing types) |
|---|---|---|---|
| power | 324 / 927 / 927 / 8 | 324/324 components, 194/194 nets | none (power_pin_not_driven 34 and ground_pin_not_ground 6 reported, expected) |
| capbank | 26 / 42 / 42 / 0 | 26/26, 2/2 | none |
| discharge | 35 / 107 / 107 / 0 | 35/35, 25/25 | none |
| card | 299 / 977 / 977 / 3 | 299/299, 201/201 | none |
| root | 684 components, 433 nets, no net spans two boards | — | — |

All 130 MCU netlist pins are physical balls equal to the manifest. Mutations, each detected by the verifier (KiCad's own
ERC stays clean on a mirrored MCU — only the netlist comparison catches it): mirrored MCU 72 failures, H5/J7 ball swap 3,
legacy harness numbering 62, duplicated reference 6. `footprint_link_issues` (no footprint library ships) is the one
ignored type. KiCad warns "annotation errors" for references that do not end in a digit (UMCU, RMRST…): do not run
Annotate on this set. KiCad's CLI ERC does not check duplicate references and its netlist merges same-named parts —
the verifier checks both itself.

## LV input (load dump)

**Study (Opus agent, analysis before design):** ISO 16750-2 test B is the test-A alternator source (79–101 V behind
Ri 0.5–4 Ω) clipped at Us* 35 V by the vehicle's central suppression. Modelled as a Thevenin source the SMC TVS would
take 11.2 A / 96 J at 0.5 Ω (no SMC sheet rates a pulse beyond 1 ms); modelled as the real parallel clamp, any local
TVS that clamps below 35 V takes the unsuppressed current (70–100 A). A 400 ms-rated absorber (Vishay SM8S24A, DO-218AB)
passes only with Ri ≥ 1.5 Ω guaranteed and still trips the polyfuse inside the pulse; a series limiter dissipates 4–8 W
for 400 ms in a SOT-223 (+55…86 K) or costs an eFuse whose quiescent current breaks the parking-drain budget. The
GEN3 reference (net report + BOM) uses a 22.8 V-breakdown TPSMC24CA before its Schottky with 16–25 V capacitors — no
precedent for test B, but precedent for the TVS position.

**Decision — let-through:** keep every clamp dark at 35 V and show every downstream part rated for the plateau; handle
the fast pulses with bulk capacitance instead of a low clamp.

| Element | Change | Basis |
|---|---|---|
| DTVSC | TPSMC33A-VR at the pin node (cathode FCO, anode TVSM), ahead of DREVC | knee 36.7 V min (36.45 V at 18 °C) ≥ 36 V; zero current at 35 V in both models |
| DTVSC2 (new) | TPSMC18A-VR, anode TVSM, cathode DGND | anti-series pair: pulse 1 (−150 V) 12.5 A / 0.17 J in the TVS, DREVC at 38.3 V of 60 V; pulse 3a (−220 V) DREVC ≈ 37 V; −14 V reverse: still off (18.86 V at −40 °C) |
| DTVH / DTVL | TPSMC33CA-VR | the 24 V parts would absorb at 34 V |
| CLVC3 (new) | 100 µF / 50 V hybrid polymer (Panasonic EEH-ZC1H101P, 28 mΩ, 125 °C, AEC-Q200) on NRC | pulse 2a (+112 V, 2 Ω, 50 µs): the rail peaks at 22.8–36.2 V (td to 10 % / 50 %), under the 40 V boost abs max and under the TVS knee; the bead LFC is transparent at pulse frequencies |
| ULDO15 | NCV4276CDSADJR4G (D2PAK-5) | 35 V for 400 ms at the verifier's 0.356 A: 156 °C in DPAK at 85 °C ambient (fails 150 °C), 141 °C in D2PAK |
| Downstream at the 35 V plateau | FS26 VSUP 34.6 V (36 V "high-voltage extended operation", load dump named), TPS55340-Q1 34.1 V (38 V rec / 40 V abs), V15B 33.7 V, DB15 reverse 33.7 V of 40 V, flyback drain 56.3 V of 72 V, UCC28C40 VDD zener/resistor 0.13 / 0.12 W, QLVS gate held at 15.6 V | all pass |
| Load at 35 V | 0.94 A whole inverter — the input protection never trips, so its 24 V V_max never applies | |

The verifier gains thirteen rows (knee vs Us*, plateau current, each downstream rating, the pulse 2a closed form, pulse 1
and 3a on DREVC, the 26 V/2023 jump start, −14 V reverse, DTVH/DTVL off) and the two old load-dump rows become one
informational absorb table. **Interface requirement IR-03**: the vehicle provides central load-dump suppression with
Us* ≤ 36 V at the inverter pin; no source-impedance constraint remains; test A (unsuppressed dump) is not supported —
in a BEV the 12 V DC/DC provides the suppression.

Found on the way and fixed in the same change (G-15…G-18): pulse 1 avalanched DREVC; the 2023 26 V jump start opened
the 24 V TVS at −40 °C; the input polyfuse was undersized for the whole inverter at 9 V; KL15 pulse 1 reached the FS26
WAKE1 pin. Still measured or asked (QP-LV-03, VR): ULDO15 at the real V15 current at T_max (35 V/400 ms × 5), the FS26
"limited period" duration, flyback regulation with 34 V in, the hybrid capacitor's pulse-current life over 500 pulse-2a
hits, TI's VIN 32 V pin text vs the 38 V table, and the firmware treating the FS26 VSUP-overvoltage interrupt as
information, not a fault.

## Firmware

All 35 items of the firmware README's "contract contradictions and open items" list are closed — 30 by contract text
(§10b/§10c now state the implemented decision: V5GD on PTD27, the eFlexPWM fault route, 2·f_sw loop, self-test before
precharge, the CAL floors and reserves, the §6 rows for V_DC-invalid and unknown speed, FW-08b/FW-11 wording, the
MCU_GATE_EN hold, the amplitude planes and RSX position, the corrected |H| ≈ 2.07 / 1.91 V pk), 2 by code plus contract,
3 into one silicon/RM checklist:

| Item | Closure |
|---|---|
| FW-08 decision (item 26) | below n_x with the battery path lost the inverter applies **zero current** (id = iq = 0, no field-weakening d-current) and INV_STATUS reports 0 Nm with the zero-torque bit; the DC-link trim (`dcl_trim`) runs only in RUN/DERATE with the battery path proven, takes back at most 50 Nm of regen above V_DC max and never adds motoring torque; the pre-fix tree reported −50 / +17.5 Nm under the "zero torque" row and set id −52 A at 7000 rpm |
| Service lock (item 19) → **FW-32** | UDS server: SecurityAccess 0x27 (4-byte seed/key; the key function is a build-time hook, empty by default → every seed refused, NRC 0x22; three wrong keys lock out until restart) and RoutineControl 0x31 routine 0xF010 (NRC 0x33 without unlock, NRC 0x22 with HV present/unknown or the bridge armed); a clear rewrites the NVM record and logs a DTC, effective at the next power-up; CAN IDs 0x7E1/0x7E9 and the routine id are placeholders until the OEM diagnostic spec binds them; a random seed from the HSE is checklist item T-35 |
| Current-loop liveness (item 35) → **FW-31** | named in the contract |
| Silicon / RM checklist | `firmware/docs/target-bringup.md`, 38 rows T-01…T-38: every TODO(RTD)/TODO(RM)/TODO(HW-RM) marker with file:line, what to read or measure, the acceptance check and what keeps the image from arming; `make target-check` fails on a marker without a row or a row without a marker |
| Image identity | `TI_FW_ID` 0x0A0F0011 — a new EOL/HIL validation record is required before this image arms (calibration record stays layout 2) |
| Tests | 243 / 2192 → **256 tests / 2310 checks / 0 failed** (host, ASan/UBSan, −O2 from a clean build directory; target-check 47 markers each with a checklist row); round-start snapshot 253 run / 52 failing checks, all inside the 13 round-17 tests; 17 mutations (ASC wait, LV logic, watchdog due time, DC-link trim) all caught |
| FS26 watchdog timing (self-found) | on the target the answer landed at ≈ 3.0 ms, at the edge of the window (`fs26_wd_due()` waited ≥ 2000 µs from a stamp taken after the SPI transfer; the host sim's tick grid hid it at 2.03 ms). **Fixed**: refresh at the top of the 1 ms task, due at ≥ 1500 µs — answers exactly 2000 µs apart on all four SKUs, design band 1890–2110 µs inside the FS26 open window 1.579–2.857 ms (margins 311 / 747 µs); the old firmware asserted FS0B when the FS26 oscillator ran 5 % fast; exact-grid test fails before / passes after |
| ASC exit vs the A.12 opto release (G-19) | new CAL `cal_asc_release_ns` = 1500 ns (range 1070–5000, never below the verifier's 1.07 µs release); the first high-side pulse waits that plus the dead time from the ASC_CLR falling edge — 4.0 µs (SiC) / 5.0 µs (IGBT) against deadlines of 2.07 / 3.57 µs; the old firmware fired at 2.0 µs, short of both. Contract §4c steps 2–3 now quote the verifier's Safety A.8 row (LS start ≥ 4.42 µs, entry ≤ 7.56 µs, release ≤ 1.06 µs) |
| LV supply supervision → **FW-33** (for the let-through LV input) | VSUP read through the FS26 AMUX (VSUP/14, configured at boot, read every 1 ms; an event starts above 20 V, ends below 19.5 V). Information only while inside the vehicle profiles: load-dump band above `cal_vsup_jump_max_v` 27 V tolerated for `cal_vsup_ld_ms` 500 ms (range 400–1000), jump-start band ≤ 27 V for `cal_vsup_jump_ms` 65 s (60–120 s) — RUN continues, torque unchanged, `DTC_LV_OVERVOLTAGE` stamped for the duration; beyond a band `DTC_LV_OV_SUSTAINED` and the §6 command-lost ramp (torque to zero, outputs off below n_x, no FAULT, recovery when KL30 recovers); no reading → `DTC_LV_VSUP_UNKNOWN`. 27 V not 26 V because the 2023 jump start (26 V) reads up to 26.5 V at the AMUX worst case. Tests: 35 V for 400 ms stays in RUN at 100 Nm; 35 V past 500 ms ramps and recovers; 24 / 26.5 V for 60 s information only, sustained at 65 s. Checklist T-39: AMUX reading and profiles on the bench |
| Harness fix (self-found) | the exact-timing harness sampled currents in lock-step with the rotor, a constant rounding offset the current controllers integrated into a false resolver-rate fault after 3.4 s at 1000 rpm — a fixed ±0.55 A noise pattern on the simulated phase currents; no existing expectation changed |

Wording decisions recorded in the contract: the overcurrent-trip clear is a VCU fault reset below n_x (the old "through
FW-15" did not match the code); a lost DESAT record across a brown-out is an accepted residual risk (A/B keeps the
previous one); FW-21 is the bootloader + HSE deliverable; the FS26 OTP comparison belongs at EOL.

## Datasheets

Thirteen sheets archived (97 PDFs on file), extracted in `EXTRACTED-PARAMS.md` §33 (updated), §35 (pin numbering), §38, §39:

| Item | Finding | Consequence |
|---|---|---|
| Murata BLM31PG121SH1L | spec obtained from a distributor mirror: 3.5 A at 85 °C / 2 A at 125 °C, DCR 20 mΩ, application code "Automotive Usage: Powertrain/Safety"; no literal AEC-Q200 line | bound as is; VR-14 asks Murata for the line |
| TDK MPZ2012 automotive catalog | AEC-Q200 catalog lists 30/100/220/330/600/1000 Ω — there is no 120 Ω automotive MPZ2012; the T000/TD25 suffix is packaging, the grade is the catalog | LUB/LVB/LWB alternate MPZ2012S221ATD25 confirmed; LFH1/LFL1/LFC have no TDK drop-in (stated in the BOM) |
| Littelfuse SMDJ | plain SMDJ: no AEC-Q101 statement; **SMDJ-HRA**: "automotive grade AEC-Q101", identical electricals, I_FSM 300 A, ≈ 0.9–1 kW at the 10 ms end of the curve | TVSEP/TVSEN → SMDJ8.5A-HRA (G-02) |
| AEC-Q101 Schottky for DEX | PMEG4050EP-Q (70 A, 0.49 V at 5 A, 300 µA at 40 V/25 °C) chosen; SBR3U40P1-7 (75 A but 40 mA leakage at 125 °C) second; SS3P4HM3 (50 A) weakest; the onsemi codes in the brief do not exist | DEXP/DEXN bound (G-01) |
| Diodes DMP6023LEQ-13 | DS39935 Rev 1 (2017) still "Advance Information", byte-identical re-download | VR-20; the part ships from stock |
| Bourns MF-MSMF /33X | no statement on transients above V_max; the time-to-trip curve is a graph only | VR-15/16 (informational — the 35 V case is a double event) |
| Littelfuse TPSMC-VR | the archived sheet is the -VR series: 10/1000 µs 1.5 kW only, curve to 1 ms, no ISO 16750-2 / 7637-2 statement; Littelfuse markets a separate TPSMD-FL "FlatSuppressX" 3 kW AEC-Q101 family for test B (search evidence only — the site blocks downloads) | the LV-input study decides (G-11) |
| TT/Welwyn SQP · Yageo SQP | TT p.1: "will not burn or emit incandescent particles under any condition of applied temperature or overload"; Yageo: "flameproof ceramic case" only; no 10 W family states an engineered fail-open (Bourns FW / TE FWFU do, ≤ 7 W) | G-05: no-flame closed by the TT statement; fail-open time is a QP characterisation |
| Samtec IPL1 double row | engineering drawing Rev AL Fig. 1: numbering sequential per row | JIC/JICC note corrected; IR-19 |


## What stays open

Only items that are external by nature, each numbered: vendor answers (VR-01…32 — one verifier WARN remains for the
FS26 "limited period" duration at 36 V, VR-29), OEM/motor data (IR-01…41, incl. the eight inputs the qualification plan
needs: EMC classes, insulation levels, pollution degree, ISO 16750-3/-4 profiles, sleep current at temperature, CAN DBC,
negative exciter fault allocation, coolant flow) and the physical qualification tests (92 QPs). The verifier's fifteen
remaining WARNs are: module thermal at the 30 s peak (QP-TH-01/02, VR-27), turn-off overshoot (QP-PS-01, VR-01), the
4XX can (VR-12), the VCC2 window and low corner (QP-GD-01), gate power at the 20 kHz option (QP-GD-03), DESAT and
short-circuit extinction on both silicons (VR-02/03/07, QP-SC-01…03), the IGBT short-circuit condition (VR-03), the SPO
freewheel energy for both SKUs (IR-06…10), the ASC path during VCC2 UVLO (VR-09, QP-SF-07) and the FS26 plateau
duration (VR-29). No verifier row, firmware item or BOM line points to an unnamed "bench" or "vendor" any more.

Counts at the end of the round: ERC 965 · design-verify 157 PASS / 15 WARN / 0 FAIL / 23 info · sim 23 / 6 / 0 ·
2057/2057 pins in the three sheet sets (KiCad 5 EasyEDA-import, KiCad 5 native, KiCad 9/10 proved by kicad-cli) ·
686 components / 236 BOM lines · 8XX SiC ₹72,981 @1k (+₹78 for the LV entry, +₹30 for the exciter parts) · 104
datasheets · firmware 256 tests / 2310 checks, `TI_FW_ID` 0x0A0F0011 (new EOL/HIL record required).
