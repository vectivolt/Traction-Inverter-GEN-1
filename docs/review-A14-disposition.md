# Review round 15 — three rechecks of `a8c75eb` (rev A.14)

**Inputs.** Three independent delta rechecks of the A.13 push: (1) a full recheck with a six-item register
(A13-R01…R06) that compiled a C reproduction of the ADC macro and recomputed the native-KiCad coordinates
against the KiCad 5.1.12 source; (2) a recheck that executed 12 C cases / 174 checks against the SHA-verified
`bridge.c`, `safe_state.c` and timer helpers; (3) a recheck that reproduced a low-speed contactor-loss case
against the unchanged state-machine functions.

**Method.** Every claim verified against the source first (the exciter nets, the BOM package field, the ADC
`MAP[]` macro, the `mirrorLibY()` workaround and the verifier's placement formula, the `cont_lost` expression).
Fixes: schematic, ERC, BOM, verifier, exporter and docs by this session; the ADC triple and the contactor-loss
policy by an Opus agent with fail-before/pass-after tests.

## Verdict

All three rechecks are right. Two of the round-14 additions were wrong in a way the round-14 verifier could
not see: **the exciter TVS was drawn on the connector node**, so an external battery fault fed it without
passing through the PTC (the verifier modelled the PTC in the path), and **the shipped KiCad files were the
EasyEDA-import variant only** — native KiCad applies the orientation matrix that EasyEDA's importer ignores, so
a native reader of the pre-mirrored library would have connected H5 to FLT_CLR_M. Both are corrected at zero
BOM cost (two net moves, one 2.2 Ω resistor per line, a second output folder, a matrix-aware verifier with a
mutation test). The PTC package, the ADC channel class and the low-speed contactor-loss policy are corrected
with them. The round-14 corrections stand.

**"Already Fixed" for a gate means the gate exists and is open** — never that the physical qualification is
complete.

## Review 1 — full recheck (A13-R01…R06)

| ID | Review said | Class | Verification | Action in rev A.14 |
|---|---|---|---|---|
| A13-R01 | CRITICAL — the exciter TVS is on the connector node; the fault current bypasses the PTC (25 A / 287 W at 24 V, 46 A / 555 W at 35 V with nothing to interrupt) | **Confirmed** | `TVSEP.K` was on `VREX_PC` with `JVEH.R1`; the verifier's `rPath` assumed the PTC in series | TVSEP/TVSEN moved to the protected node `VREX_PX/NX` (amplifier side of FEXP/FEXN); RSXP/RSXN 2.2 Ω between the amplifier and that node; the REXM monitor taps the protected node; ERC graph cut: the connector node carries only JVEH and the PTC, and a TVS on a connector node fails; verifier rows recomputed with the PTC in the path (15.3 A / 27.7 A, TVS 3.4 J / 3.2 J vs ≈ 8 J). **F159** |
| A13-R02 | MAJOR — "no back-drive" assumed VEXD ≈ 12.1 V; with the rail absent/cranking the ALM2402 reverse diode conducts (not current-limited, pulse-rated); the bidirectional TVS does not enforce the −0.3 V output limit | **Confirmed** (argument scope) | ALM2402 §8.3.6 | rows for the VEXD-absent pulse (4.9 A for ≈ 48 µs through RSX until CLDE charges, rail then ≈ 10.8 V ≤ 18 V abs max) and the negative fault (4.4 A through the lower diode until the PTC trips); gate ㉘ widened to VEXD off/cranking/on and both polarities, PTC and TVS currents and the reverse rail current measured separately. **F160** |
| A13-R03 | MAJOR — MF-MSMF020 bound to a 1206 package; the family is 1812 | **Confirmed** | Bourns dimension table 4.37 × 3.07 mm | fp 1812; part changed to MF-MSMF020/33X (33 V, 40 A, 0.02 s at 8 A — the unsuffixed 30 V part is flagged for new designs). **F161** |
| A13-R04 | MAJOR — NTC_A on ADC5_S11 but `MAP[]` uses `S32K3_ADC_CH('P', …)` → PCDR[11] instead of ICDR[11] | **Confirmed** | macro evaluates 11 vs 43 | the board-map generator emits the instance/subtype/channel triple for every ADC input and `MAP[]` uses it; regression test; the MT2_SIG (ADC1_P0) and HW_ID (ADC3_P0) schedules derive from the same data. **F162** |
| A13-R05 | CRITICAL — the shipped KiCad files pre-mirror the library for EasyEDA's importer while native KiCad 5 applies the "1 0 0 −1" matrix; the verifier ignores the matrix — read natively H5 → FLT_CLR_M, J7 → ASC_CLR_M | **Confirmed** | `mirrorLibY()`, `c.x + p.x / c.y + p.y` | `kicad5-gen` now writes `kicad5/traction-native/` (same sheets, un-mirrored library, README, its own zip) beside the EasyEDA-import folder (README added); `kicad5-verify` checks both with their consumer's placement rule (native: the parsed matrix applied before the position); the mutation script flips the MCU's matrix in the native sheet and must see the verifier fail. **F163** |
| A13-R06 | MINOR — the BOM quoted 0.13 A hold at 85 °C; the table gives 0.09 A (0.13 A is 60 °C) | **Confirmed** (docs) | Bourns p.9 | corrected; 40–60 mA excitation vs 90 mA hot hold. **F161** |
| target bindings | the IMCR/REG_PROT bindings remain intentionally unfilled | **Already Fixed** (gate, fail closed) | — | unchanged: the target refuses to arm without them |

## Review 2 — SHA-verified firmware recheck

| ID | Review said | Class | Verification | Action |
|---|---|---|---|---|
| A13-R01 | same topology defect | **Confirmed** | as above | **F159** |
| A13-R02 | same supply-off condition; "the clamp avoids reverse current entirely" must not be kept as an all-state claim | **Confirmed** | — | wording removed; rows and gate as above. **F160** |
| previous A12-R01…R08 | corrected (H5/J7, pin numbers, CBAL, DESAT hold, clock, ballast, keep_hv) — 12 cases / 174 checks pass | **Confirmed closure** | their harness | none |
| amplitude | evaluate the 6.5 V pp floor at the resolver, after the series parts | **Improvement Recommended** | — | judged row: 7.0 V pp cold through RSX + PTC (6.4 V pp for an hour after a trip — the FW-10 window then flags the line, as intended) |
| TVS pulse data | the SMCJ curve ends at 10 ms; 60 ms was an extrapolation | **Confirmed** (wording) | — | the corrected trip time (≤ 20 ms from the 8 A point) is bounded against the 10 ms point; the bench measures it |
| LDO analytic maximum | P_pass,max = (Vs−Vo)²/4R = 0.53–0.60 W above the 96 mA budget | **Improvement Recommended** | — | INFO row added (0.60 W at 113 mA → 120 °C / 135 °C); the hot first article measures the loaded current |

## Review 3 — low-speed contactor loss

| ID | Review said | Class | Verification | Action |
|---|---|---|---|---|
| A13-R01 | same topology defect | **Confirmed** | as above | **F159** |
| A13-R02 | MAJOR — `cont_lost` requires n ≥ n_x, so a known low-speed OPEN/INVALID contactor report does not raise the battery-path-lost row; `st_run()` sets arm/torque_enable before seeing the open contactors and returns without clearing them | **Confirmed** | `app.c detect()`, `state_machine.c st_run()` | loss detected at any speed while armed (OPEN, INVALID, stale explicit); ordinary torque permission removed in the same invocation, only the §6-selected zero-torque/current-control or ASC action kept; tests across speed × feedback × current. **F164** |
| previous findings | corrected at their locations; 47 Ω ballast sound (0.574 W / 118.6 °C at the corners) | **Confirmed closure** | — | none |

## Self-found in this round

| Item | Fix |
|---|---|
| The register-row splice defect of round 14 recurred once during this round (an escaped newline in an edit script) and was caught by the per-row count check | rows relocated; the check is in memory |

## Marine

The A.14 deltas were ported into `marine/` (shared card: exciter topology, PTC package; firmware behaviours;
the KiCad variant note). `npm run marine`: 86 PASS · 22 WARN · 0 FAIL, unchanged counts. M8 forks at Road rev A.14.

## What stays open

Unchanged gates ②③④⑤⑥⑦⑨⑩⑫⑬⑭⑮⑰⑱⑲ ⑳㉑㉒㉓㉕㉖㉗ and the target evidence the firmware demands
before arming; ㉘ widened (VEXD states, both polarities, PTC/TVS/reverse-rail currents). The native-KiCad
variant is verified by the placement rule and a mutation test, not yet by an export from a running KiCad
instance — do that once at layout kickoff.

## Register

F159–F164 in `calculations/design-verify.mjs`; the CSV beside this file lists every review ID with its class.
