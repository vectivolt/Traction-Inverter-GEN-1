# Review round 19 — three rechecks of `e315bf1` (rev A.18)

**Inputs.** Three independent delta rechecks of the A.17 push `e315bf18…` (26 September 2026): (1) an HTML/Markdown recheck
with 350 C excerpt assertions per build (GCC −O0/−O2, Clang ASan/UBSan), independent Python branch-topology, pin-coordinate,
current-envelope and RC/KCL calculations (A17-R01 md: the 26 V endpoint; A17-R02 md: the ≥ 8 A trip-time law); (2) a recheck
with 19 C cases / 194 assertions and six reduced-order transients (A17-R01 html: the resolver ring's callback-derived time
origin; A17-R02 html: the same trip-time law); (3) a CSV register with 14 ring cases / 401 assertions, 40 physical-time angle
cases and 18 freshness cases (A17-R01 csv: the same origin defect, plus the recovery's phase ambiguity). The user's
instruction: fix and align end to end, verify every claim first, six-way classification, only what is genuinely required —
and run the QP-RX-04 sweep numbers for the Marine port.

**Method.** Each claim taken to the source before acting: `calculations/design-verify.mjs` (the 24 V-only PTC row, the I⁻²
comment), `docs/interface-requirements.md` IR-16, the Bourns MF-MSMF sheet (the one maximum trip time, the typical curve),
`firmware/src/hal/sdadc_ring.c` lines 147–150 (the first-callback anchor, the resync from equal slots) and
`s32k396_resolver.c` (the callback stamps with its own execution time). An **Opus adversarial cross-check** (`xcheck19`)
was put on the round's own hardware question — whether a lower-standoff exciter TVS removes the parked-trickle exposure the
Marine numbers made acute — and on the round's figures; an **Opus firmware agent** rewrote the ring's time origin with
fail-before/pass-after tests. The exciter fault model was lifted into one shared module
(`calculations/exciter-fault.mjs`) so the Road verifier and the Marine port compute from the same constants.

## Verdict

All three rechecks are right, and the round found one more defect of its own. The 26 V endpoint was genuinely unevaluated
(41.6 A at the interface's own 0.05 Ω); the ≥ 8 A energy PASS rested on a trip-time law that only the 8 A / 20 ms point
supports; and the resolver ring did anchor its clock on the first completion callback, so a late or constantly-late
callback could become the reference — the residual the round-18 firmware agent had itself flagged as "not detectable from
software alone", which turned out to be closable: the SWG start instant on the microsecond timer is an origin independent
of every callback. The self-found defect: the round-18 window row printed 1–200 Ω for a criterion error (instantaneous
power tested against the steady-state limit where the PTC trips in milliseconds); the correct window is 14–83 Ω at 24 V,
as the round-18 cross-check had computed. **Hardware change: one, zero-cost — TVSEP/TVSEN move from SMDJ8.5A-HRA to SMDJ7.0A-HRA on the same pad (F203).** The
Marine sweep made the parked-trickle case acute (the kit's 11.4–12 V rail: 3.8–4.3 W in the TVS once the PTC has tripped and
the card sleeps, capped at V_BR·I_trip — round 18's 5.2 W was the uncapped formula), and the Opus cross-check refuted
round 18's "coupling does not help once the PTC has tripped": the tripped PTC is a thermostat and the island's heat
replaces its own I·(V_S − V_BR), but the benefit scales with (V_S − V_BR)/V_BR, which only the TVS class sets — with the
8.5 V class the coupled junction still reached 148–171 °C, with the 7.0 V class ≤ 132 °C (1.7–2.5 W uncoupled), every
dark condition kept with ≥ 2.4 V margin, leakage ≤ 1.4 % peak dip at 125 °C, at +0.043 Ω on each harness allocation.
IR-16 is re-stated at every permitted voltage at the verifier's own 5 % margin (0.08 / 0.14 / 0.37 / 0.26 Ω; the 24 V
figure now needs an OEM statement, with a 0.1 Ω pulse-rated 2512 per line as the fallback). The Marine kit requirement
(≥ 0.27 Ω on the fault loop — a routing/segregation rule for the loom, which is ours) and a Marine bench procedure
(QP-MA-11) carry the ship-bus and kit-rail cases.

**"Already Fixed" for a gate means the gate exists and is open** — never that the physical qualification is complete.

## Review 1 — HTML/Markdown recheck (A17-R01 md, A17-R02 md/html, dispositions)

| ID | Review said | Class | Verification | Action in rev A.18 |
|---|---|---|---|---|
| A17-R01 (md) | MODERATE — IR-16 allows ≥ 0.05 Ω at ≤ 26 V but the current check is hard-coded at 24 V; 26 V / 0.05 Ω gives 41.58 A (40.6 A even at the 25 °C V_BR,min) against the 40 A I_max; 35 V / 0.29 Ω gives 40.07 A (a downward rounding); avalanche-only minima 0.066 / 0.291 Ω; parameterise by voltage, do not round safety minima down, align IR-16 | **Confirmed** | `design-verify.mjs` `const i24 = (rExt, vb, rd) => (24 − vb)/…` (round 18, mine); the shared module reproduces 41.58 A at 26 V / 0.05 Ω | PTC row evaluated at 16 / 24 / 26 V (and 35 V) with `EX.rExtMin(v)` at 38 A (the verifier's 5 % margin), rounded UP: IR-16 → **0.09 Ω at ≤ 26 V, 0.33 Ω at 35 V, 0.25 Ω negative** (bare 40 A thresholds 0.017 / 0.066 / 0.291 / 0.22 Ω stated beside them); QP-RX-04 step 2 runs 16 / 24 / 26 V. **F199** |
| A17-R02 (md/html) | MAJOR/MODERATE — the ≥ 8 A energy PASS (1.69 J) assumes trip time falls at least as fast as I⁻² above 8 A; the sheet gives one maximum (8 A / 20 ms) and a typical curve; 20 A × 20 ms = 4.3 J, 40 A × 20 ms = 8.9 J; keep I⁻² as an explicit assumption, make the region conditional, measure ∫v·i | **Confirmed** (over-claimed PASS) | the sheet (p.1 row: 0.02 s at 8 A; p.8 "typical"); the local exponent of the typical curve is 1.7–1.8 near 8 A — energy still falls with current, but nothing above 8 A is guaranteed | row → **WARN** with the assumption explicit (the 8 A max/typ ratio 3.8× carried up the typical curve: 0.5 J at 40 A; the full-20 ms sensitivity printed beside it); QP-RX-04 step 2 is a RELEASE CRITERION (trip ≤ 20 ms and ∫v·i ≤ 5.3 J at 0 / 0.09 / 0.5 Ω, cold / hot / post-trip); VR-16 asks Bourns for the maximum envelope above 8 A vs temperature and R_min at −40 °C. **F200** |
| md/html — prior dispositions | pin numbering, the RSX path, the LDO gate and the sustained-short WARN rows are correct and must be kept; the 42.43 / 48.91 mA hold arithmetic without PTC credit supports the part | **Already Fixed** (round 18) / agreed | — | none (the hold row credits R_min: 41.9 / 48.3 mA — same conclusion) |
| md/html — readiness | power-off bench READY; target routing, terminal-fault qualification, HV and production NOT READY | agreed | the IMCR placeholders are a deliberate build error (FW-24) | see "Readiness" |

## Review 2 — HTML recheck (A17-R01 html)

| ID | Review said | Class | Verification | Action in rev A.18 |
|---|---|---|---|---|
| A17-R01 (html) | MAJOR — the first completion callback creates the timing origin (`t_org = now_us − period`), so an 80 µs-late first anchor is accepted with an 80 µs stamp bias (19.2° at 10 000 rpm / 4 pp) and a re-acquisition absorbs a latency that was just rejected; anchor to an independent hardware event, keep frames untrusted until the absolute phase is established | **Confirmed** | `sdadc_ring.c` 147–150 and `resync()`; `s32k_sdadc_dma_irq()` stamps with `hal_time_us()` at execution; the round-18 agent's own "anchor residual" note | **FW-35 rewritten**: the origin is the SWG start instant on the STM timer (the SDADC blocks are triggered by the SWG period start), anchored by `hal_swg_start()` with a declared uncertainty (the enable-write bracket + `cal_swg_start_lat_us`); the ring never anchors itself; every completion — the first, and every one after a resync — is judged against the absolute cadence; resync derives the block in progress from the clock and checks each DMA slot against it (a mismatch is `lost`); on ambiguity a synchronized producer restart (`hal_sdadc_restart()`, ≤ `cal_rslv_restart_max` per key cycle, DTC counted). As built: k_org = 1 (the ring counts a block after it completes); every completion is checked against the absolute cadence in 64-bit time with the 3 µs uncertainty added to both sides of the allowance (first channel [−u, 30 µs + u] of its block's end, the others [−u, 50 µs + u]); early completions break the ring too (a case the agent found: an early completion moved the old origin onto itself); resync derives the count from the clock and uses the DMA positions only to confirm it (a looser rule than "every slot equals the index" so a legitimately late channel is not declared lost; a channel one period behind is caught within two periods); `hal_sdadc_init()` takes the start latency and stops the SWG; the 1 ms task restarts at most 3 times per key cycle (the count survives an MCU reset inside the cycle). Pre-fix reproduction: a first callback 29–800 µs late stamped its block with the full latency and the ring stayed unbroken; a constant 40 / 60 µs delay published 50 of 50 frames too new; after a break the same delay was absorbed (49 frames too new, the FOC angle 5.3° el behind the rotor at 10 000 rpm); a 5-period stall of all DMAs re-acquired with counts disagreeing with the clock. Residuals: T-40 (the deadline now counts from the carrier boundary and includes the SDADC output latency — margin to confirm), T-41 (error flags → lost), T-42 (start latency; SGEN STAT flags not wired to lost, their meaning undocumented); a rare unnecessary restart from a late-DMA channel whose interrupt is held off ≈ one period (bounded per key cycle); a restart at speed latches the resolver-invalid row (FW-28), as the frozen-channel case did in round 18. **F201** |
| html — diversion transients | six reduced-order transients confirm the corrected path (37 A PTC / 4.5 A diversion at 24 V / 0 Ω; 1.5 mJ in RSX) | **Already Fixed** (round 18) | agrees with the round-18 cross-check | none |
| html — A17-R02 | as Review 1 | **Confirmed** | — | as F200 |

## Review 3 — CSV register (A17-R01 csv)

| ID | Review said | Class | Verification | Action in rev A.18 |
|---|---|---|---|---|
| A17-R01 (csv) | MAJOR — the first origin is callback time minus a period; a block whose callbacks run 800 µs late is published with a fresh stamp (actual age 800 µs, reported 100 µs); a constant 40 / 60 µs delay passes the 30 µs limit over 39 frames; the resync from equal modulo-4 slots does not establish phase (a paused channel resuming 30 µs out of phase is accepted unless `lost` is set) | **Confirmed** | as Review 2; the slot-equality resync in `resync()` | as F201: the clock-derived block index with the per-channel slot check replaces the slot-equality inference; the platform contract that the SDADC/DMA error, overrun and trigger-miss flags set `lost` is written down (T-41) — a channel cannot resume out of phase silently on the target; the sim tests the case with `lost` unset and expects the slot/clock check to catch a lap, and a restart otherwise |
| csv — latency sign, freshness | the corrected sign passes 40 physical-time cases; the freshness helper passes 18 | **Already Fixed** (round 18) | — | none |

## Round 19's own findings

| Finding | Class | Action |
|---|---|---|
| The round-18 window row printed "unprotected ≈ 1–200 Ω" for every voltage: its criterion OR-ed "energy over the allowance" with "power over the steady state", so a 0.5 Ω fault at 167 W for 1.8 ms counted as unprotected | **Confirmed** (self-found, mine) | criterion corrected in the shared module — energy where the PTC trips, steady-state power where it never does — 14–83 Ω at 24 V, 6–34 Ω at 16 V, 16–95 Ω at 26 V, as the round-18 cross-check had computed. **F202** |
| The Marine kit's isolated 12 V rail: a sustained short with the card asleep leaves the TVS at V_BR·min(P_d/(V_S − V_BR), I_trip) — 3.8–4.3 W with the 8.5 V class at 11.4–12.6 V (a parked Road battery at 12.0–12.4 V reaches the same column), the low-V_P case; on the kit rail every short is in the unbounded region (the converter limits at ≈ 5 A) | **Confirmed** (the user's Marine request) | `marine-verify.mjs` §7 (1 PASS at the kit-loom minimum, 1 WARN, 1 INFO); Marine kit requirement ≥ 0.27 Ω on the exciter fault loop (design-basis §12, a routing/segregation rule); QP-MA-11 (the sweep at the ship's voltages, −25 / 55 °C); and the class change F203 below |
| Opus cross-check `xcheck19`: the 8.5 V class cannot be held below 148–171 °C coupled at a parked 12 V battery or the Marine rail; the 7.0 V class can (≤ 132 °C); the coupling DOES help the tripped case (refuting round 18); the class alone does not close the never-trip region (the coupling does, for every class) | **Confirmed** → **Improvement Recommended, taken** | **F203**: TVSEP/TVSEN → SMDJ7.0A-HRA (same SMC pad, same series, same price); the shared module, parts-db, ERC lock, card comments, dfm.md (the PAIR's transfers), IR-16 (+0.043 Ω each), QP-RX-04/05, VR-16 re-scoped, the coupled-island row added |
| Cross-check corrections of round-18 figures: the trickle formula was uncapped (5.2 W at 12 V where a tripped PPTC passes at most I_trip: 4.2 W); "V_max 33 V exceeded by 6 %" in the 35 V row is false while the TVS is intact (the PTC sees ≈ 26–27.5 V); I_R of the 8.5A is 20 µA; the plain SMDJ sheet's Z_th Fig. 6 (1 ms–1000 s) already answers most of VR-17; R_PTC and P_d are 23 °C figures | **Confirmed** | **F204**: corrected in the module and rows; VR-17 narrowed; VR-16 (c)/(e) carry the cold R and P_d |

## What stays open (numbered)

- **QP-RX-04 step 2** (≥ 8 A clearing: trip time ≤ 20 ms and ∫v·i ≤ 5.3 J, release criterion) and **VR-16** (the maximum envelope above 8 A; R_min at −40 °C).
- **QP-RX-04 step 2b / QP-MA-11** (the sustained-short sweeps, Road and Marine) with the layout island; **IR-42**; **VR-17**.
- **QP-RX-05 / VR-33** (the regulator's reverse current) — unchanged from round 18.
- **IR-16** at its round-19 values — the 26 V figure (0.09 Ω) is an allocation to confirm on the harness drawing.
- **Firmware**: T-41 (the SDADC/DMA error flags wired to `lost`), T-42 (the SWG-start latency and first-block phase on the target), T-40 (completion-interrupt latency distribution) — target measurements.

## Readiness (after this round)

| Area | Level |
|---|---|
| Power-off inspection / continuity / schematic-to-component reconciliation | READY |
| Schematic / BOM production freeze | READY subject to the release gates above; the TVS class change is a BOM line on the same pad (no layout or netlist change); the 24 V harness allocation (0.08 Ω) needs the OEM's statement or the 2512 fallback |
| Resolver acquisition-time integrity (the reviewers' NOT READY item) | closed in the image by construction (FW-35: an origin independent of every callback; every completion judged absolutely); T-42 confirms the origin uncertainty on the target |
| Target-controlled motor operation / HV / DVT / production | per `qualification-plan.md` — unchanged in nature; TI_FW_ID 0x0A0F0013 needs its EOL/HIL record |

## Counts and deliverables

ERC 969 / 0 · verify 156 PASS / 18 WARN / 0 FAIL / 26 info (the new WARN is the ≥ 8 A row; one PASS became that WARN; the coupled-island row is the new INFO) · sim 23 / 6 / 0 · Marine 87 / 23 / 0 (+41) · pin-verify 2057/2057, KiCad 10 proof PASS · BOM unchanged · BOM ₹ unchanged (the 7.0A is the same price class) · register F199–F204 · firmware 290 tests / 2705 checks / 0 failed in all three build flavours (host, −O2, ASan/UBSan), target-check 52 markers (T-41, T-42 new); on the pre-fix ring 108 failing checks in the 17 round-19 tests, 16 mutations each caught; TI_FW_ID 0x0A0F0013; new CALs cal_swg_start_lat_us 2 µs (0–20, HIL T-42) and cal_rslv_restart_max 3 (0–10); origin uncertainty 3 µs (bracket + 1 µs timer resolution + the start-latency CAL) = 0.72° el at 10 000 rpm; an anchor with u ≥ 25 µs is refused · `review-A18-disposition.csv` alongside.
