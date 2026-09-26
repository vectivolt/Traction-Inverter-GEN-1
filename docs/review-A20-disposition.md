# Review round 21 — three rechecks of `551c405` (rev A.20)

**Inputs.** Three independent delta rechecks of the A.19 push `551c4057…` (26 September 2026): (1) an HTML-package recheck
that reran its independent frame-protocol fixture (56 cases / 40 008 assertions at −O2 and under UBSan), six alternate-selection
predicate cases and independent discharge and diversion calculations (A19-N01: README gate ㉘ still at 0.29 Ω); (2) a recheck
that compared the firmware Git trees between A.18 and A.19, reran its resolver fixture (20 cases / 3 807 assertions, −O2 and
ASan/UBSan) and reproduced the diversion figures (A19-N01, plus "some component comments retain ≈ 4.8–4.9 A"); (3) a recheck
that executed 24 assertions against the two round-20 ERC predicates for all four SKUs and recomputed the 4XX / 8XX discharge
times and the back-drive balance at 24 / 26 / 35 V (A18-R01 / A18-R02 closed; no new defect). All three close F205–F207 and
ask for no hardware change. The user also re-stated "run the QP-RX-04 sweep numbers for the Marine port too": the sweep exists
since round 19 (`marine/calc/marine-verify.mjs` §7, QP-MA-11); it was re-run this round and the Road documents that quote it are
aligned to its current figures.

**Method.** Each claim taken to the source: README gate ㉘ against `docs/interface-requirements.md` IR-16; then a tree-wide grep
for every 8.5 V-class figure (0.29 / 0.05 / 0.09 / 0.25 Ω; 1.5–4 W and 5.2 / 8.3 W; 4.7–4.9 A, 0.28–0.29 mC, 10.4–11.5 V) in live
text, as distinct from the findings register and the round narratives; the shared model re-run (`node marine/calc/marine-verify.mjs`,
`npm run verify`) for every current value quoted below. No agents this round: every item is grep-verifiable prose.

## Verdict

All three rechecks are right. The one defect is documentation, but it is not harmless: the number they found in the README gate
also sat in QP-RX-04's own step-3 instruction — the line a technician would set the 35 V fixture from — and at 0.29 Ω the
cold-corner PTC current is 42.6 A against the part's 40 A I_max (37.9 A at the current 0.37 Ω). The broader sweep found the
remaining 8.5 V-era figures in live text: the QP's post-trip pass criterion (0.6 / 0.3 / 1.5–3.8 W), its Marine cross-reference
(5.2 / 8.3 W — the uncapped formula of round 18, where the current sweep gives 2.0 / 2.5 W), QP-RX-05's rail figure (10.5 V),
VR-33's envelope (4.8 A / 0.28 mC / ≤ 11 V), the README gate's trickle and sweep resistances, the card comment and the RSX BOM
description (the "4.8–4.9 A" the second review named), and the round-18/19 narratives in both design-basis documents. Every live
figure now reads the 7.0 V-class value from the shared model with the old one marked historical beside it. **No circuit,
BOM-total or firmware change.**

**"Already Fixed" for a gate means the gate exists and is open** — never that the physical qualification is complete.

## Review 1 — HTML-package recheck (A19-N01)

| ID | Review said | Class | Verification | Action in rev A.20 |
|---|---|---|---|---|
| A19-N01 (html, reviewer 1) | MINOR — README gate ㉘ (≈ line 742) still says "35 V double event (IR-16 ≥ 0.29 Ω …)" while IR-16 states ≥ 0.37 Ω at 35 V; by the cold-current model 42.62 A at 0.29 Ω against 37.92 A at 0.37 Ω; do not configure the fault-test fixture from the stale number | **Confirmed** — and broader than reported | `README.md` line 742 quoted 0.29 Ω; `docs/qualification-plan.md` QP-RX-04 **step 3** quoted "≥ 0.29 Ω at 35 V since round 18" — the fixture instruction itself; `calculations/exciter-fault.mjs` `iCold(35, 0.29)` = 42.6 A, `iCold(35, 0.37)` = 37.9 A | both → 0.37 Ω with round 18's value marked as the 8.5 V-class figure; the gate's ≥ 8 A sweep resistances (0 / 0.09 / 0.5 Ω → 0 / 0.14 / 0.5 / 1.33 Ω, as QP-RX-04 states them) and its trickle figure (1.5–4 W → 0.9–1.7 W at 12.6–16 V; the same figure in IR-42) aligned with the QP; QP §15 item 6's "≥ 0.27 Ω at 35 V" bullet given its resolution. **F208** |
| html — F205 / F206 / F207 closures | T-05/T-06 at 0x0A0F0013; the control-card CSV keeps SMDJ7.0A-HRA primary with SMDJ7.0A as the prototype-only alternate, the 8.5 V class excluded; both 4XX discharge CSVs carry four 220 Ω primaries and SQP10AJB-220R; the diversion calculation evaluated at the IR-16 allocations | Already Fixed (round 20) | `firmware/docs/target-bringup.md` T-05/T-06; `docs/bom-control-card*.csv` (four variants, one alternate text); `docs/bom-discharge-igbt4.csv`, `docs/bom-discharge-sic4.csv`; verification-report "Exciter back-drive with VEXD absent" | none |
| html — firmware unchanged | `firmware/src`, `include`, `tests`, `tools`, `Makefile` identical between A.18 and A.19; the frame-protocol fixture reran at 56 cases / 40 008 assertions at −O2 and under UBSan | Not Applicable (confirmation) | `git diff --stat 00f6252 551c405 -- firmware/src firmware/include firmware/tests firmware/tools firmware/Makefile` is empty | none |
| html — discharge calculation | 803 µF, 500 → 60 V, 45 kΩ passive: four 220 Ω 1.470 s (1.697 s at C +10 % / R +5 %); four 470 Ω 3.072 s (3.549 s) — the corrected selection meets the 2 s screening target, the old alternate does not; times exclude detection/enable delay and do not establish pulse capability or stuck-ON containment | Already Fixed (F206) — reproduces the round-20 figures (1.5 / 3.1 s, 3.5 s at the corners) | the F206 register row; QP-DC procedures carry the pulse and stuck-ON items | none |
| html — diversion figures | 24 V / 0.08 Ω: node 9.021 V, DEX 3.844 A, PTC total 34.834 A; 35 V / 0.37 Ω: 9.038 V, 3.851 A, 36.058 A; τ 59.9 µs, Q 0.231 mC — supports "≈ 3.9 A / 60 µs / 0.23 mC"; calculations, not transient simulations or measurements; cold PTC, nonlinear diode, parasitics, thermal coupling and ULDOEX reverse current stay open | Already Fixed (F207) — reproduces the row | verification-report "Exciter back-drive with VEXD absent": 3.84–3.85 A, 9.02–9.04 V, PTC 34.8 / 36.1 A, τ 60 µs, 0.23 mC; the limitations are the row's own text and QP-RX-05 / VR-33 | none |
| html — release position | READY FOR POWER-OFF BENCH TEST; integrated target-controlled motor operation and production NOT READY; T-40/41/42, terminal-fault clearing and sustained-short tests, ULDOEX reverse power, installation resistance, motor-specific qualification remain | Not Applicable (position statement) | matches the A.19 disposition's readiness table and open list | none |

## Review 2 — Markdown/HTML recheck (A19-N01, component comments)

| ID | Review said | Class | Verification | Action in rev A.20 |
|---|---|---|---|---|
| md — A18-D01 / F205 closed | primary SMDJ7.0A-HRA unchanged; alternate SMDJ7.0A prototype-only; the 8.5 V configuration explicitly excluded; the four control-card BOM variants share the corrected file hash; Littelfuse's pages give the plain and HRA 7 V parts the same headline electricals, AEC-Q101 on the HRA page only — qualification is not interchangeable; a prototype fitted with the plain grade must record it | Already Fixed (round 20) | `calculations/parts-db.mjs` TVSE rule (alt text names the class and the exclusion); the ERC voltage-class lock (8 checks, four SKUs) | none — the alt text already says "proto only" |
| md — F206 closed | primary four 220 Ω; alternate SQP10AJB-220R; both 4XX BOMs corrected; 800 µF bulk-only: 1.464 s (1.691 s at the corners) against 3.061 s (3.535 s) for 470 Ω; the overload/no-flame condition stays attached to the alternate | Already Fixed (F206) | `docs/bom-discharge-igbt4.csv`, `docs/bom-discharge-sic4.csv`; the ERC discharge-alternate lock | none |
| md — firmware ID and trees | `#define TI_FW_ID 0x0A0F0013u`; T-05/T-06 name it, the previous value marked history; the firmware trees are identical to A.18; the resolver fixture reran at 20 cases / 3 807 assertions (−O2, ASan/UBSan); T-40/41/42 still carry the silicon evidence | Already Fixed (round 20) / gates exist | `firmware/src/app/app.h`; `target-bringup.md` T-05/T-06/T-40…42 | none |
| md — diversion figures reproduce | 9.021 V / 34.834 A / 3.844 A at 24 V / 0.08 Ω; 9.038 V / 36.058 A / 3.851 A at 35 V / 0.37 Ω; τ ≈ 59.9 µs; 0.230–0.231 mC; not the cold maximum-PTC-current corner; LDO reverse path, rail loading and PTC thermal dynamics excluded; keep the RSX-limited topology | Already Fixed (F207) — reproduces the row | as review 1 | none |
| A19-N01 (md, reviewer 2) | MINOR — the README gate names 0.29 Ω for the 35 V event where IR-16 says 0.37 Ω; **some component comments also retain the old ≈ 4.8–4.9 A diversion figures alongside the corrected ≈ 3.9 A**; mark them historical or generate the summaries from the final constants; BOM impact zero | **Confirmed** | `boards/control-card.tsx` line 593 ("4.8 A peak decaying with tau = 59 us, rail then ~10 V", unmarked, 18 lines above the corrected figure); `calculations/parts-db.mjs` RSX description ("4.9 A peak … I2R energy ~1.6 mJ"); `docs/vendor-requests.md` VR-33 ("4.8 A peak, τ ≈ 59 µs, 0.28 mC … ≈ 10.4–11 V … 0.28 mC at ≤ 11 V"); `docs/design-basis.md` §11q ("4.7 A / 60 µs"); `marine/design-basis.md` A.15 bullet ("4.9 A peak … 0.2 mJ") | every one reads ≈ 3.9 A / 60 µs / 0.23 mC, rail ≈ 8.6 V (≤ 9.1 V), RSX ≈ 1.0 mJ (I₀²·R·τ/2 at 3.85 A) with the 8.5 V-class value marked; the card rebuilt and the BOMs regenerated (description text only, totals unchanged). **F208** |
| md — open gates | T-40/41/42; QP-RX-04 step 2 / VR-16; step 2b / IR-42 / VR-17; QP-RX-05 / VR-33; IR-16 (actual fault-loop impedance or a qualified local limiting solution) — existing gates, not new defects | Already Fixed (gates exist and are open) | `qualification-plan.md`, `vendor-requests.md`, `interface-requirements.md` | none |
| md — "no new MCU, amplifier, DC-link, brake chopper or power-stage redesign is justified" | — | Not Applicable (agreed) | — | none |

## Review 3 — Markdown recheck (A18-R01 / A18-R02 closed, predicates executed)

| ID | Review said | Class | Verification | Action in rev A.20 |
|---|---|---|---|---|
| md — A18-R01 / A18-R02 closed | SMDJ7.0A-HRA primary, SMDJ7.0A prototype-only candidate, the 8.5 V configuration excluded; SQP10-220RJB15 primary with SQP10AJB-220R candidate in both 4XX BOMs; the 8XX population stays four 470 Ω with a 470 Ω candidate — the 4XX correction did not propagate wrongly | Already Fixed (round 20) | `docs/bom-discharge-igbt.csv` (8XX, 470 Ω); the two 4XX CSVs; the four card CSVs | none |
| md — discharge times | 4XX 803 µF / 880 Ω ∥ 45 kΩ: 1.470 s (1.697 s); 8XX 323 µF / 1880 Ω ∥ 66 kΩ: 1.565 s (1.808 s); the old 4XX candidate 3.072 s (3.549 s) — no resistance change warranted; pulse energy, repeated discharge and stuck-switch behaviour stay separate | Already Fixed (F206) — the 8XX figure matches its own row | the discharge rows of the verification report | none |
| md — back-drive at the current allocations | 24 V / 0.08 Ω: 9.021 V, 34.83 A, 3.84 A; 26 V / 0.14 Ω: 9.019 V, 34.66 A, 3.84 A; 35 V / 0.37 Ω: 9.038 V, 36.06 A, 3.85 A; τ ≈ 59.9 µs, 230–231 µC, 99 % of final voltage at ≈ 276 µs — "the 60 µs time constant is not the end of the pulse"; wiring inductance, ESR, heating, current sharing and LDO reverse conduction excluded; PTC at the 23 °C minimum | Already Fixed (F207) — reproduces the row; QP-RX-05 already states "99 % by 280 µs" (4.6 τ) | verification-report back-drive row; QP-RX-05 pass criteria | none |
| md — predicate execution | 24 assertions against the two new `erc-audit.mjs` predicates for SiC-8XX, IGBT-8XX, SiC-4XX and IGBT-4XX: current entries accepted, the original 8.5 V TVS entry and cross-variant resistor substitutions rejected, missing alternates handled; the predicates search text and do not qualify a part | Not Applicable (agreed — the round-20 disposition describes the locks as regression locks on the alternate text, not as qualification) | `calculations/erc-audit.mjs` (the TVSE-class and RDIS-value locks; mutation-tested in round 20) | none |
| md — firmware ID | header and T-05/T-06 agree; this aligns the documentation, it does not create the validation record; the A.19 firmware changes were documentation | Already Fixed / gate exists (the T-05/T-06 record) | as review 2 | none |
| md — open requirements | QP-RX-04 step 2 / VR-16; step 2b; QP-RX-05 / VR-33; IR-16 and the cold PTC; target routing, register protection and timing — not new defects, not to be misreported as missing components | Already Fixed (gates exist and are open) | — | none |
| md — readiness | READY FOR POWER-OFF BENCH TEST; target-controlled HV/motor operation and production NOT READY | Not Applicable (position statement) | matches this disposition | none |

## The Marine sweep, as quoted now (QP-MA-11, `marine/verification-report.md` §7, re-run this round)

| Point | Current value (SMDJ7.0A-HRA, shared model) | What the Road documents quoted before this round |
|---|---|---|
| PTC current, direct short through the 0.27 Ω kit-loom minimum | 18 V 16.8 A · 24 V 26.4 A · 28 V 32.8 A · 31.2 V 37.9 A (PASS at 95 % of 40 A); the 12 V rail 12.7 A at 0 Ω | — (unchanged) |
| Kit-loom requirement (bare 38 A thresholds at the cold corner) | 24 V 0.079 Ω · 28 V 0.184 Ω · 31.2 V 0.269 Ω → **≥ 0.27 Ω** | "≥ 0.25 Ω" (round-19 interim, 8.5 V class) in design-basis §11r and the F202 row |
| Post-trip trickle in the TVS, card asleep | 11.4 V 2.46 W · 12.0 V 2.02 W · 13.2 V 1.5 W · 18 V 0.73 W · 24 V 0.45 W · 28 V 0.35 W · 31.2 V 0.3 W (WARN — QP-MA-11 measures the coupled pair) | "5.2 W at 12.0 V, 8.3 W at 11.4 V" (the uncapped 8.5 V-class formula of round 18) in QP-RX-04 step 2b, traceability row 23c, design-basis §11r, the README round-19 row and the F202 row |
| Sub-8 A window (unprotected on paper) | 12 V ≈ 4–17 Ω · 13.2 V ≈ 5–23 Ω · 18 V ≈ 10–47 Ω · 24 V ≈ 17–78 Ω · 28 V ≈ 22–98 Ω · 31.2 V ≈ 25–114 Ω | — (unchanged) |
| Coupled junction at 11.4 V / 85 °C ambient / T_t 125 °C | ≤ 124 °C | — (unchanged) |

## What stays open (numbered)

Unchanged from round 20: **QP-RX-04 step 2** (≥ 8 A clearing, release criterion) and **VR-16**; **QP-RX-04 step 2b /
QP-MA-11** (the sustained-short sweeps, the coupled pair's transfers) with **IR-42** and **VR-17**; **QP-RX-05 / VR-33**;
**IR-16** (the 24 V figure needs the OEM's statement or the 0.1 Ω series fallback); target measurements T-40 / T-41 / T-42.

## Readiness (after this round)

| Area | Level |
|---|---|
| Power-off inspection / continuity / schematic-to-component reconciliation | READY |
| Schematic / BOM production freeze | READY subject to the release gates above |
| Target-controlled motor operation / HV / DVT / production | per `qualification-plan.md` — unchanged; the fixture instructions now carry the current IR-16 figures |

## Counts and deliverables

ERC 977 / 0 · verify 156 PASS / 18 WARN / 0 FAIL / 26 info · sim 23 / 6 / 0 · Marine 87 / 23 / 0 · pin-verify 2057/2057, KiCad 10 proof PASS · BOM totals unchanged (one description text) · firmware unchanged (290 tests / 2705 checks / 0 failed; target-check 52 markers) · register F208 · `review-A20-disposition.csv` alongside.
