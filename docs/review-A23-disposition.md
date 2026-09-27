# Review round 24 — four rechecks of `4ed0101` (rev A.23)

**Inputs.** Four independent rechecks of the A.22 push `4ed0101b…` (27 September 2026): (1) a closure recheck of the five carried
findings A20-F01…F05 with a standalone C extraction of the flat-map torque solver (eight four-quadrant cases at −O2 and
ASan/UBSan, a 20 000-case screen, an independent MTPA minimisation); (2) a delta recheck that reproduced a MAJOR firmware
defect in the temperature acquisition path with the exact `temp.c` (Git blob 04583c24…, GCC −O0/−O2, Clang ASan/UBSan, 63
assertions per build); (3) and (4) — the same report received twice — a recheck with a 4 049-case torque-vector fixture
(25 214 assertions, an independent double-precision oracle) and the SDADC reference-margin finding; plus a status register
with a MODERATE finding on the inductance-map check and the ripple limiter (an independent double-precision oracle). All four
keep the A.22 corrections and ask for no topology, MCU, capacitor-bank or brake-chopper change.

**Method.** Every claim to the source and the archived data sheets before any edit. The SDADC claim was checked in
`docs/datasheets/S32K39.pdf` (Rev. 3, 03/2024, 119 pages) by extracting §13.2 Table 38: the row "VREFP External reference
positive voltage: AVDD − 0.025 … AVDD + 0.025 V" is there, on printed page 54, and `docs/datasheets/EXTRACTED-PARAMS.md` had
recorded "VREFP = AVDD ±0.025 V" since the pin freeze. The FS26 figures (VLDOx_VREF_match ±1 %, VREF_ACC ±0.75 %,
VLDOx_ACC_NORMAL ±1.75 %, COUT_VREF 1.1 / 2.2 / 3.3 µF, IREF 0–30 mA) were read from `docs/datasheets/FS26.pdf`. Firmware by
two Opus agents with fail-before/pass-after tests; hardware and documents by hand; every change proved by the generators,
the ERC and pin-freeze locks, the KiCad proof and the firmware suite in three flavours and under QEMU.

## Verdict

**The reviewers are right on all three findings, and one of them is a wrong disposition of ours.** The SDADC's own table
requires its reference within ±25 mV of its supply; the round-23 row judged the general +100 mV note and called the figure
absent (F220 was closed on that basis). The fix is a net change with no new part: every non-R2R reference group on V5A, the
node of the SDADC supply (F240). The temperature-acquisition freshness defect is real and reproducible (F241). The map check
bounded the inductance but not the flux slope, and the ripple limiter looked only at its interval's ends (F242). Everything
else the reviewers examined — the R2R separation, the SMT SWD land, the torque solver and the MTPA fallback, the BOM intro,
the KiCad-10 handoff — is confirmed by their own executed checks.

**"Already Fixed" for a gate means the gate exists and is open** — never that the physical qualification is complete.

## Review 1 — the five carried findings, rechecked (closure register)

| ID | Review said | Class | Verification | Action in rev A.23 |
|---|---|---|---|---|
| A20-F01 | still open: the disposition used the general/SAR window; NXP S32K396 DS Rev. 5 §13.2 Table 41 p. 54 — and Rev. 3 Table 38 p. 54 — give VREFP = AVDD ± 0.025 V for the SDADC; FS26 matching ±1 % ≈ ±50 mV is twice that; the supply/reference nets are unchanged (E12 V5A; E10/F13/E6/H6 VREF5) | **Confirmed** — and our round-23 row was wrong: the archived Rev. 3 has the row (Table 38, p. 54) and our own extracted-parameters file recorded it | `docs/datasheets/S32K39.pdf` §13.2 Table 38 (pdftotext, printed page 54); EXTRACTED-PARAMS.md "Max ADC input"; FS26 Table 108 | E6/H6/E10/F13 → V5A (= E12 VDDA_SDADC = VDD_HV_A), the ratiometric loads (RHWP, ROF1, RSN/RMT/RT..P pull-ups) with them, CMA1/CMA2 on the V5A reference island, CSB5 2.2 µF, CR2R1 330 nF, the R2R ladder keeps the FS26 VREF as its isolated supply; the verifier row judges |VREFP − AVDD| (IR drop < 1 mV) against 25 mV; VR-34 closed by design; layout rule in the handoff §4.11. **F240** |
| A20-F02 | source fix confirmed, qualification open: A9 on VR2R behind 10 Ω with 1 µF + 100 nF (τ 11 µs, 14.5 kHz); keep VR-35 / QP-RX-01 | Already Fixed (round 23); the branch is re-sized this round so the FS26 VREF holds its window alone | `boards/control-card.tsx` RR2R/CR2R1/CR2R2; verification-report "LV A.4" | CR2R1 330 nF (corner 37 kHz; the DC drop at the assumed ≤ 1 mA unchanged, 10 mV); VR-35 and QP-RX-01 kept |
| A20-F03 | closed for the flat-map/no-LUT path: eight historical cases return ±100 N·m within 6 µN·m; 20 000 cases, 0 postcondition failures, 0 bound violations, max excess 0.15 mN·m | Already Fixed (round 23, F217) — confirmed by the reviewer's executed fixture | `eight_cases_O2.csv` (the reviewer's file), `firmware/tests/test_torque*.c` | none; the reviewer's note that the status frame's applied torque is model-derived, not shaft torque, is already the contract's wording (§10e) |
| A20-F04 | closed for the constant-inductance example: 246.30612 A vs the independent 246.30611 A | Already Fixed (round 23, F218) | `firmware/src/control/torque.c` | none |
| A20-F05 | closed: `SKUS[SKU].intro`, four headers checked | Already Fixed (round 23, F219) | `docs/bom*.md` | none |
| "final schematic/BOM freeze: hold for F01" | — | **Confirmed** as the gate; released by F240 | verification-report "LV A.4" | this round |

## Review 2 — the temperature-acquisition defect (A22-R01, MAJOR) and the confirmations

| ID | Review said | Class | Verification | Action in rev A.23 |
|---|---|---|---|---|
| A21-R01 (CAD version) | corrected at the generator/delivery level: no footprint library in the legacy sets, KiCad 10.0.6+ named | Already Fixed (round 23, F216) | `calculations/kicad-fp-gen.mjs` | none |
| SWD header | `Samtec_FTSH-105-01-L-DV-K`, attr smd, ten SMT pads — corrected | Already Fixed (round 23, F215) | `kicad/traction/traction.pretty` | none |
| R2R filter | topology present; 14.47 kHz ideal corner; qualification conditional; note 7's RF-AC qualification: keep VR-34 | Already Fixed (round 23); VR-34 now closed by design (the references are on the supply node) | verification-report "LV A.4" | see A20-F01 |
| A22-R01 | `hal_adc_read()` returns the cached code and timestamp and `s_seen` stays true after the first conversion; `sense_slow()` discards both and passes task time to `temp_update()`; `accept()` re-stamps `last_ms` — a stopped channel (one conversion at 1 ms, nothing for 60 s) stays valid, TEMP_OK, at its old temperature; open/short/rate detection on genuinely new codes still works (the negative control); affected TMOD_U/V/W, NTC_H, NTC_A, MT1/MT2 | **Confirmed** (the mechanism read in the source: app.c sense_slow, temp.c update_one/accept, the HAL cache) | `firmware/src/app/app.c`, `firmware/src/sense/temp.c`, the HAL | a three-state acquisition contract per channel — new sample / held sample within a hold time / expired sample → invalid with a diagnostic and the invalid-temperature response — with the per-channel acquisition timestamp carried into the module and a hold-time CAL (cal_temp_hold_ms 10 ms of the 1 ms slow schedule, range 3–50 ms; the ceiling keeps an open HVIL inside FW-09's 100 ms); DTC_ADC_SLOW_STALE; every slow consumer audited and given the contract where a decision hangs on it (V5GD/VOFS, IGN, INTRLOK_N, VSUP, HW_ID); host tests stop each channel while the fast path runs, never-converted, the 2^32 µs wrap, recovery, a constant temperature converted on schedule stays valid for minutes; 76 checks failed on the old behaviour, 17 of 18 mutations caught. **F241** |
| "A.22 firmware cannot inherit the unchanged-code verdict" | the identity is 0x0A0F0015, layout 4; the expansion needs its own evidence | Not Applicable as a finding — agreed: the T-items and QP-FW-10 are that evidence, on silicon | `firmware/docs/target-bringup.md` | none |

## Reviews 3 and 4 — JSWD closed, the torque vector checks, the SDADC margin (A22-R01 hardware, MAJOR)

| ID | Review said | Class | Verification | Action in rev A.23 |
|---|---|---|---|---|
| A21-R01 / F215 (JSWD) | closed: SMT, 10 pads 0.74 × 2.79 mm, rows 4.064 mm, no drilled signal terminals; both schematic exports assign `traction:Samtec_FTSH-105-01-L-DV-K` | Already Fixed (round 23) | the reviewer's footprint inspection | none |
| A22-R01 (hardware) | the SDADC references and AVDD come from different FS26 outputs; Rev. 5 Table 41 p. 54: AVDD − 0.025 ≤ VREFP ≤ AVDD + 0.025; FS26 ±1 % matching does not guarantee it; counterexample VREF5 5.000 V / V5A 5.040 V (−40 mV); calibration does not close it; do not short VREF and LDO2; keep the FS26 COUT_VREF 1.1–3.3 µF window; move all four non-R2R groups, not some | **Confirmed** (the same finding as review 1's A20-F01) | as above | **F240**: the four groups on V5A (all of them — note 8), the regulator outputs never joined, the ratiometric loads moved, CSB5 sized for the window alone (1.36–2.92 µF effective), the R2R branch re-sized; the reviewers' post-change checks (ADC gain/offset, OCP/OVP thresholds, HW_ID, supply diagnostics, effective capacitance, source loading) are re-derived in the verifier rows "LV A.4" and "Sensing" and stay QP items (QP-EOL-06/07, QP-TH-06, QP-RX-01) |
| torque solver | 4 049 cases / 25 214 assertions, zero violations, at −O2 and ASan/UBSan; four-quadrant example within 7 µN·m; the tests do not prove global optimality for arbitrary maps, WCET, ripple feed-forward, commissioning, signed update | Already Fixed (round 23, F217/F218) — confirmed; the caveats are the T-items (T-36, T-57, T-58) and QP-FW-10 | the reviewer's fixture | none |

## The status register — the inductance map and the ripple limiter (A22-R01 firmware, MODERATE)

| ID | Review said | Class | Verification | Action in rev A.23 |
|---|---|---|---|---|
| A22-R01 (map/limiter) | `map_ok` bounds L (20 µH–5 mH, ≤ 2 % rise, last ≥ 0.3 × first) but not d(L·i)/di: Lq = [1750,1750,1750,1750,560,560] µH passes and gives dλ/di = −1820 µH above k = 3; `torque_ripple_scale` checks only the interval's ends and the base point: at id −102 A, iq 280 A, ω_e 480 rad/s, 632.6 V, feed-forward −10/+20 A the interior iq 288.5 A needs 315.1 V against 312.3 V available while the checked points pass | **Confirmed** (both mechanisms read in the source; the reviewer's oracle numbers reproduced in the fix's tests) | `firmware/src/nvm/calib.c`, `firmware/src/control/torque.c` | the record check requires a positive flux slope with margin at both ends of every interpolated segment (the affine derivative of the piecewise-linear apparent-inductance model), so the reviewer's map is refused at load, import and commit; the limiter cuts the interval at every breakpoint and at zero and bounds each piece in closed form (the agent's oracle showed breakpoints alone are not enough — the flux is quadratic inside a segment), proved against a 257-point double-precision scan over 20 000 maps passing the new check (0 beyond 1e-6); the margin is 0.25 × L0 (0.3 refused the project's own saturating reference plant in the piecewise-linear model); the reviewer's cases as negative controls, 13 checks failed on the old code, 14 mutations caught; 438 tests / 5 139 checks at that point. **F242** |
| FW-35 regression | valid at tested scope: 56 cases under −O2 and UBSan | Already Fixed (round 19) — confirmed | — | none |
| the 64/16-bit division helper | code-verified: 820 885 oracle cases per build over every permitted divisor; CPU WCET not measured | Already Fixed (F236) — confirmed; the cost is T-36 | `firmware/tests/test_time.c` | none |
| the new signed-update / commissioning / Qt functionality | open validation item: source reading only | Not Applicable as a finding — the evidence is QP-FW-10 and the T-items on silicon; the host and QEMU proofs are what exists today | `docs/qualification-plan.md` QP-FW-10 | none |

## What stays open (numbered)

Unchanged: **QP-RX-04 step 2** (≥ 8 A clearing) and **VR-16**; **QP-RX-04 step 2b / QP-MA-11** with **IR-42** and **VR-17**;
**QP-RX-05 / VR-33**; **IR-16**; **VR-35** (the R2R ladder current — the branch is sized on an assumption); the target
measurements T-05…T-07, T-36, T-40…T-42, T-57…T-59's silicon half; the dyno maps; a physical CAN adapter; IR-43; the release
key custody; the image's notarisation. New this round: the reference move's own measurements — **QP-TH-06** (the V5S/V5A drift
after calibration, the V_DC chain drift with the ±1.75 % LDO2 as the reference), **QP-EOL-06/07** (the calibration records
on the new reference), and the layout rule of the handoff §4.11 (one V5A reference island, ≤ 20 mΩ from E12 to any VREFH
ball), which the layout review must check.

## Readiness (after this round)

| Area | Level |
|---|---|
| Power-off inspection / continuity / schematic-to-component reconciliation | READY |
| Handoff to PCB layout | READY on KiCad 10.0.6+ — the reference-net finding that held the freeze is closed by construction; the handoff carries the reference-island rule |
| Integrated temperature monitoring (firmware) | the freshness contract is in and host-proved (F241); the silicon slow-schedule period is a T-item |
| Torque-control release | the solver is confirmed by two independent fixtures; the map and limiter rules are host-proved (F242); T-05…T-07, T-36, T-57/T-58 and the dyno remain the gates |
| Target-controlled motor operation / HV / DVT / production | per `qualification-plan.md` — unchanged |

## Counts and deliverables

Register F240–F244 (5 findings, all fixed; F220 reopened and superseded by F240; F243/F244 found by the firmware agents on the way). Hardware: net changes and two value changes
(CSB5 2.2 µF, CR2R1 330 nF), no new part, no cost change. Firmware: TI_FW_ID 0x0A0F0016, CAL layout 4 (191 fields / 92 CAL rows — cal_temp_hold_ms added), 82 DTCs
(DTC_ADC_SLOW_STALE, DTC_TEMP_OPEN_SHORT added), 444 tests / 5199 checks / 0 failed in three host flavours and on the Cortex-M7
under QEMU, 63 target markers, the bridge's check PASS, the protocol exports current, the tool's 8 suites passing on the new
exports. Pipeline: ERC 981/0/0 · verify 158/18/0 · sim 23/6/0 · Marine 87/23/0 · KiCad proof PASS (689 components bound). See `review-A23-disposition.csv` alongside (every table above, one row per line).
