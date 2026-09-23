# Design Verification Report — rev A.9 (2026-09-23)

End-to-end verification of the 220 kW / 800 V traction inverter at actual operating corners
(V_bus 500–850 V · KL30 9–16 V · 10 kHz · 65 °C coldplate), worst-case component tolerances.
Three independent layers:

1. **Geometric pin-verify** (sheets vs netlist, `kicad5-verify.mjs`) — the sheets ship only at 100 %
2. **Structural ERC audit** (netlist vs design intent, `erc-audit.mjs`) — 0 fail, incl. a lock-in per fixed finding
3. **Numeric verification** (this report, `design-verify.mjs`): **116 PASS · 14 WARN · 0 FAIL** (+17 info)

A WARN is an item this analysis cannot close on paper — each names its bench or vendor gate.
Every SKU of the platform (8XX/4XX × SiC/IGBT — `loss-model.mjs`) is checked on the
same PCBs; losses and thermal use the shared model that `sim-verify.mjs` also runs.

## Findings log (F1–F36 rev A.3 campaign · F37–F46 rev A.4 · F47–F51 rev A.4.1 · F52–F57 rev A.4.2 · F58–F59 rev A.4.3 · F60–F62 rev A.5 docs audit · F63–F76 rev A.6 external review round 6 · F77–F89 rev A.7 review round 7 · F90–F97 rev A.8 review round 8 · F98–F105 its cross-check · F106–F113 rev A.9 review round 9 · F114–F119 its cross-check — all fixed; review cross-reference in [`review-A6-disposition.md`](review-A6-disposition.md), [`review-A7-disposition.md`](review-A7-disposition.md) and [`review-A8-disposition.md`](review-A8-disposition.md))

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
| F107 | MED | QDIS gate followed the bias rail through 47 Ω: up to 20.9 V against the +18 V recommended / +22 V abs, with the < 10 % load region extrapolated (A8-N02) | RQDG 1.5 k with RQDPD 10 k: V_GS 13.4–18.1 V |
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
| Link peak with the FW-06 response (15.6 µs to HS-off + ASC request, then 7.5 µs all-off at 481 A) | 906 V | 1000 V can U_N at 85 °C | ✅ PASS | 91% of limit · round 7: the whole chain, not a written 20 µs — the motor's stored magnetic energy adds a motor-dependent step on the non-ASC path (below n_x); HIL event-to-ASC measurement + dyno contactor opening under full regen are the gates |

### Regeneration, battery path lost — 4XX bus

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Link charging at 150 kW regen (C_min 723 µF) | 0.42 V/µs; 500→530 V trip in 74 µs | - | ℹ️ | a 100 µs response would end at 568 V and a once-per-PWM-period sample at 5 kHz (200 µs) at 603 V — why FW-06 is 20 µs on a free-running V_DC slot |
| Link peak with the FW-06 response (15.6 µs to HS-off + ASC request, then 7.5 µs all-off at 566 A) | 542 V | 600 V can U_N at 85 °C | ✅ PASS | 90% of limit · round 7: the whole chain, not a written 20 µs — the motor's stored magnetic energy adds a motor-dependent step on the non-ASC path (below n_x); HIL event-to-ASC measurement + dyno contactor opening under full regen are the gates |

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
| QDIS stuck ON with the battery connected | 384 W continuous (96 W/resistor) | not survivable by 10 W parts | 🟡 WARN | F23: bounded, not survived — firmware fires QDIS only with contactors reported OPEN + auto-timeout; a pre-existing FET short is caught at the next precharge (link plateaus ≈5 % low, abnormal τ); the fail-open flameproof wirewound class opens the string. Never demonstrated on a live battery |

### Discharge — 4XX values

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Passive 500→60 V, worst (R+5 %, C+10 %) | 88.5 s | 120 s service rule | ✅ PASS | 74% of limit · nominal 76.6 s (12 × 15 k, 2 strings) |
| Bleeder V on the worst-tolerance resistor @500 V | 90.5 V | 200 V working (plain 2512) | ✅ PASS | 45% of limit · review A.6 F24: 5 × 27 k put 184 V (92 %) on it at 850 V |
| Bleeder W/resistor @500 V (R−5 %) | 0.49 W | 2 W | ✅ PASS | 24% of limit |
| Active + passive 500→60 V, worst corner | 1.7 s | 2 s crash target | ✅ PASS | 85% of limit · nominal 1.47 s; paths combined ONCE + 2.5 ms bias delay (F25: S5 counted the bleeder twice) |
| Energy per 10 W wirewound (C+10 %) | 27.6 J | 100 J single-pulse | ✅ PASS | 28% of limit · peak 75 W/resistor decaying τ = 0.78 s; firmware ≤ 3 discharges/5 min (thermal recovery) |
| V per wirewound | 125 V | ≥350 V axial class | ✅ PASS | 36% of limit |
| QDIS stuck ON with the battery connected | 284 W continuous (71 W/resistor) | not survivable by 10 W parts | 🟡 WARN | F23: bounded, not survived — firmware fires QDIS only with contactors reported OPEN + auto-timeout; a pre-existing FET short is caught at the next precharge (link plateaus ≈5 % low, abnormal τ); the fail-open flameproof wirewound class opens the string. Never demonstrated on a live battery |

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
| Gate-power demand per bank, SiC @10 kHz | 1.32 W | 2.31 W worst-part capacity (typ 3.56 W) at 253 kHz | ✅ PASS | 57% of limit · Qg·ΔV·f + ICC2 + 5.1 k bleeder per domain; F27: IGBT uses the full ±15 V Qg (no scaling); IGBT SKUs fit RT 8.2 k (~308 kHz) |
| Gate-power demand per bank, SiC @20 kHz option | 1.99 W | 2.31 W worst-part capacity (typ 3.56 W) at 253 kHz | 🟡 WARN | 86% of limit · Qg·ΔV·f + ICC2 + 5.1 k bleeder per domain; F27: IGBT uses the full ±15 V Qg (no scaling); IGBT SKUs fit RT 8.2 k (~308 kHz) |
| Gate-power demand per bank, IGBT @5 kHz (full 4.36 µC, RT 8.2 k) | 1.99 W | 2.81 W worst-part capacity (typ 4.34 W) at 308 kHz | ✅ PASS | 71% of limit · Qg·ΔV·f + ICC2 + 5.1 k bleeder per domain; F27: IGBT uses the full ±15 V Qg (no scaling); IGBT SKUs fit RT 8.2 k (~308 kHz) |
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
| Slow edges at LVC inputs (Δt/ΔV 5–10 ns/V) | RC nodes and FS0B via USCH, FLT diode-OR and FS1B strap via USCH2 (no limit); RDY ≈0.2 µs/V remains | - | ℹ️ | round 7 RR01/RR02: the two RC nodes were 14,000–63,500 ns/V — buffered. The remaining open-drain release edges only return a latch or AND input to its idle level with no output change (PRE release with CLR high holds; RDY releases while MCU_GATE_EN is low per §9 sequencing and FW-14) |

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
| Drain worst case (clamped load dump) | 61.2 V | 80 V BUK7Y14-80E | ✅ PASS | 77% of limit · F38 — replaces SMBJ85A (94.4 V min breakdown, forward path in OFF) |

### Safety A.8

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| FS1B load at its V_OL point (strap + FAULT_OUT) | 0.84 mA | 2 mA (V_OL ≤ 0.4 V; current limit ≥ 4 mA) | ✅ PASS | 42% of limit · A6-R01: the 1 k pulls took 5.45 mA — a 4 mA-limit part sat at 1.33 V, ASC_SET_N at 1.67 V (> VIL); the old row compared with the 22 mA maximum and divided by 1000 again |
| ASC latch preset low level (FS1B at V_OL, 1k into 10k) vs USCH2 V_T− | 0.84 V | 1.07 V V_T− minimum at V5A 4.9 V | ✅ PASS | 78% of limit · F40/A6-R01; round 8 buffers the preset (R7-02), so the Schmitt threshold, not the LVC VIL, is the limit |
| FLT diode-OR low level (V_OL + BAT46, cold) vs USCH2 V_T− | 0.75 V | 1.07 V V_T− minimum | ✅ PASS | 70% of limit · round 8: DFLT1/2 Schottky — a 1N4148 (≈0.7 V cold) would put the node at the threshold |
| FS0B load at its V_OL point (5.1 k into the USCH input) | 0.93 mA | 2 mA (V_OL ≤ 0.4 V) | ✅ PASS | 47% of limit · pin ≤ 0.4 V: under the SBC's own 0.7 V read-back threshold and the buffer's 1.0 V V_T− minimum |
| FAULT_OUT asserted level at the VCU (10 k to 5 V) | 1.11 V | 1.5 V (5 V CMOS V_IL) | ✅ PASS | 74% of limit · sink-only through DFO: the VCU must pull up (firmware-contract §9) |
| FAULT_OUT shorted to KL30 (FS1B released): ASC_SET_N clamp vs USCH2 V_I abs max | 5.96 / 6.12 / 6.33 V at 16 / 24 / 35 V (125 °C) | 6.5 V abs max (74LVC3G17) | ✅ PASS | 97% of limit · to ground: blocked by DFO. Into V5A ≤ 0.14 mA live / 0.63 mA with V5A off (disabled LDO2 discharges it through 20–60 Ω: ≤ 40 mV). RFS1/RFS4 14.3 mA for the ≤ 0.4 s pulse, 8.9 mA at a jump start (short-time overload of the 0603s — accepted for a shorted wire). A7-N04: the round-7 BAT46 into V5A back-fed a sleeping rail with ≈8 mA. Vishay alt BZT52B5V6 (+6·10⁻⁴/K): ≤ 6.43 V |
| ASC break-before-make: HS off before LS on | LS starts ≥ 3.94 µs after the latch sets | HS off by 2.71 µs (0.21 µs to EN + 2.5 µs IGBT dead time) | ✅ PASS | 69% of limit · RR05, FS1B path shown (FS0B → USCH → ANDs → EN); the MCU path is faster (eFlexPWM fault on the high-side outputs → IN+ low, tpHL ≤ 0.13 µs), and its low sides come on by PWM after the dead time. EN stays high on the MCU path so LS DESAT keeps priority (DS §8.12). SiC dead time is 1.0 µs — more margin |
| ASC entry, latch set → LS gates on (worst) | 7.52 µs | counted in the FW-06 budget (§2b) | ℹ️ | release ≤ 0.75 µs (TLP152 tpHL 0.19 µs + DASCR discharge + tASC_f 0.48 µs); exit is MCU-sequenced (FW-06a) |
| Latched driver FLT masks ASC on every path (UASCG) before DRV_EN drops | ASC_CMD low ≤ 11 ns, LS ASC pins released ≤ 0.76 µs | DRV_EN drop ≥ 22 µs after FLT | ✅ PASS | 3% of limit · round 8 R7-01/A7-N01: the faulted NSI6611 holds its own gate off through IN-low and EN-low with ASC high (DS Fig. 8.11); the gate removes ASC from the HEALTHY low sides and the eFlexPWM fault forces IN low, so the bridge reaches SPO (FS1B-ASC included). The MCU re-enters ASC only through §4c after the FW-15 reset. Wiring locked in erc-audit |
| TLP152 LED current — ASC opto (RASCL, from UASCG) | 10.3 mA cold · 10.67 mA hot · 14.99 mA max | guaranteed: ≥ 1.25 × I_FLH 7.5 mA, ≤ 20 mA abs · recommended 10–15 mA | ✅ PASS | 74LVC1G08-Q100: guaranteed margins 1.37× over I_FLH and 1.33× under the abs max; the 10–15 mA window closes only with the ±28 % V_F tempco band (typical-derived — T7-04 bench). Cold = −40 °C V_F 1.95 V with R_out ≤ 21.9 Ω (V_OH ≥ 3.8 V at −32 mA, −40…85 °C); hot = 100 °C V_F 1.7 V with R_out ≤ 34.4 Ω (125 °C); max at V5A 5.1 V, V_F 1.228 V, R_out 0. R7-03/A7-N03: 470 R from the MCU pin gave 6.4–6.8 mA; cross-check R8X-04: 270 R mixed temperatures and dipped to 9.9 mA cold |
| TLP152 LED current — discharge opto (RQDL, from USCH2 ch3) | 10.3 mA cold · 10.67 mA hot · 14.99 mA max | guaranteed: ≥ 1.25 × I_FLH 7.5 mA, ≤ 20 mA abs · recommended 10–15 mA | ✅ PASS | 74LVC3G17-Q100: guaranteed margins 1.37× over I_FLH and 1.33× under the abs max; the 10–15 mA window closes only with the ±28 % V_F tempco band (typical-derived — T7-04 bench). Cold = −40 °C V_F 1.95 V with R_out ≤ 21.9 Ω (V_OH ≥ 3.8 V at −32 mA, −40…85 °C); hot = 100 °C V_F 1.7 V with R_out ≤ 34.4 Ω (125 °C); max at V5A 5.1 V, V_F 1.228 V, R_out 0. R7-03/A7-N03: 470 R from the MCU pin gave 6.4–6.8 mA; cross-check R8X-04: 270 R mixed temperatures and dipped to 9.9 mA cold |

### Safety A.9

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| RFS4 with FAULT_OUT shorted to KL30 and FS1B asserted (22 mA limit end) | 0.234 / 0.484 / 0.651 W at 16 / 24 / 35 V | 0.27 W continuous at 85 °C (ESR03, 0.33 W at 70 °C); ≈1.3 W for 5 s overload | ✅ PASS | 86% of limit · A8-03: 0.23 W was only the 16 V case. Pulses ≤ 0.65 W last ≤ 0.1 s (FS1B_TDUR) or the ≤ 0.3 s boot hold — ≤ 0.2 J against the 5 s overload rating; the 16 V row is the continuous case (no release). BACKUP_SAFETY_PATH_FS1B = 0 stops RSTB loops. A 0.1 W 0603 is 0.08 W at 85 °C and fails the continuous case |
| RFS4 with FS1B held a whole key-on (18 V/60 min at 65 °C · 24 V/60 s at 25 °C) | 0.3 W · 0.48 W (element ≈150 °C) | 0.33 W continuous at ≤ 70 °C (ESR03); 155 °C element | 🟡 WARN | R9X-14: 18 V fits. The 24 V jump start runs the resistor at 1.47× its nameplate for 60 s — under its 155 °C element limit at 25 °C but outside the rating, in a triple condition (FAULT_OUT shorted to KL30, a high-limit FS1B part, a jump start). Drift or an open only disconnects FAULT_OUT from an already-shorted wire, and the FS1B preset keeps working through the strap. Accepted; bench item |
| FW-16 self-test residual energy, 8XX bank (read < 3 V, or QDIS 2 τ from < 60 V) | ≤ 26 mJ (read) · ≤ 15 mJ (QDIS 2 τ = 1.4 s) | 0.1 J design limit | ✅ PASS | 26% of limit · A8-N03: "< 60 V" allowed 0.62 J at C_max 355.3 µF. R9X-07: the first "< 12 V read" rule assumed a 1 V error — it can be 9 V. n_ss from E_LL,pk(n_ss) ≤ 12 V. Fixture-qualified, not a destructive test |
| FW-16 self-test residual energy, 4XX bank (read < 3 V, or QDIS 2 τ from < 60 V) | ≤ 64 mJ (read) · ≤ 39 mJ (QDIS 2 τ = 1.63 s) | 0.1 J design limit | ✅ PASS | 64% of limit · A8-N03: "< 60 V" allowed 1.54 J at C_max 883.3 µF. R9X-07: the first "< 12 V read" rule assumed a 1 V error — it can be 9 V. n_ss from E_LL,pk(n_ss) ≤ 12 V. Fixture-qualified, not a destructive test |

### LV A.9

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| LV feed switch V_GS from NRC: KL30 6 / 9 / 16 / 39 V (clamped) | −3.7 / −5.8 / −10.5 / −15.6 V | ±20 V V_GS max; ≥ 4.5 V for the 35 mΩ point | ✅ PASS | 78% of limit · at the 9 V crank floor the gate sits 5.8 V below the source (≤ 35 mΩ; 0.6 A → ≤ 13 mW); even at 6 V (3.7 V) it stays past V_GS(th) max 3 V. R9X-10: taken from NRC, not KL30 |
| LV feed switch held off by a hot 2N7002 (leakage × 10 k gate-source) | 0.5 V at 50 µA | 1.0 V V_GS(th) min | ✅ PASS | 50% of limit · R9X-10: with the first 100 k the same leakage made 5 V — the switch could half-close in a hot parked car |
| LV feed switch inrush into the power board at each wake (drain-gate 100 nF slew) | 0.68 A at 12 V · 1.06 A at 16 V | 3 A (polyfuses, harness, VBATC dip) | ✅ PASS | 35% of limit · R9X-04: slew = gate current / 100 nF ≈ 15 V/ms at 12 V; without it the gate-drain charge alone let 9–31 A through (2.5–4.4 mJ) and dipped VBATC ~2 V at every wake |
| Parking drain, whole inverter (KL30 present, FS26 in LPOFF) | ≤ 43 µA at 25 °C · ≤ 126 µA at 85 °C | 0.1 mA at 25 °C (OEM sleep budgets ≤ 0.1–1 mA per ECU) | ✅ PASS | 43% of limit · N17 + R9X-03: before, the power board's LV side drew ≥ 47 mA from the QA01C-18 no-load inputs alone (≈150 mA in all) and ULDOEX kept the exciter at ≈0.9 mA; both now follow V5A. S1 start-up already starts VDD at 0 V, so a switched feed costs no start time |

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
| V15 behind ULDO15 @24 V jump start | 15.0 V | 16.5 V QA01C normal-max | ✅ PASS | 91% of limit · F51 — boost pass-through clamped; LDO input 23.5 V << 40 V rating |
| ULDO15 input at clamped load dump | ≈33 V | 40 V NCV4276C operating max | ✅ PASS | 83% of limit · F51 — TPSMC24CA clamp level on the 12 V node |

### LV A.7

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| CB15O1/2 (V15B) at clamped load dump | ≈33 V | 50 V MLCC rating | ✅ PASS | 66% of limit · round 7 RR10 — V15B follows V12L−Vf in pass-through; the 25 V parts were overstressed |

### LV A.4

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| ULDO15 dissipation @24 V sustained | 2.8 W | TSD-protected (survival case, not an operating mode) | ✅ PASS | 50% of limit · jump start is stationary service — brief V15 brown-out via TSD is acceptable; passive bleeder unaffected |
| VCORE COUT effective (2×22 µF @1.5 V) | 32 µF | 20–100 µF eff (Table 106) | ✅ PASS | 63% of limit · F52 — was 10 µF nominal; inductor now 2.2 µH per CORE_LSEL_OTP |
| VPRE COUT effective (2×22 µF @6 V) | 26 µF | ≥22 µF test condition | ✅ PASS | 83% of limit · F52 |
| VPRE input effective (22 µF @14 V) | 11.4 µF | ≥10 µF eff | ✅ PASS | 87% of limit · F52 — CLVC2 4.7 µF was under the input spec |
| VREF5 rail effective (whole rail: 3.3 µF nom) | 2.2 µF | 1.1–3.3 µF eff window | ✅ PASS | 68% of limit · reviewer correction accepted: count CMA1/CMA2, judge the rail not one part |
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
| QDIS gate V_GS (QA01C-18 envelope through the 1.5 k/10 k divider) vs HCM75S12T4K3 +22 V abs | 13.4–18.1 V (rail 16.9–20.9 V) | +22 V abs max (+18 V recommended) | ✅ PASS | 82% of limit · round 9 A8-N02: 47 Ω passed the rail straight to the gate — up to 20.9 V at this 2–4 % load, above the +18 V recommended level with the < 10 % load region extrapolated. The divider keeps the top at the recommended level and 3.9 V under the abs max; the low end fully enhances a 0.45 A discharge. BENCH: V18Q, QDVO and V_GS at start-up, no load and ON |

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
| V15 boost setpoint (110k/9.53k) | 15.41 V | 13.5–16.5 V module window | ✅ PASS | 28% of limit · QA01C/ISO5V input range |
| Boost switch current @9 V | 0.72 A avg | 5.25 A limit | ✅ PASS | 14% of limit |
| NCV4276 5 V load (6 driver VCC1 + optos) | 50 mA | 400 mA | ✅ PASS | 13% of limit |
| FS26 VMONEXT divider (52.3k/10k @5 V) | 0.794 V | 0.8 V fixed reference ±window | ✅ PASS | 6% of limit · F34 — old 10k/18.7k fed 3.26 V = permanent OV; OTP window set around 100 % |
| S32K39 core topology | FS26 VCORE→V15S 1.5 V → QBAL ballast → V11 1.14 V | - | ✅ PASS | F36 — per DS Table 11; direct VCORE→V11 is not a supported topology |
| NCV4276 dissipation @12 V | 0.35 W | ~1.5 W DPAK on copper | ✅ PASS | 23% of limit |
| Polyfuse hold (worst chain @9 V) | 1.19 A | 3 A hold | ✅ PASS | 40% of limit |
| Load-dump path | TVS 24 V standoff, clamp ~39 V | - | ✅ PASS | F25 — all 12 V-node MLCCs raised to 50 V rating; TPS55340 Vin abs 45 V rides the clamped pulse |

### Sensing

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| VDC divider @850 V (6.2 k bottom) | 1.865 V | 2 V AMC FS (= 911 V readable) | ✅ PASS | 93% of limit · OV witness keeps headroom above V_bus,max — F27 |
| VDC chain error, WORST CASE uncalibrated | ±2.08 % (±18 V at the 880 V OV trip) | OV trip below the 1000 V can rating | ✅ PASS | 90% of limit · top 1 % correlated + bottom 0.1 % + AMC1311B 0.2 %+offset + receiver 0.2 % + VREF5 0.5 %; RSS would claim ±0.74 % |
| VDC chain error after EOL gain/offset calibration | ≈±0.3 % (residual drift/nonlinearity) | 5 % cross-check window | ✅ PASS | calibrated values feed protection only after the stored record passes CRC + range checks (F42) |
| Shared receiver offset VOFS monitored | UVOF output → MCU ADC (PTB1) | ±5 % of 0.5 V | ✅ PASS | F11: a failed UVOF would shift BOTH channels by up to 0.5 V (≈228 V) and pass the 5 % cross-check — now read directly; BMS pack voltage is the third witness when contactors are closed |
| Divider dissipation @850 V | 255.6 mW total | 6× 1206 (250 mW ea) | ✅ PASS | 17% of limit · 141.7 V per 200 V-rated 1206 — 71 % |
| Hall output at 480 A pk | 3.57 V | 0.3–4.7 V buffer swing | ✅ PASS | 49% of limit |
| Hall ratiometric ref vs ADC ref | V5S(V5A) vs VREF5 | - | 🟡 WARN | two 5 V sources — ~±1–2 % gain drift between them; calibrate at EOL or move VREFH to V5A (GEN3 ships the same topology) |
| HVIL signatures (drive hi/lo/open) | 3.0 / 2.0 / 2.5 V | - | ✅ PASS | distinct at ±5 % R tolerance (worst separation 0.38 V) |
| Resolver monitor dividers @4 V pk | 2.83 / 3.31 V | 5 V SDADC input | ✅ PASS | 66% of limit |
| Resolver drive @9 V KL30 | ≈6.5 V pp available vs 8 V pp target | - | 🟡 WARN | ALM2402 swing at cold-crank INCLUDING ULDOEX dropout (~0.3 V @ ~150 mA, A.4.3) — angle still tracks (amplitude-invariant demod); GEN3-equivalent behavior |

### Safety

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| FS0B → driver EN path | 2 gate delays (~20 ns) + driver td | - | ✅ PASS | no software; erc-verified topology |
| ASC latch power | V5A + RASCP default-low | - | ✅ PASS | survives MCU reset; FS1B can SET via RFS1 |
| ASC drive path | TLP152 + QA01C + 5.1 V clamp, DCN-referenced | - | 🟡 WARN | DS 1.2 confirms ASC forces OUTH high at a GND2-referenced 0-5 V pin (F28 level fix applied); behaviour DURING VCC2-UVLO is unspecified — bench-verify that gate power (SBC-held flybacks) is sufficient for ASC hold |
| Default-OFF discipline | 11 pulldowns power + 4 card | - | ✅ PASS | erc-verified |

## Open vendor/bench inputs (every WARN above names one)

hiitio: module stray inductance Ls (SiC D3), SiC short-circuit envelope at 850 V ·
NOVOSENSE: RST/EN behaviour during DESAT soft turn-off and the I_STO distribution · TDK:
VGT12EEM saturation current and working-insulation rating · Faratronic: the 4XX 50 µF/600 V
can (ripple/ESR/life) · Murata: MGJ2 reinforced certificate · coldplate Rth (thermal test) ·
motor data (flux linkage, n_max, Ld/Lq) for the safe-state decision. Datasheet values used are
in `docs/datasheets/EXTRACTED-PARAMS.md`.

## Method

Closed-form worst-case analysis (tolerance corners: R ±5 %, precision ±1 %, C +10 %,
KL30 9–16 V, V_bus to 850 V / 500 V) — the correct tool at schematic phase; the time-domain
companion is `sim-verify.mjs`. Review A.6 added an independent cross-check: every
contested number (losses, overshoot, DESAT timing, startup, hold-up, discharge, ripple,
sensing) was recomputed in a separate script by a second reviewer; the two agree within
model assumptions. SPICE adds nothing without vendor switch models; the double-pulse,
short-circuit, thermal and EMC items are bench gates listed in `docs/firmware-contract.md`
and `docs/review-A6-disposition.md`.
