# Review round 22 — three rechecks of `0ff44d9` (rev A.21) and the layout handoff

**Inputs.** Three independent delta rechecks of the A.20 push `0ff44d9d…` (26 September 2026): (1) a Markdown/HTML
recheck with independent fault-current, KCL/RC, post-trip and discharge calculations and a firmware-tree hash comparison
(A20-N01: the qualification plan's §0.2 DUT/FW/VAL baseline); (2) an HTML recheck that reran the independent resolver fixture
(20 cases / 3 807 assertions, −O2 and ASan/UBSan) against the current `sdadc_ring.c` blob and reproduced the diversion
figures (no new finding); (3) a recheck with the 56-case fixture rerun and a closure register (no new finding). All three close
F208 and ask for no schematic or BOM change. **The user's question this round** — why the schematic is "still not production
ready" and why it is not furnished so that it can go to PCB layout "without any worrying" — was taken as the fourth input:
the handoff to layout was inspected as a layout engineer would receive it, which none of the reviewers did (all three
excluded PCB layout from their scope).

**Method.** The reviewers' item to the source (§0.2 against `docs/bom-control-card.csv` and `firmware/src/app/app.h`). Then the
handoff itself: `kicad/traction/README.txt` ("Footprint fields name the package … no footprint library is shipped"), a
kicad-cli ERC of the sets (686 `footprint_link_issues`), the tscircuit footprint helpers (generic 0.9 × 1.4 mm pads for every
IC). Three agents on the user's instruction: an Opus agent drew the custom footprints from the archived datasheets, a Sonnet
agent audited every multi-pin symbol's pin numbers against its datasheet, a Sonnet agent collected every layout-relevant rule
in the repository for the handoff document. Every change proved by the generators and the extended KiCad verifier.

## Verdict

The reviewers are right and their one item is real but small (F209). The answer to the user's question is in two parts.
The "NOT READY" verdicts in all three reviews are about bench, target and vendor evidence — tests that need a built board and
answers only the OEM and the vendors can give; the schematic itself carried no open circuit defect for three rounds. But
the handoff was genuinely unfurnished: the KiCad sets bound no footprints (the repository said so, as a "later phase"), so
the first thing a layout engineer does — Update PCB from Schematic — could not run for any of the 686 components. That is
closed this round (F210), and the new pad-per-pin proof immediately found three real land defects that would have reached the
layout: the five NCV4276C regulators coded as 3-lead DPAKs (F211), two thermal pads with no symbol pin (F212) and a
two-terminal crystal on a four-pad pattern (F213). **No circuit change; BOM totals unchanged; firmware unchanged.**

**"Already Fixed" for a gate means the gate exists and is open** — never that the physical qualification is complete.

## Review 1 — Markdown/HTML recheck (A20-N01)

| ID | Review said | Class | Verification | Action in rev A.21 |
|---|---|---|---|---|
| A20-N01 (md, reviewer 1) | MINOR — `qualification-plan.md` §0.2 still names SMDJ8.5A-HRA as the exciter TVS, 0x0A0F0011 as the production firmware ID and a validation record for that ID, while §7 and the BOM use SMDJ7.0A-HRA and the header 0x0A0F0013; a technician could select the wrong assembly baseline or prepare an old-ID record; mark the old baseline historical and add one current definition; do not relabel an old record | **Confirmed** — and broader | §0.2 rows HW/FW/VAL as quoted; the same 0x0A0F0011 in the plan's opening baseline (line 10), the QP-EOL identity criterion, the validation-record specification and a closes-list; `firmware/README.md` items 22 and 29; `docs/firmware-contract.md` §10c | §0.2 → **HW A.20+** (SMDJ7.0A-HRA, ESR18EZPF2R20; an A.15–A.17 build named as a different protection configuration whose exciter-fault results do not transfer, F205), **FW 0x0A0F0013**, **VAL for 0x0A0F0013** measured and sealed on the image flashed; every other mention marked as history; the firmware README and contract brought to the current ID. **F209** |
| md — F208 confirmed closed | README gate ㉘ and QP-RX-04 step 3 at 0.37 Ω; 0.29 Ω historical; 42.62 / 37.92 A reproduced; 0.35 Ω PTC is a 23 °C figure | Already Fixed (round 21) | — | none |
| md — TVS and 4XX alternates retained | SMDJ7.0A / SQP10AJB-220R as before | Already Fixed (round 20) | — | none |
| md — charging and post-trip figures | 9.021 V / 34.834 A / 3.844 A; 59.9 µs; 230–231 µC; 276 µs to 99 %; 1.72 / 0.93 / 0.45 / 0.26 W | Already Fixed (F207/F208) — reproduces the rows | — | none |
| md — firmware unchanged | the whole `firmware/` tree is `37f4054…` at A.19 and A.20 | Not Applicable (confirmation) | `git diff --stat 551c405 0ff44d9 -- firmware` is empty | none |
| md — readiness | power-off bench READY; complete resolver terminal-fault qualification, target-controlled HV/motor operation and production NOT READY | Not Applicable (position statement) | the same gates as this disposition's open list | none |

## Review 2 — HTML recheck (no new finding)

| ID | Review said | Class | Verification | Action in rev A.21 |
|---|---|---|---|---|
| html — A19-N01 / F208 closed | README gate ㉘, QP-RX-04 step 3, the post-trip figures, the RSX description and the control-card CSV all at the 7.0 V-class values | Already Fixed (round 21) | — | none |
| html — fixture-setting calculation | 42.623 A at 0.29 Ω, 37.923 A at 0.37 Ω, 37.918 A at 24 V / 0.08 Ω, 37.363 A at 26 V / 0.14 Ω; the PTC resistance is a 23 °C model input | Already Fixed (F199/F203) — reproduces the rows; the cold R is VR-16 | — | none |
| html — diversion figures | 9.021 / 9.019 / 9.038 V; 34.834 / 34.655 / 36.058 A; 3.844 / 3.843 / 3.851 A; 59.9 µs; 0.97–0.98 mJ in RSX | Already Fixed (F207/F208) | — | none |
| html — firmware regression | tree identical; 20 cases / 3 807 assertions at −O2 and ASan/UBSan on the current `sdadc_ring.c` blob `4257356…` | Not Applicable (confirmation) | — | none |
| html — open gates | T-40/41/42, QP-RX-04 step 2 / VR-16, step 2b / QP-MA-11, QP-RX-05 / VR-33, IR-16 | Already Fixed (gates exist and are open) | — | none |
| html — "keep A.20 as the baseline; no further schematic/BOM change" | — | Not Applicable (agreed for the circuit; the handoff is a different matter — F210) | — | see the self-found items |

## Review 3 — Markdown recheck with closure register (no new finding)

| ID | Review said | Class | Verification | Action in rev A.21 |
|---|---|---|---|---|
| md — A19-N01 / F208 closed | the three locations agree at 0.37 Ω; the 0.29 Ω mentions are history | Already Fixed (round 21) | — | none |
| md — firmware intact | `src`, `include`, `tests`, `tools`, `docs`, `Makefile` identical; 56 cases / 40 008 assertions rerun at −O2 and under UBSan | Not Applicable (confirmation) | — | none |
| md — post-trip and diversion descriptions | 2.457 / 2.024 / 1.720 / 1.186 / 0.930 / 0.447 / 0.261 W; 3.84–3.85 A / 59.9 µs / 0.230–0.231 mC | Already Fixed (F208) — reproduces the shared model | — | none |
| md — BOM and identity corrections preserved | SMDJ7.0A-HRA / SMDJ7.0A; 220 Ω / SQP10AJB-220R; T-05/T-06 at 0x0A0F0013 | Already Fixed (round 20) | — | none |
| md — release position | READY FOR POWER-OFF BENCH TEST; motor operation and production NOT READY | Not Applicable (position statement) | — | none |

## Self-found — the layout handoff (the user's question)

| ID | What was found | Class | Verification | Action in rev A.21 |
|---|---|---|---|---|
| F210 | Every KiCad symbol carried a bare package code and the sets shipped no footprint library; Update PCB from Schematic could not run; the tscircuit footprints are generic placeholders | **Confirmed** (a handoff gap, not a circuit defect) | `kicad/traction/README.txt` (A.20): "no footprint library is shipped"; kicad-cli ERC: 686 `footprint_link_issues`; `packages/cells.tsx` `SmdFP(n)` | `calculations/footprints.mjs` (code → footprint), `kicad-fp-gen.mjs` (the shipped `traction.pretty`: standard patterns copied verbatim from KiCad 10.0.6 with provenance; datasheet-drawn patterns per `MANIFEST.md`), `fp-lib-table` in all three sets and zips, per-board project files, F2 = `traction:<name>`, generator refusal of unbound parts, `kicad-sch-verify` judging footprint links and pads per pin; `docs/layout-handoff.md` |
| F211 | NCV4276C ×5 coded as 3-lead DPAK / D2PAK lands | **Confirmed** by the new proof (symbol pin 5 had no pad) | NCV4276C DS p.1: "DPAK 5-PIN", "D2PAK 5-PIN"; pin 2 INH, tab = pin 3; the pin audit confirms all five symbols | 5-lead lands (TO-252-5_TabPin3 / TO-263-5_TabPin3) |
| F212 | ALM2402 PWP pad and TPS55340 PowerPAD had no symbol pin | **Confirmed** by the pin audit (datasheets: pad to ground; PowerPAD must be soldered to AGND) | the verifier listed pads 15 / 17 with no pin | pins 15 (AGND) / 17 (DGND) added at the source; ERC lock updated; the lands now net their pads (proved) |
| F213 | The Kyocera CX3225GA crystal is two-terminal; the 4-pad 3225 pattern would put it on two of four pads | **Confirmed** by the pin audit (rendered datasheet) | the verifier listed pads 3/4 with no pin | the 2-pad Kyocera land, drawn from the sheet |
| pin audit | 31 part groups against their datasheets: 30 match pin for pin (FS26 48 + EP now confirmed — its map had been marked "VERIFY vs DS"; VGT12EEM dots; LFPAK56 tab = pin 5; TO-247-4 lead order; ACT45B winding pairs); QLVS SOT-223 undecidable from a datasheet that prints no pin numbers | Not Applicable (confirmation) / QLVS resolved by the land's TO-261 orientation (tab right: 1 = G, 2 = D = tab, 3 = S — the symbol's numbering) | `scratchpad/pin-audit.md` (summarised in `docs/layout-handoff.md` §1a) | none beyond F211–F213 |
| doc sweep | README DFM table still described the superseded 10× 27 k bleeder; `mcu-pin-manifest.md` said 158 open balls beside its own 159; VR-17 still asked about the SMCJ8.5A; the RFS4 fault-hold row and dfm §4 fillet rule still on the ESR03 (QP §15 item 6 had said so) | **Confirmed** (documentation) | as cited in F209 | corrected; QP §15 item 6 bullets carry their resolutions. **F209** |

## What stays open (numbered)

Unchanged from round 21: **QP-RX-04 step 2** (≥ 8 A clearing, release criterion) and **VR-16**; **QP-RX-04 step 2b /
QP-MA-11** (the sustained-short sweeps, the coupled pair's transfers — now with the island geometry as a layout input) with
**IR-42** and **VR-17**; **QP-RX-05 / VR-33**; **IR-16** (the 24 V figure needs the OEM's statement or the 0.1 Ω series
fallback); target measurements T-40 / T-41 / T-42. Before layout starts (from `docs/layout-handoff.md` §6): the PD2-sealed vs
PD3 creepage decision (IR-36 provisional PD2/OVC II), the HCS600 solder-pin vs press-fit mount, the VGT12EEM core tie, the
coolant flow and pressure, the EMC class and the insulation test levels (OEM inputs).

## Readiness (after this round)

| Area | Level |
|---|---|
| Power-off inspection / continuity / schematic-to-component reconciliation | READY |
| Schematic / BOM production freeze | READY subject to the release gates above |
| **Handoff to PCB layout** | **READY**: every on-board symbol bound to a footprint in the shipped library, proved pad-per-pin by kicad-cli; the open decisions are listed with their owners in `docs/layout-handoff.md` §6 and do not block placement of the LV card or the discharge board (the PD2/PD3 decision sets the HV spacings on the power board — obtain it first or lay out to the PD3 figures) |
| Target-controlled motor operation / HV / DVT / production | per `qualification-plan.md` — unchanged |

## Counts and deliverables

See the README round-22 row: ERC 977 / 0 · verify 156 PASS / 18 WARN / 0 FAIL / 26 info · sim 23 / 6 / 0 · Marine 87 / 23 / 0 · pin-verify 2059/2059 (the two pad pins) · KiCad 10.0.6 proof PASS with every on-board symbol bound and every pin number on a pad · BOM totals unchanged · firmware unchanged (290 / 2705 / 0) · register F209–F213 · `review-A21-disposition.csv` alongside.
