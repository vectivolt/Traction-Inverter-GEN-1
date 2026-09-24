# Review round 13 — rechecks of `30ae0a2` and the production closure (rev A.12)

**Inputs.** Two targeted rechecks of rev A.11 (`A11_recheck.html` with findings A11-R01…R06 and A11-N01; a
second recheck with its own A11-R01…R06 and two notes), plus the standing instruction to close every gap
that can be closed at schematic/BOM level. Both rechecks retain all A.11 corrections and ask for no
power-stage change.

**Method.** Each claim was checked against the source and the datasheets (S32K39 DS Rev. 3 Tables 3/38/40,
TPS55340-Q1 SLVSBV5C, NCV4276C thermal table, FS26 Table 3, Littelfuse TPSMC-VR). The pin freeze was done from
NXP's own GEN3 control-card schematic (SPF-91122 rev C) on the user's disk, parsed ball by ball and anchored
on the datasheet's supply balls. Four Sonnet look-ups bound the remaining CLASS parts; an Opus agent ported
Road A.9–A.11 into Marine; an Opus agent wrote the firmware (`firmware/`).

## Verdict

Both rechecks are right on every point they raise. All are fixed at zero or near-zero BOM cost: the safety
contract now applies the winding-energy rule at every speed; the resolver input bound holds with the MCU
unpowered and the SDADC gets its datasheet anti-alias capacitor; the verifier counts distinct assemblies; the
boost's real (Q1) ratings replace the non-Q1 figure that had suggested an upgrade; the LDO thermal row uses the
datasheet reference pad; the exciter amplitude and the TVS long-pulse cases are stated as the conditional
items they are. On top of that, rev A.12 closes the fabrication blocker (the MCU ball map) and binds every
barrier-relevant CLASS part to an orderable MPN with a published rating. What remains open is measurement
and vendor evidence, listed at the end with acceptance criteria.

**"Already Fixed" for a gate means the gate exists and is open** — it is never a claim that the physical
qualification is complete (both rechecks asked for this to be said plainly).

## Recheck 1 (A11-R01…R06, N01)

| ID | Review said | Class | Verification | Action in rev A.12 |
|---|---|---|---|---|
| A11-R01 | CRITICAL — rule (a) was fixed but the §6 matrix is still speed-split, the KL30 row is back-EMF-only and rule (b) applies only at n ≥ n_x; standstill freewheel of the 0.35 mH motor at 340 A rms reaches 1065–1089 V | **Confirmed** (contract) | Reproduced: our own round-12 row is the zero-back-EMF case (61 J → 1089 V from the 880 V trip). Winding energy does not depend on speed. | §6: a column note makes the energy condition apply in both columns; the battery-path-lost row at n < n_x now uses FW-06 LS-ASC as the sink if the link crosses the trip while current flows; the KL30 row cites rule (a) instead of the back-EMF test; rule (b) applies at every operating point where (a) fails and is barred for rows whose premise is the lost battery. **F135** |
| A11-R02 | MAJOR — the 10 k injection bound assumed a powered 5.7 V clamp; unpowered (0.7 V clamp) 35 V gives 3.42 mA; both bias legs pull VMID (6.5 mA at 35 V) | **Confirmed** (margin) | (35 − 0.7)/(0.99 × 10.12 k) = 3.42 mA > 3 mA (the S32K39 operating AND absolute limit). | RSINF/RCOSF and RSIN/RCOS → **12 k**: 2.92 mA at 35 V into a 0 V node with −1 % resistors; VMID buffer 5.4 mA. Computed row; ERC values. **F136** |
| A11-R03 | MAJOR — the SDADC needs C_AAF 180 pF min / 220 pF typ directly across its inputs; the round-12 100 pF is below it | **Confirmed** (datasheet condition) | S32K39 DS Table 38: R_AAF 5–20 kΩ, C_AAF 180–220 pF. | CSINA2/CCOSA2 → **220 pF C0G** at the pins; corner ≈ 23 kHz (22.7), −24° on both channels; the excitation-monitor path carries the phase reference. ERC values. **F136** |
| A11-R04 | MODERATE — "ratio-cancelled" overstates it: Z_DIFF 215–380 kΩ gives up to 1.09° at independent corners | **Improvement Recommended** (wording) | Reproduced: 1.09° in the recheck's phase form; the verifier's amplitude-ratio form on the same Z_DIFF corners gives 1.29°, and the larger is carried. | Row and comments now say "cancels only as far as the channels match — an EOL calibration item (FW-20), bound 1.3°". |
| A11-R05 / N01 | MODERATE — the verifier counts JSON pages, not distinct assemblies | **Confirmed** (tooling) | Static reading confirmed. | Fails unless exactly {power, capbank, disch, card} are compared, none missing, none duplicated, ≥ 1500 pins. **F137** |
| A11-R06 | MODERATE — the fitted TPS55340QRTERQ1 is 38 V recommended / 40 V absolute; the 34 V (and an older 45 V) figures were wrong and the "≥ 42 V boost" suggestion a false premise | **Confirmed** (documentation) | TPS55340-Q1 SLVSBV5C §6.1/6.3 (archived): 40 V abs, 38 V rec. | Verify model, BOM text and the gate ㉗ rationale corrected; the commercial part marked PROTO ONLY (34 V). **F138** |
| §8 exciter | 8 V pp is not guaranteed: the SWG MAXAPP low corner gives 6.98 V pp, low-low corners 6.5 V pp | **Improvement Recommended** (specification) | Reproduced. | The row states 8 V pp as a target and ≥ 6.5 V pp as the resolver-selection minimum; gate ㉕ carries it. |
| §8 UCC12050 grade | the catalogue part is not AEC-Q100; the -Q1 alternate costs +₹260 | **Improvement Recommended** | Correct. | The BOM names UCC12051QDVERQ1 as the production AVL part and the UCC12050 as the proto fit (consistent with every other -Q1 part on the card). |

## Recheck 2 (A11-R01…R06 and notes)

| ID | Review said | Class | Verification | Action in rev A.12 |
|---|---|---|---|---|
| R01 | CRITICAL — low-speed energy gap; rule (b) at n ≥ n_x only; battery retention cannot be credited where the premise is a lost battery | **Confirmed** (contract) | Same as A11-R01 (their 712.8 / 1089.4 V reproduce our row). | See A11-R01. **F135** |
| R02 | MAJOR — injection with an unpowered MCU (3.42 mA); the PESD2IVN24 sits behind the 10 k and cannot help | **Confirmed** | Same as A11-R02. | 12 k. **F136** |
| R03 | MODERATE — the boost upgrade argument used the non-Q1 datasheet | **Confirmed** (documentation) | Same as A11-R06. | **F138** |
| R04 | MODERATE — the LDO thermal PASS used 40 K/W; the NCV4276C DPAK reference pad is 58.5 K/W (1.14 in²) / 75.1 K/W; the 50 mA is typical | **Confirmed** (model) | NCV4276C thermal table (archived). | Row: 76 mA worst (50 mA × 1.2 + AMC1311), 0.76 W, 58.5 K/W → Tj ≈ 129 °C at 85 °C (WARN); layout rule ≥ 1.2 in² 2 oz copper per LDO (dfm §4); measured at the hot first article. **F139** |
| R05 | MODERATE — exciter topology accepted; 8 V pp conditional; ALM2402 cold slew close | **Improvement Recommended** | Same as recheck-1 §8. | Target/minimum stated; gate ㉕. |
| R06 | MAJOR — the TPSMC-VR pulse curve stops at 1 ms; no 400 ms case, 4 Ω included, is "covered" | **Confirmed** (wording) | Correct. | Both TVS rows are WARN with "no datasheet rating beyond 1 ms"; gate ㉗ text corrected (the boost is not the limiter; the OEM's Ri and supplier long-pulse data or a test are). |
| Hall timing | 0.70 ms was the decay to 0.3 V; the window edge is 0.2 V → 0.83 ms | **Confirmed** (wording) | Correct. | Row recomputed to the 0.2 V edge. |
| Verifier | as A11-R05 | **Confirmed** | — | **F137** |

## Production closure (what rev A.12 binds beyond the rechecks)

| Item | Was | Now | Evidence |
|---|---|---|---|
| **MCU ball map** (gate ①, the fabrication blocker) | symbolic pin1…pinN | all 289 balls of the S32K396 289-MAPBGA bound: 130 connected (63 signal, 67 supply/ground), 159 open; label `<ball>_<signal>` on the sheet | `calculations/mcu-ballmap.json`, `docs/mcu-pin-manifest.md`; parsed from SPF-91122 rev C sheets 8–13, anchored on the DS balls J6 E7 B8 E9 G13 E11 F1 H5; ERC asserts label/net/function per ball |
| Port corrections found by the freeze | PTG10/PTA6/PTA7 low sides; PTB0 V_DC2; PTB4/PTB5 HW_ID/V5GD; PTB2/PTB3 NTCs; PTA10 NTC_A; PTA11–13; PTF5 | PTA6/PTA7/PTC8 = PWM_1_B[0..2]; PTE1; PTC6/PTD27; PTE2/PTE5; PTD29; PTE6/PTA15/PTE18; PTE23 | each new ball's function list (manifest table) — PTG10 had no PWM, PTB0/PTB4/PTB5/PTF5 no ADC, PTB2/PTB3 are mux-address outputs, PTA10 is JTAG_TDO, PTA11–13 are absent on this package |
| Gate ⑯ (FAULT inputs) | assumed | PTC26 = PWM_1_FAULT[0], PTC25 = PWM_1_FAULT[2], same eFlexPWM instance as the six PWM outputs | manifest; ERC |
| FS26 pins | "VERIFY vs DS" | all 48 + EP verified against FS26 DS Table 3 | this round |
| 40-way harness (JIC/JICC) | HARNESS-2x20-CLASS | Samtec IPL1-120-01-L-D-K header (both boards) with IPD1-20-D-K housings and CC79L-2024-01-L crimps: −55…125 °C, 3.8 A/pin at 95 °C, positive latch; TE AMPMODU Mod II (6-102618-8 / 3-87456-6 / 281556-2) as the alternate | look-up of eight families: Micro-Fit 3.0 (24 circuits max, 3 mm), Milli-Grid (2 A), Micro-Lock Plus (26 max), JST PHD/PUD (85 °C), DF11 (34 max, 85 °C), WR-MPC3/WR-WTB (pitch / 2 A) fail a hard requirement; **no 2.54 mm dual-row family states AEC-Q200** — Samtec's A-Series (IATF 16949, PPAP L3) MPN and the pin-numbering scheme are confirmed at layout (dfm §4). **F142** |
| 4XX DC-link can (gate ⑧) | FILM-50uF-600V class | Faratronic C3D1U506KFAA382 (LCSC C783662), alt Vishay MKP1848C65060JP5 | look-up; PO datasheet check for the 5/10 kHz ripple rating. **F142** |
| ASC / discharge optos (gate ⑪) | TLP152 (no V_IORM) | Vishay VOW3120-X017T (V_IORM 1414 Vpk, DIN EN 60747-5-5, CPG ≥ 10 mm) drawn by pin on both boards; LED resistors 270 Ω for its 1.0–1.6 V V_F (10.8–15.8 mA); its UVLO adds a default-OFF; entry 7.56 µs, release 1.06 µs re-verified | `VOW3120.pdf`, EXTRACTED §27; ERC by pin. **F141** |
| ASC / discharge biases (gate ㉔) | QA01C-18 (hipot only) | TI UCC14141-Q1 (reinforced, V_IORM 1414 Vpk / V_IOWM 1000 Vrms, 8–18 V in) in the single-output configuration, 62 k/10 k → 18.0 V (17.4–18.6 V); shared cell `IsoBias18`; QDIS gate 11.6–16.3 V, RASCG 88 mW re-verified. Certificates listed "planned" in SLUSF10B §7.6 — the one residual of gate ㉔ | `UCC14141-Q1.pdf`, EXTRACTED §28; ERC by pin (all 36). **F141** |
| Y-caps (gate ㉔) | Y1 class | Vishay VY1472M63Y5UQ6TV0 (Y1 500 VAC / X1 760 VAC / 1500 VDC); Murata DE1E3RA472MJ4BP01F alternate | `VY1-series.pdf`, `DE1-RA.pdf`, EXTRACTED §29. **F141** |
| Discharge resistors | WW-10W class | TT/Welwyn SQP10-470RJB15 / -220RJB15 (alt Yageo SQP10AJB-470R / -220R) | look-up; `Yageo-SQP.pdf`, EXTRACTED §30; gate ㉖ (stuck-ON test) stays. **F142** |
| V_DC bias production part | UCC12050 | UCC12051QDVERQ1 on the production AVL | BOM |
| Marine | fork at A.8 | fork at **A.12** (rounds 7–13 ported: the A.9–A.11 fixes by one agent, the A.12 deltas by another): winding-energy rule at every speed, VOW3120 / UCC14141-Q1 / VY1 barrier rows PASS for M8 and M10 (V_IOWM 1414 VDC ≥ 1150 V), resolver 12 k / 220 pF, HW_ID 30 k, M8 can MPN | `marine/` — `npm run marine` 86 PASS · 22 WARN · 0 FAIL (was 83/25/0) |
| Firmware | contract only | `firmware/` (C11, no heap, bounded loops; 14.2 k lines): safety state machine, fault manager, FS26 SBC handling, sensing, FOC (double-update), resolver, CAN, discharge, NVM, four SKUs. `make test`: **178 tests, 1209 checks, 0 failed** (also under ASan/UBSan and -O2); `make target-check` compiles the five S32K396 platform files. Traceability FW-01…FW-20 → tests in `firmware/docs/traceability.md`. Needs the target/HIL: FW-05/06 ADC-watchdog → PWM-fault route and the 15.6 µs, FW-15 PTC26/PTC25 routing and register write-protect, FW-12 on silicon, FW-10 SDADC/SWG latency, WCET and the 62 CAL values; FW-21 (signed images) needs a bootloader + HSE — not implemented. The agent's ten contract ambiguities are listed in `firmware/README.md`; three were contract defects and are fixed (V5GD ball PTB5 → PTD27, "eTPU" → eFlexPWM fault input, FW-05 safe state) | this round; `firmware/README.md` |

## What stays open (bench, vendor, OEM) — with acceptance criteria

②③④⑤⑥⑦⑨⑩⑫⑬⑭⑮⑰⑱⑲ and ㉕–㉗ as listed in the README, unchanged in substance; ① ⑪ ⑯ are closed; ㉔ is
closed by the UCC14141-Q1 / VY1 bindings except for the PO-time check that TI's "planned" VDE/UL certificates
have issued. Every one of them is a measurement or a supplier statement; none is a schematic change. What
is still deliberately NOT claimed: AEC-Q qualification of the VOW3120 (UL/VDE/CQC only) and of the harness
connector (no 2.54 mm dual-row family publishes AEC-Q200); both are stated in the BOM.

## Register

F135–F142 in `calculations/design-verify.mjs` (verification-report.md §Findings); the CSV beside this file
lists every review ID with its class.
