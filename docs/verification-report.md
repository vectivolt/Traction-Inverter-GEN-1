# Design Verification Report — rev A.16 (2026-09-25)

End-to-end verification of the 220 kW / 800 V traction inverter at actual operating corners
(V_bus 500–850 V · KL30 9–16 V · 10 kHz · 65 °C coldplate), worst-case component tolerances.
Three independent layers:

1. **Geometric pin-verify** (sheets vs netlist, `kicad5-verify.mjs`) — the sheets ship only at 100 %
2. **Structural ERC audit** (netlist vs design intent, `erc-audit.mjs`) — 0 fail, incl. a lock-in per fixed finding
3. **Numeric verification** (this report, `design-verify.mjs`): **157 PASS · 15 WARN · 0 FAIL** (+23 info)

A WARN is an item this analysis cannot close on paper — each names its bench or vendor gate.
Every SKU of the platform (8XX/4XX × SiC/IGBT — `loss-model.mjs`) is checked on the
same PCBs; losses and thermal use the shared model that `sim-verify.mjs` also runs.

## Findings log (F1–F36 rev A.3 campaign · F37–F46 rev A.4 · F47–F51 rev A.4.1 · F52–F57 rev A.4.2 · F58–F59 rev A.4.3 · F60–F62 rev A.5 docs audit · F63–F76 rev A.6 external review round 6 · F77–F89 rev A.7 review round 7 · F90–F97 rev A.8 review round 8 · F98–F105 its cross-check · F106–F113 rev A.9 review round 9 · F114–F119 its cross-check · F120–F122 rev A.10 schematic rechecks · F123–F134 rev A.11 system review · F135–F142 rev A.12 rechecks, pin freeze and production closure · F143–F158 rev A.13 round 14 · F159–F166 rev A.14 round 15 · F167–F173 rev A.15 round 16 (three rechecks of 32214be) · F174–F184 rev A.16 round 17 (gap closure) · F185–F189 rev A.16 round 17 (LV entry: test-B let-through, pulses 1/2a, FLVC, KL15) — all fixed; review cross-reference in [`review-A6-disposition.md`](review-A6-disposition.md), [`review-A7-disposition.md`](review-A7-disposition.md) and [`review-A8-disposition.md`](review-A8-disposition.md))

| # | Severity | Finding | Fix |
|---|---|---|---|
| F90 | **HIGH** | A DESAT during latched ASC did not reach SPO: the faulted NSI6611 holds its own gate off (DS Fig. 8.11), but the healthy low sides stayed on through ASC, and a reset edge with the latch still set re-energised the faulted switch (R7-01/A7-N01) | UASCG: ASC_CMD = latch AND no-FLT on every path (FS1B-ASC included); FW-15 clears the ASC latch before the recovery; §4c transition table |
| F91 | MED | The FLT diode-OR reached the fault-latch preset at 111–135 ns/V (R7-02); buffering it exposed a 1N4148 low level at the Schmitt threshold (N12) | USCH2 74LVC3G17-Q100 on both latch presets; DFLT1/2 BAT46 |
| F92 | MED | Discharge TLP152 driven 470 Ω from the MCU pin: 6.4–6.8 mA vs 7.5 mA I_FLH max; the ASC opto proof used an assumed output drop (R7-03/A7-N03) | QDIS_CMD from USCH2 ch3, RQDL 270 Ω; both LEDs 9.6–14.2 mA from the guaranteed LVC V_OH point and V_F cold |
| F93 | MED | FW-16 boot test masked by RDY with gate power off; single-polarity (R7-04/A7-N02) | sensitized test with gate power up, before precharge, both polarities; coverage stated |
| F94 | MED | FAULT_OUT short to KL30 back-fed V5A through DSET/RENP2 (≈8 mA with V5A off) (A7-N04) | ZSET to ground, RENP2 removed: ≤ 0.6 mA into V5A |
| F95 | LOW | bom-gen wrote per-board CSVs before the semantic checks, so a failed run overwrote good outputs (A7-N05) | validate first, then publish atomically (temp + rename); mutation-tested |
| F96 | LOW | S4 called a 0.8 s junction pole conservative, and its case node delayed plate heating (R7-07, N14) | junction-case and case-plate static (an upper bound at every instant); +3 °C |
| F97 | LOW | Contract said the NSI6611 reset needs EN low ≥ t_FLT_MUTE; the DS (Fig. 8.8) has a mute time from the fault plus a ≥ t_RST_FIL pulse after it (N13) | §7 corrected; FW-15 unchanged |
| F98 | MED | Contract (cross-check R8X-01/02/03): FW-15 could leave the ASC latch set, so ASC returned at the reset edge with PWM held low (LS on with IN+ low — DESAT undocumented); FW-16 relied on sequence position, not measured V_DC/speed, for its HV-free step a; FW-16 d/e could not drop RDY while FS_GPIO1 held the flyback OR, and the FS_GPIO1 input was never tested | FW-15 always clears ASC (re-entry only through §4c); FW-16 measured preconditions, VCU precharge handshake, stored-pass arming; d/e drive FS_GPIO1 low, new g tests the OR; new h injects FLT (latch + mask covered in the field); FS1B_TDELAY = 0 required |
| F99 | MED | Dead-state gaps (R8X-06 + self-found): a Hi-Z USCH2 floated the fault-latch preset and the ASC mask — FW-16 passed with both gone; behind UASCG a Hi-Z ULAT floated the gate input (round 8 had moved the ASC pull-down off the latch) | RFCB 100 k on FLT_CMB_B (dead USCH2 = latched FLT: SPO, ASC masked, FW-16 b fails); RASCP moved to ASC_Q (dead ULAT = no ASC) |
| F100 | MED | QDIS_CMD sat next to V15 on the 4-way discharge header: a pin short put 15 V into USCH2 — which also carries both latch presets — and back into V5A (R8X-07) | header V15-GND-CMD-GND on both boards, locked by pin number |
| F101 | LOW | TLP152 LEDs 9.9 mA at the consistent cold corner, under the 10 mA recommended I_F; the model paired the 125 °C output resistance with the −40 °C V_F and omitted the far-end pulldown (R8X-04) | RASCL = RQDL = 261 Ω 1 %: 10.3–14.8 mA, judged against 10–15 mA |
| F102 | LOW | ZSET C5V6 "≤ 6.0 V" held only at 25 °C/5 mA: 6.62 V at a 35 V load dump on a KL30-shorted FAULT_OUT at 125 °C, over the USCH2 6.5 V abs max (R8X-05) | BZT52-B5V6 (±2 %): ≤ 6.34 V. C5V1 rejected — its soft knee through RFS2 would sag the released level toward V_T+ |
| F103 | LOW | FLT_LS_N had no glitch filter (CFLTF only on FLT_HS_N); a glitch now drops ASC and latches SPO with no automatic retry (R8X-12) | CFLTF2 100 pF |
| F104 | LOW | Lock-ins missed the FLT diode-OR, the FS1B back-feed by net and all of parts-db — a moved DFLT2 anode and an added FS1B pull-up both passed 854/0; two A.8 rows were fixed PASS (R8X-08) | ERC by net, pin number and first-match MPN per SKU; both rows computed; every new lock-in mutation-tested |
| F105 | LOW | S4 "true upper bound" held only for the assumed 60 s plate pole; VCC2 used 50 % FB-sense conduction on every corner; UASCG delay 4.4 ns (R8X-11/16) | static-plate bound reported (8XX IGBT 142 °C, WARN); per-corner conduction 1/0.5/0.2; 5.5 ns |
| F106 | MED | PSASC/PSQD are QA01C-18 (+18/−3 V), but F61 had bound the base QA01C sheet (+20/−4 V): the ASC timing assumed 18–24 V and the discharge gate ≈19.5 V (A8-N01) | QA01C-18 envelope 16.9–20.9 V modelled (Mornsun Fig. 1, line, tempco): ASC entry 7.07 → 7.52 µs, FW-06 end-point 905 → 906 V; MPN locked |
| F107 | MED | QDIS gate followed the bias rail through 47 Ω: up to 20.9 V against the +18 V recommended / +22 V abs, with the < 10 % load region extrapolated (A8-N02) | RQDG 1.5 k with RQDPD 10 k: V_GS 13.3–18.2 V with 1 % resistors (a divider, not a clamp — F122) |
| F108 | MED | FLT/RDY pulled up to V5A while the drivers' VCC1 is V5GD: 0.2 V over the FLT/RDY abs max (VCC1, no +0.3 V) at rail corners, and an undefined V5GD-loss state (A8-01; P-03, R8X-10) | pull-ups and the diode-OR pull-up on V5GD via harness pin 39 (DGND/HW_ID neighbours), RV5GP for an open pin: a lost V5GD is SPO, deterministically |
| F109 | MED | FW-16 allowed its ASC-actuating steps below 60 V: 0.62 J (8XX) / 1.54 J (4XX) at C_max into a latent high-side short (A8-N03) | eligibility < 12 V read (≤ 30/75 mJ), QDIS top-up from ≤ 60 V, n_ss from E_LL,pk |
| F110 | MED | Self-found (N17): the power board's LV side — V15 boost and four bias modules, V5GD, flyback controllers — ran on unswitched KL30, ≈150 mA while the vehicle sleeps | card-side P-FET switch (DMP6023LEQ) on V5A: ≤ 1 µA asleep |
| F111 | LOW | RFS4's 0.23 W case was only 16 V; 24/35 V reach 0.48/0.65 W and 0.23 W continues if FS1B is never released; BACKUP_SAFETY_PATH_FS1B = 1 repeated it through RSTB (A8-03) | RFS4 = ROHM ESR03EZPF1001 (0.27 W at 85 °C, ≈1.3 W/5 s); BACKUP_SAFETY_PATH_FS1B = 0 |
| F112 | LOW | The 10–15 mA LED window used Toshiba's typical V_F tempco as if guaranteed (A8-02) | ±28 % tempco band at every corner; the row judges the guaranteed I_FLH and abs-max margins first |
| F113 | LOW | "Battery opens in the SPO window = double fault" assumed independence (A8-G02) | §6 motor/vehicle release rule: energy-safe SPO, proven keep-connected, or the total-KL30-loss backup |
| F114 | MED | Cross-check R9X-01: a dead V5GD unpowers the AMC1311 LV sides, so the receivers sit at their +0.5 V offset — a valid-looking "0 V bus" (850 V could be reported as discharged); the N18 discriminator relied on it | V5GD read on PTB5 (RV5GP/RV5GS 2 × 47 k); V_DC invalid and SPO forced outside 4.75–5.25 V; also catches a back-powered, hovering V5GD (R9X-06) |
| F115 | MED | R9X-02: the drawn 2×20 harness numbers row by row, so VBAT faced ASC_CMD/QDIS_CMD/FLT_LS_N/RDY_HS and V5GD faced VDC1_P; "MICROFIT3-40" does not exist | HARNESS40 re-laid so VBAT and V5GD touch only ground or their own rail in both row-by-row and odd/even numbering (ERC checks both); class MPN until the layout picks the family |
| F116 | MED | R9X-03: ULDOEX (resolver exciter LDO) had INH on VBATC — ≈0.9 mA in FS26 LPOFF; the parking row summed the power board only | INH on V5A; the row now sums every unswitched load (≤ 43 µA at 25 °C) |
| F117 | LOW | R9X-04/10: QLVS turned on through its gate-drain charge (9–31 A into ≈45 µF at each wake), and a hot 2N7002's leakage × 100 k could half-close it while parked | 10 k/4.7 k gate network + 1 k/100 nF drain-gate: ≤ 1.1 A inrush, ≤ 0.5 V leakage drive |
| F118 | LOW | R9X-07/08: FW-16 trusted a 1 V low-voltage V_DC error (≈ ±9 V real); V5GD now follows V5A, so an FS26 restart erased a latched DESAT | read < 3 V or QDIS for 2 τ; FW-15 writes the DESAT to NVM and §9 gates arming on it |
| F119 | LOW | R9X-05/09/11/12/14: QLVS/QLVN G/S label swaps and a V5A–V5GD bridge passed the ERC; release rule (c) claimed the backup could cover the V5GD row; 25 V caps on the 20.9 V bias rails; RFS4 with FS1B held through a jump start unlisted; stale texts | pin-number and no-bridge locks; (c) limited to total KL30 loss; CASC/CQD 50 V; RFS4 held-case row (WARN, accepted); texts fixed |
| F120 | MED | Schematic recheck A9-S01: RDY_HS/RDY_LS (open-drain, 5.1 k to V5GD) drove 74LVC1G11-Q100 AND inputs directly at ≈20–100 ns/V against the 10 ns/V recommended limit; the round-7/8 "state-benign" rejection left the shutdown chain outside the datasheet | USCH3 74LVC3G17-Q100 on both RDY lines (RRDB dead-state pull-down); ERC by pin number |
| F121 | LOW | S9-01: RASCG (2.2 k) carries up to 0.12 W for as long as ASC is held, but was a generic 0603 (0.1 W at 70 °C → 82 mW at 85 °C) | ROHM ESR03EZPF2201 (0.33 W at 70 °C → 0.27 W at 85 °C); 2.2 k kept for the ASC timing |
| F122 | LOW | S9-02 and notes: discharge sheet heading "67.5k bleeder (58 s)"; "V_GS 13.4–18.1 V" read as a hard ceiling; the A9 disposition still quoted the interim pin-39 V5GD pin | 66 k (56 s nom / 65 s worst); 13.3–18.2 V with 1 % resistors ("a divider, not a clamp"); the final harness map quoted |
| F123 | **HIGH** | §6 release rule (a) accepted SPO on back-EMF alone and omitted the stored winding energy the diodes rectify into an isolated link: 1.5·L·I² = 61 J (8XX) / 84 J (4XX) with the 0.35 mH screening motor against 40 J of link headroom (R1-F01, R2-F08) | rule (a) now requires ½·C_min·(U_N² − V_max²) ≥ W_mag + W_emf from the motor's dq model; screening row (WARN): 8XX covers that motor only to 277 A rms, above it rule (b) |
| F124 | **HIGH** | Resolver exciter drawn with the MFB feedback pair swapped (4.7 nF output→summing node, 24 k output→inverting input): first-order 2 kHz roll-off, |H(10 kHz)| 0.18, so the S32K39 SWG (≤ 2.30 V pp) would have driven ≈ 0.7 V pp instead of 8 V pp (R2-F04) | textbook MFB 10k/24k/10k with 1.5 nF to ground and 220 pF feedback: f0 17.9 kHz, Q 0.70, |H| 1.85 → 7.7 V pp; SWG amplitude is the firmware knob; ERC by topology and value |
| F125 | **HIGH** | VDC-channel bias "MGJ2D150505SC" is not an existing order code, and the MGJ2 family is reinforced only to 150 Vrms (R1-F12, R2-F02/F03) | TI UCC12050 per channel (V_IOWM 1200 Vrms / 1697 VDC, VDE 0884-11 reinforced, the AMC1311's own class) behind its own NCV4276C from V15; 10 µF X7R both sides; ERC by pin |
| F126 | MED | The Hall open-wire pull-down claimed in README/design-basis since A.4 was never drawn (R1-F03) | R⟨ph⟩B0 100 k at each card input: open wire or dead sensor reads 0 V, outside the 0.2–4.8 V validity window now in FW-05 |
| F127 | MED | LEM HC5FW drawn as a 3-pin VCC/OUT/GND symbol; the device has 1 V_ref, 2 V_out, 3 Gnd, 4 U_C and E1–E4 mass pins and no connector (R1-F04) | 8-terminal symbol by DS number (V_ref open, E1–E4 to Gnd); the off-board carrier drawing derives from it; ERC by terminal number |
| F128 | MED | A resolver wire shorted to KL30 (shares the vehicle connector) injected ≈ 23 mA into an SDADC pin through 330 + 120 Ω against the S32K39's 3 mA limit (operating and absolute maximum), and both legs pulled 20 mA into the VMID buffer through the 680 Ω bias (R2-F13, R1-F20) | RSINF/RCOSF and RSIN/RCOS → 10 k: 1.0 / 1.8 / 2.9 mA at 16 / 24 / 35 V, buffer 2.7 mA; caps rescaled (47 p + 100 p differential, 22 p common-mode on both legs — the P-only caps converted CM to DM): corner 47 kHz, −12° on both channels; SWG given its 47 pF load |
| F129 | LOW | VREF5 sat at 3.3 µF nominal, the FS26 upper limit, before tolerance (R2-F11) | CSB5 2.2 µF → 1 µF (0805 X7R 16 V): 2.1 µF nominal, 1.4–2.3 µF effective over tolerance, bias, temperature and aging |
| F130 | **HIGH** | LV-entry coordination (R1-F14, R2-F10, found by the Opus check): the notes described the classic TPSMC24CA (V_BR 22.8–25.2 V), which conducts at the 24 V/60 s jump start and, in an ISO 16750-2 test B at Ri ≤ 2 Ω, takes more than the (extrapolated) 400 ms capability; at 0.5 Ω the polyfuse then trips inside the pulse, sees ≈ 30 V against its 24 V V_max and latches until a KL30 cycle; the archived datasheet was the -VR series | TPSMC24CA-VR bound explicitly (AEC-Q101, 24 V stand-off, V_BR 26.7–29.5 V, same pad and price): dark at 24 V, clamps ≤ 33 V under the boost's 34 V abs max; test B covered on paper at Ri ≥ 4 Ω, the OEM's Ri is gate ㉗ (≤ 2 Ω: TVS pulse test or 35 V-rated rails). Polyfuse kept (the 33 V part holds 1.17 A hot against the 1.19 A worst chain) |
| F131 | LOW | UEXD (ALM2402QPWPRQ1) bought as "HTSSOP16"; the PWP package is 14-pin (R1-F05, R2-F26) | footprint HTSSOP14-PWP; ERC asserts the 14-pin package against the symbol |
| F132 | LOW | Board NTCs drawn as 2-pin headers, bought as 0603 NTCs (R2-F27) | on-board RTAMB/RTHS 0603 (symbol = BOM) |
| F133 | LOW | kicad5-verify exited 0 on an empty page set (R2-F29) | fails unless 4 sheets and ≥ 1500 pins were compared |
| F134 | LOW | Contract and document defects: FW-05 threshold in A rms; §6 merged "contactor open" with "BMS limit 0"; FW-15 "NVM first" ahead of the safe action; FW-02 τ bands overlap; HW_ID "pin 40"; "LQFP-176"; stale "inside 6 µs" IGBT text; ALT field offered M7 rectifiers and a 3-lead TO-247; CAN termination fixed; FW-18 silent on invalid witnesses; S6 double-update unstated (R1-F02/F11/F18/F23/F24/F25/F27, R2-F18/F19/F24/F33/F34/F35) | all corrected: instantaneous ±601/±707 A; rows split; retained-RAM latch, queued NVM; plausibility wording + EOL measurement; pin 2; 289-MAPBGA; RR04 wording; ALT = qualify-before-use; endpoint DNP option; "unknown, never safe"; delay stated |
| F135 | **HIGH** | Round 13 (A11-R01 ×2): rule (a) was fixed but the §6 matrix stayed speed-split — standstill freewheel of the 0.35 mH screening motor at 340 A rms takes an isolated 8XX link to 1089 V from the trip; the KL30 row still cited the back-EMF test and rule (b) applied only at n ≥ n_x | column note: the energy condition applies in both columns; battery-path-lost row at n < n_x uses FW-06 LS-ASC as the energy sink; KL30 row cites rule (a); rule (b) at every point where (a) fails, barred where the premise is the lost battery |
| F136 | MED | Round 13 (A11-R02/R03): the 10 k injection bound assumed a powered 5.7 V clamp — unpowered, 35 V gives 3.42 mA against the 3 mA absolute limit; and the SDADC anti-alias capacitor (C_AAF 180 pF min, Table 38) had been cut to 100 pF | RSINF/RCOSF and RSIN/RCOS 12 k (2.92 mA at 35 V into 0 V, −1 % R; VMID buffer 5.4 mA); 220 pF C0G at the pins; corner 23 kHz, −24° both channels; the channel-matching bound is stated (1.3° from the Z_DIFF corners by the verifier's amplitude-ratio form; the recheck's 1.09° used a phase form — the larger is carried) instead of "ratio-cancelled" |
| F137 | LOW | Round 13 (A11-R05/N01): the verifier's guard counted JSON pages, not distinct assemblies | exact set {power, capbank, disch, card}, no duplicates, ≥ 1500 pins |
| F138 | LOW | Round 13 (A11-R06 / R03): the LV-entry argument used the commercial TPS55340's 34 V absolute maximum (and an older row 45 V) — the fitted -Q1 part is 38 V recommended / 40 V absolute, so the "≥ 42 V boost" premise was false | model, BOM and gate ㉗ corrected; the commercial part marked PROTO ONLY |
| F139 | LOW | Round 13 (R04): the V_DC bias LDO thermal row assumed 40 K/W; the NCV4276C DPAK reference pad is 58.5 K/W and the UCC12050's 50 mA is typical | 0.76 W worst → Tj ≈ 129 °C at 85 °C on the reference pad (WARN); ≥ 1.2 in² 2 oz copper per LDO in dfm §4; measured at the hot first article |
| F140 | **HIGH** | Pin freeze (rev A.12): the A.4 "GEN3-exact" MCU port list was symbolic and partly wrong — PTG10 has no PWM output, PTB0/PTB4/PTB5/PTF5 no ADC, PTB2/PTB3 are external-mux ADDRESS outputs, PTA10 is JTAG_TDO, PTA11–13 do not exist on the 289-MAPBGA | all 289 balls bound from SPF-91122 rev C (anchored on the DS supply balls): PWM_1 A/B pairs on PTC31/PTA6, PTC30/PTA7, PTC29/PTC8 with FAULT0/FAULT2 on PTC26/PTC25; 17 analog inputs on ADC-capable balls with the currents on three instances; SDADC pairs as GEN3; FS26 48 pins verified; ERC by ball |
| F141 | **HIGH** | Barrier parts without a published working voltage (gates ⑪/㉔): the TLP152 optos and the QA01C-18 bias modules on the 850 V link carried UL1577 test voltages only, no V_IORM/V_IOWM | Vishay VOW3120-X017T (V_IORM 1414 Vpk, DIN EN 60747-5-5, CPG ≥ 10 mm) and TI UCC14141-Q1 (reinforced, V_IORM 1414 Vpk / V_IOWM 1000 Vrms, DS §7.5 — certificates listed "planned", gate kept for the status) drawn on both boards in the single-output configuration; LED resistors 270 Ω for the lower V_F; Y-caps bound to Vishay VY1 (Y1 500 VAC / 1500 VDC) |
| F142 | MED | Orderability: the harness connector, the 4XX capacitor can and the discharge resistors were class placeholders ("HARNESS-2x20-CLASS", generic can, generic 10 W) | Samtec IPL1-120-01-L-D-K / IPD1-20-D-K / CC79L crimps (−55…125 °C, 3.8 A/pin, positive latch; no 2.54 mm family states AEC-Q200 — A-Series MPN to confirm, pin numbering to confirm against the print), Faratronic C3D1U506KFAA382, TT SQP10-470RJB15 / -220RJB15 |
| F143 | **CRITICAL** | Round 14 (A12-R01/R02): two MCU supply balls were wrong in the A.12 freeze — H5 (V15, the 1.5 V core input, 2.75 V abs max) carried the 5 V VREF5; J7 (V25, the 2.5 V flash-regulator output) was grounded | H5 → V15S; J7 → V25 + CV25 220 nF; the whole map re-derived against NXP's GEN3 net report (U513, 244 balls): every supply ball and 164/165 port names agree; the four signals on GEN3-open balls moved to netlist-confirmed balls (B5/T15/D5/U4) |
| F144 | **HIGH** | Round 14 (A12-R03): the exported MCU symbol numbered its pins 1…130 with the ball only in the name — not a physical package binding | pages.mjs emits the ball ID as the KiCad pin NUMBER for '<ball>_<signal>' labels; kicad5-verify fails unless every MCU X record's number equals the manifest ball for its label |
| F145 | LOW | Round 14 (A12-R04): the NMOS ballast network lacked NXP's 1 nF gate-stability capacitor (DS Table 13) | CBAL 1 nF C0G, NMOS_CTRL to VSS |
| F146 | **CRITICAL** | Round 14 (A12-R05): the DESAT ISR called br_spo(true), dropping the undelayed MCU_GATE_EN and bypassing the 22–53 µs hardware FLT → DRV_EN delay that protects the driver's soft turn-off | bridge module enforces a DESAT hold (CAL 60 µs ≥ the RC upper corner) for every caller; PWM inhibit stays immediate; regression tests |
| F147 | **HIGH** | Round 14 (A12-R06): now_ms() = us/1000 of a wrapping 32-bit counter — the ms value jumped 4 294 967 → 0 after 71.6 min, so unsigned ages became ≈ 4.29·10⁹ ms (false VCU/BMS staleness) | 64-bit monotonic µs clock in the HAL, ms derived from it (wraps at 2³² ms consistently); every /1000 timestamp producer audited; wrap-straddling tests |
| F148 | **HIGH** | Round 14 (A12-R08): keep_hv was speed-only while §6 credits battery retention at every speed — a low-speed high-current SPO relied on the battery without requesting it (0 rpm, 340 A: rule (a) false, rule (b) true, keep_hv false) | keep_hv derived from actual reliance on rule (b), held until rule (a) holds or ASC is active; propagated to the CAN status; lost-battery rows cannot borrow (b) |
| F149 | MED | Round 14 (A12-R07): the bias-LDO model used 76 mA (50 mA typ × 1.2) — the production UCC12051-Q1 draws 80 mA MAX at no load (96 mA with the AMC1311 → 0.96 W, Tj 141 °C) | R5L 47 Ω ballast per LDO (LDO ≈ 0.5 W, Tj ≈ 116 °C; resistor 0.45 W in a 2512); UCC12051QDVERQ1 promoted to the primary MPN, UCC12050 the proto fit |
| F150 | **CRITICAL** | Round 14 (F01): the target fault routing (IMCR indices / SSS for PTC26/PTC25 → PWM_1 FAULT0/2) was placeholder 0u | placeholders are a build error; the host build reports the route unbound and arming is refused (fail closed) until the RM-derived values are filled and the route validated |
| F151 | **CRITICAL** | Round 14 (F02): hal_pwm_config_locked() returned true from a register readback while REG_PROT/XRDC protection was a TODO — "configured" was presented as "write-protected" | config_matches() and protection_locked() separated; the lock witness reads the lock bits or returns false; arming-evidence record (route bound, config, lock, fault/OVP routes validated by an NVM-stored EOL record) gates gate-enable |
| F152 | **HIGH** | Round 14 (F03/F05): the excitation-monitor dividers (5.1 k) let 5.5–5.7 mA into the SDADC pads at a 35 V wire fault (3 mA limit) and the pair had no C_AAF | 18 k / 42.2 k / 84.5 k (1.96 mA at 35 V, 2.81 mA at 50 V, same ratios) + CEXM 220 pF; 3.0° fixed reference offset absorbed by FW-20 |
| F153 | **HIGH** | Round 14 (F04): the ALM2402 outputs reached the vehicle connector unprotected — a 24/35 V harness fault exceeds its 18 V output rating whatever the supply does | SMCJ8.5CA at each connector node (clamps at 10.6–11.4 V, below the 12.1 V rail + a diode: no reverse current into the ALM2402) + an MF-MSMF020 PTC (0.2 A, 30 V, AEC-Q200) per line; fault currents, TVS energy and amplitude loss in the register; bench gate ㉘ |
| F154 | MED | Round 14 (F18): the motor-temperature lines relied on an ESD-class PESD5V0L1BA and an unbound "350 mA class" fuse against a KL30 short | SMAJ5.0A (400 W) carries the ≈ 16 A for the ≈ 16 µs the Littelfuse 0438.375WRA (0603, 63 V, I²t 0.0041 A²s, AEC-Q200) takes to open; the 1 k series resistor limits the buffer input to 3.9 mA |
| F155 | **HIGH** | Round 14 (F23): torque_to_current() clamped id/iq after the field-weakening voltage calculation without a final feasibility witness — a finite but physically infeasible current request could be returned | final voltage-magnitude check after the clamps; iq (then id) reduced until feasible; an explicit infeasible status → zero torque + speed-limit request + DTC; motor-map sweep test |
| F156 | LOW | Round 14 (F24): the "any single-channel fault is detectable by KCL" claim fails for a stuck-at-zero channel at zero current and for equal gain errors | activity check per channel above a current threshold, coverage table per operating state (firmware docs); the window/range/stale checks kept; no fourth sensor |
| F157 | MED | Round 14 (F09): a shorted QDIS with the battery connected bypasses the software timeout (4 × 96 W in the resistors) and nothing detected it | firmware detects an unexpected discharge at the next contactor opening → latched DTC, no re-energisation, contactor-open request; the resistor's benign failure at 96 W stays gate ㉖ |
| F158 | LOW | Round 14 (N03, F17, F21, §3): stale DTVH/DTVL text (34 V boost, "Ri ≥ 4 Ω covered"), CAN termination population per vehicle, "the paperwork is the cost" wording, an "800 V inverter" claim against a 500–850 V contract, the 4XX-as-220 kW reading | BOM texts corrected; termination marked an end-node population; safety-work wording corrected; README states 500–850 V; variants.md states the 4XX AC-power envelope and the 750 V RFQ question |
| F159 | **CRITICAL** | Round 15 (A13-R01 ×3): the round-14 exciter TVS sat on the CONNECTOR node — an external battery fault fed it 25/46 A (287/555 W) without passing through the PTC; the verifier had modelled the PTC in the path | TVSEP/TVSEN moved to the protected (amplifier-side) node; RSX 2.2 Ω between the amplifier and that node; the monitor taps the protected node; ERC graph-cut check (the connector node carries only JVEH and the PTC) |
| F160 | MED | Round 15 (A13-R02): the "no back-drive" argument assumed VEXD ≈ 12.1 V; with the rail absent/cranking the ALM2402 reverse diode conducts (pulse-rated only), and a negative fault reaches the −0.3 V output limit | rows for the VEXD-absent pulse (RSX-bounded, rail pumped to ≈ 10.8 V ≤ 18 V) and the negative fault; gate ㉘ extended to VEXD off/low/on and both polarities |
| F161 | MED | Round 15 (A13-R03/R06): MF-MSMF020 was bound to a 1206 package (the family is 1812) and the hot hold current was quoted for 60 °C (0.13 A) instead of 85 °C (0.09 A); the unsuffixed part is flagged for new designs | MF-MSMF020/33X (33 V, 40 A, 0.02 s at 8 A) in 1812; 0.09 A at 85 °C against the 40–60 mA excitation |
| F162 | **HIGH** | Round 15 (A13-R04): NTC_A moved to ADC5_S11 but the target ADC map still used the precision-channel class ('P' → PCDR[11] instead of ICDR[11]) | the board-map generator emits the instance/subtype/channel triple and MAP[] uses it for every input; regression test |
| F163 | **CRITICAL** | Round 15 (A13-R05): the shipped KiCad files are the EasyEDA-import variant (library pre-mirrored because that importer ignores the orientation matrix) while native KiCad 5 applies the matrix — read natively, H5 landed on FLT_CLR_M and J7 on ASC_CLR_M; the verifier checked only the import convention | separate 'traction-native/' variant (un-mirrored library, same sheets) and zip; kicad5-verify applies the matrix for the native variant and checks both; a flipped matrix is a detected mutation |
| F164 | **HIGH** | Round 15 (review-3 A13-R02): the contactor-loss detector required n ≥ n_x, so a known low-speed OPEN/INVALID contactor report did not raise the battery-path-lost row, and st_run() kept arm/torque_enable for the same invocation | loss detected at any speed while armed (OPEN/INVALID/stale explicit); ordinary torque permission removed in the same invocation; the §6 policy selects the response |
| F165 | **HIGH** | Round 15 (self-found while deriving the ADC schedule from the ball map): ADC1's injected chain was never started, so MT2_SIG, INTRLOK_N and TMOD_W were never converted on the target; the pre-fix driver also read out of bounds for the standard-class channel | the 1 ms list starts ADC0/3/4/5 normally and ADC1's injected conversions; hal_adc_init() reads the chain masks back and refuses a configuration that does not match the ball map; the FW-06 sample wait includes the injected conversions (measured on the target) |
| F166 | MED | Round 15 (self-found): HW_ID was classified in app_init before any slow conversion had run — on the target a false "HW_ID short", and the board never arms | init_identity() starts the slow list before each sample; TI_FW_ID bumped (0x0A0D000F) — the image needs a new EOL/HIL validation record before it arms |
| F167 | **HIGH** | Round 16 (A14-R04 review 3 / A14-N01): the SWG's low corner (MAXAPP 1.884 V pp) through the round-15 series losses gave 6.34 V pp at the winding — under the 6.5 V pp resolver floor — and the monitor plane (protected node, before the PTC) had been called "what the resolver gets" | exciter MFB 24 k → 28 k (|H| ≈ 2.09): low corner ≈ 7.2 V pp at the winding; FW-10 trim setpoint 7.2 V pp at the monitor plane with the SWG-headroom and −40 °C slew checks; planes documented (winding = monitor × 0.964 cold, × 0.875 post-trip) |
| F168 | MED | Round 16 (A14-R02 ×3): the round-15 back-drive row read one RC time constant (48 µs) as the end of the pulse, ignored CEXD (26.7 µF total) and graded a reverse-diode pulse on the rail voltage; the negative-fault case put 4.4 A through the amplifier's lower diode and 0.9 J into a generic 1206 | exponential model (4.9 A peak, τ 59 µs, 0.19 mJ) marked OPEN (WARN) pending the measured diode envelope; **TVS changed to the unidirectional SMCJ8.5A** — a negative fault is carried by its forward diode (I_FSM 200 A) and the amplifier diode sees < 0.3 A; RSX bound to ERJ-8ENF2R20V with its pulse stress stated |
| F169 | MED | Round 16 (A14-R03 review 1 / R05 review 3): the TVS-energy PASS used a 20 ms allowance (400 W) extrapolated beyond the SMCJ curve (ends at 10 ms) and treated the Bourns 8 A / 20 ms point as a universal clearing bound; the 0.5 Ω source impedance is unallocated (0.1 Ω → 50 A > the PTC's 40 A) | rows conditional on the measured clearing time (5.5 J at 10 ms supported), a source-impedance allocation row (≥ 0.27 Ω at 35 V) — WARN, gate ㉘ |
| F170 | LOW | Round 16 (A14-R01/R03 ×2): the /33X PTC's hot hold current is 0.07 A at 85 °C, not the unsuffixed part's 0.09 A the A.14 text carried | corrected (BOM, EXTRACTED §31); judged against 35 mA nominal / 60 mA assumed (WARN: measured with the selected resolver) |
| F171 | **HIGH** | Round 16 (A14-R04 review 1 / R03 review 2 / R01 review 3): hal_adc_read_phase() writes the timestamp only on a complete triplet while sense_fast() ignored the return and passed an uninitialised timestamp with zero-filled channels into isns_update() | deterministic HAL contract (explicit invalid result, outputs untouched); the caller consumes only complete triplets and otherwise marks the current invalid through the sensor-failure path while V_DC and resolver acquisition keep running; tests for every missing-channel combination |
| F172 | **CRITICAL** | Round 16 (A14-R01 review 2): resolver validity did not expire when new blocks stopped — the gap check ran only when a later block was consumed (valid after 1 s of silence) | per-tick age check of the last accepted coherent frame with a bounded hold (CAL from the angle-error envelope), then angle invalidated and the resolver-invalid safe state dispatched; tests with delivery stopped at standstill and rotating |
| F173 | **CRITICAL** | Round 16 (A14-R02 review 2): one SIN-DMA heartbeat tagged all three SDADC channels fresh — a frozen EXC/COS buffer read as new and a completion between the reads let a mixed-generation tuple through | per-channel completion handshake, one coherent frame (EXC + SIN + COS + epoch) copied atomically with the generation checked across the copy, partial/overrun detection; incoherent frames feed the age policy, never a fresh stamp; host DMA model per channel |
| F174 | MED | Round 17 (gap closure): the exciter back-drive with VEXD absent stayed OPEN because the charge into the 26.7 µF rail ran through the ALM2402 reverse diodes (pulsed use only, no envelope) | DEXP/DEXN Nexperia PMEG4050EP-Q (AEC-Q101, I_FSM 70 A, V_F 0.49 V at 5 A) from each protected node to VEXD carry the 4.9 A / 59 µs / 0.29 mC exponential; NCV4276C output abs max 40 V covers the back-fed rail — row PASS with a rated path |
| F175 | MED | Round 17: the exciter TVS energy row was conditional and the source-impedance row asked the OEM for ≥ 0.27 Ω because a single fault (≤ 24 V) and a load-dump-coincident 35 V case were judged together | rows split: single fault PASS at any source impedance (3.4 J at the 20 ms bound, 37 A ≤ 40 A); TVSEP/TVSEN upgraded to SMDJ8.5A-HRA (3 kW, AEC-Q101, same SMC pad, ≈ 9 J at 10 ms) so the 35 V energy is inside the curve too; the 35 V PTC current is the documented double event IR-16/IR-33 |
| F176 | LOW | Round 17: the PTC hold row judged 70 mA against a "60 mA assumed maximum" that had no source | resolver interface requirement IR-13 (Z_primary ≥ 60 Ω at 10 kHz) bounds the excitation at 41 mA rms — 58 % of the hot hold, PASS |
| F177 | LOW | Round 17: the "resolver drive at 9 V KL30 ≈ 6.5 V pp" WARN was a stale A.4.3 model — the outputs have swung ±1.91 V pk around the 2.5 V AFE mid-rail since round 12 | recomputed: the rail only has to clear 4.6 V (VEXD ≈ 8.3 V at 9 V KL30) — PASS; the slew ceiling is the binding limit |
| F178 | MED | Round 17: gate ㉖ asked the bench to prove the discharge resistors do not flame when QDIS sticks ON, although the bound TT/Welwyn SQP sheet states it ("will not burn or emit incandescent particles under any condition of applied temperature or overload") | both stuck-ON rows PASS on the statement; the Yageo alternate (flameproof case only) is constrained in the BOM; VR-28 asks for the qualification data; the fail-open time becomes a QP characterisation |
| F179 | LOW | Round 17: RFS4 (0603 ESR03, 0.33 W) ran at 1.47× its nameplate for the 24 V / 60 s jump start with FS1B held | RFS4 → ESR18EZPF1001 (1206, 0.5 W at 70 °C, AEC-Q200; ROHM ESR series Rev.012 archived) — 0.96× for 60 s, element ≈ 107 °C, PASS |
| F180 | LOW | Round 17: the hall ratiometric-reference drift row named "calibrate at EOL or move VREFH" without a bound | EOL gain calibration (FW-20) plus the ±5 % torque-accuracy allocation IR-26 bound it — PASS; VREFH stays on VREF5 (GEN3 parity) |
| F181 | **HIGH** | Round 17: the KiCad hand-off was verified only by our placement rule. The installed KiCad 10.0.6 loads the KiCad 5 legacy sheets but resolves no symbols (sym-lib-table, cache library and project file tried), so KiCad 9/10 cannot netlist the legacy set at all | sym-lib-table + cache library added to both legacy folders/zips (KiCad 5–8 open them without the remap dialog); a KiCad 9/10 format set (kicad/traction, embedded symbols + project library) converted from the native sheets and proved by kicad-cli netlist export against the built netlist on every board, with mutation tests (mirrored MCU, ball swap, harness numbering, duplicate reference) |
| F182 | LOW | Round 17 datasheet round: the JIC/JICC note asked to confirm interleaved pin numbering; the BOM had TDK "AT000" beads as automotive alternates and the Murata BLM31 sheet was missing; the plain SMDJ has no AEC-Q101 statement | Samtec drawing: numbering is sequential per row (row A 1–20, row B 21–40); TDK's automotive MPZ2012 catalog has no 120 Ω part (220 Ω alternate confirmed, 120 Ω has none); BLM31PG121SH1L sheet archived (automotive application code, no literal AEC-Q200 line — VR-14); the -HRA suffix is the AEC-Q101 SMDJ; DMP6023LEQ still "Advance Information" (VR-20) |
| F183 | MED | Round 17 (found by the KiCad-10 proof): the legacy KiCad 5 sets numbered the JIC/JICC harness pins P1–P40 — the MCU ball rule in pages.mjs matched the harness labels P1_…P40_ — against pads 1–40 (netlist unaffected, symbol pin numbers wrong) | the ball rule applies to UMCU only; all sets and PDFs regenerated; the modern set takes pin numbers from the built netlist and the verifier detects the old numbering as a mutation (62 failures) |
| F184 | LOW | Round 17 (found by the KiCad-10 proof): the legacy generator classed diodes by MPN prefix (SMAJ/SMBJ/SMCJ…) — after the SMDJ8.5A-HRA upgrade TVSEP/TVSEN were drawn as resistors, and the PMEG Schottky needed the same rule | SMDJ, 5.0SMDJ and PMEG classed as diodes in kicad5-gen; regenerated |
| F185 | **HIGH** | Round 17 (LV-entry study): ISO 7637-2 pulse 1 (−75…−150 V, 10 Ω) reached the reverse Schottky DREVC unclamped — the card's TVS sat behind it on NRC, so DREVC avalanched at ≈ 654 W (−150 V) against P_ARM 144 W (10 µs, 125 °C; ≈ 5.8 W at 1 ms); pulse 3a (−220 V / 50 Ω) exceeded P_ARM too. GEN3 places its TVS ahead of the Schottky | pin-side anti-series pair on FCO: DTVSC TPSMC33A-VR (cathode FCO) + DTVSC2 TPSMC18A-VR (cathode DGND) — DREVC sees ≤ 38.3 V at −150 V and 37.1 V at 3a; the −14 V reverse battery leaves DTVSC2 dark (18.9 V knee at −40 °C) |
| F186 | MED | Round 17: ISO 16750-2:2023 raised the jump start to 26 V / 60 s at RT and T_min; the TPSMC24CA-VR knee (26.7 V at 25 °C) is 25.1 V at −40 °C — it would conduct for the whole minute | every TVS on a KL30-derived net is a 33 V stand-off part (knee 34.4 V at −40 °C); IR-02 stays 24 V until the OEM names the 2023 edition; an INFO row lists the 24 V-sized rows that change at 26 V |
| F187 | MED | Round 17: FLVC (MF-LSMF300/24X, 3 A hold at 23 °C) carries the WHOLE inverter — 2.54 A at 9 V, 1.73 A at 13.5 V — against a 1.50 A hold at 85 °C; the verifier compared one chain (1.19 A) with the 23 °C hold | FLVC → Bel 0680L5000-05, 5 A slow-blow 2410 (125 V DC / 100 A and 75 V DC / 500 A interrupting; 36 A²s): 0.65 of its 85 °C / 80 % rerating at 9 V; hot-plug, pulse-1 and pulse-2a I²t each ≤ 2 % of melting; AEC-Q200 report requested (VR-32); the chain-polyfuse row now judges at 85 °C |
| F188 | MED | Round 17: KL15 pulse 1 drove the FS26 WAKE1 pin through RIGN1 (5.1 k straight from KL15): ≈ 29 mA reverse against the −5 mA WAKE rating (−0.3 V abs min), 2.6 mA at a reversed battery; the 1N4148WS steering diode (75 V) would avalanche at −150 V | RIGN1 moved behind DIGN, DIGN → US1M (1000 V, already in the BOM): WAKE1 and IGN_SNS both behind one blocking diode; WAKE1 still reads 3.6 V at a 6 V KL15 |
| F189 | **HIGH** | Round 17 (gate ㉗): ISO 16750-2 test B (35 V / 400 ms, Ri 0.5–4 Ω) could not be shown on paper with the 24 V stand-off TVS at any Ri — no SMC sheet rates a pulse beyond 1 ms — and if the generator is the unsuppressed 79–101 V source with the 35 V clamp in parallel (the parameter set ISO 16750-2 gives for test B), a local clamp below 35 V takes 70–100 A at Ri 0.5 Ω; ULDO15 (DPAK-5) also reached ≈ 150 °C at the old ≈ 33 V clamp | let-through: no TVS on a KL30-derived net below 36.7 V (ERC-locked), CLVC3 100 µF hybrid polymer holds pulse 2a on NRC ≤ 37.5 V, DTVH/DTVL → TPSMC33CA-VR, ULDO15 → NCV4276CDSADJR4G (D2PAK-5, ≈ 140 °C); the FS26 (36 V HV extended operation), TPS55340-Q1 (38 V rec) and NCV4276C (40 V) carry the 35 V plateau; no Ri is required of the OEM (IR-03). Open: the FS26 limited period (VR-29), the TPS55340-Q1 pin text (VR-30), CLVC3 pulse life (VR-31) |
| F77 | **HIGH** | The A.6 RC timing nodes drove non-Schmitt LVC inputs: the clear one-shot into ULAT2 /CLR at ≈63,500 ns/V (5 ns/V allowed), the soft-off delay into UAND2 at ≈14,000 ns/V (10 ns/V) (RR01/RR02, A6-R02/R03) | 74LVC3G17-Q100 Schmitt buffer (no Δt/ΔV limit) on both nodes and on FS0B; one-shot 61–230 µs, delay 22–53 µs at its thresholds |
| F78 | **HIGH** | FS1B loaded 5.45 mA through 1 k pull-ups: V_OL ≤ 0.4 V holds only to 2 mA and the limit can be 4 mA — FS1B 1.33 V, ASC_SET_N 1.67 V (> VIL), SBC read-back (< 0.7 V) fails; the checker compared with 22 mA and divided by 1000 twice (A6-R01) | RENP1/2 5.1 k (NXP value): 1.79 mA incl. strap and a specified FAULT_OUT load, ASC_SET_N ≤ 0.84 V; checker at the V_OL point |
| F79 | **HIGH** | ASC entry had no break-before-make: FS0B/FS1B assert together on the MCU-dead path (HS turn-off raced the LS ASC), and the MCU path had no ordered entry (RR05) | CASCD 12 nF + DASCR: LS ASC ≥ 3.4 µs after the latch, entry ≤ 7.0 µs, release ≤ 0.75 µs; MCU path = eFlexPWM fault (high sides off) → ASC_REQ → PWM-ASC with EN high after the dead time (§4c). A first draft also dropped DRV_EN from the latch (DASC) — removed: with EN low the NSI6611 does not give DESAT priority over ASC (DS §8.12, cross-check) |
| F80 | **HIGH** | Flyback FB divider on VDD: the 2.2 k start feed could hold FB above 2.5 V with the converter stopped (12.3 V at a 16 V rail for a 1.5 mA controller; 18 V clamp at a 24 V jump start) → no restart, gate power lost (A6-R07); the VCC2 model omitted the aux diode — the real rail was 16.9 V, not 15.6 V (A6-R06) | FB senses its own aux rectifier (1N4148WS + 100 Ω + 100 nF), 52.3k/15k: VCC2 15.4 V nom, 14.0–16.7 V corners in a 13.5–17.0 V window |
| F81 | MED | CB15O1/2 22 µF/25 V on V15B, which follows V12L − Vf to ≈33 V in pass-through (RR10) | 22 µF/50 V 1210 (same part as CLVC2); boost PM 72–84° over 20–35 µF effective |
| F82 | MED | TLP152 ASC opto LED at 5.4–7.0 mA through 470 Ω — below its 7.5 mA guaranteed turn-on current (self-found, N10) | RASCL 270 Ω: 10.6–13.5 mA |
| F83 | MED | The one-shot comment claimed runaway code could not hold the chain permissive; repeated clear pulses do (RR03) | claim corrected; FW-15 locked eFlexPWM fault inputs (PWM forced low while FLT) + FW-12 WD_ERR_LIMIT 2 → FS0B |
| F84 | MED | DESAT timing row judged PASS at the 400 mA soft-off only; the 100 mA DS minimum needs 8.9–10.1 µs vs tP ≤ 6 µs (RR04/A6-R04) | both corners shown, WARN = release gate (I_STO distribution, SC envelope at 850 V and actual gate bias, contained SC test) |
| F85 | LOW | Thermal: S4/steady-state put only the switch die into the coldplate term; the diode heats the same plate (RR07) | tjPos: plate carries IGBT + diode; 8XX/4XX IGBT 30 s peak 129/123 °C |
| F86 | LOW | 8 kHz SiC mode allowed a 1.2 kHz current-loop crossover (43.3° PM) (RR08) | ≤ 1.1 kHz at 8 kHz (47.2°) in the SKU table |
| F87 | LOW | bom-gen exited 0 with missing inputs; JSWD 10-pin source vs a 20-pin BOM part (A6-R09/R10) | preflight aborts before any write (missing/empty/malformed/stale); Samtec FTSH-105-01-L-DV-K + contact-count check |
| F88 | LOW | Flyback peak-current check used one transformer's Lp for the bank current; README kept pre-A.6 loss numbers (A6-R14, README note) | bank Lp = 10 µH/3 (2.05 A, 68 %); README sizing regenerated from the loss model |
| F89 | LOW | ULAT/ULAT2 were TI SN74LVC1G74DCUR, a catalog part with no AEC-Q100 variant (self-found, N11) | Nexperia 74LVC1G74DC-Q100, pin-identical |
| F63 | **HIGH** | S8 turn-off overshoot used `Ln·20e12·1e-6` for 20 kA/µs — 1000× too small (0.3 V instead of 300 V at 15 nH), so its PASS was void; with the DS fall time (13 ns cold at 3.3 Ω ≈ 30 kA/µs at 481 A) no EconoDUAL-class loop holds 1080 V at 850 V (review R-F01) | SI units; overshoot budget from the DS tf; RG_OFF start value 6.8 Ω (Eoff booked in the loss model), RG_ON 3.3 Ω (the only characterized point, was 1.5/1.0); DPT gate at 850 V cold/hot; module Ls requested from hiitio |
| F64 | **HIGH** | SiC conduction loss used the IGBT transistor-only formula `I·√(1/8+m·cosφ/3π)`: synchronous SiC conducts ½·I²·R per switch — understated 2.4× (136 → 330 W/switch at 340 A) (R-F02) | exact ½·I²R + switching at the fitted Rg + Qrr + dead-time diode; thermal and efficiency restated (peak 30 s Tj 122 °C at 850 V, not 89 °C; 99.0 % semiconductor efficiency at the continuous point) |
| F65 | **HIGH** | IGBT short-circuit rating carried as "10 µs class"; HCG600 DS Table 5 says tP ≤ 6 µs at 800 V/175 °C/15 V; 150 pF blanking = 4.5 µs worst detection alone (R-F03) | IGBT blanking 82 pF C0G: 2.98 µs worst detection + ~1.5 µs soft-off < 5 µs derated; contained SC test is the release gate (the DS-minimum 100 mA soft-off current is not coverable) |
| F66 | **HIGH** | Gate-power flyback could not start at KL30 9 V: 4.7 k needed 8.47 V at the 12 V node (divider current omitted) AND the UCC28C40's 0.4 V UVLO hysteresis gave ~0.15 ms bursts on 4.7 µF (R-F17/F18, cycle-by-cycle S1) | 2.2 k 1206 start + 47 µF VDD (one-burst start in every corner) + 18 V VDD zener (the C40 has no internal clamp — the lower start resistor would lift VDD past 18 V at jump start with the flyback disabled) |
| F67 | MED | "220 kW / 120 kW over 500–850 V" is not deliverable at 340/185 A: full power needs ≥654/656 V (PF 0.85, 5 % modulation reserve) (R-F09) | published P(V_dc) envelope per SKU; firmware derates by V_dc |
| F68 | MED | Passive bleeder 5 × 27 k: the low-tolerance part carries 184 V at 850 V = 92 % of a plain 2512's 200 V working rating (R-F24 at 850 V) | 2 × 6 × 22 k = 66 k: 154 V (77 %), 56 s / 65 s to 60 V |
| F69 | MED | Global DRV_EN drop (≈0.5–0.9 µs after DESAT) could interrupt the faulted driver's soft turn-off — the NSI6611 DS does not state RST/EN priority during soft-off (R-F05) | 10 k/3.3 nF between the latch and the AND's Schmitt input: 12–40 µs; FS0B/MCU paths undelayed |
| F70 | MED | Fault-latch clear was level-sensitive: a stuck-low MCU pin held PRE=CLR=L (both outputs high) and silently disabled the global latch (residual of R-F06; the proposed "fault-dominant" fix would deadlock the NSI6611 FLT reset) | clear is a hardware one-shot (15 nF into the 10 k pull-up + BAT46 clamp): ≥54 µs per falling edge, re-arms by itself |
| F71 | MED | IGBT build thermal/efficiency omitted the FWD die, used m·cosφ = 0 and DS energies at 0.51 Ω; Qg scaled linearly (R-F26/F27) | separate IGBT/diode dies with m·cosφ (motoring + regen), energies referred to our driver, full Qg; 8XX IGBT rated at 5 kHz (127 °C end of 30 s, not 110 °C) |
| F72 | MED | Both V_DC receivers share the +0.5 V offset buffer UVOF: its failure shifts both channels by up to 228 V and passes the 5 % cross-check — OV and discharge witness blinded (R-F11) | VOFS routed to an MCU ADC (zero parts); BMS pack voltage is the third witness; the "fully independent" wording corrected |
| F73 | MED | BOM class MPNs had drifted from the netlist: RFS1–4 printed R0603-120R (re-creating F40), CLVC2 4.7 µF (re-creating F52), 10 more lines; the IGBT variant BOM printed the SiC value next to the IGBT MPN | parts-db fixed; SKU rows carry their value; bom-gen FAILS on any value/MPN disagreement |
| F74 | LOW | Simulation defects: S5 counted the bleeder twice; S10 hold-up put the VEE cap in parallel with VCC2 and ignored the bleeder/gate loads (15 ms claimed, 1.1–3.2 ms real); S4 started cold; S6 applied the 10 kHz bandwidth to the 4–6 kHz IGBT; S1 had no startup model (R-F14/F17/F25/F28/F32) | all rewritten on the shared loss model; ASC through total LV loss not credited (unchanged conclusion, corrected number) |
| F75 | LOW | Resolver cable shields terminated into AGND at the vehicle connector (R-F35) | shields on the connector ground (DGND); AGND keeps its single-point tie |
| F76 | LOW | Documentation overstated: HVIL "hardware window comparator" (it is an MCU ADC signature), RSS labelled worst case (±0.7 % → ±2.1 % worst), 0.62 ripple factor (worst 0.65), XM3 inductance reused for a D3 module, bias-bank "3.87 W" (100 % CS limit before losses; 2.3 W worst parts), and two "drop-in" module alternates that are not (HCS800FH120D4B3 has a lettered press-fit pin map; FF6MR12W2M1H is not an EconoDUAL-3 package code) (R-F12/F21/F29/F37) | wording and numbers corrected; IGBT SKUs fit RT 8.2 k (~308 kHz) for gate-power margin; alternates list limited to pin-map-verified parts |
| F1 | **HIGH** | Flyback CS resistor 0.033 Ω vs UCC28C43's 1 V threshold ⇒ 30 A "limit" = no overcurrent protection (value was scaled for the NJW4140's low CS threshold) | 0.22 Ω/1210 ⇒ 4.5 A limit vs 2.4 A worst-case operating peak |
| F7 | **HIGH** | FB divider (18k/15k/1.3k, GEN3 values for the NJW ref) regulates VCC at **5.26 V** with the 2.5 V UCC28C43 reference ⇒ UVLO lockout, gate supply never starts | 75k/15k ⇒ VCC 15.0 V |
| F21 | **HIGH** | Flyback VCC had **no start path** (aux-winding-only feed cannot bootstrap) | 4.7 k trickle-start from the 12 V rail (425 µA @9 V vs 100 µA start spec) |
| F20 | **HIGH** | Five net→net alias `<trace>`s left pins floating on the drawing: gate-bias winding returns (6×), module-NTC returns (3×), MCU temp inputs (5×), card-side TMOD_RTN unterminated | all aliases removed — direct binding; TMOD_RTN star-tied to AGND via RTMR 0 Ω |
| F2 | MED | Bus-to-chassis Y caps specced as **Y2** (250 Vac line class) at an 850 V DC bus | Y1-class 4.7 nF (500 Vac / 8 kV impulse); alt 2×Y2 series / CeraLink |
| F4 | MED | The two "independent" V_DC senses shared **one** bias module (common-cause vs the stated safety mechanism #7) | second reinforced module (PS5C) — channel 2 fully independent |
| F25 | MED | 12 V-node MLCCs were 25 V-rated under a 24 V-standoff TVS that clamps ≈ 39 V in load dump | all KL30-node caps ⇒ 50 V rating |
| F26 | MED | Active discharge (4×560 Ω) = 2.19 s at the R+5 %/C+10 % corner — over the 2 s crash target | 4×470 Ω ⇒ 1.60 s nom / 1.84 s worst |
| F27 | LOW | V_DC divider hit exactly 2.0 V full-scale at 850 V — the OV witness saturated right where it matters | bottom 6.65 k ⇒ 6.2 k (full-scale = 911 V) |
| F28 | **HIGH** | ASC pin driven from an 18 V rail vs **abs max GND2+6 V** (NSI6611 DS 1.2) | 2.2 k series + 5.1 V zener clamp at the ganged pins |
| F29 | MED | Module symbol used symbolic aux pins; HS DESAT sensed the DC+ power terminal | REAL HCS600 pin map (1=G_L…9=HS drain-sense, 10/11=AC); HS DESAT moved to the dedicated aux sense pin |
| F30 | LOW | Gate off-bias −4.3 V is not a HCS600-recommended combo (+15 pairs with −5) | zener split ⇒ +15/−5.1 (also consolidates to the C5V1 already on the BOM) |
| F31 | **HIGH** | UCC28C43's real UVLO is 8.4/7.6 V (SLUS458I) — the gate-power flyback cannot start at 9 V cold-crank | UCC28C40 grade (7.0/6.6 V) |
| F32 | **HIGH** | VGT12EEM's real Lp is 10 µH (not a 200 µH-class part) — at 52 kHz the peak current would hit ~10 A every cycle | oscillator retimed to ~250 kHz (10 k/680 pF); DCM Ipk ≈ 1.2 A |
| F33 | **HIGH** | FB divider targeted 15 V on the NF winding; with NP:NF:NS = 1:1.6:2.9 that drives the secondaries to ~27 V (zener overstress) | 56k/15k ⇒ VCC_reg 11.8 V ⇒ V_sec 21.4 V ⇒ +15.6/−5.1 V rails |
| F34 | **HIGH** | FS26 VMONEXT is a fixed 0.8 V reference — the 10k/18.7k divider fed it 3.26 V = permanent overvoltage fault | 52.3k/10k ⇒ 0.794 V at 5 V nominal |
| F35 | MED | FS0B/FS1B low-side outputs clamp at 4–22 mA; 120 Ω pull-ups forced 42 mA | 1 k pull-ups (4.6 mA) |
| F37 | **HIGH** | Gate-power transformer secondaries used the NON-dot end for the rectifier (TDK dots: NP=pin 2, NS=pin 8) ⇒ forward-mode transfer ≈2.9×Vin ≈ 35 V into gate rails rated +22 V abs | S-winding use swapped: rectifier on pin 8 (dot), return pin 5; pins 6/7 (no internal connection) NC'd; locked by ERC |
| F38 | **HIGH** | Flyback drain clamp SMBJ85A drawn forward (anode at drain) ⇒ conducts every OFF interval; and 94.4 V min breakdown cannot protect an 80 V FET | US1M blocking diode into SMAJ13A TVS returned to the rail: drain ≤60 V at clamped load dump, TVS dark below 13 V standoff (reflected 7.4 V) |
| F39 | **HIGH** | DESAT clamp fed VCC2 *into* the DESAT node (A1=VCC, K=DESAT) and treated BAT64-04 as common-cathode (it is a series pair) | Series pair correctly oriented: anode end on DESAT, cathode end on VCC2, junction pin NC |
| F40 | **HIGH** | ASC latch strapping: 120 Ω/120 Ω made a 2.5 V "low" at /PRE (indeterminate) and demanded 42 mA from the MCU on /CLR; SN74LVC1G74 symbol pin order did not match DCU package | 1 k series / 10 k pull-ups (asserted low = 0.45 V), MCU clear via 1 k, real CLK=1/D=2//Q=3/GND=4/Q=5//CLR=6//PRE=7/VCC=8 map |
| F41 | **HIGH** | KL15 → diode → PTA25 with no interface: 13.3 V onto a 5 V-domain MCU pin (RIGN1/2 belong to the WAKE1 divider, not this path) | 47 k/10 k divider + 100 nF after the diode; 16 V reads 2.68 V, load-dump injection ≤0.72 mA vs 3 mA spec |
| F42 | MED | AMC1311 fail-safe (negative differential when HV side dead) rails the single-supply receiver to 0 V — indistinguishable from a discharged bus | Receivers re-zeroed to +0.5 V via buffered VREF5 divider (OPA376); fail-safe ≈0 V vs healthy-zero 0.5 V |
| F43 | LOW | Reviewer flagged divider linear-range margin at 900 V/1 % | Not applicable as reviewed: system max is 850 V and the bottom leg is 0.1 % — worst-corner FS ≈ 902 V; margin documented |
| F44 | **HIGH** | VBAT_H/VBAT_L appeared ONLY as harness pins — no source anywhere on the card ⇒ the whole gate-power system had no positive feed | FVBH/FVBL polyfuses from the reverse-protected node NRC feed pins 31/32 & 35/36; power board keeps per-bank fuse+TVS+filter |
| F45 | **HIGH** | Symbol pin maps did not match packages: 74LVC1G11 (unpowered — no VCC pin at all), 74LVC1G32, NCV4276C (output on NC pin 4), TPS55340 (6-pin symbol for RTE-16; SS and FREQ missing entirely), BUK9Y14 (G/S swapped vs LFPAK56), ALM2402 (PWP-14; VCC_O supplies absent; SHDN tied stiff to a rail though it is also the open-drain OT flag — grounded/floating = shutdown), QA01C-class SIP-7 modules drawn as 4-pin, PESD parts drawn as 3-pin arrays, FS26 as a 34-pin abstraction | Every one rebound to the real package pins from its datasheet; FS26 now full LQFP-48+EP with VDIG/VBOS/bootstraps/DEBUG strap and DS-specified unused-pin terminations; MCU remains explicitly symbolic (no package table in the DS — bind at layout, printed on sheet) |
| F47 | **HIGH** | AMC1311 pins 2/3 swapped on BOTH V_DC channels (real: 2=IN, 3=SHTDN active-high w/ internal pull-up) — the analog inputs were grounded; channel agreement could not validate the measurement | IsoVSense symbol rebound: IN(2)=divider tap, SHTDN(3)=DCN; locked in ERC |
| F48 | **HIGH** | FS26 TRKIN grounded as an "unused tracker input" — it is the input SUPPLY of the VREF regulator, so VREF5 (ADC reference, temp networks, receiver offset) had no source | TRKIN → VPRE (headroom ≥ VREF+350 mV inside the 6.35 V max; CIN_TRK ≥0.5 µF eff at the pin — VPRE bank, layout note) |
| F49 | **HIGH** | TPS55340 pin 5 treated as a second VIN and tied to 12 V — pin 5 is SYNC, abs max 7 V | SYNC → DGND per DS ("if not used, tie to AGND") |
| F50 | MED | FS26 capacitor values under DS minimums: VBOS 1 µF (needs 4.7 µF; 3.3–6.1 eff), LDO1/V3B 1 µF (needs 4.7 µF; 2.35–15 eff) — and VREF 1 µF vs COUT_VREF 1.1–3.3 µF eff (found in the same audit) | CVBOS 4.7 µF · CSB6 4.7 µF · CSB5 2.2 µF |
| F51 | **HIGH** | Boost topology passes V12L−V_f straight to V15 whenever V12L > setpoint (a boost cannot regulate below its input): 24 V jump start / clamped load dump would put 19–33 V on QA01C modules rated 13.5–16.5 V (21 V/1 s surge) | NCV4276C-ADJ 40 V/0.4 A post-regulator: mild dropout in normal operation (V15 ≈ 15.0–15.2 V), hard 15.0 V clamp during pass-through; TSD covers the sustained-24 V service case |
| F52 | **HIGH** | FS26 VCORE buck network out of spec: LCOR 4.7 µH (Table 106 allows 1/1.5/2.2 µH by OTP), COUT 10 µF nominal vs 20–100 µF effective, bootstrap 100 nF vs 47 nF; VPRE COUT/input caps under the effective minimums | LCOR 2.2 µH (CORE_LSEL_OTP=0x02) · V15S 2×22 µF (≈32 µF eff) · CBTC 47 nF · CBTP 22 nF (typ; Rev 6.1 Table 100 allows 22–100 nF, so the prior 100 nF was legal — narrative corrected) · CSB1/2 22 µF · CLVC2 22 µF; effective-capacitance rows added |
| F53 | **HIGH** | LCAN1/LCAN2 mapped as windings 1-2/3-4 — ACT45B is physically wound 1-4 and 2-3, so transceiver CANH landed on the external CANL net (both ports) | Pin map corrected to the TDK circuit diagram; ERC asserts end-to-end pairing |
| F54 | MED | ULDO15 (ADJ + ceramic COUT) drawn without the required feed-forward capacitor; COUT 4.7 µF below the reference design | Cb 220 pF across the 49.9 k leg (f_z 14.5 kHz, in the 11–18 kHz window) · COUT 22 µF |
| F55 | **HIGH** | UEXD (ALM2402: 18 V abs, 16 V rec) fed from raw VBATC — 24 V jump start exceeds abs max and TPSMC24CA clamps far above 18 V | ULDOEX 12.1 V protective LDO (same NCV4276C-ADJ family) feeds VCC/VCC_O1/VCC_O2; crank behavior unchanged (dropout) |
| F56 | **HIGH** | Flyback switch BUK9Y14-80E is logic-level: V_GS abs ±10 V DC vs the 11.8 V VDD drive; the 5.6 V gate zener "fixed" it by conducting ~0.29 A through every ON interval (~0.4 W each, doubling the aux budget) | BUK7Y14-80E (standard-level, ±20 V, same LFPAK56/current class) + zener repurposed to a dark 15 V protective clamp |
| F57 | LOW | LDO ordering code transposed (NCV4276CADJDTRKG) | NCV4276CDTADJRKG per the DS ordering table |
| F62 | MED | The on-sheet ASIL-D + DISCHARGE review panel had silently vanished from the power sheet (its single void pick stopped fitting as the sheet grew through A.4.x) — found by the A.5 docs audit | Panel placement retries every void largest-first, and the build now FAILS if the panel cannot be placed; panel text refreshed (DRV_EN chain incl. RDY+fault-latch, 57 s bleed, "never energize w/o the DISCHARGE BOARD") |
| F60 | MED | JVEH bound to TE 776231-1 called "AMPSEAL 23" — the TE drawing in docs/datasheets shows 776231-1 is the **35-position** header (mates plug 776164) | Rebound to **770669-1**, the real 23-position AMPSEAL PCB header (mates the 770680-1 plug already cited); drawing TE-770669-1.pdf fetched |
| F61 | LOW | Rails named V18A/V18Q assume "+18 V": the QA01C DS selection row is **+20/−4 V**. ASC path unaffected (2.2 k + 5.1 V clamp); QDIS gate ≈19.5 V vs +22 abs / +18 rec | Documented + WARN row; clamps verified for +20 V; gate-divider option noted for proto |
| F58 | LOW | LCOR reconciliation: the netlist value became 2.2 µH in A.4.2 but the parts-db class MPN still printed "IND-4.7uH-2A" on the sheet/BOM — an assembler would fit the old value | MPN string now IND-2.2uH-4A with the OTP pairing in the description; value, MPN, BOM and OTP agree |
| F59 | MED | UB15 (TPS55340) COMP node had only 10 nF to ground — no compensation zero; simplified CCM screening gives ~0° phase margin | Series R3/C4 = 2 kΩ/100 nF per TI §8.2.1.2.11 + 470 pF HF pole cap; RHPZ ≈ 450 kHz (not limiting); measured Bode remains a bench gate |
| F46 | MED | No global hardware reaction to a driver DESAT trip (FLT only went to the MCU); driver soft-shutdown is per-channel | FLT_HS/LS diode-OR → SN74LVC1G74 fault latch → third AND input in DRV_EN: any DESAT latches all six channels off until the MCU clears after diagnosis |
| F36 | MED | S32K39 core is 1.14 V via an external NMOS ballast from a 1.5 V rail (DS Table 11) — direct FS26-VCORE→V11 is not a supported topology | VCORE→V15S 1.5 V + SQ2310ES ballast (GEN3-exact) regulated by the MCU's BCTRL loop |
| — | LOW | Hall ratiometric reference (V5A) ≠ ADC reference (VREF5): ±1–2 % gain drift between two 5 V rails | accepted (GEN3-identical); EOL calibration note |
| — | LOW | ALM2402 resolver swing at 9 V cold-crank ≈ 7 Vpp vs 8 Vpp target | accepted — amplitude-invariant demodulation |

## Margin tables


### Power stage — 8XX SiC

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Current-limited envelope P_pk / P_cont | 168/91 kW @500 V · 220/120 kW @700 V | 220/120 kW from 654/656 V up | ℹ️ | 340/185 A rms, PF 0.85, 5 % modulation reserve — firmware derates P(V_dc) below these voltages (review A.6 F09) |
| Tj steady-state bound, peak 30 s (340 A, 850 V, 10 kHz) | 135 °C (554 W/switch) | 175 °C Tj max | ✅ PASS | 77% of limit · cond 329 + sw 204 + Qrr 7 + dead-time 14 W; 65 °C coolant + 0.045 K/W coldplate (shared by the position's IGBT and diode, RR07); the 30 s transient is in S4 |
| Tj steady-state bound, continuous (185 A, 700 V, 10 kHz) | 90 °C (200 W/switch) | 175 °C Tj max | ✅ PASS | 52% of limit · cond 98 + sw 91 + Qrr 3 + dead-time 7 W; 65 °C coolant + 0.045 K/W coldplate (shared by the position's IGBT and diode, RR07); the 30 s transient is in S4 |
| Semiconductor efficiency @ continuous (120 kW, 700 V) | 99.01 % (1197 W) | - | ℹ️ | six switches, conservative 175 °C R_DS(on); caps/busbar/LV add ≈0.1–0.2 pt |

### Power stage — 8XX IGBT

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Current-limited envelope P_pk / P_cont | 168/91 kW @500 V · 220/120 kW @700 V | 220/120 kW from 654/656 V up | ℹ️ | 340/185 A rms, PF 0.85, 5 % modulation reserve — firmware derates P(V_dc) below these voltages (review A.6 F09) |
| Tj steady-state bound, peak 30 s (340 A, 850 V, 5 kHz) | 142 °C (560 W/switch) | 150 °C Tvjop | 🟡 WARN | 95% of limit · IGBT 191+369 W · diode 34+58 W (motoring); 65 °C coolant + 0.045 K/W coldplate (shared by the position's IGBT and diode, RR07); the 30 s transient is in S4 |
| Diode Tj bound, peak 30 s regeneration (cosφ −0.85) | 121 °C (239 W) | 150 °C | ✅ PASS | 81% of limit · F26 — the FWD is its own die (Rth 0.10 K/W); regen loads it hardest; RR07 — the IGBT's 404 W heats the shared coldplate too |
| Tj steady-state bound, continuous (185 A, 700 V, 5 kHz) | 99 °C (250 W/switch) | 150 °C Tvjop | ✅ PASS | 66% of limit · IGBT 85+165 W · diode 16+26 W (motoring); 65 °C coolant + 0.045 K/W coldplate (shared by the position's IGBT and diode, RR07); the 30 s transient is in S4 |
| Diode Tj bound, continuous regeneration (cosφ −0.85) | 90 °C (108 W) | 150 °C | ✅ PASS | 60% of limit · F26 — the FWD is its own die (Rth 0.10 K/W); regen loads it hardest; RR07 — the IGBT's 181 W heats the shared coldplate too |
| Semiconductor efficiency @ continuous (120 kW, 700 V) | 98.56 % (1751 W) | - | ℹ️ | six switches, conservative 175 °C R_DS(on); caps/busbar/LV add ≈0.1–0.2 pt |

### Power stage — 4XX IGBT

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Current-limited envelope P_pk / P_cont | 99/62 kW @250 V · 150/90 kW @400 V | 150/90 kW from 379/364 V up | ℹ️ | 400/250 A rms, PF 0.85, 5 % modulation reserve — firmware derates P(V_dc) below these voltages (review A.6 F09) |
| Tj steady-state bound, peak 30 s (400 A, 500 V, 5 kHz) | 133 °C (496 W/switch) | 150 °C Tvjop | ✅ PASS | 89% of limit · IGBT 241+255 W · diode 42+40 W (motoring); 65 °C coolant + 0.045 K/W coldplate (shared by the position's IGBT and diode, RR07); the 30 s transient is in S4 |
| Diode Tj bound, peak 30 s regeneration (cosφ −0.85) | 121 °C (267 W) | 150 °C | ✅ PASS | 81% of limit · F26 — the FWD is its own die (Rth 0.10 K/W); regen loads it hardest; RR07 — the IGBT's 299 W heats the shared coldplate too |
| Tj steady-state bound, continuous (250 A, 400 V, 5 kHz) | 100 °C (253 W/switch) | 150 °C Tvjop | ✅ PASS | 67% of limit · IGBT 126+128 W · diode 23+20 W (motoring); 65 °C coolant + 0.045 K/W coldplate (shared by the position's IGBT and diode, RR07); the 30 s transient is in S4 |
| Diode Tj bound, continuous regeneration (cosφ −0.85) | 94 °C (140 W) | 150 °C | ✅ PASS | 63% of limit · F26 — the FWD is its own die (Rth 0.10 K/W); regen loads it hardest; RR07 — the IGBT's 151 W heats the shared coldplate too |
| Semiconductor efficiency @ continuous (90 kW, 400 V) | 98.07 % (1775 W) | - | ℹ️ | six switches, conservative 175 °C R_DS(on); caps/busbar/LV add ≈0.1–0.2 pt |

### Power stage — 4XX SiC

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Current-limited envelope P_pk / P_cont | 99/62 kW @250 V · 150/90 kW @400 V | 150/90 kW from 379/364 V up | ℹ️ | 400/250 A rms, PF 0.85, 5 % modulation reserve — firmware derates P(V_dc) below these voltages (review A.6 F09) |
| Tj steady-state bound, peak 30 s (400 A, 500 V, 10 kHz) | 143 °C (618 W/switch) | 175 °C Tj max | ✅ PASS | 82% of limit · cond 456 + sw 141 + Qrr 5 + dead-time 16 W; 65 °C coolant + 0.045 K/W coldplate (shared by the position's IGBT and diode, RR07); the 30 s transient is in S4 |
| Tj steady-state bound, continuous (250 A, 400 V, 10 kHz) | 98 °C (261 W/switch) | 175 °C Tj max | ✅ PASS | 56% of limit · cond 178 + sw 70 + Qrr 2 + dead-time 10 W; 65 °C coolant + 0.045 K/W coldplate (shared by the position's IGBT and diode, RR07); the 30 s transient is in S4 |
| Semiconductor efficiency @ continuous (90 kW, 400 V) | 98.29 % (1567 W) | - | ℹ️ | six switches, conservative 175 °C R_DS(on); caps/busbar/LV add ≈0.1–0.2 pt |

### Power stage — all SKUs

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Peak switch current vs module rating | 566 A pk (4XX, 400 A rms) | 600 A DC / 1200 A 1 ms | ✅ PASS | 47% of limit · 8XX SKUs: 481 A pk |
| Phase-current sensing headroom | ≈620 A (566 A pk + 10 % ripple, 4XX) | ±900 A LEM range | ✅ PASS | 69% of limit · 8XX ≈530 A — the ±900 A sensor covers every SKU (review F33 assumed 600 A rms; above ≈480 A rms a 4XX-HP frame needs a larger sensor) |
| Turn-off overshoot, SiC 850 V / 481 A, cold (RG_OFF 6.8 Ω) | 1104 V at 15 nH (17 kA/µs est.) | 1080 V repetitive guard · 1200 V abs | 🟡 WARN | hot 1000 V. DPT GATE, not closed on paper: module Ls unpublished (hiitio RFQ); at the DS 3.3 Ω tf (13 ns) the same loop would reach 1294 V. Levers: RG_OFF → 10 Ω (≈+30 mJ Eoff, +11 °C at peak) and/or firmware I_pk(V_dc) above 800 V. The old S8 0.3 V term was a 1000× unit error (F01). IGBT SKUs: tf 200–385 ns ⇒ <40 V |

### DC link — 8XX bank (8XX SiC)

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Ripple per can, 30 s peak (worst M/cosφ, 340 A) | 13.8 A | 15.4 A @10 kHz/70 °C | ✅ PASS | 90% of limit · bank 221 A = 0.65·I (Kolar max); 30 s is far inside the can's thermal τ |
| Ripple per can, continuous (185 A) | 7.5 A | 15.4 A | ✅ PASS | 49% of limit |
| Voltage vs U_N at 85 °C, OV trip 880 V | 880 V | 1000 V | ✅ PASS | 88% of limit · normal max 850 V = 85 % |
| Stored energy at V_max (C +10 % + 3 µF local) | 128.4 J | - | ℹ️ | 320 µF nominal |

### DC link — 4XX bank (4XX IGBT)

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Ripple per can, 30 s peak (worst M/cosφ, 400 A) | 16.2 A | 18 A @10 kHz/70 °C | ✅ PASS | 90% of limit · bank 260 A = 0.65·I (Kolar max); 30 s is far inside the can's thermal τ |
| Ripple per can, continuous (250 A) | 10.2 A | 18 A | ✅ PASS | 56% of limit |
| Voltage vs U_N at 85 °C, OV trip 530 V | 530 V | 600 V | ✅ PASS | 88% of limit · normal max 500 V = 83 % |
| Stored energy at V_max (C +10 % + 3 µF local) | 110.4 J | - | ℹ️ | 800 µF nominal |

### DC link — 8XX bank (8XX SiC)

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| ESR heating per can, continuous | 0.44 W | 1.85 W (15.4 A²·7.8 mΩ = the 15 K rise) | ✅ PASS | 24% of limit |

### DC link — 4XX bank (4XX IGBT)

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| 4XX can binding | 50 µF / 600 V (85 °C) in the same 37.5 mm positions | ≥18 A rms @10 kHz/70 °C | 🟡 WARN | CLASS part until the Faratronic RFQ returns the exact MPN + ripple/ESR/life data (review F20/F21) — the busbar drawing is unchanged |

### Regeneration, battery path lost — 8XX bus

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Link charging at 220 kW regen (C_min 291 µF) | 0.89 V/µs; 850→880 V trip in 34 µs | - | ℹ️ | a 100 µs response would end at 962 V and a once-per-PWM-period sample at 5 kHz (200 µs) at 1038 V — why FW-06 is 20 µs on a free-running V_DC slot |
| Link peak with the FW-06 response (15.6 µs to HS-off + ASC request, then 7.6 µs all-off at 481 A) | 906 V | 1000 V can U_N at 85 °C | ✅ PASS | 91% of limit · round 7: the whole chain, not a written 20 µs — the motor's stored magnetic energy adds a motor-dependent step on the non-ASC path (below n_x); HIL event-to-ASC measurement + dyno contactor opening under full regen are the gates |

### Regeneration, battery path lost — 4XX bus

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Link charging at 150 kW regen (C_min 723 µF) | 0.42 V/µs; 500→530 V trip in 74 µs | - | ℹ️ | a 100 µs response would end at 568 V and a once-per-PWM-period sample at 5 kHz (200 µs) at 603 V — why FW-06 is 20 µs on a free-running V_DC slot |
| Link peak with the FW-06 response (15.6 µs to HS-off + ASC request, then 7.6 µs all-off at 566 A) | 542 V | 600 V can U_N at 85 °C | ✅ PASS | 90% of limit · round 7: the whole chain, not a written 20 µs — the motor's stored magnetic energy adds a motor-dependent step on the non-ASC path (below n_x); HIL event-to-ASC measurement + dyno contactor opening under full regen are the gates |

### Regeneration, battery path lost — budget

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| FW-06 latency to the ASC request (RR06/A6-R08) | 15.6 µs = divider lag 6.19 + AMC1311B 2.1 + receiver 0.3 + sample wait 5 (≥ 200 kS/s per channel) + conversion 1 + compare→fault→ASC_REQ 1 | allocated in FW-06 | ℹ️ | the divider's 6.2 µs is the lag of a first-order filter behind a ramp; the route (ADC analog watchdog → eFlexPWM fault + ASC_REQ) is a firmware deliverable measured on HIL — no comparator is added unless that measurement misses the budget |

### Discharge — 8XX values

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Passive 850→60 V, worst (R+5 %, C+10 %) | 65.3 s | 120 s service rule | ✅ PASS | 54% of limit · nominal 56.5 s (12 × 22 k, 2 strings) |
| Bleeder V on the worst-tolerance resistor @850 V | 153.9 V | 200 V working (plain 2512) | ✅ PASS | 77% of limit · review A.6 F24: 5 × 27 k put 184 V (92 %) on it at 850 V |
| Bleeder W/resistor @850 V (R−5 %) | 0.96 W | 2 W | ✅ PASS | 48% of limit |
| Active + passive 850→60 V, worst corner | 1.81 s | 2 s crash target | ✅ PASS | 91% of limit · nominal 1.57 s; paths combined ONCE + 2.5 ms bias delay (F25: S5 counted the bleeder twice) |
| Energy per 10 W wirewound (C+10 %) | 32.1 J | 100 J single-pulse | ✅ PASS | 32% of limit · peak 101 W/resistor decaying τ = 0.67 s; firmware ≤ 3 discharges/5 min (thermal recovery) |
| V per wirewound | 212.5 V | ≥350 V axial class | ✅ PASS | 61% of limit |
| QDIS stuck ON with the battery connected | 384 W continuous (96 W/resistor) | not survivable by 10 W parts — must not flame: TT/Welwyn SQP 'will not burn or emit incandescent particles under any condition of applied temperature or overload' (SQP.pdf) | ✅ PASS | F23 / round 17: bounded, not survived, and now non-flaming by the manufacturer's statement for the bound RDIS part (SQP10-470RJB15); the Yageo SQP alternate states only a flameproof case, so it is an alternate only with the same statement or the overload test in docs/qualification-plan.md (VR-28 asks TT and Yageo for the qualification data). Firmware fires QDIS only with contactors reported OPEN + auto-timeout; a pre-existing FET short is caught at the next precharge (link plateaus ≈5 % low, abnormal τ) and at the next contactor opening (F157), latched as service-required |

### Discharge — 4XX values

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Passive 500→60 V, worst (R+5 %, C+10 %) | 88.5 s | 120 s service rule | ✅ PASS | 74% of limit · nominal 76.6 s (12 × 15 k, 2 strings) |
| Bleeder V on the worst-tolerance resistor @500 V | 90.5 V | 200 V working (plain 2512) | ✅ PASS | 45% of limit · review A.6 F24: 5 × 27 k put 184 V (92 %) on it at 850 V |
| Bleeder W/resistor @500 V (R−5 %) | 0.49 W | 2 W | ✅ PASS | 24% of limit |
| Active + passive 500→60 V, worst corner | 1.7 s | 2 s crash target | ✅ PASS | 85% of limit · nominal 1.47 s; paths combined ONCE + 2.5 ms bias delay (F25: S5 counted the bleeder twice) |
| Energy per 10 W wirewound (C+10 %) | 27.6 J | 100 J single-pulse | ✅ PASS | 28% of limit · peak 75 W/resistor decaying τ = 0.78 s; firmware ≤ 3 discharges/5 min (thermal recovery) |
| V per wirewound | 125 V | ≥350 V axial class | ✅ PASS | 36% of limit |
| QDIS stuck ON with the battery connected | 284 W continuous (71 W/resistor) | not survivable by 10 W parts — must not flame: TT/Welwyn SQP 'will not burn or emit incandescent particles under any condition of applied temperature or overload' (SQP.pdf) | ✅ PASS | F23 / round 17: bounded, not survived, and now non-flaming by the manufacturer's statement for the bound RDIS part (SQP10-470RJB15); the Yageo SQP alternate states only a flameproof case, so it is an alternate only with the same statement or the overload test in docs/qualification-plan.md (VR-28 asks TT and Yageo for the qualification data). Firmware fires QDIS only with contactors reported OPEN + auto-timeout; a pre-existing FET short is caught at the next precharge (link plateaus ≈5 % low, abnormal τ) and at the next contactor opening (F157), latched as service-required |

### Discharge — 8XX values

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| QDIS stress | 0.45 A pk (4XX 0.57 A) | 1200 V / 42 A part | ✅ PASS | fully-enhanced switch, no linear region |

### Gate drive

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| VCC2 low corner vs UVLO-rising MAX | 13.57 V | 12.8 V | ✅ PASS | 94% of limit · §5 corner stack: V_FB, divider, both rectifiers, split zener |
| VCC2 low corner vs recommended-min | 13.57 V | 13 V rec-min | 🟡 WARN | 96% of limit |
| VCC2−VEE2 span (high corner) | 21.7 V | 32 V recommended (35 abs) | ✅ PASS | 68% of limit |
| Peak gate current on/off (DS §9.6 formula) | SiC 3.1/2.5 A · IGBT 5.6/10 A | 10 A driver | ✅ PASS | SiC 3.3/6.8 Ω, IGBT 1.0/1.0 Ω (SKU BOM); the IGBT sink sits at the driver's own 10 A limit |
| Gate-power demand per bank, SiC @10 kHz | 1.32 W | 2.31 W worst-part capacity (typ 3.56 W) at 253 kHz | ✅ PASS | 57% of limit · Qg·ΔV·f + ICC2 + 5.1 k bleeder per domain; F27: IGBT uses the full ±15 V Qg (no scaling); IGBT SKUs fit RT 8.2 k (~308 kHz). The 20 kHz SiC option runs at 86 % of the worst-part flyback capacity — a margin statement, measured on the six-domain gate-supply bench of docs/qualification-plan.md (gate-power capacity at 20 kHz, KL30 9–16 V) |
| Gate-power demand per bank, SiC @20 kHz option | 1.99 W | 2.31 W worst-part capacity (typ 3.56 W) at 253 kHz | 🟡 WARN | 86% of limit · Qg·ΔV·f + ICC2 + 5.1 k bleeder per domain; F27: IGBT uses the full ±15 V Qg (no scaling); IGBT SKUs fit RT 8.2 k (~308 kHz). The 20 kHz SiC option runs at 86 % of the worst-part flyback capacity — a margin statement, measured on the six-domain gate-supply bench of docs/qualification-plan.md (gate-power capacity at 20 kHz, KL30 9–16 V) |
| Gate-power demand per bank, IGBT @5 kHz (full 4.36 µC, RT 8.2 k) | 1.99 W | 2.81 W worst-part capacity (typ 4.34 W) at 308 kHz | ✅ PASS | 71% of limit · Qg·ΔV·f + ICC2 + 5.1 k bleeder per domain; F27: IGBT uses the full ±15 V Qg (no scaling); IGBT SKUs fit RT 8.2 k (~308 kHz). The 20 kHz SiC option runs at 86 % of the worst-part flyback capacity — a margin statement, measured on the six-domain gate-supply bench of docs/qualification-plan.md (gate-power capacity at 20 kHz, KL30 9–16 V) |
| Positive gate clamp (18 V zener + Vf) | 18.8 V | +22 V abs Vgs (SiC) / ±20 V (IGBT) | ✅ PASS | 94% of limit |
| Negative gate clamp (5.1 V zener + Vf) | −5.9 V | −10 V abs Vgs | ✅ PASS | 59% of limit |
| HS DESAT sense point | module aux drain pin 9 (DSH) | - | ✅ PASS | F29 — real HCS600 pin map; kelvin sensing, no busbar drop in the trip level |
| ASC drive level | 5.1 V clamp at ganged pins | GND2+6 V abs | ✅ PASS | F28 — 2.2 k + zener from the +20 V opto rail |
| DESAT trip at the switch (corners) | SiC 7.2–8.8 V ≈ 1.3+ kA · IGBT 4.2–7.2 V | - | ℹ️ | short-circuit detection, not overload — halls + firmware own the operating current limit (F46) |
| DESAT worst detection + soft-off, SiC 47 pF | 3.08 µs (detect 1.93 + STO 1.15 @400 mA) | SiC tSC NOT published — vendor letter | 🟡 WARN | min blank 0.78 µs; release gate: hiitio SC envelope at 850 V/150 °C/+16.9 V (high corner) or a contained SC test (F04) |
| DESAT worst detection + soft-off, IGBT 82 pF | 4.81 µs @400 mA typ · 10.29 µs @100 mA DS min (detect 2.98 µs) | tP ≤ 6 µs @800 V/15 V/175 °C (DS Table 5) | 🟡 WARN | RELEASE GATE: typ closes (80 % of 6 µs), the 100 mA corner does not — NOVOSENSE I_STO distribution + hiitio SC envelope at 850 V and the actual gate bias (§5: 15.4 V nom, soft-off here from the 16.9 V high corner) + contained SC test with integrated energy. Min blank 1.22 µs vs the turn-on tail (DPT) |
| Shoot-through lockout | IN+/IN− complementary pairing | - | ✅ PASS | verified structurally in erc-audit (12 checks) |
| Global DRV_EN drop after a DESAT vs the faulted driver's soft turn-off | 22–53 µs RC delay (+0.4–0.8 µs FLT) | IGBT soft-off 12 µs at the DS-minimum 100 mA | ✅ PASS | 54% of limit · F71: NSI6611 DS is silent on RST/EN during soft turn-off — 10 k/3.3 nF to the USCH Schmitt threshold (V_T− 0.22–0.49 V_CC) makes the design independent of it; FS0B/MCU paths stay undelayed |
| Fault-latch CLEAR one-shot (15 nF into 10 k, at the USCH output) | 72–210 µs low per falling edge | ≥ 49 µs to deliver the drivers' reset edge through the delay | ✅ PASS | 67% of limit · review F06 disposition: PRE=CLR=L (both outputs high) is the ONLY way to give the NSI6611s their RST/EN rising edge while FLT is still asserted — a fault-dominant latch would deadlock recovery. The one-shot bounds one stuck-low pin in hardware; a re-pulsing pin is RR03 → FW-15 (eFlexPWM fault lock), the drivers' own latch and the FS26 watchdog |
| Slow edges at LVC inputs (Δt/ΔV 5–10 ns/V) | RC nodes and FS0B via USCH, FLT diode-OR and FS1B strap via USCH2, RDY_HS/LS via USCH3 (no limit) — none left | every open-drain or RC edge on the safety chain | ✅ PASS | round 7 RR01/RR02: the two RC nodes were 14,000–63,500 ns/V — buffered. The remaining open-drain release edges only return a latch or AND input to its idle level with no output change (PRE release with CLR high holds; RDY releases while MCU_GATE_EN is low per §9 sequencing and FW-14) |

### Flyback

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| VDD in regulation (FFS 52.3k/15k, aux plateau − US1M) | 10.9 V (FFS 11.22 V) | 20 V abs | ✅ PASS | 55% of limit · F33 — a 15 V target through NF would push the secondaries to ~27 V |
| Derived gate rail VCC2 (both diodes, all corners) | 15.4 V nom · 13.57–16.9 V (VEE −4.8…−5.4 V) | 13.5–17.0 V bias window | 🟡 WARN | A6-R06: VCC2 = (V_FFS + Vf_FS)·NS/NF − Vf_sec − Vz. The A.6 model dropped the aux diode (15.6 V claimed, 17 V real). Window: NSI6611 rec-min 13 V + margin; ≤ 17 V keeps the SC current near the 15 V DS data. The low corner sits at the window edge (US1M at peak current, FB bias) — BENCH GATE: six-domain VCC2 at start, full gate load, ASC and no-load, KL30 9–16 V, 24 V and 33 V |
| Restart after a stopped interval (start feed vs FB) | FB from the aux-only FFS node | the start feed must not hold FB ≥ 2.5 V | ✅ PASS | A6-R07: with the A.6 VDD sense a stopped converter sat at 12.3 V (> the 11.83 V target) on a 16 V rail for a 1.5 mA part (DS: 2.3 typ, no min), and at the 18 V clamp at a 24 V jump start — no restart, gate power lost. FFS decays through the 67 k divider (τ 6.7 ms) and the controller restarts |
| Switching frequency (10k/680p) | 252.9 kHz | - | ℹ️ | F32 — Lp 10 µH demands small per-cycle energy; osc anchors per SLUS458I curves |
| DCM peak current vs CS limit (bank) | 2.04 A op | 3.03 A limit (0.33 Ω) | ✅ PASS | 67% of limit · A6-R14 — the common switch/shunt carries all three primaries: bank Lp = 10 µH/3 (the old check used one transformer's 10 µH: 1.18 A) |
| CS limit as the saturation guard | 3.03 A | 4.5 A (Isat unpublished — guard band) | ✅ PASS | 67% of limit · bench-verify core at current limit |
| Start threshold at the 12 V node, worst (2.2 k) | 7.72 V needed | 8.05 V at KL30 = 9 V | ✅ PASS | 96% of limit · A.6 needed 7.95 V (divider on VDD). Burst-to-takeover energy is S1's job |
| Start resistor dissipation @24 V jump start | 0.078 W | 0.25 W (1206) | ✅ PASS | 31% of limit · 12 mW at 16 V; VDD sits at the aux-derived 10.9 V |
| CS resistor power (bank, DCM) | 0.098 W | 0.75 W (1210) | ✅ PASS | 13% of limit · D_on 0.214 at 8.05 V in; the old (Ipk/√3)² assumed a 100 % duty triangle |

### Flyback A.4

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Reflected voltage vs clamp-TVS standoff | 7.4 V | 13 V SMAJ13A standoff | ✅ PASS | 57% of limit · F38 — TVS must stay dark in normal OFF; dots per TDK p.3/9 |
| Drain worst case (35 V test-B plateau / pulse-2a peak on the rail) | 58.9 V | 72 V (BUK7Y14-80E: 80 V, V(BR)DSS 72 V at −55 °C) | ✅ PASS | 82% of limit · F38 — replaces SMBJ85A (94.4 V min breakdown, forward path in OFF); round 17: the rail is let through, no longer clamped at 39 V |

### Safety A.8

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| FS1B load at its V_OL point (strap + FAULT_OUT) | 0.84 mA | 2 mA (V_OL ≤ 0.4 V; current limit ≥ 4 mA) | ✅ PASS | 42% of limit · A6-R01: the 1 k pulls took 5.45 mA — a 4 mA-limit part sat at 1.33 V, ASC_SET_N at 1.67 V (> VIL); the old row compared with the 22 mA maximum and divided by 1000 again |
| ASC latch preset low level (FS1B at V_OL, 1k into 10k) vs USCH2 V_T− | 0.84 V | 1.07 V V_T− minimum at V5A 4.9 V | ✅ PASS | 78% of limit · F40/A6-R01; round 8 buffers the preset (R7-02), so the Schmitt threshold, not the LVC VIL, is the limit |
| FLT diode-OR low level (V_OL + BAT46, cold) vs USCH2 V_T− | 0.75 V | 1.07 V V_T− minimum | ✅ PASS | 70% of limit · round 8: DFLT1/2 Schottky — a 1N4148 (≈0.7 V cold) would put the node at the threshold |
| FS0B load at its V_OL point (5.1 k into the USCH input) | 0.93 mA | 2 mA (V_OL ≤ 0.4 V) | ✅ PASS | 47% of limit · pin ≤ 0.4 V: under the SBC's own 0.7 V read-back threshold and the buffer's 1.0 V V_T− minimum |
| FAULT_OUT asserted level at the VCU (10 k to 5 V) | 1.11 V | 1.5 V (5 V CMOS V_IL) | ✅ PASS | 74% of limit · sink-only through DFO: the VCU must pull up (firmware-contract §9) |
| FAULT_OUT shorted to KL30 (FS1B released): ASC_SET_N clamp vs USCH2 V_I abs max | 5.96 / 6.12 / 6.33 V at 16 / 24 / 35 V (125 °C) | 6.5 V abs max (74LVC3G17) | ✅ PASS | 97% of limit · to ground: blocked by DFO. Into V5A ≤ 0.14 mA live / 0.63 mA with V5A off (disabled LDO2 discharges it through 20–60 Ω: ≤ 40 mV). RFS1/RFS4 14.3 mA for the ≤ 0.4 s pulse, 8.9 mA at a jump start (short-time overload of the 0603s — accepted for a shorted wire). A7-N04: the round-7 BAT46 into V5A back-fed a sleeping rail with ≈8 mA. Vishay alt BZT52B5V6 (+6·10⁻⁴/K): ≤ 6.43 V |
| ASC break-before-make: HS off before LS on | LS starts ≥ 4.42 µs after the latch sets | HS off by 2.71 µs (0.21 µs to EN + 2.5 µs IGBT dead time) | ✅ PASS | 61% of limit · RR05, FS1B path shown (FS0B → USCH → ANDs → EN); the MCU path is faster (eFlexPWM fault on the high-side outputs → IN+ low, tpHL ≤ 0.13 µs), and its low sides come on by PWM after the dead time. EN stays high on the MCU path so LS DESAT keeps priority (DS §8.12). SiC dead time is 1.0 µs — more margin |
| ASC entry, latch set → LS gates on (worst) | 7.56 µs | counted in the FW-06 budget (§2b) | ℹ️ | release ≤ 1.06 µs (VOW3120 tpHL 0.5 µs max + DASCR discharge + tASC_f 0.48 µs); exit is MCU-sequenced (FW-06a) |
| Latched driver FLT masks ASC on every path (UASCG) before DRV_EN drops | ASC_CMD low ≤ 11 ns, LS ASC pins released ≤ 1.07 µs | DRV_EN drop ≥ 22 µs after FLT | ✅ PASS | 5% of limit · round 8 R7-01/A7-N01: the faulted NSI6611 holds its own gate off through IN-low and EN-low with ASC high (DS Fig. 8.11); the gate removes ASC from the HEALTHY low sides and the eFlexPWM fault forces IN low, so the bridge reaches SPO (FS1B-ASC included). The MCU re-enters ASC only through §4c after the FW-15 reset. Wiring locked in erc-audit |
| VOW3120 LED current — ASC opto (RASCL, from UASCG) | 10.77 mA cold · 10.93 mA hot · 15.84 mA max | guaranteed: ≥ 1.25 × I_FLH 8 mA, ≤ 25 mA abs · recommended 10–16 mA | ✅ PASS | 74LVC1G08-Q100: guaranteed margins 1.35× over I_FLH and 1.58× under the abs max; the 10–16 mA window closes only with the ±28 % V_F tempco band (typical-derived — T7-04 bench). Cold = −40 °C V_F 1.72 V with R_out ≤ 21.9 Ω (V_OH ≥ 3.8 V at −32 mA, −40…85 °C); hot = 100 °C V_F 1.53 V with R_out ≤ 34.4 Ω (125 °C); max at V5A 5.1 V, V_F 0.865 V, R_out 0. R7-03/A7-N03: 470 R from the MCU pin gave 6.4–6.8 mA; cross-check R8X-04: 270 R mixed temperatures and dipped to 9.9 mA cold on the TLP152; A.12: the VOW3120 V_F (1.0–1.6 V) moves the window, 261 R would reach 16.4 mA |
| VOW3120 LED current — discharge opto (RQDL, from USCH2 ch3) | 10.77 mA cold · 10.93 mA hot · 15.84 mA max | guaranteed: ≥ 1.25 × I_FLH 8 mA, ≤ 25 mA abs · recommended 10–16 mA | ✅ PASS | 74LVC3G17-Q100: guaranteed margins 1.35× over I_FLH and 1.58× under the abs max; the 10–16 mA window closes only with the ±28 % V_F tempco band (typical-derived — T7-04 bench). Cold = −40 °C V_F 1.72 V with R_out ≤ 21.9 Ω (V_OH ≥ 3.8 V at −32 mA, −40…85 °C); hot = 100 °C V_F 1.53 V with R_out ≤ 34.4 Ω (125 °C); max at V5A 5.1 V, V_F 0.865 V, R_out 0. R7-03/A7-N03: 470 R from the MCU pin gave 6.4–6.8 mA; cross-check R8X-04: 270 R mixed temperatures and dipped to 9.9 mA cold on the TLP152; A.12: the VOW3120 V_F (1.0–1.6 V) moves the window, 261 R would reach 16.4 mA |

### Safety A.9

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| RASCG continuous dissipation while ASC is held (UCC14141-Q1 top into the ZASC clamp) | 88 mW | 272 mW at 85 °C (ESR03EZPF2201, 0.33 W at 70 °C) | ✅ PASS | 32% of limit · S9-01: a generic 0603 rated 0.1 W at 70 °C allows only 82 mW at 85 °C. 2.2 k is kept: it sets the ASC break-before-make RC with CASCD |
| RFS4 with FAULT_OUT shorted to KL30 and FS1B asserted (22 mA limit end) | 0.234 / 0.484 / 0.651 W at 16 / 24 / 35 V | 0.27 W continuous at 85 °C (ESR03, 0.33 W at 70 °C); ≈1.3 W for 5 s overload | ✅ PASS | 86% of limit · A8-03: 0.23 W was only the 16 V case. Pulses ≤ 0.65 W last ≤ 0.1 s (FS1B_TDUR) or the ≤ 0.3 s boot hold — ≤ 0.2 J against the 5 s overload rating; the 16 V row is the continuous case (no release). BACKUP_SAFETY_PATH_FS1B = 0 stops RSTB loops. A 0.1 W 0603 is 0.08 W at 85 °C and fails the continuous case |
| RFS4 with FS1B held a whole key-on (18 V/60 min at 65 °C · 24 V/60 s at 25 °C) — 1206 anti-surge since round 17 | 0.3 W · 0.48 W (element ≈107 °C) | 0.5 W continuous at ≤ 70 °C (ESR18EZPF, AEC-Q200); 155 °C element | ✅ PASS | R9X-14 / round 17: 18 V fits; the 24 V jump start runs the 1206 at 0.97× its nameplate for 60 s — inside the rating (the 0603 ESR03 ran at 1.47×), in a triple condition (FAULT_OUT shorted to KL30, a high-limit FS1B part, a jump start). Drift or an open only disconnects FAULT_OUT from an already-shorted wire, and the FS1B preset keeps working through the strap. Accepted; bench item |
| FW-16 self-test residual energy, 8XX bank (read < 3 V, or QDIS 2 τ from < 60 V) | ≤ 26 mJ (read) · ≤ 15 mJ (QDIS 2 τ = 1.4 s) | 0.1 J design limit | ✅ PASS | 26% of limit · A8-N03: "< 60 V" allowed 0.62 J at C_max 355.3 µF. R9X-07: the first "< 12 V read" rule assumed a 1 V error — it can be 9 V. n_ss from E_LL,pk(n_ss) ≤ 12 V. Fixture-qualified, not a destructive test |
| FW-16 self-test residual energy, 4XX bank (read < 3 V, or QDIS 2 τ from < 60 V) | ≤ 64 mJ (read) · ≤ 39 mJ (QDIS 2 τ = 1.63 s) | 0.1 J design limit | ✅ PASS | 64% of limit · A8-N03: "< 60 V" allowed 1.54 J at C_max 883.3 µF. R9X-07: the first "< 12 V read" rule assumed a 1 V error — it can be 9 V. n_ss from E_LL,pk(n_ss) ≤ 12 V. Fixture-qualified, not a destructive test |

### LV A.9

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| LV feed switch V_GS from NRC: KL30 6 / 9 / 16 / 42 V (pin-side clamp, round 17) | −3.7 / −5.8 / −10.5 / −15.6 V | ±20 V V_GS max; ≥ 4.5 V for the 35 mΩ point | ✅ PASS | 78% of limit · at the 9 V crank floor the gate sits 5.8 V below the source (≤ 35 mΩ; 0.6 A → ≤ 13 mW); even at 6 V (3.7 V) it stays past V_GS(th) max 3 V. R9X-10: taken from NRC, not KL30 |
| LV feed switch held off by a hot 2N7002 (leakage × 10 k gate-source) | 0.5 V at 50 µA | 1.0 V V_GS(th) min | ✅ PASS | 50% of limit · R9X-10: with the first 100 k the same leakage made 5 V — the switch could half-close in a hot parked car |
| LV feed switch inrush into the power board at each wake (drain-gate 100 nF slew) | 0.68 A at 12 V · 1.06 A at 16 V | 3 A (polyfuses, harness, VBATC dip) | ✅ PASS | 35% of limit · R9X-04: slew = gate current / 100 nF ≈ 15 V/ms at 12 V; without it the gate-drain charge alone let 9–31 A through (2.5–4.4 mJ) and dipped VBATC ~2 V at every wake |
| Parking drain, whole inverter (KL30 present, FS26 in LPOFF) | ≤ 59 µA at 25 °C · ≤ 158 µA at 85 °C | 0.1 mA at 25 °C (OEM sleep budgets ≤ 0.1–1 mA per ECU) | ✅ PASS | 59% of limit · N17 + R9X-03: before, the power board's LV side drew ≥ 47 mA from the QA01C-18 no-load inputs alone (≈150 mA in all) and ULDOEX kept the exciter at ≈0.9 mA; both now follow V5A. S1 start-up already starts VDD at 0 V, so a switched feed costs no start time. Round 17 adds CLVC3's leakage, 16 µA estimated (0.01·C·V at 16 V; IR-39 asks the OEM for the hot budget) |

### LV A.4

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| IGN_SNS at 16 V KL15 | 2.68 V | 5 V ADC range | ✅ PASS | 54% of limit · F41 — was a raw diode into PTA25 (13.3 V) |
| IGN pin injection @40 V load dump | 0.72 mA | 3 mA S32K39 injection spec | ✅ PASS | 24% of limit |

### Sensing A.4

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Fail-safe window (healthy-zero 0.5 V vs railed ~0.05 V) | 0.45 V window | ≥0.2 V discrimination | ✅ PASS | 44% of limit · F42 — AMC1311 dead-HV state now distinguishable from a dead bus |
| V_DC linear FS, worst tolerance corner | 902 V | 850 V operating max | ✅ PASS | 94% of limit · F43 — reviewer corner assumed 900 V operation and 1% bottom; ours is 850 V / 0.1% |

### LV A.4

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| TPS55340 SYNC pin level (grounded) | 0 V | 7 V abs on SYNC | ✅ PASS | 1% of limit · F49 — pin 5 is SYNC, not a second VIN; 12 V there exceeds abs max |
| V15 behind ULDO15 @24 V jump start | 15.0 V | 18 V UCC14141-Q1 recommended max (16.5 V was the QA01C window) | ✅ PASS | 91% of limit · F51 — boost pass-through clamped; LDO input 23.5 V << 40 V rating |
| ULDO15 input at the 35 V test-B plateau (let-through) | ≈33.8 V | 40 V NCV4276C operating max | ✅ PASS | 85% of limit · F51; round 17 (F189): 35 V less DREVC, the fuse and polyfuses, DRL and DB15 (≈ 1.2 V) — no TVS clamps the 12 V node below 35 V any more |

### LV A.7

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| CB15O1/2 (V15B) at the 35 V test-B plateau | ≈33.8 V | 50 V MLCC rating | ✅ PASS | 68% of limit · round 7 RR10 — V15B follows V12L−Vf in pass-through; the 25 V parts were overstressed |

### LV A.4

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| ULDO15 dissipation @24 V sustained | 2.8 W | TSD-protected (survival case, not an operating mode) | ✅ PASS | 50% of limit · jump start is stationary service — brief V15 brown-out via TSD is acceptable; passive bleeder unaffected |
| VCORE COUT effective (2×22 µF @1.5 V) | 32 µF | 20–100 µF eff (Table 106) | ✅ PASS | 63% of limit · F52 — was 10 µF nominal; inductor now 2.2 µH per CORE_LSEL_OTP |
| VPRE COUT effective (2×22 µF @6 V) | 26 µF | ≥22 µF test condition | ✅ PASS | 83% of limit · F52 |
| VPRE input effective (22 µF @14 V) | 11.4 µF | ≥10 µF eff | ✅ PASS | 87% of limit · F52 — CLVC2 4.7 µF was under the input spec |
| VREF5 rail effective (whole rail: 2.1 µF nom) | 1.2–2.79 µF | 1.1–3.3 µF eff window (FS26 COUT_VREF) | ✅ PASS | round 12 (R2-F11): 2.2 µF put the nominal rail AT the 3.3 µF limit and the A.4 row never looked at the +tolerance corner; CMA1 stays 1 µF as the reservoir at the MCU VREFH pins (AN5032) |
| ULDO15 feed-forward zero (49.9k·220pF) | 14.5 kHz | 11–18 kHz (onsemi Cb guidance) | ✅ PASS | 81% of limit · F54 — COUT 22 µF ceramic |
| ULDOEX feed-forward zero (38.3k·270pF) | 15.4 kHz | 11–18 kHz (onsemi Cb guidance) | ✅ PASS | 86% of limit · F55 |
| VEXD target for ALM2402 | 12.07 V | 16 V recommended max (18 V abs) | ✅ PASS | 75% of limit · F55 — was raw VBATC: 24 V jump start exceeded abs max |

### Flyback A.4

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| QF gate drive vs BUK7Y14-80E VGS abs | 11.8 V | ±20 V DC (was BUK9Y: ±10 V) | ✅ PASS | 59% of limit · F56 — logic-level part was outside abs max at the VDD drive |
| Gate zener standing load | 0 W (BZT52-C15 dark at the ≈11 V VDD) | was ~0.4 W/zener all ON-time | ✅ PASS | F56 — the 5.6 V clamp conducted ~0.29 A through every ON interval (historical estimate, not carried into the new budget) |

### Discharge

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| QDIS gate V_GS (UCC14141-Q1 envelope through the 1.5 k/10 k divider) vs HCM75S12T4K3 +22 V abs | 11.6–16.3 V (rail 17.4–18.6 V) | +22 V abs max (+18 V recommended) | ✅ PASS | 74% of limit · round 9 A8-N02: 47 Ω passed the QA01C-18 rail straight to the gate (up to 20.9 V at this load); the divider was kept when the regulated UCC14141-Q1 replaced it (A.12) — the top sits 5.7 V under the abs max and the low end (VOW3120 V_OH bound, UVLO 11–13.5 V rising first) still fully enhances a 0.45 A discharge. BENCH: V18Q, QDVO and V_GS at start-up, no load and ON |

### IGBT SKUs

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Gate rails legality (+15.6/−5.1) | on 15.6 V · off −5.1 V | ±20 V abs; VGE(th) min 5.0 V | ✅ PASS | 78% of limit · DS characterizes at ±15; high Vth + Miller clamp justify −5.1 off-bias — dv/dt shoot-through is a DPT row |
| Pin map / footprint | IDENTICAL to HCS600FH120D3C1 (DS p.8: 1=G_L 2=E_L 3=DC− 4=DC+ 5/6=NTC 7=G_H 8=E_H 9=C-sense 10/11=AC) | - | ✅ PASS | zero layout change; MODx pinLabels carry over (KS labels = Kelvin emitter) |
| Short-circuit rating used for DESAT timing | tP ≤ 6 µs @800 V, 175 °C, VGE 15 V (DS Table 5) | - | ✅ PASS | F03: docs and S9 carried a 10 µs class; 850 V/15.6 V operation shortens it — treated as ≈5 µs |

### LV A.4

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| UB15 comp zero (2k·100nF) vs output pole | 796 Hz vs ~140 Hz | fZ slightly above fP (TI rule) | ✅ PASS | 60% of limit · F59 — series RC replaces the lone 10 nF (screening phase margin ~0°); bench Bode gates it |
| UB15 RHP zero @12 V/0.33 A | 451 kHz | far above the loop crossover | ✅ PASS | F59 — light-load boost: RHPZ not the constraint |

### LV

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| V15 boost setpoint (110k/9.53k) | 15.41 V | 13.5–16.5 V design band (the UCC14141-Q1 accepts 8–18 V; the bias LDOs 5–40 V) | ✅ PASS | 28% of limit · kept at the tighter QA01C-era band |
| Boost switch current @9 V | 0.72 A avg | 5.25 A limit | ✅ PASS | 14% of limit |
| NCV4276 5 V load (6 driver VCC1 + optos) | 50 mA | 400 mA | ✅ PASS | 13% of limit |
| FS26 VMONEXT divider (52.3k/10k @5 V) | 0.794 V | 0.8 V fixed reference ±window | ✅ PASS | 6% of limit · F34 — old 10k/18.7k fed 3.26 V = permanent OV; OTP window set around 100 % |
| S32K39 core topology | FS26 VCORE→V15S 1.5 V → QBAL ballast → V11 1.14 V | - | ✅ PASS | F36 — per DS Table 11; direct VCORE→V11 is not a supported topology |
| NCV4276 dissipation @12 V | 0.35 W | ~1.5 W DPAK on copper | ✅ PASS | 23% of limit |
| Chain polyfuse hold (FVBL/FL1, worst chain @9 V, 85 °C) | 1.19 A | 1.50 A hold at 85 °C (MF-LSMF300/24X; 3.0 A is the 23 °C figure) | ✅ PASS | 79% of limit · round 17 (F187): the row compared one chain with the 23 °C hold — FLVC, which carries the whole inverter, is judged in LV A.16 |
| Load-dump path | let-through (round 17): 33 V/18 V stand-off TVS pair at the pin, 33 V TVS on the power board, 100 µF hybrid bulk on NRC | - | ✅ PASS | F25 — all 12 V-node MLCCs raised to 50 V rating; F189 — nothing clamps below the 35 V plateau and every downstream part is rated for it (TPS55340-Q1 VIN 38 V rec / 40 V abs — the earlier 45 V abs text was wrong) |

### Sensing

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| VDC divider @850 V (6.2 k bottom) | 1.865 V | 2 V AMC FS (= 911 V readable) | ✅ PASS | 93% of limit · OV witness keeps headroom above V_bus,max — F27 |
| VDC chain error, WORST CASE uncalibrated | ±2.08 % (±18 V at the 880 V OV trip) | OV trip below the 1000 V can rating | ✅ PASS | 90% of limit · top 1 % correlated + bottom 0.1 % + AMC1311B 0.2 %+offset + receiver 0.2 % + VREF5 0.5 %; RSS would claim ±0.74 % |
| VDC chain error after EOL gain/offset calibration | ≈±0.3 % (residual drift/nonlinearity) | 5 % cross-check window | ✅ PASS | calibrated values feed protection only after the stored record passes CRC + range checks (F42) |
| Shared receiver offset VOFS monitored | UVOF output → MCU ADC (PTB1) | ±5 % of 0.5 V | ✅ PASS | F11: a failed UVOF would shift BOTH channels by up to 0.5 V (≈228 V) and pass the 5 % cross-check — now read directly; BMS pack voltage is the third witness when contactors are closed |
| Divider dissipation @850 V | 255.6 mW total | 6× 1206 (250 mW ea) | ✅ PASS | 17% of limit · 141.7 V per 200 V-rated 1206 — 71 % |
| Hall output at 480 A pk | 3.57 V | 0.3–4.7 V buffer swing | ✅ PASS | 49% of limit |
| Hall ratiometric ref vs ADC ref | V5S(V5A) vs VREF5 — ±1–2 % gain drift after the EOL gain calibration (FW-20) | ±5 % torque-accuracy allocation (IR-26, docs/interface-requirements.md) | ✅ PASS | round 17: two 5 V sources; the static ratio is removed at EOL and the residual temperature drift is bounded inside the torque allocation; the safety checks (KCL stuck-channel, hardware OC) do not depend on it. GEN3 ships the same topology; moving VREFH_SAR_0123 to V5A would make the precision channels ratiometric too — not taken |
| HVIL signatures (drive hi/lo/open) | 3.0 / 2.0 / 2.5 V | - | ✅ PASS | distinct at ±5 % R tolerance (worst separation 0.38 V) |
| Resolver monitor dividers @4 V pk | 2.83 / 3.31 V | 5 V SDADC input | ✅ PASS | 66% of limit |
| Resolver drive @9 V KL30 (round 17 recheck of the A.4.3 row) | outputs centred on VMID_REX 2.5 V (V5A/2): 0.59–4.41 V at the 7.64 V pp setpoint; VEXD ≈ 8.3 V at 9 V KL30 | VEXD ≥ 4.6 V (upper swing + 0.2 V rail margin) | ✅ PASS | 56% of limit · the A.4.3 row assumed a single-ended drive centred on VEXD/2 with an 8 V pp target; since round 12 the two ALM2402 outputs swing ±1.91 V pk around the 2.5 V AFE mid-rail, so the rail only has to clear 4.6 V — any KL30 ≥ 5.3 V keeps the full amplitude. Cold crank is not an amplitude gap; the slew ceiling (2.07 V pk at −40 °C) is the binding limit and is independent of VEXD |

### System A.11

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| SPO freewheel energy at 340 A rms into the isolated link (8XX SiC, screening motor 0.35 mH, zero back-EMF) | 61 J → 1092 V from the 880 V trip | 32.8 J headroom to 1000 V U_N at C_min 291 µF | 🟡 WARN | round 12: rule (a) of §6 covers this motor only up to 250 A rms at zero back-EMF; with back-EMF the decay is slower and the generated work adds 19–76 J (Opus check: 1098–1301 V at 340 A for E_LL,pk 300–800 V), and at E ≥ V₀ the current does not decay at all — a diode-bridge simulation with the real L_d(i)/L_q(i), ψ_f, R_s decides; otherwise rule (b). The reviewers' 300 µH/20 mΩ ODE from 850 V ends at 1038 V (8XX) / 667 V (4XX) |
| SPO freewheel energy at 400 A rms into the isolated link (4XX IGBT, screening motor 0.35 mH, zero back-EMF) | 84 J → 716 V from the 530 V trip | 28.6 J headroom to 600 V U_N at C_min 723 µF | 🟡 WARN | round 12: rule (a) of §6 covers this motor only up to 233 A rms at zero back-EMF; with back-EMF the decay is slower and the generated work adds 19–76 J (Opus check: 1098–1301 V at 340 A for E_LL,pk 300–800 V), and at E ≥ V₀ the current does not decay at all — a diode-bridge simulation with the real L_d(i)/L_q(i), ψ_f, R_s decides; otherwise rule (b). The reviewers' 300 µH/20 mΩ ODE from 850 V ends at 1038 V (8XX) / 667 V (4XX) |

### Sensing A.11

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Resolver excitation at 10 kHz (SWG → MFB → ALM2402 H-bridge) | |H| 2.07 (1.94–2.2 over ±10 % caps) → 8.7 V pp at the amplifier (7.8–9.5 over the SWG range, untrimmed) | the trim (FW-10) holds the MONITOR plane at 7.2 V pp; the resolver floor 6.5 V pp is at the WINDING | ℹ️ | f0 16.6 kHz, Q 0.73, passband gain −2.15 (round 16: 28 k, was 24 k / 1.85 — the SWG low corner gave 6.34 V pp at the winding through the RSX + PTC losses). Round 12 (R2-F04): the A.10 network had the MFB feedback pair swapped (|H| 0.18) |

### Sensing A.15

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Excitation at the WINDING with the SWG at its LOW corner (1.884 V pp, trim saturated) | 7.1 V pp (amplifier 7.81 × 0.909: RSX 2.2 Ω + PTC 1.3 Ω per line into 70 Ω) | ≥ 6.5 V pp (gate ㉕) | ✅ PASS | 92% of limit · A14-R04 (review 3): the corner the round-15 row did not carry — with the 24 k it was 6.34 V pp. The 70 Ω primary is the screening assumption; the selected resolver's impedance decides (gate ㉕) |
| FW-10 trim setpoint 7.2 V pp at the monitor: SWG amplitude needed vs its low-corner maximum | 1.843 V pp (amplifier 7.64 V pp = 3.82 V pk per output) | ≤ 1.884 V pp (MAXAPP min) · ≤ 4.14 V pp per output (−40 °C slew) | ✅ PASS | 98% of limit · winding 6.94 V pp cold, 6.3 V pp for an hour after a PTC trip (5 Ω) — the FW-10 window flags that state, as intended. The SWG ramps up from ≈ 1.5 V pp under the trim: the untrimmed max corner (9.5 V pp) would slew-limit |

### Sensing A.11

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Hall signal open wire (R⟨ph⟩B0 100 k) | reads 0 V; 2.5 → 0.2 V (the window edge) in 0.83 ms | outside the HC5FW 0.2–4.8 V window (FW-05); RL ≥ 10 k | ✅ PASS | round 12 (R1-F03): the README had claimed the pull-down since A.4; an unpowered sensor reads the same 0 V. The Σi = 0 check alone could not see a stale 2.5 V (zero-current) reading at standstill |

### Sensing A.13

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| VDC bias LDO (NCV4276C DPAK) behind the 47 Ω ballast, production UCC12051-Q1 at 96 mA | 0.57 W → Tj ≈ 119 °C at 85 °C (134 °C at 100 °C) on the 58.5 K/W pad | 150 °C Tj max | ✅ PASS | 79% of limit · A12-R07: without the ballast the same current is 0.96 W → 141 °C. R5L carries 0.45 W (2512 1 W, ≈ 0.82 W derated at 85 °C); LDO input ≥ 9.8 V at the 96 mA / 14.55 V corner (dropout needs 5.5 V). Layout rule dfm §4 stays; the hot first article measures both input currents |
| LDO pass dissipation, analytic maximum over load (P = (Vs−Vo)·I − R·I²) | 0.6 W at 113 mA (15.45 V in, 4.9 V out, 46.5 Ω) | Tj ≈ 120 °C at 85 °C, 135 °C at 100 °C | ℹ️ | round 15 check: the worst case sits above the 96 mA budget (the resistor makes the LDO dissipation a concave function of load), so the 96 mA screen is not the ceiling — the hot first article measures the loaded input current |
| R5L ballast dissipation (47 Ω 2512, 1 W) | 455 mW at 96 mA, +5 % R | ≈ 820 mW at 85 °C (1 W at 70 °C, linear to 155 °C) | ✅ PASS | 55% of limit · the resistor sits on the 2 oz copper next to the LDO; ERC locks the 47 Ω and the V15 → V15Lx → LDO chain |

### Sensing A.11

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| VDC bias barrier working voltage (UCC12050 V_IOWM) | 1697 VDC / 1200 Vrms reinforced (VDE 0884-11) | 850 VDC link (same class as the AMC1311, 1.2 kVrms) | ✅ PASS | 50% of limit · round 12 (R1-F12, R2-F02/F03): the A.4.4 bind "MGJ2D150505SC" does not exist and the MGJ2 family is reinforced to 150 Vrms only — every module family checked (MGJ1/2, NXE/NXJ, RECOM RxxP/R1SX, Mornsun QA, ADuM6028) fails the working-voltage criterion |

### Sensing A.13

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Excitation-monitor pad injection (REXM 18 k, MCU unpowered, 0 V pad) | 1.96 mA at 35 V · 2.81 mA at 50 V | 3 mA per pad (S32K39 operating and abs max) | ✅ PASS | 94% of limit · F03: the round-12 5.1 k / 12 k / 24 k network let 5.45–5.66 mA in (powered-clamp assumption); 18 k / 42.2 k / 84.5 k keeps the 0.70 / 0.82 ratios. REXM1/3 dissipate 68 mW during a 35 V pulse (0603, 100 mW at 70 °C; 75 V rated) and 32 mW at a 24 V/60 s jump start |
| Excitation-monitor anti-alias filter (220 pF C_AAF across SDADC1, Thevenin 12.6 k / 14.8 k per leg) | corner 26.3 kHz, −20.8° at 10 kHz (SIN/COS −23.8°) | R_AAF ≤ 20 kΩ per leg, C_AAF 180–220 pF (Table 38) | ✅ PASS | F05: the pair had no external capacitor. The 3° static offset between the demodulation reference and the signal channels is a fixed number, absorbed by the FW-20 phase calibration; the 220 pF ±5 % C0G stays ≥ 180 pF |
| Exciter terminal fault — clamp at the protected node during the PTC trip window (PTC in the path, A13-R01) | 10.7 V at 24 V (15.6 A) · 10.9 V at 35 V (28.3 A ≤ 40 A PTC I_max) | ≤ 12.8 V while VEXD is up (12.1 V rail + a diode) · 18 V output abs max | ✅ PASS | 85% of limit · F04: the amplifier's regulated 12.1 V rail does not protect an output pin forced by the harness (ALM2402 output abs max 18 V; its output diodes conduct reverse current only as pulses, DS §8.3.6 — the clamp below the rail avoids that entirely). SMDJ8.5A-HRA (round 17; SMCJ8.5A in round 16, SMCJ8.5CA in round 15) V_BR 9.44–10.4 V clears the excitation swing; the MF-MSMF020 (0.2 A hold, 30 V, AEC-Q200) trips in 0.06 s at 6 A and faster at these 30–70× overloads, then holds the TVS at ≈ 0.3–0.8 W. Line-to-ground shorts are the ALM2402's own current limit (≈ 750 mA). BENCH (gate ㉘): both lines, MCU on/off, 24 V/60 s and 35 V/400 ms |

### Sensing A.15

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Exciter TVS energy — single fault (line shorted to KL30 up to the 24 V jump start), PTC clearing bounded by the 8 A / 20 ms sheet point | 1.7 J at ≤ 10 ms · 3.3 J at the 20 ms bound (15.6 A through the PTC) | 9 J supported at 10 ms (SMDJ 3 kW/1 ms, ≈ 0.9 kW/10 ms) — a lower energy over a longer pulse is inside the curve | ✅ PASS | round 17: the round-16 conditional row mixed the 24 V single fault with the 35 V load-dump case. At 24 V the 20 ms bound (all sheet trip points above 8 A are faster) puts less energy into the SMDJ8.5A-HRA than its 10 ms capability (it was already inside the 1.5 kW SMCJ's 5.5 J), so the single fault is closed on paper; docs/qualification-plan.md still records the clearing waveform (cold, hot, post-trip) as a characterisation, not a release gate |
| Exciter TVS energy — load-dump-coincident fault (35 V for ≤ 400 ms, ISO 16750-2 test B, AND a line short at the same time) | 3.1 J at ≤ 10 ms · 6.2 J at 20 ms (28.3 A) | 9 J supported at 10 ms by the 3 kW SMDJ8.5A-HRA (round 17 upgrade; the 1.5 kW SMCJ8.5A supported 5.5 J and left the 20 ms bound uncovered) | ✅ PASS | round 17: a short to a 35 V network exists only while a test-B pulse is on the harness — a double event (IR-16 / IR-33) — and the upgraded 3 kW SMDJ8.5A-HRA on the same SMC pad now covers even that energy at the 20 ms PTC bound without extrapolation, so the TVS side of the double event is closed on paper; the PTC I_max side stays the accepted double event (next row). VR-17 (pulse data beyond 10 ms) becomes informational |
| Exciter PTC current vs I_max 40 A — single fault at 24 V with ZERO external impedance | 37 A (V_BR max 10.4 V, PTC R_min 0.35 Ω, TVS r_dyn) | 40 A (MF-MSMF020/33X I_max; trip-cycle life tested at V_max/I_max, no arcing or burning) | ✅ PASS | 92% of limit · round 17: the single fault passes with no source-impedance allocation at all (the zero-impedance case is a bound — any real harness adds ≥ 50 mΩ; the 3 kW TVS's lower r_dyn raised this figure from 35 A) — the round-16 '≥ 0.27 Ω' requirement applied to the 35 V case only |
| Exciter PTC current vs I_max 40 A — load-dump-coincident fault (35 V) | at 0.1 Ω external: 52 A > 40 A · needs ≥ 0.27 Ω source + harness; V_max 33 V is also exceeded by 6 % | double event (IR-16 / IR-33) | ℹ️ | the PTC still trips (faster than at 8 A); above I_max its survival is not warranted — it may fail open, which the FW-10 amplitude window reports as a resolver fault (fail-safe, not fail-dangerous). Accepted as a double event; the OEM allocation is IR-16 and the PTC transient question VR-16 |
| Exciter back-drive with VEXD absent — rated diversion (DEXP/DEXN Schottky to VEXD, round 17) | 4.8 A peak, τ = 59 µs (99 % of the 0.28 mC by 270 µs); 0.13 mJ in the Schottky, 1.5 mJ in RSX; rail then ≈ 10.5 V | PMEG4050EP-Q I_FSM 70 A (8.3 ms) · NCV4276C output abs max 40 V with the input at 0 V (DS Maximum Ratings: V_Q −1…40 V) · ALM2402 18 V abs | ✅ PASS | 7% of limit · round 17 closes the round-16 OPEN row: the internal ALM2402 diodes (≈ 0.8 V at 5 A, Fig. 7) now share only the residual above the Schottky's 0.49 V max at 5 A; reverse-biased 8–12 V in service its leakage (300 µA max at 40 V/25 °C, graph at 125 °C) is a DC offset the amplifier sinks; the LDO's output-above-input case is specified (40 V abs), and the rail cannot exceed the TVS clamp. Gate ㉘ keeps the measurement as characterisation (reverse rail current into ULDOEX), no longer as a release gate |
| Exciter negative terminal fault — UNIDIRECTIONAL SMDJ8.5A-HRA forward-conducts (node at −0.7…−1.2 V) | TVS forward ≈ 27 A at −24 V until the PTC trips; amplifier lower diode ≈ 0.23 A through RSX (114 mW) | I_FSM 300 A (8.3 ms half sine — SMDJ-HRA sheet; the SMCJ had 200 A) · ALM2402 −0.3 V abs bounded by the diode | ✅ PASS | 13% of limit · round 16 (A14-R02): with the round-15 BIdirectional part a negative fault put 4.4 A (43 W) through the lower output diode and RSX for the PTC trip time — 0.9 J in a 1206; the excitation never goes below ground (4–8 V around VMID), so the unidirectional part is free. Negative faults themselves are an OEM allocation (gate ㉘) |
| Exciter PTC hold current (MF-MSMF020/33X, 0.07 A at 85 °C) vs the excitation current at the interface minimum resolver impedance | ≈ 34 mA rms with the 70 Ω screening resolver · 41 mA rms at the 60 Ω interface minimum (IR-13, docs/interface-requirements.md) | 70 mA at 85 °C (Bourns derating table — the /33X row; the unsuffixed 020 is 90 mA) | ✅ PASS | 58% of limit · round 17: the round-16 '60 mA assumed maximum' had no source; the resolver interface now states Z_primary ≥ 60 Ω at 10 kHz (IR-13), which bounds the excitation current at 58 % of the hot hold. The selected resolver is measured against it at qualification (docs/qualification-plan.md); a nuisance trip shows as the FW-10 amplitude window, not as a silent fault |
| Amplitude planes: monitor (protected node) vs winding | monitor 7.2 V pp (trim setpoint) → winding 6.94 V pp cold (× 0.964), 6.3 V pp for an hour after a trip (× 0.875) | ≥ 6.5 V pp at the WINDING (gate ㉕); the post-trip state is flagged by FW-10 | ℹ️ | the round-15 wording 'the monitor sees what the resolver gets' was too strong: the monitor does not observe the PTC or harness drop. EOL characterises monitor-to-terminal transfer with the real harness and sets the FW-10 window with that allowance |

### Sensing A.13

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Motor-temp line KL30 short: SMAJ5.0A carries the fault until FMT opens | ≈ 16 A through ≈ 1 Ω (fuse + harness) · V_C ≤ 9.2 V | I_PP 43.5 A (400 W, 10/1000 µs) | ✅ PASS | 37% of limit · F18: the PESD5V0L1BA was an ESD-class part. The 1 k RMTxS limits the OPA333 input to 3.9 mA (10 mA abs); the 10 k pull-up injects < 0.5 mA into VREF5. FMT = Littelfuse 0438.375WRA (0603, 63 V DC, I²t 0.0041 A²s → ≈ 16 µs melting at 16 A, AEC-Q200) |

### Discharge

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Stuck-ON QDIS with the battery connected, 8XX | 0.45 A · 96 W per resistor (10 W rated) | resistor must not flame (gate ㉖): TT/Welwyn SQP statement — no burning or incandescent particles under any overload (SQP.pdf) | ✅ PASS | F09 / round 17: the software timeout is bypassed by a shorted switch; the firmware detects the unexpected discharge at the next contactor opening (latched DTC, no re-energisation); the no-flame property is the manufacturer's statement for the bound part, the fail-open time is a characterisation in docs/qualification-plan.md (VR-28; the Yageo alternate needs the same statement) |
| Stuck-ON QDIS with the battery connected, 4XX | 0.57 A · 71 W per resistor (10 W rated) | resistor must not flame (gate ㉖): TT/Welwyn SQP statement — no burning or incandescent particles under any overload (SQP.pdf) | ✅ PASS | F09 / round 17: the software timeout is bypassed by a shorted switch; the firmware detects the unexpected discharge at the next contactor opening (latched DTC, no re-energisation); the no-flame property is the manufacturer's statement for the bound part, the fail-open time is a characterisation in docs/qualification-plan.md (VR-28; the Yageo alternate needs the same statement) |

### Gate drive

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| SC current-extinction budget, SiC (F07) | 3.38 µs at the 400 mA I_STO typ · 6.83 µs at the 100 mA DS minimum (fall/tail 0.3 µs assumed) | SiC withstand not published | 🟡 WARN | the detection and soft-off terms are the DESAT rows above; the current fall/tail term is an ASSUMPTION until the module's SC waveform is in hand — release gates ③ (vendor SC data, contained SC test). Values are tuned only within the false-trip and overshoot limits; a stronger driver only if this budget cannot close |
| SC current-extinction budget, IGBT (F07) | 6.61 µs at the 400 mA I_STO typ · 12.09 µs at the 100 mA DS minimum (fall/tail 1.8 µs assumed) | ≤ 6 µs withstand at 800 V (hiitio Table 5; 850 V not documented) | 🟡 WARN | the detection and soft-off terms are the DESAT rows above; the current fall/tail term is an ASSUMPTION until the module's SC waveform is in hand — release gates ③ (vendor SC data, contained SC test). Values are tuned only within the false-trip and overshoot limits; a stronger driver only if this budget cannot close |

### Safety A.13

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Firmware DESAT hold before MCU_GATE_EN drops (TI_CAL_DESAT_EN_HOLD_US default 60 µs) | 60 µs | ≥ 54.1 µs (10 k / 3.3 nF RC upper corner + FLT) | ✅ PASS | 90% of limit · A12-R05: the DESAT ISR used to call br_spo(true) at once, bypassing the deliberately delayed FLT → DRV_EN path through the undelayed MCU_GATE_EN AND input; the hold is enforced inside the bridge module for every caller (firmware tests: early/late ISR entry, apply_decision and recovery during the hold) |

### Sensing

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| V_DC divider for an extended 920 V variant | 6.2 k bottom = 911 V full scale (893 V worst corner); 5.1 k would give 1108 V | separate release: scaling, OV thresholds, calibration, can hot-voltage and switching margins together | ℹ️ | F10: the fitted divider is right for the 500–850 V contract only; the 470 k tops are 1 % thin-film, the bottoms 0.1 % (BOM) |

### Sensing A.11

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Resolver wire shorted to KL30: SDADC pin injection (RSINF 12 k + RSINR 120, MCU rail at 0 V, −1 % R) | 1.33 / 2 / 2.92 mA at 16 / 24 / 35 V into a 0 V node | 3 mA per pin — the S32K39 operating AND absolute-maximum limit, in every power state | ✅ PASS | 97% of limit · round 12/13: the GEN3 330 Ω + 120 Ω let 23 mA in; the round-12 10 k held only the powered case (5.7 V clamp) — unpowered it was 3.42 mA. The VMID buffer sinks 5.4 mA through the two 12 k bias resistors at 35 V (OPA348 ≈ 7 mA at 125 °C; it saturates toward V5A, an invalid-resolver state for FW-10). 220 pF C_AAF at the pins (S32K39 Table 38: 180 pF min) + 47 pF + 22 pF common-mode both legs → corner 23 kHz, −24° at 10 kHz on both channels; source 24 k vs Z_DIFF 215–380 k: gain 0.899–0.94, cancels only as far as the channels match — worst independent corners 1.29° electrical, an EOL calibration item (FW-20), not "ratio-cancelled" |

### LV A.16

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Test B by ABSORPTION — why the entry changed (24 V stand-off TVS, Us 79/101 V, td 400 ms) | TPSMC24CA-VR (SMC): Ri 0.5 Ω: 11.2 A / 96 J (35 V behind Ri) · 70–100 A / ≤ 492 J (clamp in parallel) · Ri 2 Ω: 3.7 A / 30 J (35 V behind Ri) · 23–33 A / ≤ 125 J (clamp in parallel) · Ri 4 Ω: 2 A / 16 J (35 V behind Ri) · 12–18 A / ≤ 62 J (clamp in parallel) ‖ SM8S24A (DO-218AB): Ri 0.5 Ω: 14.9 A / 121 J (35 V behind Ri) · 94–134 A / ≤ 501 J (clamp in parallel) · Ri 2 Ω: 4 A / 32 J (35 V behind Ri) · 25–36 A / ≤ 122 J (clamp in parallel) · Ri 4 Ω: 2 A / 16 J (35 V behind Ri) · 13–18 A / ≤ 60 J (clamp in parallel) | SMC: no rating beyond 1 ms (1.5 kW at 1 ms extrapolates to ≈ 112 W at 400 ms); SM8S24A 50 A for 10 × 400 ms (Vishay load-dump table) | ℹ️ | round 17 (F189): the result hinges on how the generator makes Us* — a 35 V source behind Ri, or the unsuppressed 79–101 V source with the 35 V clamp in parallel (the alternator physics, and the parameter set ISO 16750-2 gives for test B). In the second model any local clamp below 35 V takes the full unsuppressed current: the SM8S24A holds only for Ri ≳ 1.5 Ω, the SMC part at no Ri. Let-through removes the dependence; the rows below carry the 257 ms plateau of Us 101 V / td 400 ms |
| Test B: KL30 pin-side TVS knee (DTVSC V_BR,min at 18 °C + DTVSC2 forward) vs Us* + 1 V | 36.95 V | ≥ 36 V (Us* 35 V + generator tolerance; ISO 16750-2 test B at RT) | ✅ PASS | 97% of limit · the TVS draws 0 A at every Ri 0.5–4 Ω in both generator models, so no Ri is required of the OEM (IR-03). At −40 °C the knee is 34.9 V (a cold central clamp also sits lower); the old TPSMC24CA-VR knee was 26.7 V |
| Test B: inverter LV current at the 35 V plateau vs FLVC and the chain polyfuses | 0.94 A (L chain 0.53 A · H chain 0.12 A) | FLVC 3.92 A rerated (5 A × 0.98 at 85 °C × 0.8); FVBx/FHx 1.50 A hold at 85 °C | ✅ PASS | 35% of limit · nothing trips during the pulse, so no fuse or polyfuse V_max applies (the 24 V polyfuse V_max mattered only because the old TVS drew the dump through it) |
| Test B: FS26 VSUP / VSUP_PWR at the 35 V plateau | 34.6 V | 36 V — FS26 High Voltage Extended Operation, full function for a limited time (DS Rev.3 Fig. 8); 40 V abs | 🟡 WARN | 96% of limit · the datasheet names load dump as the use case but gives no duration for its limited period; the design needs 5 × ≤ 0.4 s — VR-29 (NXP). VSUPOV_I latches at 19.3–20.7 V: an interrupt, not a fault reaction |
| Test B: TPS55340-Q1 VIN / EN at the 35 V plateau (V12L) | 34.17 V | 38 V recommended / 40 V abs (SLVSBV5C §6.3/§6.1) | ✅ PASS | 90% of limit · in pass-through (V12L above the 15.4 V setpoint) SW ≈ VIN; the DS pin table still reads VIN 2.9–32 V against the 38 V table — VR-30 (TI) |
| Test B: DB15 (SS34) reverse when the boost restarts at the end of the dump | 33.77 V | 40 V V_RRM (SS34) | ✅ PASS | 84% of limit · V15B holds the plateau value on CB15O while V12L falls back; the switch closing puts it across the diode |
| Test B: ULDO15 junction, D2PAK-5 (85 °C ambient, V15 0.356 A, Us 101 V / td 400 ms) | 141 °C (+49 K from 6.7 W) | 150 °C (NCV4276C Tj max; TSD 150–210 °C) | ✅ PASS | 94% of limit · F189: in DPAK-5 the same pulse reaches 156 °C — over the 150 °C maximum, so ULDO15 moves to NCV4276CDSADJR4G. The single-pulse curves are read from the DS figures and the V15 load is the conservative 4 × 1 W class figure; the thermal first article measures it at 35 V |
| Test B: ULDOEX / UGDL junction (DPAK-5, 85 °C ambient) | 101 °C (1.3 W) / 121 °C (1.5 W) | 150 °C | ✅ PASS | 81% of limit · ULDOEX at the IR-13 bound (≤ 42 mA rms excitation + ALM2402 quiescent ≈ 60 mA); UGDL at the 50 mA V5GD budget; steady part on the 58.5 K/W pad |
| Pulse 2a (+112 V / 2 Ω, 50 µs) on NRC — closed form ΔV = Us·b/(b−a)·(e^(−a·t*) − e^(−b·t*)), t* = ln(b/a)/(b−a) | 37.5 V (td read to 50 %) · 23.4 V (td to 10 %); C_eff 92 µF | 40 V (FS26 VSUP, TPS55340-Q1 VIN abs); TVS knee 36.95 V | ✅ PASS | 94% of limit · a = ln10 or ln2 over td, b = 1/(Ri·C_eff); C_eff = CLVC3 at −20 % + the card MLCCs at bias, QLVS off (the power board's ≈ 14 µF adds when it is on). Without CLVC3 the same pulse reaches 78 V — the bead and MLCCs do not attenuate a 50 µs pulse. Us = +112 V is the 2011 upper level quoted from secondary sources (2004 levels +37/+50 V, TIDUC41) — IR-38 |
| Pulse 1 (−150 V / 10 Ω, 2 ms) — reverse voltage on DREVC behind the pin-side pair | 38.3 V (FCO at −24.8 V, 12.5 A) | 60 V V_RRM (STPS5L60S) | ✅ PASS | 64% of limit · F185: V_BR,max(DTVSC2) + R_d·I + V_F(DTVSC) with NRC still at 13.5 V; 0.17 J in DTVSC2 (311 W peak vs 1.5 kW at 10/1000 µs). Before, the TVS sat behind DREVC: the Schottky avalanched at ≈ 655 W against P_ARM 144 W (10 µs, 125 °C; ≈ 5.8 W at 1 ms, DS Fig. 3). A single 33 V bidirectional part at the pin would leave 58.8 V (0.98) |
| Pulse 3a (−220 V / 50 Ω) — reverse voltage on DREVC | 37.1 V (3.9 A) | 60 V V_RRM | ✅ PASS | 62% of limit · the 150 ns pulses of 3a/3b move the NRC bulk by millivolts |
| Reverse battery −14 V / 60 s vs the negative-leg knee (DTVSC2 at −40 °C) | 14 V vs 18.86 V | dark (no conduction) | ✅ PASS | 74% of limit · DREVC blocks the rest; KL15 is blocked by DIGN (US1M) |
| Test B: DTVH/DTVL (TPSMC33CA-VR) dark at the V12 plateau | 34.17 V vs 36.45 V knee (18 °C) | below the knee | ✅ PASS | 94% of limit · the power-board TVSs sit behind DREVC, the chain polyfuses and DRH/DRL; a 24 V part here would take the dump through FVBx/FHx |
| Jump start 26 V / 60 s (ISO 16750-2:2023, RT and T_min) vs the lowest KL30 TVS knee | 26 V vs 34.4 V at −40 °C (76 % of the knee) | IR-02 stays 24 V (2012 edition) | ℹ️ | the 2023 edition raises the jump start to 26 V and adds T_min; the old TPSMC24CA-VR knee was 25.1 V at −40 °C (it would conduct for the whole minute). Rows sized at 24 V that change at 26 V: flyback start resistor 0.104 W (0.25 W 1206), ULDO15 3.5 W (TSD survival case), RFS4 with FS1B held (Safety A.9), the exciter single-fault TVS/PTC rows (Sensing A.15) |
| FLVC continuous current at 85 °C: 9 V crank / 13.5 V (whole inverter) | 2.54 A / 1.73 A | 3.92 A (5 A × 0.98 at 85 °C × 0.80 continuous) | ✅ PASS | 65% of limit · F187: the MF-LSMF300/24X it replaces holds 1.50 A at 85 °C (1.69× at 9 V, 1.15× at 13.5 V) — the old row compared one chain with the 23 °C hold. Load model: flybacks 2 × 4.23 W, boost 6.45 W, card 5.0 W constant-power + 0.2 A linear |
| FLVC I²t: KL30 hot-plug into CLVC1–3 (16 V, 30 mΩ loop) / pulse 2a / pulse 1 | 0.59 / 0.03 / 0.068 A²s | 36 A²s melting (< 10 ms, Bel 0680L5000) | ✅ PASS | 2% of limit · each ≤ 2 % of melting (pulse 2a repeats 500×: 0.1 % per hit); a hard short behind the fuse (≥ 90 A at 13.5 V through 0.15 Ω) is > 18 × I_n: open in 0.01–0.1 s, inside the 500 A / 75 V DC interrupting rating |
| KL15 pulse 1 (−150 V / 10 Ω): DIGN reverse and the WAKE1 pin | 164 V on DIGN (US1M) · WAKE1 ≥ 0 V | 1000 V (US1M); WAKE1 −0.3 V abs min, −5 mA reverse DC (FS26 Table 5) | ✅ PASS | 16% of limit · F188: RIGN1 now hangs behind DIGN, so neither pulse 1 nor a reversed KL15 reaches WAKE1 — before, 5.1 k straight from KL15 took ≈ 29 mA at pulse 1 (5.9× the −5 mA rating) and 2.6 mA at a reversed battery. The added diode drop leaves WAKE1 at 3.58 V for a 6 V KL15 (V_IH 3.5 V at the high-threshold OTP, 2.0 V at the low) |

### Power stage — all SKUs

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| IGBT short-circuit rating condition vs the design corner | 6 µs at 800 V / 15 V / 175 °C (hiitio DS) | design corner 850 V / VCC2 up to 16.7 V | 🟡 WARN | round 12 (R1-F06, R2-F05): the RR04 release gate (contained SC test) now also asks hiitio for the SC statement at 850 V and the 16.7 V gate-rail corner, or the gate rail's upper corner is tightened; the stale "inside 6 µs" BOM text was removed |

### Safety

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| FS0B → driver EN path | 2 gate delays (~20 ns) + driver td | - | ✅ PASS | no software; erc-verified topology |
| ASC latch power | V5A + RASCP default-low | - | ✅ PASS | survives MCU reset; FS1B can SET via RFS1 |
| ASC drive path | VOW3120 + UCC14141-Q1 + 5.1 V clamp, DCN-referenced | - | 🟡 WARN | DS 1.2 confirms ASC forces OUTH high at a GND2-referenced 0-5 V pin (F28 level fix applied); behaviour DURING VCC2-UVLO is unspecified — NOVOSENSE statement requested (VR-09, docs/vendor-requests.md) and the ASC-hold measurement with SBC-held gate power is a procedure in docs/qualification-plan.md |
| Default-OFF discipline | 11 pulldowns power + 4 card | - | ✅ PASS | erc-verified |

## Open vendor/bench inputs (every WARN above names one)

Since round 17 every remaining WARN resolves to a numbered item in one of three documents: the questions to
vendors with acceptance criteria in [vendor-requests.md](vendor-requests.md) (HIITIO module Ls, SiC
short-circuit envelope at 850 V and FIT; NOVOSENSE I_STO distribution, RST/EN during soft-off, ASC during
VCC2 UVLO; TDK VGT12EEM saturation/working insulation; Faratronic 4XX can; Murata BLM31PG121SH1L specification (the MGJ2 line of earlier rounds is stale — superseded by the UCC12051-Q1 since A.11); Bourns,
Littelfuse, Diodes, Samtec, NXP, LEM, the coldplate supplier), the assumptions about the vehicle, motor,
resolver and harness in [interface-requirements.md](interface-requirements.md) (motor data for the
safe-state decision, resolver impedance, the exciter fault double event, central load-dump suppression at Us* ≤ 36 V (no R_i needed since round 17), LV-loss
behaviour) and the executable procedures with pass criteria in [qualification-plan.md](qualification-plan.md)
(double pulse, contained short circuit, thermal, VCC2 six-domain bench, terminal-fault bench ㉘, discharge
㉖, HIL/EOL). Datasheet values used are in docs/datasheets/EXTRACTED-PARAMS.md.

## Method

Closed-form worst-case analysis (tolerance corners: R ±5 %, precision ±1 %, C +10 %,
KL30 9–16 V, V_bus to 850 V / 500 V) — the correct tool at schematic phase; the time-domain
companion is `sim-verify.mjs`. Review A.6 added an independent cross-check: every
contested number (losses, overshoot, DESAT timing, startup, hold-up, discharge, ripple,
sensing) was recomputed in a separate script by a second reviewer; the two agree within
model assumptions. SPICE adds nothing without vendor switch models; the double-pulse,
short-circuit, thermal and EMC items are bench gates listed in `docs/firmware-contract.md`
and `docs/review-A6-disposition.md`.
