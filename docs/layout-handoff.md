# Layout handoff — Traction Inverter (220 kW / 800 V), rev A.21

Round 22 (2026-09-26). This is the layout engineer's entry point; every number and rule is sourced with a trailing
`(source: file:line)` note into the repository documents that remain authoritative (`docs/dfm.md` §4, `docs/design-basis.md`,
`docs/interface-requirements.md`, `docs/qualification-plan.md`). It is re-checked at every review round; the footprint library
and its proof are generated (`calculations/kicad-fp-gen.mjs`, `kicad-sch-verify.mjs`), so §1a cannot drift behind the sheets.

For the PCB layout engineer receiving the KiCad project. Every number/rule below is sourced from the repository, with a trailing `(source: file:line)` note. Where a decision is still open, this document says so and gives the provisional basis if the sources state one — it does not pick a value for you.

---

## 1. What you are receiving

Four physical assemblies, one 40-way harness plus three smaller interboard links (source: design-basis.md:5, 108-120):

| # | Assembly | Schematic source | PCB layout needed? |
|---|---|---|---|
| Sheet 1 | **Power board (HV)** | `boards/power.tsx` | **Yes** — DC entry, DC-link sense/Y-caps, 3× EconoDUAL-3 module gate drive, dual gate-power flybacks, isolated V_DC sensing ×2, LV protection, ASC buffer, 40-way harness header (source: boards/power.tsx:1-4) |
| Sheet 2 | **Cap-bank busbar assembly** | `boards/capbank.tsx` | **No — not a PCB.** "NOT an FR4 PCB: at 340 Arms/~200 A ripple the interconnect is two copper plates with an insulation film, and the 16 film cans solder/bolt to it. This sheet is the electrical drawing handed to the busbar vendor" (source: boards/capbank.tsx:1-3; design-basis.md:116-118). FR4 cannot carry the 340 A-class bus current (source: design-basis.md:118). |
| Sheet 3 | **Discharge board** | `boards/discharge.tsx` | **Yes** — small PCB bolted directly across the cap-bank/busbar DC studs (source: boards/discharge.tsx:1-5; design-basis.md:128-130) |
| Sheet 4 | **Control card (LV)** | `boards/control-card.tsx` | **Yes** — S32K396 MCU, FS26 SBC, resolver AFE, CAN, motor-temp inputs, vehicle harness |

**Three boards to lay out (power, discharge, card); one busbar drawing (cap bank) — not a PCB.**

Interboard links, each a named connector/stud/tab drawn on both mating sheets and asserted pin-by-pin by `erc-audit.mjs` (source: design-basis.md:108-120):

| Link | Interface | Carries |
|---|---|---|
| L1 power⇄card | `JIC`⇄`JICC`, 40-way harness | 6× gate PWM, FLT/RDY, EN/ASC, V_DC + NTC feedbacks, 15 V bias, grounds |
| L2 power⇄discharge | `JDIS`⇄`JCTL`, 4-way | V15 / QDIS_CMD / 2×GND |
| L3 busbar (cap bank) | `JCBE[PN]`⇄HV entry · `JCB[UVW][PN]`⇄module DC terminals · `JCBD[PN]`⇄`JDCP/JDCN` | the DC bus itself — bolted, torque-controlled joints |
| L4 card⇄LEM×3 | `JLEM` 10-way | 5 V per sensor + 3 hall outputs, 100 kΩ pull-downs |
| L5 HVIL | `JHVIL` through the HV connector loop to the card | interlock continuity |

**Caveat on board size**: the `.tsx` files' `<board width/height>` is explicitly **not** a stated PCB outline — "PCB coordinates are a coarse grid only so builds succeed" (source: packages/cells.tsx:2-3). Do not take it as the intended outline — full detail in §7.4.

### 1a. The KiCad project and its footprint library (what makes "Update PCB from Schematic" run)

| Item | Where | What it is |
|---|---|---|
| KiCad 9/10 project | `kicad/traction/traction.kicad_pro` (zip: `kicad/Traction-Inverter-KiCad-modern.zip`) | root sheet + four board sheets; every sheet embeds its symbols; `traction.kicad_sym` + `sym-lib-table` |
| Per-board projects | `kicad/traction/traction-power.kicad_pro`, `-capbank`, `-disch`, `-card` | the same project file beside each board sheet: open one to lay out that board as its own PCB (each board is a separate PCB; nets cross boards only through the named connector, stud or tab) |
| Footprint library | `kicad/traction/traction.pretty/` + `fp-lib-table` (and the same in `kicad5/traction/`, `kicad5/traction-native/` and their zips) | ONE library, nickname `traction`, at `${KIPRJMOD}/traction.pretty` — nothing from your own library tables is needed |
| Footprint field | every on-board symbol: `traction:<name>` | the generator refuses to write a sheet with an unbound part (`calculations/footprints.mjs`, `kicad5-gen.mjs`) |
| Standard patterns (45 files) | `traction.pretty/SOURCES.json` | copied VERBATIM from the KiCad 10.0.6 libraries, names kept, sha256 recorded (KiCad libraries licence CC-BY-SA 4.0 with the KiCad exception); the picks that the datasheets decided are in MANIFEST.md §B (TPS55340 RTE0016C EP 1.68 × 1.68, VY1 disc D 16.0, VY2 disc D 12.5, the 5-lead DPAK/D2PAK lands, XAL4040, VSSOP-8 2.3 × 2 mm for the Nexperia DC package, SOT-223 with the tab as pad 4, PMEG4010EH on SOD-123F) |
| Datasheet-drawn patterns (18 files, 17 referenced) | `traction.pretty/MANIFEST.md` | one row per pattern: part, datasheet page/figure, pitch/pad/hole sizes, pad-number ↔ pin-number map, and every doubt — the EconoDUAL 3 module (aux pins 1/2 and 5–9 at HIITIO's positions on rows 58.4 mm apart; the four M6 power terminals as Ø6.4 clearance holes with Ø12 pads where the laminated busbar bolts — the PCB cut-out around them is your call), UCC14141-Q1 DWN-36, AMC1311B DWV (rows 10.9 mm apart: TI's 9.1 mm creepage, which is why KiCad's SOIC-8W was not used), VOW3120 SMD-8 wide (10 mm pad gap), TDK VGT12EEM (SMD, 4 + 4 gull-wings), Samtec IPL1 1×2 / 1×4 / 2×20 (Samtec numbers row by row: 1–20 on one row, 21–40 on the other, 21 opposite 1 — the same as the harness map), TE 770669-1 AMPSEAL (23 cavities on 3 rows, 2 NPTH pegs, the PCB edge on Fab), the Faratronic can (4 leads: two per electrode at 37.5 × 20.3 mm), the Bel 2410 fuse, TDK ACT45B (windings 1–4 and 2–3), the TT/Yageo SQP10 at **55.88 mm** lead pitch (a 48–50 mm body; the sheet helper's 38 mm is a placeholder), onsemi SMA flat-lead, the Kyocera 2-pad crystal, the NXP FS26 HLQFP48 (its own EP/mask — the TI land differs), the TI ALM2402 PWP0014H (mask-defined 1.82 × 2.86 pad opening), and the LEM HC5FW pattern for the sensor carrier |
| Off-board parts | `USNSU/V/W` (LEM HC5FW-900-S) | no footprint, `on_board no`: the sensors sit on their carriers at the phase busbars (§3.3, §4.6) and reach the card through `JLEM`; a drawn `HC5FW-900-S` pattern is in the library for the carrier |
| Proof | `node calculations/kicad-sch-verify.mjs` (runs kicad-cli) | ERC `footprint_link_issues` = 0 except the three off-board parts; every symbol pin NUMBER has a pad of that number in its footprint; the netlists equal the tscircuit sources net by net; the MCU's 130 connected balls equal the manifest |

Pin numbering. Round 22 audited every multi-pin symbol's pin numbers against its datasheet (31 part groups: all match; the audit
is in `docs/review-A21-disposition.md`). Three things the audit and the pad-per-pin proof changed at the source: the five
NCV4276C regulators are 5-lead DPAK/D2PAK (INH on pin 2, tab = pin 3) and are bound to the 5-lead lands; the ALM2402 PWP pad
(pin 15, AGND) and the TPS55340 PowerPAD (pin 17, DGND) are now symbol pins, so their pads carry a net; the Kyocera CX3225GA
crystal is a two-terminal part on its own 2-pad land. `QLVS` (SOT-223, Diodes prints no pin numbers): with the tab to the right
the land's top lead is pin 1 = G, the middle pin 2 = D = tab, the bottom pin 3 = S — as the symbol numbers them.

Pads with no symbol pin are intentional and carry no net: the BAT64-04 series junction (pad 3), the NCV4276C NC pin 4, the
UCC12051 SYNC_OK output, ALM2402 NC pins 7/8, unused FS26 pins, the VOW3120 NC leads 1/4/6, the UCC14141 PG (3) and RLIM (32)
left open by design, the VGT12EEM unwound leads 6/7, JVEH cavities 22/23, the SWD pin 7 and the MCU's 159 open balls
(`docs/mcu-pin-manifest.md`). The verifier prints the list per board.

The tscircuit sources' footprint helpers are placeholder geometry (`packages/cells.tsx`: "PCB coordinates are a coarse grid"):
`AxialFP(38)`, `FilmCanFP` (2 holes for a 4-lead can), `TO247_4L` (uniform 2.54 mm where the HCM75S12T4K3 has 5.08 / 2.54 /
2.54), `XfmrEEFP` (THT for an SMD transformer) and `EconoDual3FP` (arbitrary aux positions) — the pin NUMBERS are right, the
KiCad lands are the geometry. **Confirm with the vendor before fabrication** (MANIFEST.md §E): the EconoDUAL 3 aux-pin PCB hole
(HIITIO gives none; 1.6 mm drawn, the tolerance stack suggests ≥ 2.0–2.4 mm for guaranteed insertion), the Samtec IPL1
single-row pin-1 end and PCB hole, the 4XX can's body size (assumed equal to the M206), the LEM HC5FW ASIC pad size
(scaled from the 1:1 drilling view), and the ST BAT46ZFILM package (SOD-123 per the BOM; no sheet archived).

---

## 2. Layer stacks and copper

**These layer counts come from the cost roll-up (a planning/costing document) and the README roadmap line — not from a stack-up drawing.** No stack-up (dielectric, plane assignment, impedance) is defined anywhere in the mined sources.

| Board | Layers | Copper weight | Area | Source |
|---|---|---|---|---|
| Power board | **6** | **2–3 oz** | **~0.12 m²** | cost-rollup.md:13 |
| Control card | **6** | not stated | not stated | cost-rollup.md:13; README.md:682 ("card 6-layer") |
| Discharge board | **2** | not stated | not stated | cost-rollup.md:13 |
| Cap-bank busbar | N/A — not a PCB | "two copper plates + insulation film" | not stated | design-basis.md:117; capbank.tsx:2 |

No copper weight is stated for the card or discharge board individually — only "power 6L 2–3 oz" is qualified (source: cost-rollup.md:13). Flag this gap to the stack-up owner.

### Copper-area and thermal-via rules (dfm.md §4)

| Location | Rule | Source |
|---|---|---|
| U5LB/U5LC (NCV4276C 5-lead DPAK, TO-252-5, V_DC-sense bias LDOs, ≈0.76 W each) | **≥1.2 in² of 2 oz copper under each tab** (DS 58.5 K/W on 1.14 in² → Tj≈129 °C at 85 °C) | dfm.md:83-84 |
| ULDO15 (D2PAK-5, NCV4276CDSADJR4G, since round 17) | same **≥1.2 in² of 2 oz copper under its tab** | dfm.md:141-142 |
| UCC14141-Q1 (ASC/discharge bias, reinforced) | keep primary/secondary copper split under the package, **≥10 mm barrier**, matching VOW3120's ≥10 mm creepage on the same net pair; TI recommends **4 layers/2 oz outer copper** for its thermal path (RθJA 52.3 K/W at 1 W) | dfm.md:99-104 |
| R5LB/R5LC (LDO ballast, 2512, 0.45 W each) | **on the same 2 oz copper as the LDO, ≥5 mm from the UCC12051-Q1 and the AMC1311**; input cap C5Lx1 between ballast and LDO IN pin | dfm.md:108-109 |
| Resolver-exciter TVS (TVSEP/TVSEN) | own **≥3 cm² 2 oz copper island with a thermal-via array** to inner AGND copper, FEXP/FEXN pads on the same island (full rule in §4.11) | dfm.md:119-132 |
| UMCU 289-MAPBGA (0.8 mm pitch) | **via-in-pad or dog-bone per the NXP AN**, **7×7 thermal via array under the package** (DS thermal test board), **all 25 VSS balls to the ground plane**; 159 open balls stay unconnected | dfm.md:85-87 |
| RASCG (ESR03 2.2 k) | terminal (fillet) temperature ≤138 °C at its 0.12 W is part of the part's rating | dfm.md:93-96 |
| RFS4 (ESR18 1206, since round 17) | 0.5 W at 70 °C; fillet (terminal) temperature on the ESR18's 1 kΩ ≤ R line (ROHM ESR Rev.012 Fig. 4, knee 125 °C, zero at 155 °C): **≤ 137 °C at its 0.30 W (FS1B held), ≤ 126 °C at 0.48 W (24 V / 60 s jump start)** — measured by QP-TH-04 | dfm.md §4 (round 22) |

MCU package note: "MCU remains explicitly symbolic (**no package table in the DS — bind at layout, printed on sheet**)" (source: verification-report.md:190, F45); cells.tsx also flags "mark §VERIFY before layout" for MCU/module aux-pin numbering (source: packages/cells.tsx:7).

---

## 3. Electrical classes, clearance and creepage

### 3.1 HV working voltages

| Parameter | Value | Source |
|---|---|---|
| DC link, 8XX SKUs | 500–850 V (700 V nom) | design-basis.md:15 |
| DC link, 4XX SKUs | 250–500 V (400 V nom) | design-basis.md:15 |
| OV trip | 880 V (8XX) / 530 V (4XX) | interface-requirements.md:55 (IR-21) |
| DC-link can rating | U_N 1000 V (8XX) / 600 V (4XX) at 85 °C | interface-requirements.md:55 (IR-21) |
| Module blocking voltage | 1200 V (71% utilization at 850 V max bus) | design-basis.md:48,63 |

A pack whose full-charge voltage exceeds 850 V is "a **separate configuration** (different divider bottoms, thresholds, cans and DPT re-qualification together)" (source: interface-requirements.md:55).

### 3.2 Isolation barrier register (design-basis §6a)

"A one-minute hipot figure is not a working-voltage rating" (source: design-basis.md:210-211):

| Element | Class needed | Evidence | Layout implication |
|---|---|---|---|
| AMC1311B ×2 | reinforced, 850 VDC | V_IOWM 1.2 kVrms | — |
| UCC12050/12051-Q1 ×2 | reinforced | V_IOWM 1200 Vrms/1697 VDC | — |
| NSI6611ASC-Q1 ×6 | reinforced | SOIC-16W reinforced grade | — |
| VGT12EEM-200S1A4 ×6 | reinforced (gate domains at DC± potential) | 2.6 kVrms test | primary/secondary split ≥10 mm (§2 table) |
| VOW3120 ×2 | reinforced | V_IORM 1414 Vpk, **CPG/CLR ≥10 mm** | matches UCC14141-Q1 ≥10 mm rule |
| UCC14141-Q1 ×2 | reinforced | V_IORM 1414 Vpk | ≥10 mm split (§2 table) |
| CY1/CY2 (Y-caps to chassis) | Y1 | **Y1 500 VAC/X1 760 VAC/1500 VDC** | — |
| HC5FW ×3 | per LEM/sleeve construction | reduced-insulation variant + sleeve | see §3.3 |
| **Busbar/HV connector spacing** | per IEC 60664-1 at the agreed pollution degree | **"layout rule"** | governs §3.4 |

Source: design-basis.md:213-223.

### 3.3 LEM phase-current sensor — busbar aperture and sleeve

- HC5FW 900-S/SP1 bare-part rating: **creepage 3.6 mm, clearance 2.7 mm, CTI 550**, 2.5 kV/1 min per IEC 60664-1 (source: design-basis.md:182-185).
- On its own this is short of basic insulation at 850 V. Each phase busbar therefore carries an **insulating sleeve through the aperture: ≥1 kV DC-rated heat-shrink or powder coat, ≥10 mm past both faces** — a mechanical-drawing note, not a PCB change (source: design-basis.md:185-188).
- Release gate: LEM confirms the sleeve spec for 850 V DC working (source: design-basis.md:188; interface-requirements.md:47, IR-20).
- `JLEM` connector: 10-way, `Header(10,2)`, pins S5U/OU/G1/S5V/OV/G2/S5W/OW/SH/G3 (source: boards/control-card.tsx:681-683).

### 3.4 Creepage/clearance basis (the table QP-HV-06 will audit against)

| Item | Value | Source |
|---|---|---|
| **Provisional PD/OVC for the Road HV audit** | **PD2 / OVC II** (provisional, pending OEM altitude/OVC statement) | interface-requirements.md:94 (IR-36) |
| Required creepage at 850 V, PD2, material group II | **≈6 mm** (IEC 60664-1 Table F.5) | design-basis.md:185-186 |
| Bare HC5FW sensor (for comparison — insufficient alone) | 3.6 mm creepage / 2.7 mm clearance | design-basis.md:182-185 |
| **Marine alternative reading (PD3/OVC III, ABS, IEC 61800-5-1)** | **12.5 mm @ 800 V, 16 mm @ 1000 V** | marine/design-basis.md:48 |
| Final audit method | **QP-HV-06**: CAD clearance/creepage report on every HV-to-LV and HV-to-HV net pair, plus optical measurement at every barrier part, the HV connector and the busbar on 3 first-article boards | qualification-plan.md:1439-1459 |

"PD 3 or OVC III widens the HV spacings on the power board and the barrier footprints" (source: interface-requirements.md:94). If the PD2-sealed-enclosure argument is not accepted, **lay out to the 12.5/16 mm figures**, not the ≈6 mm one — see open decision §6.1.

### 3.5 Gate-domain isolation

- NSI6611A-Q1: reinforced, SOIC-16W, CMTI ≥150 V/ns (source: design-basis.md:227-233).
- Six independent floating gate-power secondaries via VGT12EEM, primaries paralleled (source: design-basis.md:239-241).
- VGT12EEM withstand: **2.6 kVrms/1 min NP,NF→NS; 1.3 kVrms/1 min coil→core** (source: qualification-plan.md:1349-1350) — core-potential tie in the layout decides which figure governs (open decision, §6.4).

### 3.6 LV classes

| Item | Value | Source |
|---|---|---|
| KL30 functional range | 9–16 V continuous | interface-requirements.md:14 (IR-01) |
| Jump start | 24 V/60 s; ISO 16750-2:2023 raises it to 26 V/60 s (design stays dark there) | interface-requirements.md:15 (IR-02) |
| Load dump test B | 35 V/400 ms, let-through architecture since A.16 | qualification-plan.md:1099-1148 (QP-LV-03) |

---

## 4. Placement and routing rules by block

### 4.1 Gate loops and Kelvin source

- Commutation loop target: 1 µF/1200 V film snubber across DC+/DC− at module terminals; **laminated busbar target ≤5 nH external** (source: design-basis.md:64-66).
- Loop inductance is the binding DPT constraint — "no EconoDUAL-class loop holds 1080 V at 850 V" at the DS fall time (source: verification-report.md:151, F63); WARN at 15 nH (1104 V vs 1080 V repetitive guard) (source: verification-report.md:261).
- Each of the 6 gate-driver channels references its own **Kelvin pin (NSI6611A-Q1 pin 3, GND2)** (source: packages/cells.tsx:155-156). DESAT sense point: "module aux drain pin 9 (DSH) — **kelvin sensing, no busbar drop in the trip level**" (source: verification-report.md:356).
- ASC pin: 1 k series resistor at each driver pin because "the shared ASC_DRV node (12 nF to DC-) is stiff, while each pin is referenced to its own Kelvin source — **the L_s·di/dt between them must not drive the pin's clamp cells hard**" (source: packages/cells.tsx:181-184).
- Gate-power secondary: "+15.4 V/−5.1 V about **each** Kelvin source" (source: design-basis.md:242-243).

### 4.2 DESAT

- 2×US1M 1 kV series sense diodes + 47 pF blanking cap at the driver (source: design-basis.md:227-228; packages/cells.tsx:190-196). BAT64-04 clamp: anode on DESAT, cathode on VCC2 — never a forward path from supply into the DESAT node.
- Worst-case detection timing is a bench release gate, not closed on paper — do not treat blanking/RC values as final without the SC test result (source: verification-report.md:359-360).

### 4.3 Driver bypass / rail decoupling

"driver rail decoupling — both sides of the barrier (**NSI6611 DS layout rule**): **input-side VCC1-GND1 bypass lives in each channel, not only at the LDO**" (source: packages/cells.tsx:203-204). Per channel: 100 nF (V5GD–DGND), 100 nF + 4.7 µF (VCC2–Kelvin), 4.7 µF (VCC–Kelvin), 10 µF (Kelvin–VEE). Isolation test point: "I-ISO on VCC2–Kelvin and VEE–Kelvin of all six domains" (source: qualification-plan.md:390).

### 4.4 Gate-power transformers (VGT12EEM)

- Three transformers, primaries paralleled on the switch node; one carries the aux winding (source: packages/cells.tsx:283).
- Secondary rectifier **must be on the dotted pin**: "NP1||NP2 = pins 1-2 with DOTS AT PIN 2" (source: packages/cells.tsx:297); "the secondary **RECTIFIER MUST HANG ON PIN 8 (dot)** to conduct only" (source: packages/cells.tsx:300); confirmed design-basis.md:241 (dot end, pin 8).
- Primary/secondary copper split ≥10 mm (§2 table); confirm core potential in the layout (§6.4).

### 4.5 V_DC sense dividers and AMC1311 inputs

- Two independent channels: 6×470 kΩ top + 6.2 kΩ bottom divider into AMC1311B, each with its own reinforced bias (UCC12050/12051-Q1) (source: design-basis.md:190-196; packages/cells.tsx:320-333).
- Each channel's bias LDO is `U5LB`/`U5LC` and ballast `R5LB`/`R5LC` (source: boards/power.tsx:131-140) — copper rules in §2 table (≥1.2 in² 2 oz under LDO tab, ballast on same copper ≥5 mm from UCC12051-Q1/AMC1311).
- Note (not a placement rule, but relevant): both channels' receivers share VREF5/V5A and a +0.5 V offset buffer — a shared single point of failure the layout should not obscure (source: design-basis.md:201-203).

### 4.6 LEM sensors and busbar aperture

See §3.3 in full. Placement fact: HC5FW is a "connectorless… PCB-mount THT device… on an off-board carrier at each phase busbar" (source: interface-requirements.md:47, IR-20) — the sleeve is a mechanical-drawing item, not a PCB layer (source: design-basis.md:187).

### 4.7 Discharge board

- Bolts directly across the cap-bank busbar's DC studs (source: design-basis.md:128-130).
- Studs `JDCP`/`JDCN`: `StudFP(6.5, 12)` — **6.5 mm hole/12 mm OD (M6 class)** (source: boards/discharge.tsx:18-19), mating `JCBDP`/`JCBDN` on the cap-bank sheet (source: boards/capbank.tsx:26-27).
- 4-way bias/command header `JCTL` (mates `JDIS` on the power board): **pin order V15, GND, CMD, GND — CMD is never next to V15**, "a pin short would push 15 V into the card's USCH2 output and its V5A rail" (source: boards/power.tsx:39-43; boards/discharge.tsx:20-24). QP-FAI-03 pass criterion: "The discharge header reads V15-GND-CMD-GND on both boards" (source: qualification-plan.md:2803).
- Passive bleeder: **12× 22 kΩ 2512 2 W, 6-series×2-parallel = 66 kΩ** (source: boards/discharge.tsx:26-36; design-basis.md:135-141) — see README discrepancy, §7.1.
- Active path: 4× 470 Ω (8XX) / 220 Ω (4XX) **SQP10 10 W cement resistors** (TT/Welwyn SQP10-xxxRJB15, Yageo SQP10AJB alternate) on the drawn land `traction:Yageo_SQP10_P38` — the sheet's helper puts the holes at 38 mm pitch; **read MANIFEST.md for the lead pitch the SQP10 body allows** (source: boards/discharge.tsx:60-63; traction.pretty/MANIFEST.md); switch `QDIS` (HCM75S12T4K3, TO-247-4, leads D-S-KS-G as numbered) on `traction:TO-247-4_Vertical` (source: boards/discharge.tsx:58).
- Title-block rule, printed on the sheet and enforced by the generator (build fails if it cannot be placed): **"NEVER energize without this board fitted"** (source: design-basis.md:133,141; verification-report.md:202, F62).

### 4.8 Cap-bank studs and can pitch (busbar drawing, not a PCB — hand this geometry to the busbar vendor)

- Entry lugs (HV cable side): `StudFP(8.5, 16)` — **8.5 mm hole/16 mm OD (M8 class)** (source: boards/capbank.tsx:16-19).
- Module DC-tab pairs (×3) and discharge studs: `StudFP(6.5, 12)` — **6.5 mm hole/12 mm OD (M6 class)** (source: boards/capbank.tsx:17,20-27).
- 16 film cans on `FilmCanFP()` — **37.5 mm pin pitch**, 1.5 mm hole/2.8 mm pad (source: packages/cells.tsx:37-38; boards/capbank.tsx:29-33).
- EconoDUAL-3 real terminal geometry (ground truth for placing the module against the busbar): power terminals (pins 3/4/10/11: DC−/DC+/AC/AC) are **M6/M8 screw lands** on a 152×62 mm courtyard, 4 power holes at 6.5 mm/12 mm OD, 7 aux pins at 1.2 mm/2.2 mm OD — "the REAL HCS600FH120D3C1 numbering (datasheet rev X.0.1 Fig 1/2)" (source: packages/cells.tsx:73-85).

### 4.9 LV entry / fuse / TVS (KL30 entry chain)

dfm.md:135-142 (quoted): "from JVEH: FLVC (Bel 0680L5000-05, 2410 ceramic slow-blow fuse; **it carries the whole inverter, keep it off the LDO/buck heat**) → FCO, where DTVSC (TPSMC33A-VR) and DTVSC2 (TPSMC18A-VR) sit in series to ground **ahead of DREVC — a short, wide loop into the DGND entry** (pulse 1 drives 12.5 A through it) → DREVC → NRC. **CLVC3** … sits beside DREVC and CLVC1 with **the same short ground return**, then LFC/CLVC2 to the FS26" (source: dfm.md:135-142). ULDO15's copper rule is in the §2 table.

### 4.10 FS26 and its caps

| Rule | Source |
|---|---|
| **CIN_TRK ≥0.5 µF effective, placed at the FS26 TRKIN pin (VPRE bank)** — DS placement rule | dfm.md:82 |
| **CV25 (220 nF) within 2 mm of ball J7** on the MCU side | dfm.md:105-106 |
| **CBAL (1 nF) at the QBAL gate, next to F1** | dfm.md:105-106 |
| VDIG: 1 µF decoupler mandatory; VBOS: decoupling mandatory | boards/control-card.tsx:179,193 |
| H5 (V15) fed from the V15S plane, **never from the power board's 15 V bias rail of the same name** — keep the labels V15S and V15 distinct | dfm.md:106-107 |

### 4.11 MCU decoupling, crystal, SWD

- MCU decoupling: **12× 100 nF, 0603**, one per supply-domain tap across V5A(×6)/V3B(×2)/V11(×4); VREF5: 1 µF + 100 nF, 0603 (source: boards/control-card.tsx:333-338). DFM decision: "MCU decoupling 0603 (fewer feeders, JLC-basic)" (source: dfm.md:42).
- Crystal: **12 pF × 2, 0603**, on XTAL/EXTAL to DGND, 40 MHz (source: boards/control-card.tsx:329-332; mcu-pin-manifest.md:131,137).
- SWD: `JSWD` = `Header(10,2)` (VREF/TMS/GND/TCK/GND/TDO/NC/TDI/GND/RST); `RBOOT` 10 k pull-up TMS→V5A (boot strap); `CRST` 100 pF on RESET_B (source: boards/control-card.tsx:341-345).
- BGA rule: via-in-pad or dog-bone, 7×7 thermal via array, all 25 VSS balls to ground plane (§2 table; source: dfm.md:85-87). MCU pin-out is symbolic — "bind at layout" (source: verification-report.md:190).

### 4.12 Resolver-excitation TVS island and PTC coupling (full rule — the densest rule in the source set)

Signal chain, amplifier outward (source: dfm.md:110-118): amplifier output node (DEXP/DEXN Schottky, **pin 1 = cathode on VEXD, pin 2 = anode on the amplifier node**) → RSXP/RSXN (2.2 Ω 1206 anti-surge) → protected node (TVSEP/TVSEN, **cathode on the node, anode to the AGND star with a wide short trace**) → FEXP/FEXN (PTC, 1812) → vehicle connector. **The TVS must sit on the amplifier side of the PTC.**

Layout rule (source: dfm.md:119-132, F193/F203):
- Each TVS sits on its own **≥3 cm² 2 oz copper island with a thermal-via array** to the inner AGND copper.
- **FEXP/FEXN (the PTC) are placed with their pads on that same island**, next to the TVS.
- The rule is written for the **pair**, not the TVS alone: TVS-to-PTC transfer ≈40 K/W, TVS junction-to-ambient ≈55 K/W, PTC body-to-ambient ≈90 K/W.
- **A lower island resistance alone RAISES the coupled junction temperature** — do not over-copper this island on the assumption that "more copper is always better" (source: dfm.md:129-131; verification-report.md:590).
- The PTC body keeps **~2 mm of free air above and beside it** — only its pads share the island (source: dfm.md:131-132).
- Steady-state numbers the layout must beat: 1.69 W/23 °C, 0.87 W/85 °C on the sheet's bare 8×8 mm pads (75 K/W) vs **≈1.6 W hot on a ≥3 cm² island with thermal vias (≈40 K/W)** (source: verification-report.md:588).
- Exact island area (beyond the ≥3 cm² floor), via count and PTC placement are **bench-verified, not fixed** — QP-RX-04 on-fail: "first the layout — island area, via count, PTC placement on the island" (source: qualification-plan.md:1699) — see open decision §6.5.
- Marine kit inherits this identical island unchanged (source: marine/verification-report.md:285).

### 4.13 Motor-temperature inputs

"**FMT (0603 fuse) first from the connector**, then TVSM (SMA) to AGND, then the 1 k into the buffer; **keep the fuse away from the LDO heat so its rating holds**" (source: dfm.md:133-134). Code: `boards/control-card.tsx:712-720` (FMT fuse 712, TVSM clamp 715, RMT divider 717-718, CMT filter 719, UMT buffer 720). SMAJ5.0A clamp: cathode on the line, anode on AGND (source: boards/control-card.tsx:714).

### 4.14 CAN

Fixed routing order: transceiver → common-mode choke (TDK ACT45B) → split termination (60.4 Ω + 60.4 Ω with 4.7 nF to DGND at the split point) → bus-side TVS across CANH/CANL to DGND → connector (source: packages/cells.tsx:355-372). **Termination is a population option**, fitted only if this inverter is a CAN bus end node (source: interface-requirements.md:44, IR-17). Resolver-cable shields terminate to AGND **at the vehicle connector** (single-point tie there); AGND itself keeps its own single-point tie elsewhere (source: verification-report.md:163, F75).

### 4.15 Harness header orientation and pin-1 rule (40-way JIC/JICC)

- "confirm the header's pin-1 corner and the odd/even row numbering **against the Samtec print before the footprint is placed**… the silkscreen and the cable drawing must agree" (source: dfm.md:143-145).
- Fixed numbering: **sequential per row, row A = 1–20, row B = 21–40** (source: qualification-plan.md:2787-2792, F182).
- Pin map: **V5GD on pin 1, HW_ID on pin 2, VBAT_H on pins 19/20, VBAT_L on pins 39/40** — chosen so every supply pin sits beside ground under both row-by-row and odd/even numbering (source: interface-requirements.md:46, IR-19).
- QP-FAI-03 on-fail: "**correct the cable drawing or silkscreen before any power-up**" (source: qualification-plan.md:2805).
- Vehicle connector `JVEH`: TE **770669-1** 23-pos AMPSEAL header + 770680-1 plug (source: dfm.md:43-45).

### 4.16 Grounding — star points and single-point ties

| Net | Rule | Source |
|---|---|---|
| AGND–DGND | single-point tie, `RAGT` 0 Ω, 0805 | boards/control-card.tsx:321 |
| TMOD_RTN (module-NTC return) | star point at the analog reference, `RTMR` 0 Ω, 0603 | boards/control-card.tsx:705-706; verification-report.md:168 (F20) |
| Resolver TVS anode | to the **AGND star**, wide short trace | dfm.md:115-116 |
| Resolver cable shields | single-point tie at the connector | verification-report.md:163 (F75) |

---

## 5. Thermal and mechanical

### 5.1 Component-height envelope

Only one explicit height limit exists in the mined sources: **CLVC3 (Panasonic EEH-ZC1H101P, φ10×10.2 mm G can) — 10.5 mm maximum height; "the card's component-height envelope and the housing clearance above it must allow it"** (source: dfm.md:138-140). No other numeric height/keep-out is stated anywhere in the mined sources — flag this as a gap for the enclosure drawing.

### 5.2 Coldplate interface

| Item | Value | Source |
|---|---|---|
| Design point | −40…+85 °C board, **65 °C coldplate** | design-basis.md:23 |
| R_th | **0.045 K/W per switch, ASSUMED, not yet measured** | interface-requirements.md:64 (IR-25) |
| Time constant | 60 s, **also assumed** | interface-requirements.md:64 (IR-25) |
| Coolant flow/pressure | **"not specified anywhere in the design basis"** | interface-requirements.md:64 (IR-25) |
| Physical coldplate | "FSW/gun-drilled Al, **3× EconoDUAL footprint**", fittings, **~360×160 mm**, ≤0.05 K/W per switch to coolant (thermal-sim assumption) | cost-rollup.md:15 |
| Measurement method | QP-TH-01: heat one switch position with DC, at nominal and minimum flow | qualification-plan.md:783-838 |

Assembly: "modules onto coldplate (**torque + TIM verify**)" (source: dfm.md:63; README.md:423). Module-mount variant still open: "power PCB onto module aux pins (**solder-pin vs press-fit variant of the HCS600 — decide with hiitio's outline drawing, VERIFY item**)" (source: dfm.md:64-65) — see §6.3.

### 5.3 Mounting / assembly sequence

Full sequence (source: dfm.md:57-68): SMT the three PCBs (single reflow side each is the layout target, ~750 placements total) → AOI → selective/wave THT → conformal coat card → cap-bank assembly (16 cans onto busbar, entry lugs + tab pairs + discharge studs torqued) → modules onto coldplate (torque + TIM verify) → power PCB onto module aux pins → busbar onto module DC tabs → discharge board bolted across the cap-bank studs → harnesses → lid + seal → EOL. Busbar joints are "**bolted, torque-controlled joints; HVIL loop opens before any service parting**" (source: design-basis.md:118).

### 5.4 Wave-side / THT rules

| Rule | Source |
|---|---|
| **No bottom-side THT**; bottom SMT limited to chip R/C if used at all | dfm.md:81 |
| Film-cap and stud holes on the **wave side only**; XAL molded inductors fine on standard profiles | dfm.md:78 |
| Keep every polarized THT part in **one orientation per board** | dfm.md:80 |
| 2512 pulse resistors and LFPAK56 need **reduced-aperture paste/stencil review** | dfm.md:77 |

### 5.5 Panelisation, fiducials, test points

| Rule | Source |
|---|---|
| **Card panelized 2-up with rails; power board single-up; discharge board 4-up** | dfm.md:79; cost-rollup.md:14 |
| **Fiducials: 3×/board + 2× local at the MCU** | dfm.md:74 |
| **Test points on every rail, PWM, FLT/RDY, SPI, both VDC senses, and the discharge command** (flying-probe first articles, bed-of-nails at volume) | dfm.md:74-76 |
| 0603 minimum passive (0402 only where loop area forces it — currently none) | dfm.md:72 |

### 5.6 Enclosure / connector data

| Item | Value | Source |
|---|---|---|
| Housing | CNC Al 6061 body + lid, seals, **hard anodize** | cost-rollup.md:17 |
| HV connectors | DC-in 2P 300 A + HVIL; 3-phase out 3P 400 A; **RADSOK-class** | cost-rollup.md:18 |
| Coolant | quick-connects (QCs) at the coldplate | cost-rollup.md:18 |
| Seals | Gore vent, EMC gasket, TIM ×3 modules, fasteners, internal harnesses (incl. 4-way discharge link + LEM harness) | cost-rollup.md:19 |

---

## 6. Decisions still open before you start

| # | Decision | What it affects | Provisional basis | Decision owner |
|---|---|---|---|---|
| 6.1 | PD2-sealed vs PD3 creepage strategy | HV spacings across the power board and every barrier footprint | **No default stated** — the source names two options ("argue PD2 inside a sealed coated enclosure… or lay out for PD3") and says only to "decide before the Road PCB layout" (source: marine/design-basis.md:48) | project/OEM |
| 6.2 | Coolant flow rate and pressure at the coldplate inlet | Validity of the 0.045 K/W R_th figure and every Tj margin in the platform | **No default stated** — "not specified anywhere in the design basis" (source: interface-requirements.md:64, 99) | OEM (or the program's own coldplate/pump spec, if fitted) |
| 6.3 | HCS600 module mount: solder-pin vs press-fit | Power-board footprint/hole pattern under the modules | **No default stated** — "decide with hiitio's outline drawing, VERIFY item" (source: dfm.md:64-65) | hiitio outline drawing / project |
| 6.4 | VGT12EEM core potential/tie in the layout | Which hi-pot withstand figure (2.6 kVrms NP,NF–NS vs 1.3 kVrms coil–core) governs QP-HV-04's applied level | **No default stated** — "confirm the core potential in the layout" (source: qualification-plan.md:1413-1414) | layout engineer (this decision is literally yours to make and document) |
| 6.5 | TVS-island exact geometry (area beyond the ≥3 cm² floor, via count, PTC placement on the island) | Coupled junction temperature of the resolver TVS/PTC pair | Concept is fixed (≥3 cm² 2 oz + thermal-via array + PTC pads on it, source: dfm.md:119-121); exact geometry is bench-verified afterward by **QP-RX-04 step 2b (RELEASE GATE)** (source: qualification-plan.md:1603, 1699) | layout engineer implements; verification bench confirms |
| 6.6 | Pollution degree / OVC for the general Road HV audit | Every HV creepage/clearance dimension QP-HV-06 checks | **PD2/OVC II, stated as provisional** (source: interface-requirements.md:94, IR-36) | OEM (altitude/OVC statement) |
| 6.7 | EMC standard edition/class | CAN chokes, Y-caps (CY1/CY2/CPET), gate-drive edge rates | **Provisional default: CISPR 25 class 3, ISO 11452 level III** (source: interface-requirements.md:92, IR-34) | OEM |
| 6.8 | Insulation test levels / PD limits (production and type) | Hi-pot/PD test levels applied to the finished boards | **No default** — "the plan uses the reinforced-barrier ratings as ceilings… and does not set production levels on its own" (source: interface-requirements.md:93, IR-35) | OEM |
| 6.9 | Altitude | Folded into the PD/OVC decision (6.6) | **No separate figure stated** | OEM |
| 6.10 | MCU/module pin-map "VERIFY before layout" checks | Correctness of the MCU footprint pin numbering and the module aux-pin numbering vs the hiitio drawing | **Resolved in rev A.21**: the MCU is bound to `MAPBGA-289_14x14mm_Layout17x17_P0.8mm`, whose pad names equal the ball map (proved by `kicad-sch-verify.mjs`); the EconoDUAL 3 aux pins are drawn from the hiitio outline (`traction.pretty/MANIFEST.md`). Residual: 6.3 (solder-pin vs press-fit) (source: packages/cells.tsx:7; calculations/kicad-sch-verify.mjs) | done — verify 6.3 with hiitio |

---

## 7. Known document inconsistencies (corrected in rev A.21 unless noted)

The schematic sources (`boards/*.tsx`, `packages/cells.tsx`) and `docs/dfm.md` / `docs/design-basis.md` are authoritative. Where `README.md` disagrees with them, **follow the schematic/dfm/design-basis value**, not the README table.

**7.1 Discharge-board bleeder count/pattern — corrected.** README's DFM table (item 3) described the superseded 10× 27 kΩ 5s×2p; it now states the released **12× 22 kΩ 2512, 6s×2p = 66 kΩ, 154 V/resistor** (source: dfm.md:35-38; design-basis.md:135-141; boards/discharge.tsx:26-36).

**7.2 RFS4 fillet-temperature figure — corrected.** dfm.md §4 now states the ESR18's own Fig. 4 line (knee 125 °C; ≤ 137 °C at 0.30 W, ≤ 126 °C at 0.48 W) and the verification report's FS1B row uses the ESR18's 0.41 W at 85 °C / 2.0 W for 5 s (source: dfm.md §4; verification-report.md "RFS4 with FAULT_OUT shorted to KL30 and FS1B asserted").

**7.3 MCU open-ball count — corrected.** mcu-pin-manifest.md now says 159 throughout (289 − 130 connected).

**7.4 Board canvas size vs. stated PCB area — open (not a document defect).** The `.tsx` `<board>` width/height is disclaimed as "a coarse grid only" (packages/cells.tsx:2-3), not a real outline; the only stated PCB area is cost-rollup.md:13's "~0.12 m²" (power board, no width/height split, smaller than the 0.16 m² the canvas implies). Neither is released — derive the real outline from the coldplate/enclosure interface (~360×160 mm, 3× EconoDUAL footprint, cost-rollup.md:15).

---

## 8. Verification the layout must leave possible

| Test | What it needs from the layout | Source |
|---|---|---|
| **QP-TH-04** (hot first article, board hot spots) | Thermal/probe access to: U5LB/U5LC tabs, a current shunt in each UCC12051-Q1 input, R5LB/R5LC + RASCG + RFS4 terminations, local ambient of VOW3120 (UASC/UQD), FEXP/FEXN and DEXP/DEXN, FMT1/FMT2 — via thermocouples and an IR window into the chamber | qualification-plan.md:910-946 |
| **QP-HV-06** (creepage/clearance audit, layout + first article) | Every barrier part (NSI6611, AMC1311B, UCC12051-Q1, UCC14141-Q1, VOW3120, VGT12EEM), the HV connector and the busbar must be **optically measurable** for clearance/creepage on 3 first-article boards, in addition to the CAD report | qualification-plan.md:1439-1459 |
| **QP-RX-04 step 2b** (TVS-island coupling sweep, RELEASE GATE) | Probe access (I-CP/I-TC) on the PTC lead, TVS lead, RSX lead and body, the protected node, the amplifier output node, and VEXD — the TVS island and its adjacent PTC pads must stay accessible, not buried under other parts | qualification-plan.md:1603-1650 |
| General test-point rule | Test points on every rail, PWM, FLT/RDY, SPI, both VDC senses, and the discharge command (flying-probe first articles, bed-of-nails at volume) | dfm.md:74-76 |
| Fiducials | 3×/board + 2× local at the MCU | dfm.md:74 |

---

## How this document is maintained

Sections 2–8 are assembled from the sources they cite; when a rule changes in `dfm.md`, `design-basis.md`, the interface
requirements or the qualification plan, this document is updated in the same review round (the round's disposition names
it). §1a is generated evidence: `npm run sheets` rebuilds the footprint library and both schematic sets and runs the KiCad
proof; a failure there blocks the sheets. Two gaps no source states, flagged rather than guessed: a real stack-up
(dielectrics, plane assignment, impedances) and a general component keep-out/height budget beyond the CLVC3 figure.
