# Marine Series — Design Basis (M8 · M10)

A **separate product series** from the Road GEN-1 inverter ([`docs/variants.md`](../docs/variants.md)):
its own product codes, ratings, firmware build, enclosure, cooling interface, type approval and
change control. Every number here is produced by `marine/calc/marine-verify.mjs`
([`verification-report.md`](verification-report.md), `npm run marine`) from datasheet values
cited in §4. Status: **definition + verified sizing, 2026-09-23** — no Marine hardware is built.

## 1. The market this series serves (sourced)

Sources: [`sources/marine-requirements-brief.md`](sources/marine-requirements-brief.md) and
[`sources/multiphase-pmsm-brief.md`](sources/multiphase-pmsm-brief.md) (2026-09-23; every fact keyed to a URL). Rule editions read: DNV Pt.4
Ch.8 July 2013, ABS MVR 2018, BV NR467 July 2026, IACS UR E10 Rev.10, IRS battery guidelines
Rev.3 (Sep 2026). Re-check the DNV and ABS clauses against current editions before quoting them.

| Segment | Propulsion per shaft (examples) | DC practice | What it means here |
|---|---|---|---|
| Water taxi / small passenger craft | Navalt Aditya 2 × 20 kW (Kerala), Candela P-12 2 × 110–160 kW; Kochi Water Metro 100-pax boats on 122 kWh LTO, IRS "Battery Prop" | 350–850 V packs | 1 cell per shaft: **M8** (or M8-SiC for range-critical boats with short motor cables) |
| Inland / coastal ferry | MF Ampere 2 × 450 kW; Ellen 2 × 750 kW; ForSea 4 × 1.5 MW; MF Hamlet 4 × 1.53 MW PM azimuths | ABB/Wärtsilä **1000 V DC** grids; Editron runs 750 V | 1–6 cells per shaft, **M8** or **M10** |
| Harbour tug 32–45 t BP | Zeetug30 2 × 925 kW (two ABB HES880 per motor) | 730 V (HES880) to 1000 V | M10, 5–8 cells per shaft at tug rating |
| Harbour tug 60–70 t BP | eWolf 2 × 2.1 MW (63.5 t); Sparky 70 t; **India GTTP: 60 t battery-electric tugs on an ABB DC grid with Kongsberg PM thrusters** — 16 tugs by Dec 2027, 50 by 2030 | 1000 V | needs the paralleled building block (§6) |

Rules of thumb from the sourced vessels: ≈1.55–1.8 t of bollard pull per 100 kW, so a 60–70 t
tug is **2 × 1.9–2.3 MW** (≈4 MW), not 2–3 MW. Marine battery strings reach **1096–1205 V at
full charge** (Corvus Orca 350–1200 V, Blue Whale LFP 560–1120 V, Leclanché MRS-3 1096 V,
Echandia LTO 1000 V) — the reason M10 exists. The closest competitor to M8 is the Danfoss
Editron EC-C1200-450: 850 V max, 350 A rms, glycol to 65 °C, IP67, DNV type approval at Temp A /
Vib B / Hum B / EMC A. It publishes peak = nominal; an honest overload rating is a
differentiator. Indian projects buy ABB, Siemens and Kongsberg today — no domestic propulsion
converter with public specifications was found.

## 2. Class requirements that shape the product (sourced)

| Requirement | Rule | Consequence here |
|---|---|---|
| Type approval | IACS UR E10 (DNV CG-0339): machinery-space classes **Temp A (0–45 °C, tested 55 °C), Humidity B (condensing), Vibration A (0.7 g; B = 4 g on machinery), EMC A, IP44** | the automotive design already exceeds the vibration level; humidity B needs conformal coat + heater |
| Per-delivery certificate | DNV product certificate for converters ≥ 100 kW; ABS/LR witnessed tests ≥ 100 kW; BV ≥ 50 kVA; IRS CN 13 for electronics | budget witnessed FAT per shipset |
| Software | IACS UR **E22 Category III** for propulsion control: ISO 9001 + ISO/IEC 90003, documentation, FAT | map the ASIL-D (ISO 26262) artefacts onto E22 deliverables |
| Single failure | no single failure disables propulsion (DNV R1: back within 30–45 s). **ABS and LR do not accept a dual-winding motor alone** as redundancy for sole electric propulsion; BV accepts double windings with one converter each | multi-winding motors give converter redundancy; motor redundancy comes from two shafts — true for every vessel in §1 |
| Trip philosophy | power reduction instead of a trip, pre-warning, "power limitation" shown; overcurrent must not act in manoeuvring or heavy seas | firmware (§10) |
| Cooling | cooling failure → alarm + automatic current reduction, costs at most one line; leak detection and containment for liquid cooling | NTC-based derating exists; add a leak sensor and drip tray |
| Overload | 100 % continuous plus a **declared** overload and duration; crash stop; two consecutive starts | 110 % / 60 s declared (§3) |
| DC grid | semiconductor fuse per converter, IEC 61660 short-circuit study, **precharge before joining a live bus**, IT network with insulation monitoring | DC entry kit (§6b); Y-caps already 4.7 nF per rail (Danfoss marine build: 3.3 nF) |
| Discharge | < 60 V within 5 s, otherwise warning labels | active path 1.8/2.3 s + label (§7) |
| PM machines | means to stop a windmilling propeller; regenerated power must not raise alarms | shaft lock when towed; back-EMF rule (§6b) |
| Semiconductor margin (**BV only**) | repetitive peak voltage ≥ 1.5 × U_P (dedicated supply) or 1.8 × U_P (common bus) | read literally: 1200 V parts ≤ 800/667 V, 1700 V parts ≤ 1133/944 V — **get BV's written interpretation**; no DNV/LR/ABS/IRS equivalent found |
| Control power | E10 DC supply ±10 %, battery-fed +30/−25 % | ship 24 V → **isolated 24→12 V, 60 W** in the kit (the card's TVS stands off 20.5 V; its LV budget is ≈25–30 W) |
| Creepage (ABS) | IEC 61800-5-1 at OVC III / PD3: 12.5 mm at 800 V, 16 mm at 1000 V | argue PD2 inside a sealed coated enclosure (Editron precedent) or lay out for PD3 — decide before the Road PCB layout, which M8 inherits |

## 3. Operating basis (what "rated" means in this series)

| Item | Marine basis | Road GEN-1 (for contrast) |
|---|---|---|
| Duty | **continuous (S1)** at rated power, for years; overload **110 % for 60 s** | 30 s peak / continuous |
| Coolant inlet | **45 °C max** — IACS UR M40 sets 45 °C air / 32 °C sea water; LT fresh-water loops are designed to 36 °C (MAN) or 20–38 °C (Siemens), so 45 °C leaves 7–9 K for tropical harbours and fouled coolers | 65 °C |
| Junction limit | **Tj ≤ 125 °C continuous**, ≤ 150 °C at overload (25 K under Tvjop) | Tvjop / 0.75·Tvjop |
| Life — ferry duty | power cycling ≥ 2 × 30 dock↔cruise cycles/day for 20 years (0.22 M at 85 % load), LESIT with 20 % margin → **the continuous rating** | vehicle mission |
| Life — tug duty | ≥ 2 × 6 jobs × 20 full-power pulses/day × 330 days × 20 years (0.79 M at 100 % load) → **the tug rating**, ≈70–78 % of the continuous one. The pulse count is an estimate — an owner's measured profile moves the tug rating, not the hardware | — |
| Switching | M8 **5 kHz** (DC-link ripple vs OV-trip margin), M10 **3 kHz** | 5–10 kHz |
| Power factor / modulation | 0.85 at m = 1.1 (SVPWM). That is 1.1 / 1.155 = 95 % of the linear limit, the same point as the 0.95 reserve in the power formula | same |
| Coldplate | 0.045 K/W per switch position, IGBT and diode heat **share** it (adds 3–4 K vs the Road model) | per die |

Switching energy is scaled with V^1.3 (the Road model is linear — fine at 30 s, optimistic for
continuous duty above the datasheet test voltage). The marine loss function reproduces the Road
`igbtLoss` exactly when set back to linear scaling; the script asserts this on every run.

**Independent cross-check (2026-09-23).** A second implementation was written from the
equations and datasheet inputs alone, without access to this model:
[`calc/xcheck.py`](calc/xcheck.py), `python3 marine/calc/xcheck.py`, stdlib only, 8 internal
self-checks. It agrees on every rating to 3 significant figures:

- thermal limits;
- ferry and tug life limits;
- power;
- Kolar ripple;
- 0 Hz stall;
- battery series ranges;
- discharge;
- precharge;
- the N−1 propeller law.

The one difference is in-period DC-link ripple. This model gives 41 V / 66 V against 39.4 V /
62.7 V there, because it compares the reference continuously (natural sampling), while the
cross-check samples it once per period. This model is the conservative one; both pass.

## 4. Semiconductors — the IGBT range, and why

All six candidates were read from their datasheets — PDFs in [`datasheets/`](datasheets/), extraction with
page/table citations in [`sources/hiitio-modules-extraction.md`](sources/hiitio-modules-extraction.md). **All share the Road D3 outline (152 × 62 mm) and 11-pin map** — one footprint,
one coldplate interface.

| Module | Class | Key data (DS) | Role |
|---|---|---|---|
| **HCG600FH120D3E1EA** | 1200 V / 600 A IGBT | VCEsat 1.82 V @600 A hot; Rth 0.07/0.10 K/W; SC 6 µs @800 V | **M8** (Road part, 800 V class) |
| **HCG600FH170D3E1** | 1700 V / 600 A IGBT | VCEsat 2.16 V chip @175 °C; Eon/Eoff 197.6/156.5 mJ @900 V/600 A/175 °C; Rth 0.039/0.057; Tvjop 150 °C; SC 3200 A ≤ 6 µs @1000 V; VISOL 4.0 kV | **M10** source A |
| **HCG600FH170D3E1A** | 1700 V / 600 A IGBT | VCEsat 2.36 V terminal @175 °C; Eon/Eoff 153.0/217.9 mJ; Erec 75.2 mJ; Rth 0.062/0.092; Qg 5.01 µC; same SC; VISOL 3.4 kV, creepage 15 mm | **M10** source B |
| HCG900FH120D3E1A | 1200 V / 900 A IGBT | terminal 2.05/2.10 V @900 A hot; Rth 0.046/0.072; Qg 10.8 µC; SC 3200 A ≤ 6 µs @800 V | evaluated (M8-HP) |
| HCG900FH120D3RC | 1200 V / 900 A IGBT | Rth(j-h) 0.095/0.13 K/W — worse than the E1A; no Tvjop | not preferred |
| HCS800FH170D3C1 | 1700 V / 800 A SiC | 6.3 mΩ chip @175 °C; Eon/Eoff 58.5/80.6 mJ @3.3 Ω; **no short-circuit rating published** | evaluated (M10-SiC) |

**M10 is rated on the worse of E1 and E1A for every parameter** (E1A conduction/Rth/Eoff/Erec,
E1 Eon with a +47 % gate-network allowance), so either part can be fitted and hiitio can be
held to two sources. Not used: the 3300 V HCG1000S330H1K1/HCG1500S330H2K1 (medium-voltage class,
above the ≤ 1500 V DC low-voltage marine grids this series targets) and the HCH900 SiC/Si hybrid.

Datasheet defects to raise with hiitio before PO (they do not change the ratings above):
the 900 A parts' E = f(I_C) curves read 2–4× their own Table 5 (ratings use the tables with a
gate-network factor); E1A's Table 1 prints package "HB3" while its outline is D3; E1 tests
I_CES at 1200 V on a 1700 V part; **no hiitio D3 datasheet publishes a terminal RMS current** —
the M8/M10 continuous currents (300/270 A) must be confirmed against it.

## 5. Cell ratings (verified, 45 °C coolant)

| | **M8** | **M10** |
|---|---|---|
| Hardware | Road 8XX IGBT PCBs, frozen fork (§9) | new 1100 V power stage, Road control card |
| Module ×3 | HCG600FH120D3E1EA | HCG600FH170D3E1 / E1A |
| DC bus | **500–850 V** (OV trip 880 V) | **650–1100 V** (OV trip 1150 V) |
| f_sw | 5 kHz | 3 kHz |
| Continuous current | **300 A rms** — capacitor-bound (thermal 329, life 312) | **270 A rms** — life-bound (thermal 290) |
| Continuous power | **214 kW @720 V** (148 @500 · 252 @850) | **254 kW @950 V** (174 @650 · 294 @1100) |
| Overload 110 % / 60 s | 330 A, Tj ≤ 125 °C | 297 A, Tj ≤ 127 °C |
| **Tug rating** (power-cycling bound) | **220 A → 157 kW** | **190 A → 179 kW** |
| Tj at rating, V_max | IGBT 117 °C · diode (regen) 97 °C | IGBT 119 °C · diode 96 °C |
| Cruise cycle ΔTj (85 %) | 52 K → 0.73 M cycles (LESIT) | 54 K → 0.59 M cycles |
| Semiconductor efficiency at cruise | 98.6 % | 98.7 % |
| Short circuit | 4.46 µs detect + soft-off vs 5 µs derated | 4.09 µs vs 4.8 µs derated (6 µs @1000 V → 1100 V) |

**M8-SiC** (the Road 8XX SiC build, frozen fork, 10 kHz) has the same 300 A / 214 kW
capacitor-bound rating, tug rating 235 A / 167 kW, and 98.9 % at cruise — the extra ≈0.3 pt is
range on a small battery. Short motor cables only (§6), and no marine delivery before hiitio's
SiC short-circuit letter (Road VERIFY ③). Built on request for small craft.

"Low RPM, high torque" is the motor's job (T = P/ω: more poles and turns); the inverter sees
kVA and a normal output frequency because low-speed PM motors carry many poles — a 1 MW,
180 rpm, 32-pole direct-drive motor makes 53 kNm at 48 Hz, a pulse ratio of 104 at 5 kHz. A
propeller's torque ∝ n², so full torque at 0 Hz never occurs in normal running; the fouled-
propeller / pushing case is covered in §6b.

## 6. Architecture — one cell per winding set; MW comes from more sets

**How MW marine PM motors are built.** With standard 3-phase converters at 0.1–5 MW, the motor
is **multi-three-phase**: N separate 3-phase winding sets with **isolated neutrals**, shifted
60°/N (dual 30°, triple 20°, quad 15°). Sourced precedents (multiphase research brief):

- Danfoss EM-PMI540 **DUAL/QUAD**: 284–896 kW, 600–2400 rpm, one 350 A inverter per set.
- The Switch dual-winding PM machines: 0.5 to more than 12 MW; 32–40 poles; 0–130/220/400 rpm;
  70–1500 kNm; 450/500/690 V.
- A 45 MW quadruple-star motor on four VSIs.

True 15- and 24-phase machines appear only where the converter is custom anyway: naval AIM,
Siemens Permasyn. Low-speed MW motors have many poles and parallel paths, so extra sets cost
mainly terminal boxes and cables. That is why a standard 3-phase cell can serve as "one of N".

**How N cells run one motor:**

- **Current control.** Each cell runs its own dq current loop at its set's angle offset. The
  master (a cell or the propulsion controller) sends torque, per-cell sharing weights, the
  active-set mask and the N−1 limits.
- **Harmonic circulation.** Dead time and back-EMF distortion put 5th/7th harmonics (and
  11th/13th for N ≥ 3) into the x–y planes. There only the leakage inductance limits them: in a
  published 20 MW model it was 1/175 of the magnetising reactance. A **6ω resonant loop (12ω for
  N ≥ 3) in each cell's own dq frame** suppresses them. Published result: 5th/7th went from
  30 %/10 % to 3 %/1 %. This is firmware only.
- **Mutual coupling.** It is feed-forward from the shared reference, with a moderate
  current-loop bandwidth. No fast data link is needed for current control.
- **Coordination.** A private CAN-FD "shaft bus" carries one command frame plus N status frames
  per ms. Bus load is 19–45 % for N = 2–6; N = 8 runs at 500 Hz. A hardwired enable/E-stop goes
  to every cell (KL15), and FAULT_OUT goes to the propulsion control.
- **PWM sync.** Separate winding sets need carrier sync only to reduce x–y ripple and, on a
  common DC link, capacitor ripple. The target is ≤ 1 µs. It is done in firmware with
  time-stamped CAN-FD frames and a period-trim PLL.
  - Loss of sync gives an alarm, not a trip.
  - If the bench misses 1 µs, the Road card's spare JVEH SP1/SP2 pins are routed to a sync
    input. That is a card ECO, not a new card.
- **Industry practice is the same pattern.**
  - VACON DriveSynch (Danfoss marine) runs ≤ 4 drives on one- or multi-winding motors, with a
    per-follower winding phase-shift parameter and the encoder at the master only. It survives
    the loss of any follower.
  - Siemens' patent sends position from the master module to the slaves.
  - ABB ACS880 master/follower shares references, not angle, in < 5 ms.
- **Position sensing.** The default is one resolver per set, which meets BV's duplicated-speed
  rule. It is a **motor-specification item**: a Danfoss QUAD motor has one resolver for four
  inverters.
  - If a set has no resolver of its own, its cell runs as a follower, VACON-style: I/f current
    control below ≈10 % speed (propeller torque there is ≈1 %), then a back-EMF observer. That
    avoids a fast angle link; a CAN-rate angle would lag 10° at 30 Hz.
  - Every cell runs the back-EMF observer anyway, as a plausibility check.
- **Per-set disconnector — required for N ≥ 2, or wherever the shaft can keep turning.**
  - The problem is a shorted switch in one cell. Under pulse-off it leaves an **asymmetric
    short**, with pulsating torque and heating, while the healthy sets keep driving the shaft.
  - The Road cell has LS-ASC only. It can symmetrise a shorted low-side switch, but not a shorted
    high-side one.
  - So the set's disconnector opens automatically on a cell DESAT fault; KR requires this for PM
    motors anyway. It must be rated to break that set's short-circuit current (≈ I_ch) at the
    fundamental frequency.
  - Its aux contact goes into the Road HVIL loop (open = torque off), so the cell needs no new
    input.
- **Sustained ASC needs gate power.** A towed or windmilling PM propeller can demand ASC for
  hours, but the Road ASC holds only 1–3 ms after low-voltage loss. The cell's 24 V must
  therefore be class-grade and battery-backed, with two feeds on towed or single-screw vessels.
  - The motor needs I_ch ≈ 1 pu, because ASC copper loss is (3/2)·I_ch²·R: rated loss at 1 pu,
    4–9× at 2–3 pu.
  - ASC cannot hold a shaft at 0 rpm, which is why class wants a shaft lock.
- **Paralleling two cells on one set.** Not offered: it needs about 100 ns timing, coupling
  reactors and one controller for both. Adding a set is cheaper, and fault isolation stays
  class-aligned.

| Sets N | Angles | Harmonic loops | One cell lost: torque / shaft speed / power on that shaft |
|---|---|---|---|
| 1 | — | 6ω optional | 0 — redundancy is the second shaft (every vessel in §1 has two) |
| 2 | 0/30° | 6ω | 50 % / 71 % / 35 % |
| 3 | 0/20/40° | 6ω + 12ω | 67 % / 82 % / 54 % |
| 4 | 0/15/30/45° | 6ω + 12ω | 75 % / 87 % / 65 % |
| 6 | 3 groups × 2 | 6ω + 12ω | 83 % / 91 % / 76 % |
| 8 | 4 groups × 2 | 6ω + 12ω | 88 % / 94 % / 82 % |

N−1 follows the propeller law (T ∝ n²), and the numbers assume the shaft was sized with no
spare cell. The report's vessel rows credit real spare capacity.

**Cabling.**

- Give each set its own symmetrical screened 3-core cable, matched in length and kept apart
  from other sets in the trays.
- Specify inverter-duty insulation for ≥ 2 pu reflected-wave peaks: about 1.7 kV on M8 and
  2.2 kV on M10 with long cables.
- Fit an insulated NDE bearing plus a shaft-grounding brush.
- Fit dv/dt filters only beyond about 30–50 m of cable, or where the motor's IEC 60034-25/-18-41
  rating requires them.
- M8-SiC is for short cables only: with SiC edges, 4 m of cable already nearly doubles the
  terminal voltage.

**Scaling ladder** (the report's *Vessel sizing* rows):

| Shaft power | Build | Motor |
|---|---|---|
| ≤ 0.2 MW | 1 × M8 or M8-SiC | 3-phase |
| 0.2–0.45 MW | 2 × M8 (1–2 × M10) | dual 30° |
| 0.45–1 MW | 3–4 × M8/M10 | triple / quad |
| 1–1.5 MW (ferries); 0.9–1.3 MW tugs at tug rating | 4–6 × M10 (tugs: 6–8) | quad / 6-set |
| **1.9–2.3 MW — 60–70 t tugs (GTTP class)** | **6–7 × M10-2P** at tug rating, or two tandem quad motors on M10 cells | 6–8 sets |

**The MW-end building block, M10-2P.**

- Two 1700 V / 600 A modules per switch position, driven by one NSI6611 per position through a
  gate booster.
- Individual gate and emitter resistors per module, and one DESAT sense per pair.
- About 27 cans and ±1500 A-class current sensors.
- A new six-module power PCB. The control card is unchanged.
- Rating: 465 kW (ferry) / 329 kW (tug) per cell.
- Build it when a 60–70 t tug order is committed. Until then, 60–70 t tugs are quoted as tandem
  quad motors on M10 cells.

Evaluated and not chosen:

- **M8-HP** (1200 V/900 A, 306/221 kW): an 800 V class only, and the 900 A datasheets' energy
  curves disagree 2–4× with their own tables.
- **M10-SiC** (305/225 kW at 8 kHz): SiC's power-cycling factor of 0.33 eats the gain, and no
  hiitio SiC short-circuit or cosmic-ray data exists.

## 6b. Starting a MW motor — no starter needed

Large motors are hard to start when they are switched **directly onto a fixed-frequency supply**:
6–8 × rated inrush, voltage dip on the generators, hence star-delta, auto-transformers and soft
starters. None of that exists here — **the inverter is the soft starter**: field-oriented control
applies rated torque from 0 Hz at no more than rated current, and a PM rotor needs no magnetising
time (a MW induction motor needs its flux built first). What *is* real at MW level is below; every
item is firmware or a small kit, nothing complicated.

| MW-level concern | Why | How it is handled | Verified |
|---|---|---|---|
| Breakaway at 0 rpm (fouled propeller, tug pushing) | at 0 Hz one switch carries the full peak as DC and switches at it every period | firmware drops f_sw to **1 kHz below 2 Hz** → rated torque held indefinitely: M8 117 °C (134 % available), M10 139 °C (110 %). At the running f_sw the same point would pass 150 °C | report |
| Propeller already turning (ship moving, towed) | the PM motor is generating | **flying start** from the resolver angle and speed — no search, no transient | firmware |
| Cell joining a live DC bus (after repair, at sea) | 300–320 µF charged from ≈10 µH of feeder: **≈5–6 kA** ring | **per-cell precharge** 90 Ω (M8) / 110 Ω (M10): ≤ 10 A, 116/182 J, ≈150 ms — resistor + small contactor in the DC entry kit | report |
| Back-EMF with the cell off | PM flux cannot be switched off | motor spec: **E_LL,pk at 115 % overspeed ≤ 0.95 × OV trip** (836 V M8, 1093 V M10) → safe pulse-off is safe at every speed; ASC only for switch faults; shaft lock when towed | motor spec |
| Genset-fed bus (hybrid tugs/ferries) | step-load acceptance of the gensets | power-limit and ramp commands from the PMS on CAN; a hardwired fast load-reduction input | firmware + PMS |
| Crash stop | regeneration up to rated power | regen capped at the BMS charge limit; EMS keeps SOC headroom; brake chopper only for genset-only or full-battery cases | system |
| N cells on one shaft | all winding sets must pull together | master broadcasts one torque ramp; a cell that fails to start leaves N−1 running | firmware |

## 6c. Regenerative braking — who owns what

At sea regeneration comes from three places:

- **deceleration and crash stop**, where the ship's momentum drives the propeller;
- **a windmilling propeller** while under tow or drifting;
- **rapid thrust reversals** in tug work.

Ownership:

| Function | Owner | The cell's part | Verified |
|---|---|---|---|
| How hard to decelerate; the crash-stop profile | propulsion control (bridge lever / PCS) | executes negative torque and speed ramps | — |
| Where the energy goes | EMS / PMS and BMS | caps regen at the BMS charge limit and the PMS power limit (MFW-04/05) | — |
| Full battery | EMS keeps SOC headroom — the normal case for battery vessels | regen limited to what the battery accepts; the crash stop then relies on the headroom | — |
| Genset-only bus (diode-rectifier gensets cannot take power back) | system integrator: a **brake chopper + resistors** | option: **a standard cell in chopper mode** (firmware). Each leg's low-side IGBT switches an external resistor; the high-side diode freewheels. ≈260 kW (M8) / 303 kW (M10) per cell, IGBT at ≈64 °C — bounded by the cell's DC entry, not its silicon | report |
| Battery breaker opens during regen | — | FW-06 ≤ 20 µs (Road N9) → zero torque + ASC. Link peak 901 V (M8) / 1170 V (M10), 90 % of the cans' rating. A once-per-PWM-period V_DC sample would end at 1075 V / 1453 V | report |
| Windmilling | vessel: shaft lock when towed dead | powered: zero-torque (or BMS-limited braking) current control, no alarms (DNV); unpowered: the §6b back-EMF rule keeps the rectified link under the trip | §6b |
| Device heating in regen | cell | diode Tj in continuous regeneration: 97 °C (M8) / 96 °C (M10) | report |

Battery-electric vessels (Kochi-type ferries, GTTP tugs) need no chopper: the battery is the
brake. Only genset-only or hybrid-with-full-battery operation needs one. Because a chopper is the
same cell with a different firmware mode, **no new hardware exists for it** — the resistor bank
is sized per project.

## 7. DC link and discharge

| | M8 (Road bank, unchanged) | M10 (new bank) |
|---|---|---|
| Cans | 16 × C3D 20 µF/1100 V (U_N 1000 V @85 °C) | **20 × 15 µF, U_N ≥ 1300 V @85 °C, ≥ 15 A** — class part, Faratronic RFQ |
| Ripple per can, continuous / overload | 12.2 / 13.4 A of 15.4 A | 8.8 / 9.6 A of 15 A |
| In-period ripple at f_sw | 41 V pp → 850 + 21 V < 880 V trip | 66 V pp → 1100 + 33 V < 1150 V trip |
| Bleeder | 2 × 6 × 22 k (56 s to 60 V) | **2 × 8 × 22 k** (78 s; 150 V, 0.9 W per resistor) |
| Active discharge | 4 × 470 Ω, 32 J per resistor, 1.8 s | **5 × 470 Ω**, 40 J per resistor, 2.3 s; **≥ 1700 V switch** |

Marine has no crash case: the target is IEC 61800-5-1's < 60 V within 5 s for accessible parts
(both pass with margin). The QDIS-stuck-on case (384/515 W into 10 W parts with the battery
connected) is bounded exactly as on the Road (fire only with the DC breaker reported open,
precharge plausibility, fail-open flameproof wirewounds).

## 8. Battery and DC-grid voltage windows (the BMS ranges)

Pack series count that keeps the **whole SOC range** inside the cell's window (3 % headroom at
full charge for regeneration/BMS tolerance, 5 % load sag at empty). Cell limits are typical
datasheet values — confirm against the chosen cell.

| Chemistry (V/cell min/nom/max) | **M8 (500–850 V)** | **M10 (650–1100 V)** |
|---|---|---|
| LFP 2.5 / 3.2 / 3.65 | **211–225 s** · 225 s = 563 / 720 / 821 V | **274–292 s** · 292 s = 730 / 934 / 1066 V |
| NMC 3.0 / 3.65 / 4.2 | **176–196 s** · 196 s = 588 / 715 / 823 V | **229–254 s** · 254 s = 762 / 927 / 1067 V |
| LTO 1.5 / 2.3 / 2.7 | 305 s = 458 / 702 / 824 V — derates in the last few % SOC | 395 s = 593 / 908 / 1067 V — derates below 650 V |
| Genset DC (1.35·V_LL) | 400 V → 540 V · 440 V → 594 V | 690 V → 932 V (1025 V at +10 %) |

The BMS contract per cell class: charge-voltage limit ≤ 97 % of V_max, a regenerative-current
limit on CAN that the firmware obeys (crash-stop and deceleration energy), and an insulation-
monitoring reading (IT system).

## 9. Separation, identity and hardware deltas

**How "separate" is enforced.** M8 is a *frozen fork* of the Road 8XX IGBT build at rev A.6:
same PCBs and supply chain, but its own part number, its own firmware build, and a distinct
identity resistor — **RHWID 47 k (4.12 V on HW_ID)**, 0.68 V clear of the nearest Road code
(22 k = 3.44 V). Road firmware refuses a marine cell and marine firmware refuses a road inverter
(FW-01/02 mechanism; the marine ratings assume 45 °C coolant, the Road's 65 °C). A Road change
reaches M8 only through a marine ECO with class notification — a type-approved product does not
move with the automotive line.

| Change | M8 | M10 |
|---|---|---|
| Power PCB | Road, unchanged | **new** — 1100 V creepage/clearance (conformal coat + slots), same D3 footprint |
| Modules | HCG600FH120D3E1EA | HCG600FH170D3E1 / E1A |
| DESAT string | 2 × US1M, 4.7 k, 82 pF | **3 × US1M**, 4.7 k, 82 pF (trip 3.65–6.55 V vs 1.92 V VCEsat) |
| Gate resistors | Road IGBT 1.0/1.0 Ω | start 1.0/1.0 Ω (DS point) — set by DPT at 1100 V |
| Flyback transformer | VGT12EEM (Road gate ⑤) | **transformer certified ≥ 1150 V DC working** |
| Isolated bias (ASC/QDIS) | QA01C (Road BOM note) | **module certified ≥ 1150 V DC working** |
| Opto (UQD, UASC) | TLP152 (V4) per Road N8 | **SO6L/SOW-class reinforced opto** |
| Phase sensors | HC5FW 900-S + busbar sleeve (N7) | same + sleeve rated for 1100 V, LEM sign-off |
| Cap bank / discharge | Road | §7 values, new busbar drawing |
| V_DC sense divider | 6 × 1206 per channel (142 V each at 850 V) | **8 × 1206** per channel (138 V each at 1100 V; 6 would put 183 V = 92 % of 200 V on each) |
| Control card | Road (RHWID code only) | Road |
| M8-SiC | = Road 8XX SiC build, RHWID 100 k | — |
| Identity (HW_ID, MFW-01) | RHWID 47 k → 4.12 V | RHWID 1 k → 0.45 V |
| Enclosure / environment | marine enclosure, conformal coat, anti-condensation heater, DC fuses, marine connectors — §11 | same |

**Insulation** (component ratings, datasheets): NSI6611A-Q1 and AMC1311B are reinforced to IEC
60747-17 with 2121/2120 V DC working — good for both classes. TLP152 (UL1577 only, 5 mm),
QA01C and VGT12EEM (1-minute test voltages, no working rating) are open on the Road (N8, BOM
note, VERIFY ⑤) and must be replaced for M10. HC5FW/SP1 is a reduced-insulation part whose
insulation is completed by the busbar sleeve (N7). PCB creepage is the insulation-coordination
study; conformal coating is the marine baseline anyway (humidity, salt).

## 10. Firmware — Marine additions to the Road contract

The Road contract ([`docs/firmware-contract.md`](../docs/firmware-contract.md), FW-01…FW-21) applies unchanged.
The marine build adds:

| # | Requirement | Source |
|---|---|---|
| MFW-01 | **Identity.** HW_ID codes: M8 IGBT 47 k (4.12 V), M8-SiC 100 k (4.55 V), M10 1 k (0.45 V). All are clear of the Road codes (0.90/1.60/2.50/3.44 V). A cross-series parameter set refuses to enable the gates | FW-01/02 mechanism |
| MFW-02 | **Duty class.** Each parameter set carries its ratings and its duty class: continuous (ferry) or tug. The 110 % / 60 s overload runs on an I²t budget | §3, §5 |
| MFW-03 | **Reduce power instead of tripping.** Coolant temperature, leak, module NTC, DC-window edges, BMS and PMS limits reduce power, give a pre-warning and broadcast "power limitation". Trip only for damaging faults (DESAT, OV, hardware) | DNV/BV/ABS/LR (§2) |
| MFW-04 | **BMS contract.** Obey the charge/discharge current and voltage limits; cap regeneration at the charge limit; raise an alarm on converter failure. FW-06 runs at ≤ 20 µs on a free-running V_DC slot (Road N9) | IRS battery guidelines Rev.3; §6c |
| MFW-05 | **PMS contract.** Power-limit and ramp commands on CAN, plus a hardwired fast load-reduction input to protect gensets | §6b |
| MFW-06 | **Standstill.** Switch at 1 kHz below 2 Hz. A stall timer (≥ 50 % current at < 1 % speed for more than 10 s) warns, then derates | §6b (verified) |
| MFW-07 | **Flying start** from the resolver angle and speed | §6b |
| MFW-08 | **Safe state per set.** Safe pulse-off at every speed, relying on the motor back-EMF rule. ASC only for a switch fault in that cell, following the Road matrix: LS-ASC for a shorted low-side switch; a shorted high-side switch gets pulse-off plus **automatic opening of the set's disconnector**. Healthy sets clamp to their N−1 limits and do not trip. Towing/windmilling raises no alarm from regenerated power. Shaft-lock interlock | DNV, KR (§2), §6 |
| MFW-09 | **Multi-set control.** Angle offset per set, auto-calibrated at commissioning from back-EMF zero crossings. 6ω (plus 12ω for N ≥ 3) resonant loops, dead-time compensation, coupling feed-forward, sharing weights, N−1 limits. A backup master takes over; a cell holds its last reference for 50 ms, then ramps to zero | §6 |
| MFW-10 | **Shaft bus.** CAN-FD at 1/5 Mbit/s, 1 kHz (500 Hz for N = 8). PWM sync from time-stamped frames; loss of sync gives an alarm | §6 |
| MFW-11 | **Speed feedback.** One resolver per set (motor spec), with a back-EMF observer for plausibility. A set without its own resolver runs as a **follower**: I/f below ≈10 % speed, then the observer (VACON practice). On resolver loss: alarm, then limp-home above 10 % speed. Losing the reference must never increase thrust | BV, ABS, DNV; multiphase brief §4 |
| MFW-12 | **Joining the bus.** Precharge, then close only when \|V_link − V_bus\| < 5 % | ABS DC rules, §6b |
| MFW-13 | **Discharge.** Fire QDIS only when the DC entry is reported open (Road FW-17) | §7 |
| MFW-14 | **Monitoring inputs.** 24 V supply, leak sensor, heater, disconnector aux contact (HVIL chain), per-set winding PT1000 | §2, §6 |
| MFW-15 | **Software process.** IACS E22 Category III (ISO 9001 + ISO/IEC 90003), major changes re-approved by class; E27 cyber resilience where mandatory | §2 |
| MFW-16 | **Brake-chopper mode** (option). V_DC hysteresis drives each leg's low-side IGBT into an external resistor, with DC-entry current limiting. Used on genset-only buses | §6c |

## 11. Marine kit, environment and type approval

**Marine kit per cell.** These are planning estimates at marine volumes; RFQ ±25 %.

| Item | Why | ₹ per cell |
|---|---|---|
| Marine enclosure: IP44 minimum, IP54 target; stainless hardware; pressure-equalising vent | CG-0339 Enclosure B; condensing humidity (class B) | 8–15 k |
| Anti-condensation heater + thermostat | Humidity B; ABS/BV require anti-condensation means | 1.5–3 k |
| Isolated 24 → 12 V, 60 W (EN 50155 / IEC 60945 class), fed from **class-grade, battery-backed 24 V**; diode-OR of two feeds where the shaft can be towed | The card's TVS stands off 20.5 V against ship 24 V (18–31.2 V); sustained ASC needs gate power (§6) | 4–8 k (+1 k second feed) |
| DC entry kit: 2 semiconductor fuses + precharge (90/110 Ω + contactor) | Fuse per converter (ABS); precharge before joining a live bus | 12–25 k |
| Motor-side / DC disconnector (if in the cell's scope) | KR (PM motors); maintenance isolation | 15–30 k |
| Leak sensor + drip tray; marine glands | Liquid-cooling rules | 1–5 k |
| **Kit total** (without the disconnector) | | **≈ 27–56 k** |

Conformal coating is already on the Road EOL line. The Road Y-caps (4.7 nF per rail) already
suit insulation monitors, and automotive vibration design already exceeds class A (0.7 g).

**Type-approval plan.**

- **Classes.**
  - IRS for India: GTTP tugs and inland/water-metro craft; IRS "Battery Prop" is already on the
    Kochi boats.
  - DNV for export: the same E10 test set, product certificate per delivery ≥ 100 kW.
- **Tests (IACS UR E10 / CG-0339):**
  - dry heat 55 °C for 16 h operating (70 °C if the cell sits in a cubicle with other heat);
  - damp heat 55 °C / 95 %, two cycles;
  - vibration class A (B if mounted on machinery);
  - insulation resistance, and HV withstand at levels to agree with the society, because the E10
    tables stop at 690 V;
  - EMC to E10, including 24 dBµV/m at 156–165 MHz; agree the IEC 60533 zone with the
    integrator;
  - a short-circuit type test;
  - temperature rise at 45 °C air / 32 °C sea water.
- **Paperwork:**
  - E22 Category III software file;
  - a drive-level FMEA for the yard's FMEA;
  - a declared short-circuit contribution and withstand for the DC-grid selectivity study
    (IEC 61660).
- **One-time budget: ₹40–80 L** (planning; the Road DVT line is ₹35–70 L). It covers the test
  lab and class fees for M8, then an extension for M10.

## 12. Business case, roadmap, open gates

**Planning cost per cell** (ex-works, Road cost-rollup method, RFQ ±25 %):

| | M8 | M8-SiC | M10 |
|---|---|---|---|
| Electronics + build (Road method) | ≈ ₹0.90 L (= Road 8XX IGBT) | ≈ ₹1.15 L (= Road 8XX SiC) | ≈ ₹1.1 L — 1700 V modules at ≈1.4× the 1200 V part, 20 × 1300 V cans, certified barrier parts, new PCB (all RFQ) |
| Marine kit | ₹0.27–0.56 L | same | same |
| **Ex-works per cell** | **≈ ₹1.2–1.5 L** | ≈ ₹1.4–1.7 L | **≈ ₹1.4–1.7 L** |
| Per continuous kW (ferry rating) | ≈ ₹560–700 (214 kW) | ≈ ₹650–800 (214 kW) | ≈ ₹550–670 (254 kW) |

**Roadmap — spend NRE only where a market is proven.**

1. **Phase 1 — M8 (M8-SiC on request).**
   - Scope: marine kit, the MFW firmware and IRS/DNV type approval of the Road-fork hardware.
     No new PCB.
   - Serves water taxis, inland/coastal ferries and small craft at 20–450 kW per shaft (1–2
     cells), on 800 V-class packs or 400/440 V genset buses.
   - Brings marine references and the certificate trail.
2. **Phase 2 — M10.**
   - Scope: the 1100 V power stage, a new power PCB plus DVT, and a TA extension.
   - Serves ferries up to about 1.5 MW per shaft and 32–45 t tugs on 1000 V DC grids, with
     multi-set motors up to N = 6.
3. **Phase 3 — M10-2P.**
   - Build it on a committed 60–70 t tug programme (GTTP: 16 tugs by 2027, 50 by 2030).
   - Until then, quote tandem quad motors on M10 cells.
4. **Motor partner, in parallel with Phase 1.** A DUAL/QUAD PM motor with isolated neutrals, one
   resolver per set, the §6b back-EMF rule and I_ch ≈ 1 pu. Danfoss EM-PMI DUAL/QUAD is the
   benchmark to match.

**Open gates.** Each one is a WARN in the report or an RFQ:

1. **hiitio:**
   - 1700 V and SiC cosmic-ray FIT at the class voltages;
   - terminal RMS rating of the D3 modules (M8 300 A, M10 270 A);
   - power-cycling curves, which replace the LESIT estimate;
   - the datasheet defects in §4.
2. **Faratronic:** the 15 µF / 1300 V class can (≥ 15 A).
3. **Certified barrier parts for M10:** bias module and flyback transformer (≥ 1150 V DC
   working), an SO6L-class opto, and LEM sign-off on the 1100 V busbar sleeve.
4. **Class:**
   - current IRS Pt 4 Ch 8 and DNV texts;
   - HV test levels for 850–1200 V DC equipment;
   - BV's reading of 1.5 / 1.8 × U_P, before quoting BV-classed vessels.
5. **Motor partner:** per-set L_d/L_q/leakage, back-EMF harmonics, demagnetisation withstand,
   back-EMF at 115 % overspeed (≤ 836 / 1093 V), and N−1 thermal limits.
6. **Owners:** measured ferry and tug duty profiles. They set the tug rating.
7. **Bench:**
   - CAN-FD PWM sync accuracy;
   - 6ω loop on a DUAL motor;
   - 1 kHz standstill ripple;
   - M10 double-pulse test at 1100 V (sets RG);
   - contained short-circuit test on the 1700 V IGBT;
   - coldplate Rth (shared with Road).
8. **Before the Road PCB layout (which M8 inherits):** decide the creepage strategy, PD2 sealed
   or PD3.
