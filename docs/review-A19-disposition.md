# Review round 20 — three rechecks of `00f6252` (rev A.19)

**Inputs.** Three independent delta rechecks of the A.18 push `00f62526…` (26 September 2026): (1) an HTML recheck that
executed the unchanged `sdadc_ring.c` against its Git blob hash in 56 cases / 40 008 assertions at −O2 and under UBSan
(A18-N01: the target checklist's firmware ID); (2) a recheck with 20 C cases / 3 807 assertions under −O2 and ASan/UBSan
plus independent KCL, allocation, post-trip and thermal calculations (A18-D01: the TVS alternate; A18-N01: document
synchronisation); (3) a recheck with 51 ring scenarios / 3 181 assertions per build and an independent 4XX discharge
calculation (A18-R01: the TVS alternate; A18-R02: the 4XX discharge alternate; A18-N01). All three close the round-19
resolver-origin and high-current findings and ask for no hardware change.

**Method.** Each claim taken to the source first: `firmware/src/app/app.h` against `firmware/docs/target-bringup.md`
T-05/T-06; `calculations/parts-db.mjs` (the TVSE alternate field, the two RDIS rows); the regenerated verification
report and the live prose for figures that survived the round-19 class change. No agents this round: every item is a
metadata or documentation correction verified by grep and by the regenerated reports.

## Verdict

All three rechecks are right; nothing they found touches the circuit. The two BOM-metadata errors were real and mine
(the alternate fields were carried over untouched when the TVS class changed in round 19, and the 8XX discharge
alternate had been copied into the 4XX row at some earlier point); the stale firmware ID in two checklist rows would
have produced an avoidable no-arm at the validation station; and the reviewers' "align the figures" note was broader
than they said — the back-drive row was still being evaluated at the 8.5 V-class conditions (0 / 0.29 Ω), so it printed
PTC totals over 40 A beside a PASS. Two regression locks now keep the alternates honest. **No hardware change; BOM
totals unchanged.**

**"Already Fixed" for a gate means the gate exists and is open** — never that the physical qualification is complete.

## Review 1 — HTML recheck (A18-N01)

| ID | Review said | Class | Verification | Action in rev A.19 |
|---|---|---|---|---|
| A18-N01 (html, reviewer 1) | MINOR — `app.h` defines TI_FW_ID 0x0A0F0013 but T-05/T-06 in the target checklist still specify 0x0A0F0012; the validation function rejects a mismatch, so the likely consequence is a validation-station no-arm | **Confirmed** | `target-bringup.md` lines 26–27; also the firmware README's round-18 paragraph said "is 0x0A0F0012" in the present tense | T-05/T-06 → 0x0A0F0013 (round 19), the round-18 paragraph reworded as history; `make target-check` passes. **F207** |
| html — FW-35 closure | the callback-derived origin is gone; 56 cases pass including the 30 + 3 µs boundary (33 accepted / 34 rejected), constant 40 / 60 µs delays never accepted, recovery on the original cadence; T-40/41/42 remain target obligations | **Already Fixed** (round 19) / gates exist | agreed; the boundary behaviour is the declared uncertainty, not a hidden shift | none |
| html — 7.0 V TVS and allocations | 37.9 / 37.4 / 37.9 A at the IR-16 allocations reproduce; the coupling model is assumed; keep the gates | **Already Fixed** / agreed | agrees with `exciter-fault.mjs` | none |

## Review 2 — Markdown/HTML recheck (A18-D01, A18-N01)

| ID | Review said | Class | Verification | Action in rev A.19 |
|---|---|---|---|---|
| A18-D01 (html) | MODERATE — the primary is SMDJ7.0A-HRA but the alternate still reads "SMDJ8.5A (same electricals, no AEC-Q101 statement — proto only)"; the 8.5 V part is a different protection configuration (9.44–10.40 V, 14.4 V at 208 A) and cannot inherit the 7.0 V parked-short calculation; add a regression check | **Confirmed** (mine: the alternate was carried over untouched in round 19) | `parts-db.mjs` TVSE row | alternate → the plain SMDJ7.0A (same electricals as the -HRA, no AEC-Q101 statement, proto only), the 8.5 V class named as excluded from the A.18 qualification; ERC lock: the exciter TVS alternate must name the primary's voltage class (per SKU; 4 rows). **F205** |
| A18-N01 (html) | MINOR — parts of the A.18 disposition retain the interim 0.09 / 0.33 / 0.25 Ω allocations; inherited comments retain 8.5 V-era clamp figures | **Confirmed** — and broader than stated | the A18 disposition's first-review row; the card comments (10.4–11.5 V clamp, 4.8 A / 0.28 mC); the README gate (4.7 A); design-basis §11r values; the F199 register text; QP-RX-04's clamp prediction (10.7 V / 15.6 A) and QP-RX-05's envelope (5.0 A, 11.0–11.6 V, 1.5 mJ); the verifier's back-drive row evaluated at 0 / 0.29 Ω | every figure re-derived from the shared model at the IR-16 allocations (≈ 3.9 A / 60 µs / 0.23 mC, node ≈ 9.0 V, clamp ≤ 9.1 V at 40 A; PTC totals 34.8 / 36.1 A in the back-drive row) with the 8.5 V-class values marked historical. **F207** |
| html — remaining gates | keep QP-RX-04 step 2 / 2b, QP-RX-05 / VR-33 open; the 24 V allocation needs the OEM's statement; no series resistor automatically | agreed | — | none |

## Review 3 — Markdown recheck (A18-R01, A18-R02, A18-N01)

| ID | Review said | Class | Verification | Action in rev A.19 |
|---|---|---|---|---|
| A18-R01 (md) | MODERATE — as A18-D01, with the comparative trickle 1.72 W (7.0 V) vs 3.78 W (8.5 V) at 12.6 V | **Confirmed** | as above | as F205 |
| A18-R02 (md) | MODERATE (4XX) — the BUS_4XX RDIS row (four 220 Ω, SQP10-220RJB15) carries the alternate "Yageo SQP10AJB-470R"; four 470 Ω would take 3.07 s to 60 V (3.55 s at +5 % / +10 %) against the 2 s target; not introduced by A.18 | **Confirmed** (an earlier copy of the 8XX alternate) | `parts-db.mjs` line 34 (the description already named the 220 Ω Yageo part; only the `alt` field was wrong); the primary is correct and its discharge row passes | alternate → Yageo SQP10AJB-220R under the same no-flame condition (VR-28); ERC lock: a discharge-resistor alternate must carry the row's value (the 8XX row's value read from its MPN); both 4XX BOMs regenerated. **F206** |
| A18-N01 (md) | MINOR — as Review 2 | **Confirmed** | as above | as F207 |
| md — thermal coupling | the `coupledTj()` parameters are assumptions; keep QP-RX-04 step 2b, the cold PTC resistance (VR-16 c) and QP-RX-05 / VR-33 open | agreed | — | none |

## What stays open (numbered)

Unchanged from round 19: **QP-RX-04 step 2** (≥ 8 A clearing, release criterion) and **VR-16**; **QP-RX-04 step 2b /
QP-MA-11** (the sustained-short sweeps, the coupled pair's transfers) with **IR-42** and **VR-17**; **QP-RX-05 / VR-33**;
**IR-16** (the 24 V figure needs the OEM's statement or the 0.1 Ω series fallback); target measurements T-40 / T-41 / T-42.

## Readiness (after this round)

| Area | Level |
|---|---|
| Power-off inspection / continuity / schematic-to-component reconciliation | READY |
| Schematic / BOM production freeze | READY subject to the release gates above; the alternates are now the same protection configuration as their primaries |
| Target-controlled motor operation / HV / DVT / production | per `qualification-plan.md` — unchanged; TI_FW_ID 0x0A0F0013 needs its EOL/HIL record (the checklist rows now say so) |

## Counts and deliverables

ERC 977 / 0 (two new locks × four SKUs) · verify 156 PASS / 18 WARN / 0 FAIL / 26 info · sim 23 / 6 / 0 · Marine 87 / 23 / 0 · pin-verify 2057/2057, KiCad 10 proof PASS · BOM totals unchanged (alternates only) · firmware unchanged in code (290 tests / 2705 checks / 0 failed; target-check 52 markers) · register F205–F207 · `review-A19-disposition.csv` alongside.
