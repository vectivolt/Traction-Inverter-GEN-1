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
| Control power | E10 DC supply ±10 %, battery-fed +30/−25 % | ship 24 V → **isolated 24→12 V, 60 W** in the kit: the Road LV side is a 12 V (9–16 V) design, why in §11 (its LV budget is ≈25–30 W) |
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
| Short circuit | **open release gate**, as on the Road (RR04/③): detect + soft-off 4.81 µs at the typical 400 mA, 10.3 µs at the 100 mA DS minimum, vs 5 µs derated | same gate: 4.35 / 8.44 µs vs 4.8 µs derated (6 µs @1000 V → 1100 V) |

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
  hours, but the Road ASC holds only 0.5–3.1 ms after low-voltage loss. The cell's 24 V must
  therefore be class-grade and battery-backed, with two feeds on towed or single-screw vessels.
  - Since Road A.9 the power board's LV feed also follows the card's V5A (the QLVS switch), so the
    FS26 must stay awake while ASC is held. The marine firmware never parks it in LPOFF then, and
    an FS26 restart ends ASC like a 24 V loss.
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
| Back-EMF with the cell off | PM flux cannot be switched off, and a winding carrying current stores energy | motor spec: **E_LL,pk at 115 % overspeed ≤ 0.95 × OV trip**, cold magnets as in the Road rule (836 V M8, 1093 V M10), **and the winding-energy condition below** → pulse-off is safe at every speed with the DC grid connected; ASC for switch faults and a lost DC path (MFW-08); shaft lock when towed | report + motor spec |
| Genset-fed bus (hybrid tugs/ferries) | step-load acceptance of the gensets | power-limit and ramp commands from the PMS on CAN; a hardwired fast load-reduction input | firmware + PMS |
| Crash stop | regeneration up to rated power | regen capped at the BMS charge limit; EMS keeps SOC headroom; brake chopper only for genset-only or full-battery cases | system |
| N cells on one shaft | all winding sets must pull together | master broadcasts one torque ramp; a cell that fails to start leaves N−1 running | firmware |

**Winding energy — the second motor condition** (Road `firmware-contract.md` §6 rule (a), rev A.11,
R1-F01/R2-F08; applied in both matrix columns since round 13/A.12, F135). The back-EMF rule covers the steady rectified voltage only. At pulse-off with the DC
path lost, the diodes also rectify the stored winding energy ¾·(L_d·î_d² + L_q·î_q²) (= 1.5·L·I²)
into the isolated link, at any speed. The Road screen, with V₀ = the OV trip and C_min, is
V_pk ≤ E + √((V₀ − E)² + 2·W/C_min) ≤ U_N. The Marine links hold 32.8 J (M8: 880 → 1000 V at
291 µF) and 50.1 J (M10: 1150 → 1300 V at 273 µF).

A motor built to the §6 I_ch ≈ 1 pu rule, with ψ_f at the back-EMF limit and rated at 80 Hz, has
L_d ≈ 1.97 / 2.86 mH. At the 110 % current it stores 321 / 378 J, 8–10 times the headroom. Even at
E = 0 the link alone covers only ≈ 105 / 108 A rms, and back-EMF only raises V_pk. So such a motor
fails rule (a) by design and is released under Road rule (b):
- the DC grid stays connected through the cell's pulse-off intervals (DESAT recovery, V5GD/feed loss,
  24 V loss, MCU hang) for every opening cause, the cell's own faults and the vessel's
  fault-isolation logic included. Evidence: the IEC 61660 selectivity study (§11) and FAT — the
  firmware's keep_hv CAN request that holds the battery connected now follows actual reliance on
  this rule at any speed, not a speed-only heuristic (Road round 14/A.13, A12-R08);
- if the DC path does open with current flowing, FW-06 LS-ASC keeps the energy in the winding
  (MFW-08).

The real L_d/L_q(i), ψ_f and n_max are commissioning inputs (gate 5); a lower-inductance motor may
meet rule (a) outright.

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
| Battery breaker opens during regen | — | FW-06 (Road N9, round-7 chain): ≤ 15.6 µs to the ASC request, then ≤ 7.56 µs ASC entry (UCC14141-Q1 17.4 V low end, A.12) → zero torque + LS-ASC (MFW-08). Link peak 909 V (M8) / 1178 V (M10), 91 % of the cans' rating. A once-per-PWM-period V_DC sample would end at 1075 V / 1453 V. M10 uses the same bias module and opto, already rated ≥ 1150 V DC | report |
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
(both pass with margin). The QDIS-stuck-on case is 384/515 W total into 10 W parts with the
battery connected — **96 W per resistor on M8/M8-SiC, 103 W on M10**, above the Road A6-R11
qualification window of 70–100 W (round 17/A.16, `qualification-plan.md` QP-MA-05). It is bounded
exactly as on the Road: fire only with the DC breaker reported open; a
shorted QDIS is now detected at the next contactor opening → latched no-re-energise DTC +
contactor-open request, round 14/A.13, replacing the earlier precharge-only check. The no-flame
property is the bound part's own datasheet statement, not the qualification window, so it holds on
both sides of 70–100 W: the TT/Welwyn SQP10 sheet states the resistor **will not burn or emit
incandescent particles under any condition of applied temperature or overload** (the Yageo
alternate lacks this line — BOM note, `vendor-requests.md` VR-28). **Fail-open time** at the actual
power is a characterisation, not this release gate — QP-MA-05 runs the stuck-ON test at 103 W for
M10 (Road gate ㉖).

Two Road rules carry over, re-derived at A.12 for the UCC14141-Q1 bias: the QDIS gate is fed
through the kept 1.5 k/10 k divider, now **11.6–16.3 V** (low end set by the VOW3120's guaranteed
V_OH ≥ V_CC − 4 V); M10 uses the same part and divider, already rated ≥ 1150 V DC working. The
FW-16 boot self-test runs only at ≤ 0.1 J in the link: ≤ 26 / 24 mJ at a < 3 V reading, or after
QDIS for 2 τ = 1.4 / 1.64 s from a < 60 V reading. That top-up time is a Marine parameter-set value.

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

**How "separate" is enforced.** M8 is a *frozen fork* of the Road 8XX IGBT build at **rev A.19**.
The fork point moved from A.8 to A.11 on 2026-09-24, then to A.12 in the same pass, and to A.13,
then A.14, then A.15, then A.16, then A.17 (round 18) in the same pass, on 2026-09-25, then A.18 and A.19 (rounds 19–20) on 2026-09-26; no Marine unit is built or
type-approved yet, so none of these moves needed a class notification. M8 carries the Road fixes of
review rounds 7–20 (`../docs/review-A7-disposition.md` … `../docs/review-A19-disposition.md`). It has the same PCBs and
supply chain, but its own part number, its own firmware build, and a distinct identity resistor —
**RHWID 47 k (4.12 V on HW_ID, harness pin 2 since A.9)**, 0.68 V clear of the nearest Road code
(22 k = 3.44 V). Road firmware refuses a marine cell and marine firmware refuses a road inverter
(FW-01 mechanism; the marine ratings assume 45 °C coolant, the Road's 65 °C). A Road change
reaches M8 only through a marine ECO with class notification — a type-approved product does not
move with the automotive line.

**What A.9–A.19 brought** (Road `design-basis.md` §11i–§11s; A.12 = round 13,
`review-A12-disposition.md`; A.13 = round 14, `review-A13-disposition.md`; A.14 = round 15,
`review-A14-disposition.md`; A.15 = round 16, `review-A15-disposition.md`; A.16 = round 17, `review-A16-disposition.md`; A.17 = round 18 (rechecks of 4425af9),
`review-A16-disposition.md`):
- **A.9.** PSASC/PSQD bound as QA01C-18 (+18/−3 V, 16.9–20.9 V): ASC entry 7.52 µs, FW-06
  end-point 906 V (Marine 909 V, §6c). FLT/RDY pull-ups on V5GD (harness pin 1, read on PTB5).
  Discharge gate divider 1.5 k/10 k. FW-16 self-test energy-limited (≤ 0.1 J, §7). Anti-surge RFS4.
  Card-side LV-feed switch QLVS (parking drain ≤ 43 µA; its sustained-ASC consequence is in §6).
  Harness re-laid: V5GD 1, HW_ID 2, VBAT_H 19/20, VBAT_L 39/40. The motor/vehicle release rule
  (a)–(c) of the Road `firmware-contract.md` §6.
- **A.10.** RDY lines Schmitt-buffered (USCH3); RASCG a 0.33 W part.
- **A.11.** Release rule (a) becomes an energy inequality (§6b). Resolver exciter a real MFB
  (|H(10 kHz)| 1.85). V_DC bias = TI UCC12050 per channel behind its own 5 V LDO. Hall 100 k
  open-wire pull-downs, HC5FW drawn with its real terminals. Resolver inputs 10 k. LV-entry TVS bound
  as TPSMC24CA-VR (§11). CSB5 1 µF, ALM2402 HTSSOP14-PWP, on-board NTCs. FW-05 in instantaneous
  amperes, "BMS limit 0" split from "contactor open" (§10). Road barrier register §6a. Electronics BOM
  ₹70,913 SiC / ₹45,413 IGBT, −₹127 since A.8 and inside the §12 rounding.
- **A.12.** Barrier register gates ⑪ and ㉔ closed at BOM level: ASC/discharge optos Vishay
  **VOW3120-X017T** (V_IORM 1414 Vpk, DIN EN 60747-5-5), bias modules TI **UCC14141-Q1**
  (single-output configuration set to 18.0 V by a 62 k/10 k divider, regulated **17.4–18.6 V**),
  Y-caps Vishay **VY1472M63Y5UQ6TV0** (500 VAC / 1500 V DC) — all ≥ 1150 V DC, so the same parts
  also close M10's "certified bias module / SO6L-class opto" asks (§6a, §9); the residual is their
  certificate status (VDE/UL/CQC listed "planned" — check at PO). ASC entry now **7.56 µs** (was
  7.52 µs), release ≤ 1.06 µs (was 0.75 µs); QDIS gate divider (kept 1.5 k/10 k) now **11.6–16.3 V**;
  RASCL/RQDL **270 Ω 1 %** (10.8–15.8 mA, was 261 Ω); RASCG dissipation while ASC is held 88 mW.
  Resolver RSIN/RCOS/RSINF/RCOSF → **12 k** (holds the S32K39 3 mA injection limit with the MCU
  unpowered); CSINA2/CCOSA2 → **220 pF C0G** (S32K39 DS Table 38, was 100 pF); corner ≈ 23 kHz, −24°
  on both channels; channel matching is an EOL calibration item (bound 1.3°, Road verifier row). LV-entry boost is
  TPS55340**QRTERQ1** (-Q1): 38 V recommended / 40 V absolute — the commercial part's 34 V figure
  was the wrong reference and the "≥ 42 V boost" argument a false premise; the TVS long-pulse
  (400 ms) case is WARN (no datasheet rating beyond 1 ms — the OEM/ship supply source resistance is
  the limiter, not the boost). V_DC bias LDO (NCV4276C DPAK) thermal row: 58.5 K/W reference pad,
  0.76 W worst → Tj ≈ 129 °C at 85 °C (WARN; layout rule ≥ 1.2 in² 2 oz copper per LDO); production
  V_DC-bias part **UCC12051QDVERQ1** (AEC-Q100), UCC12050 is the proto fit. Safety contract §6: the
  winding-energy condition (rule (a)) now applies in **both matrix columns**; at n < n_x with the
  battery path lost the sink is FW-06 LS-ASC; rule (b) applies wherever (a) fails and is barred for
  rows whose premise is the lost battery — Marine's own "ASC at any speed" note (§6b, MFW-08)
  already matched this. MCU pin freeze: the S32K396 289-MAPBGA is fully bound (all 289 balls) and
  FS26's 48 pins verified — the Road control card (shared by M8 and M10) is no longer a
  symbolic-pin design. The 40-way harness connector is bound to Samtec IPL1-120-01-L-D-K /
  IPD1-20-D-K / CC79L-2024-01-L crimps, and the discharge resistors to TT/Welwyn SQP10-470RJB15 /
  -220RJB15 (the 470 Ω matches the marine active-discharge value, §7). The 4XX DC-link can
  (Faratronic C3D1U506KFAA382) is bound on a different Road board from M8's 8XX bank and does not
  change the M8/M10 cap-bank gates.

- **A.13.** Round 14 found the A.12 pin freeze was not actually closed, and hardened the shared
  firmware and analog front end; all of it applies through the shared control card. **MCU pin
  freeze, reopened and re-closed:** H5 is **V15** (the 1.5 V core input, now on V15S; A.12 had put
  5 V on it) and J7 is **V25** (the 2.5 V flash-regulator output, now with a 220 nF cap; A.12
  grounded it) — the whole ball map was re-derived against NXP's own GEN3 control-card net report
  (`NET-91122_C.net`, MCU U513, 244 connected balls) as an independent oracle: every supply ball
  and 164/165 port names agree, and the four signals that sat on GEN3-open balls moved to
  netlist-confirmed ones (HW_ID → B5/PTE0, NTC_A → T15/PTC11, MT2_SIG → D5/PTE26, ASC_REQ →
  U4/PTD7); the KiCad symbol's pin numbers are now the ball IDs, and a CBAL 1 nF was added at the
  ballast gate. **Firmware** (shared base, Marine parameter sets apply): a DESAT hold (CAL, 60 µs
  default) is now enforced inside the bridge module for every software caller, so the ISR can no
  longer drop MCU_GATE_EN ahead of the hardware's 22–53 µs FLT→DRV_EN soft-off delay — it
  comfortably clears both DESAT corners in §5/the report; the millisecond clock is now a 64-bit
  monotonic counter (the old wrapping 32-bit µs/1000 clock gave false CAN/BMS staleness after
  71.6 min, MFW-04); **keep_hv** now follows actual reliance on rule (b) at any speed instead of a
  speed-only heuristic — the behaviour §6b already assumed, now actually requested of the BMS/PMS;
  PWM fault-input routing left unbound is a build error, and gate-enable is fail-closed behind an
  arming-evidence record (route bound, config matches, protection locked, fault-route and
  OVP-route validated by an NVM EOL record); `torque_to_current` gained a final
  voltage-feasibility witness (infeasible → zero torque + speed-limit request + DTC); the KCL
  plausibility check gained a per-channel activity check; a stuck-on discharge (shorted QDIS,
  battery connected) is now detected at the next contactor opening → latched no-re-energise DTC +
  contactor-open request (§7, numbers unchanged). **Analog** (card, shared): the resolver
  excitation-monitor divider is now **REXM1/3 18 k, REXM2 42.2 k, REXM4 84.5 k** (was
  5.1 k/12 k/24 k) with a 220 pF C_AAF across the SDADC1 pair — a 35 V wire fault now injects
  1.96 mA into an unpowered pad, inside the 3 mA limit (was 5.5 mA, over it); the exciter outputs
  get an SMCJ8.5CA TVS at each connector node plus a 0.2 A PTC (MF-LSMF020) per line (the ALM2402
  output abs max is 18 V; the clamp sits at 10–11 V, below its 12.1 V rail + a diode, so no
  back-drive); the motor-temperature lines get SMAJ5.0A clamps and a bound fuse MPN. New bench
  gate ㉘ (§12) covers terminal-fault tests on each line, MCU on/off/standby. **Power board**
  (shared PCB): each V_DC-bias LDO (NCV4276C) now sits behind a 47 Ω 2512 ballast (R5LB/R5LC),
  because the production **UCC12051-Q1** (now the primary MPN; UCC12050 stays the proto fit) draws
  up to 80 mA at no load — a 96 mA budget puts the LDO at ≈ 0.5 W / Tj ≈ 116 °C, down from
  0.96 W / 141 °C. **UCC14141-Q1:** TI's VDE certificate (40058888) is issued and archived — the
  gate ㉔ component-level residual is closed (the opto and Y-cap certificates stay "planned", §12).

- **A.14.** Round 15 rechecked the A.13 push and found two defects its own round-14 verifier
  could not see, plus one firmware residual (`review-A14-disposition.md`, F159–F164); all apply
  through the shared control card. **Exciter terminal-fault protection, corrected:** the round-14
  TVS sat on the connector node, so an external battery fault fed it without passing through the
  PTC; each line is now amplifier output → **2.2 Ω (RSXP/RSXN)** → protected node (**SMCJ8.5CA**
  TVS to AGND, plus the excitation-monitor tap) → PTC → vehicle connector, and the PTC itself is
  **Bourns MF-MSMF020/33X, 1812** (the bullet above called it a 1206-package 0.2 A MF-LSMF020;
  33 V, 40 A, 0.02 s at 8 A, hot hold 0.09 A at 85 °C). With the PTC now in the fault path: 15.3 A
  at 24 V / 27.7 A at 35 V (≤ 40 A), TVS energy ≈ 3.4 / 3.2 J vs ≈ 8 J capability; with VEXD absent
  or cranking, the ≈11 V clamp back-drives the ALM2402's reverse output diode through the 2.2 Ω
  (≈ 5 A for ≈ 50 µs until the 22 µF rail cap charges, rail then ≈ 10.8 V, under the 18 V abs max)
  — a negative fault is an OEM allocation. Bench gate ㉘ widened to VEXD off/cranking/on × both
  polarities, PTC/TVS/reverse-rail currents measured separately (§12); resolver excitation
  7.0 V pp cold (≥ 6.5 V pp floor). **Firmware** (shared base): the target ADC map now carries the
  instance/subtype/channel triple from the generated board map instead of a single letter+number
  (NTC_A is a standard-class input, ADC5_S11); the contactor-loss detector — previously gated to
  n ≥ n_x, so a low-speed OPEN/INVALID contactor report never reached the MFW-08 battery-path-lost
  row — now fires at **any speed while armed** and clears ordinary torque permission in the same
  invocation; the §6 policy still picks zero-torque/current-control or ASC (not an unconditional
  all-gates-off). No power-stage change. **KiCad:** Road now also generates a native-KiCad5 zip
  (`kicad5/traction-native/`) beside the EasyEDA-import one; M8/M10 fabrication keeps using the
  EasyEDA-import folder unless a native-KiCad flow is wanted.

- **A.15.** Round 16 rechecked the A.14 push (`review-A15-disposition.md`, F167–F173): one real
  corner the round-15 rows did not carry, two interpretation errors in the round-15 fault rows and
  one part-data error; all apply through the shared control card. **Exciter amplitude, corrected:**
  the S32K39 SWG's maximum amplitude is a part spread (1.884/2.093/2.302 V pp; firmware trim only
  reduces it), so at the A.14 gain (|H| 1.85, 24 k) the low corner delivered 6.34 V pp at the
  resolver winding through the series resistor + PTC losses — under the 6.5 V pp floor. **The MFB
  feedback resistor REXA4 is now 28 k** (|H(10 kHz)| ≈ 2.09, f0 16.6 kHz): low corner 7.1 V pp at
  the winding. The FW-10 amplitude setpoint is defined at the **monitor plane** (7.2 V pp; the
  monitor taps the protected node before the PTC — winding = monitor × 0.964 cold, × 0.875 for an
  hour after a PTC trip, which FW-10's window then flags, as intended); the SWG needed is 1.84 V pp
  (≤ 1.884, 3 % headroom) and 1.91 V pk per amplifier output stays under the ALM2402's −40 °C slew
  ceiling (2.07 V pk); the trim ramps up from ≈ 1.5 V pp so the untrimmed SWG maximum never
  slew-limits. **Exciter terminal-fault protection, corrected again:** TVSEP/TVSEN is now the
  **unidirectional SMCJ8.5A** (cathode on the protected node) — the excitation never goes below
  ground, so a negative harness fault is carried by the TVS forward diode (I_FSM 200 A) and the
  amplifier's lower output diode sees < 0.3 A (the round-15 bidirectional SMCJ8.5CA put 4.4 A /
  0.9 J into a generic 1206); RSXP/RSXN are bound to **Panasonic ERJ-8ENF2R20V**. **Fault rows made
  honest:** the VEXD-absent back-drive is an exponential into 26.7 µF (4.9 A peak, τ 59 µs, 0.2 mJ
  in the diode) and is now **OPEN** pending the measured ALM2402 diode envelope (round 15 had read
  one RC time constant as the end of the pulse); the TVS-energy PASS is now **conditional** on the
  measured PTC clearing time (the SMCJ curve is supported only to 10 ms, 5.5 J) with a
  source-impedance allocation (≥ 0.27 Ω source + harness at 35 V, else the PTC's 40 A I_max is
  exceeded); the MF-MSMF020/33X PTC holds **0.07 A at 85 °C** (the A.14 text carried the unsuffixed
  part's 0.09 A) against ≈ 35 mA nominal / 60 mA assumed excitation. Bench gate ㉘ re-scoped
  accordingly (§12). **Firmware** (shared base): a deterministic phase-current acquisition contract
  (a failed ADC triplet no longer passes an uninitialised timestamp — the current is marked invalid
  through the sensor-failure path while V_DC and resolver acquisition keep running); resolver
  validity now expires on a per-tick frame-age check with a bounded hold (it used to stay valid
  indefinitely once new SDADC blocks stopped); the SDADC resolver frame is published only when all
  three channels of the same epoch have completed (a coherent-frame API — one SIN-DMA heartbeat used
  to tag all three channels fresh). No power-stage change.

- **A.16.** Round 17 ("gap closure") closed every remaining WARN/OPEN row and firmware "contract
  should say" item against a named vendor request (`vendor-requests.md`, VR-01…32), an OEM/motor/
  harness assumption (`interface-requirements.md`, IR-01…41) or an executable qualification
  procedure (`qualification-plan.md`, incl. **QP-MA-01…10** for the marine deltas) — nothing is
  closed by re-labelling (`review-A16-disposition.md`, F174–F189); all applies through the shared
  control card. **LV entry rebuilt from absorption to let-through:** every clamp on a KL30-derived
  net now sits dark at the vehicle's own central load-dump suppression point instead of trying to
  absorb the dump itself. **DTVSC → TPSMC33A-VR** at the pin node, backed by a new **DTVSC2 →
  TPSMC18A-VR** anti-series pair ahead of the reverse Schottky DREVC (the round-16 single TVS sat
  behind DREVC and let ISO 7637-2 pulse 1 avalanche it); **DTVH/DTVL → TPSMC33CA-VR**; a new **100
  µF/50 V hybrid-polymer CLVC3** (Panasonic EEH-ZC1H101P, AEC-Q200) holds the fast pulses off the
  rail; **FLVC is now a 5 A fuse (Bel 0680L5000-05)**, correctly sized for the whole inverter's LV
  current — the 3 A polyfuse it replaces nuisance-tripped hot at low KL30; **DIGN (US1M)** now sits
  ahead of the FS26 **WAKE1** sense divider, so a harness transient or a reversed KL15 no longer
  forces that pin past its −5 mA rating; **ULDO15 moves to a D2PAK-5 (NCV4276CDSADJR4G)** — the
  DPAK-5 case it replaces reached 156 °C at the 35 V/400 ms plateau; **RFS4 → ESR18EZPF1001** (1206,
  0.5 W), replacing the 0603 part a 24 V jump start with FS1B held ran at 1.47× nameplate. The
  interface requirement is now **IR-03: central load-dump suppression, Us* ≤ 36 V at the inverter's
  KL30 pin, no source-impedance constraint on the OEM** (every clamp is dark below 36.95 V, so the
  result no longer depends on how the vehicle makes Us*); test A (an unsuppressed dump) stays
  unsupported. **This does not transfer to the marine 24 V control network**: the kit's isolated
  24→12 V converter (§11), not this TVS network, is what faces ship power, and a ship's 24 V
  distribution has no alternator load-dump or starter jump-start event to test against — its
  transient/tolerance specification is **IEC 60092-504** (electrical installations in ships,
  control and instrumentation supply quality) and **IACS UR E10** (already this document's §2
  "Control power" row: ±10 % steady state, battery-fed +30/−25 %, i.e. 18–31.2 V from 24 V nominal).
  So neither the 36 V pin figure nor the FW-33 bands keyed to it (below) are ship-side numbers —
  they describe the Road card's own automotive net, which the marine converter stands between;
  QP-MA-06 is what actually qualifies the converter against the ship's ±30/−25 % band, not IR-02/
  IR-03. **Exciter terminal-fault protection, closed on paper:** the round-16 OPEN back-drive row is
  now a rated diversion (**DEXP/DEXN — Nexperia PMEG4050EP-Q**, AEC-Q101 Schottky, protected node →
  VEXD) and the round-16 conditional TVS-energy row is PASS at the upgraded **TVSEP/TVSEN —
  Littelfuse SMDJ8.5A-HRA** (3 kW, AEC-Q101, same SMC pad as the round-15 part); only the 35 V/
  PTC-current corner stays an accepted double event (a short coincident with a load-dump pulse,
  IR-16/IR-33) — bench gate ㉘ (§12) keeps the clearing measurement as a characterisation, not a
  release gate. **Firmware** (shared base): **FW-31** current-loop liveness (`cal_isns_stale_us`,
  200 µs default) catches a stopped current-loop trigger within one 1 ms task period instead of
  leaving the last duty cycle standing; **FW-32** a UDS service-lock routine (RoutineControl 0xF010
  behind a SecurityAccess 0x27 seed/key hook, fail-closed with no key function bound in the default
  build, three wrong keys lock out) replaces the stuck-on-QDIS lock's old silent clear; **FW-33** LV
  supply supervision reads VSUP through the FS26 AMUX and treats an overvoltage as information, not
  a fault, for as long as the vehicle profile allows — `cal_vsup_jump_max_v` 27 V tolerated
  `cal_vsup_ld_ms` 500 ms (load dump, IR-03) or at/below it for `cal_vsup_jump_ms` 65 s (jump start,
  IR-02), each a range-checked CAL, then the §6 command-lost ramp. **For the marine 24 V network
  these bands describe only what the FS26 sees behind the isolated converter — they are not a
  rating on ship power** (see above; QP-MA-06 is the actual ship-side qualification). The FS26
  watchdog answer is now timed from the task tick instead of a stamp that trails it: answers land
  **2.0 ms** apart (1890–2110 µs) inside the FS26's 1.579–2.857 ms window at every oscillator
  corner, and ASC exit waits a CAL (`cal_asc_release_ns`, 1.5 µs) plus the SKU dead time from the
  ASC_CLR edge, closing the shoot-through window the A.12 opto's faster release opened. **KiCad:** a
  third deliverable, **`kicad/traction/` in true KiCad 9/10 format**, generated from the
  native-KiCad5 sheets item by item and proved by `kicad-cli sch export netlist` against the built
  netlist, with mutation tests; M8/M10 fabrication still uses the EasyEDA-import folder unless a
  native/modern flow is wanted. KiCad 10.0.6 cannot resolve symbols from that EasyEDA-import folder
  at all (a CLI-verification dead end for that one folder, not a design defect). No power-stage
  change.
- **A.17.** Round 18 (three independent rechecks of the A.16 push 4425af9, Road `review-A17-disposition.md`, F190–F198):
  the round-17 diversion Schottky had been drawn on the protected node (its charging loop bypassed RSX: 27–99 A
  instead of the modelled 5 A) — moved onto the amplifier node, zero parts; every two-pin diode now numbers its
  cathode as pin 1 (the parts' and KiCad's convention); the exciter TVS/PTC rows recomputed at their worst corners
  (the 8 A / 20 ms bound applies only to a direct short; a sustained mid-impedance short with the ECU asleep is a
  bench gate with a layout rule); RSX an anti-surge 0.5 W part; the NCV4276C 40 V output rating no longer credited
  as reverse-current evidence (release gate + VR-33); firmware: fresh samples no longer aged against the earlier
  ISR clock on the target, cadence-locked resolver frames with a servicing deadline and self re-acquisition, the
  resolver chain-latency sign corrected (FW-34…36). Marine: the shared card inherits all of it; no marine cell
  change.
- **A.18.** Round 19 (three independent rechecks of the A.17 push e315bf1, Road `review-A18-disposition.md`, F199–F202):
  the exciter PTC-current row had been evaluated at 24 V while the interface allowed 26 V (41.6 A cold at 26 V / 0.05 Ω:
  the harness minima are now computed at every permitted voltage at a 5 % margin — Road IR-16 0.09 / 0.33 / 0.25 Ω); the
  ≥ 8 A TVS-energy row rested on an I⁻² trip-time law no sheet states — an explicit assumption now, a WARN closed by the
  measured clearing waveform (QP-RX-04 step 2 release criterion) and VR-16; the round-18 window row had printed 1–200 Ω
  for a criterion error (energy where the PTC trips, power where it never does: 14–83 Ω at 24 V); firmware: the resolver
  ring's time origin is anchored to the SWG start on the microsecond timer instead of the first completion callback, so an
  over-budget first or re-acquired callback can no longer become the reference (FW-35 rewritten). **Marine-specific:** the
  exciter terminal-fault model (`calculations/exciter-fault.mjs`, shared) is run at the ship's voltages in
  `marine-verify.mjs` §7 — the kit's isolated 11.4–12 V rail is the low-V_P case (3.8–4.3 W in the TVS with the 8.5 V
  class once the PTC has tripped and the card sleeps, 2.0–2.5 W with the SMDJ7.0A-HRA the shared card carries since this
  round, F203) — with QP-MA-11 (the sweep on the kit) and the Marine kit requirement below. No marine cell change.
- **A.19.** Round 20 (three rechecks of the A.18 push 00f6252, Road `review-A19-disposition.md`, F205–F207): nothing on
  the circuit — the exciter TVS alternate (the 8.5 V class, wrongly called "same electricals") and the Road 4XX discharge
  alternate (a 470 Ω copied into the 220 Ω row) corrected with ERC locks, the target checklist's firmware ID aligned, and
  every 8.5 V-era figure re-derived from the shared model. Marine: M8 uses the 8XX build (its 470 Ω discharge row was
  right); the shared card's TVS alternate correction applies; no marine cell change.

| Change | M8 | M10 |
|---|---|---|
| Power PCB | Road, unchanged | **new** — 1100 V creepage/clearance (conformal coat + slots), same D3 footprint |
| Modules | HCG600FH120D3E1EA | HCG600FH170D3E1 / E1A |
| DESAT string | 2 × US1M, 4.7 k, 82 pF | **3 × US1M**, 4.7 k, 82 pF (trip 3.65–6.55 V vs 1.92 V VCEsat) |
| Gate resistors | Road IGBT 1.0/1.0 Ω | start 1.0/1.0 Ω (DS point) — set by DPT at 1100 V |
| Flyback transformer | VGT12EEM (Road gate ⑤) | **transformer certified ≥ 1150 V DC working** |
| Isolated bias (ASC/QDIS) | UCC14141-Q1 (Road gate ㉔, A.12) | **same part** — V_IOWM 1414 V DC ≥ 1150 V; ASC entry time and QDIS gate divider re-derived for its output |
| V_DC-channel bias | UCC12050 per channel (Road A.11; production AVL UCC12051QDVERQ1, A.12) | same (V_IOWM 1697 V DC ≥ 1150 V) |
| Opto (UQD, UASC) | VOW3120-X017T, Road gate ⑪ (N8, A.12) | **same part** — V_IORM 1414 Vpk ≥ 1150 V |
| Phase sensors | HC5FW 900-S + busbar sleeve (N7, Road gate ⑩) | same + sleeve rated for 1100 V, LEM sign-off |
| Y-caps | VY1472M63Y5UQ6TV0 (Road gate ㉔, A.12) | **same part** — 1500 V DC ≥ 1150 V; Y1/500 VAC class is for the 690 V AC grid case |
| Road A.9–A.13 power-side changes | included | carried into the new power and discharge PCBs: harness map (V5GD 1, HW_ID 2, VBAT_H 19/20, VBAT_L 39/40), UCC12050 V_DC bias, RASCG 0.33 W part (88 mW actual), 50 V CASC/CQD, TPSMC24CA-VR on both LV feeds, the QDIS gate divider (now 11.6–16.3 V), TPS55340-Q1 real ratings (38 V rec / 40 V abs), 47 Ω 2512 ballast per V_DC-bias LDO for the production UCC12051-Q1 (A.13) |
| Cap bank / discharge | Road | §7 values, new busbar drawing |
| V_DC sense divider | 6 × 1206 per channel (142 V each at 850 V) | **8 × 1206** per channel (138 V each at 1100 V; 6 would put 183 V = 92 % of 200 V on each) |
| Control card | Road (RHWID code only) | Road |
| M8-SiC | = Road 8XX SiC build, RHWID 30 k | — |
| Identity (HW_ID, MFW-01) | RHWID 47 k → 4.12 V | RHWID 1 k → 0.45 V |
| Enclosure / environment | marine enclosure, conformal coat, anti-condensation heater, DC fuses, marine connectors — §11 | same |

**Insulation.** The barrier list is the Road register ([`docs/design-basis.md`](../docs/design-basis.md)
§6a, rev A.12) and is not restated here. The report's *Insulation* section applies it at 850 V (M8)
and 1150 V (M10). Round 13 bound gates ⑪ and ㉔ to orderable parts (Vishay VOW3120-X017T opto, TI
UCC14141-Q1 bias, Vishay VY1472M63Y5UQ6TV0 Y-caps), all already rated ≥ 1150 V DC — so M10 needs no
separate certified part for these, only their certificate status (VDE/UL/CQC, listed "planned")
checked at PO. Round 14 (A.13) closed the UCC14141-Q1 half of that residual: TI's VDE certificate
(40058888) is issued and archived; the opto and Y-cap certificates are still "planned," check at
PO. The register's remaining open gates ask for statements at the Road's 850 V DC:
- ⑤ VGT12EEM flyback transformer;
- ⑩ HC5FW sleeve (N7).

Marine's 1000 V grids make each of them stricter, not looser. M10 needs the same statements at
≥ 1150 V DC (its OV trip), and on an IT network a first earth fault leaves the whole link across one
Y-cap — the VY1472's 1500 V DC rating still covers that at 1150 V; its Y1/500 VAC class is sized for
the 690 V AC grid case, not the DC bus. The A.11 UCC12050 closes the V_DC-channel bias for both
classes (V_IOWM 1697 V DC; production AVL part UCC12051QDVERQ1, A.12). PCB creepage is the
insulation-coordination study; conformal coating is the marine baseline anyway (humidity, salt).

## 10. Firmware — Marine additions to the Road contract

The Road contract ([`docs/firmware-contract.md`](../docs/firmware-contract.md), kept at the current Road rev — FW-01…FW-36 as of A.17)
applies unchanged, with Marine parameter-set values. FW-05 trips at 1.25 × √2 × the 110 % current in
instantaneous amperes: ±583 A (M8) / ±525 A (M10). The FW-16 QDIS top-up is §7's. The marine build
adds:

| # | Requirement | Source |
|---|---|---|
| MFW-01 | **Identity.** HW_ID codes: M8 IGBT 47 k (4.12 V), M8-SiC 30 k (3.75 V; 100 k sat at 4.55 V, inside the > 4.6 V open-pin fault window — round 13), M10 1 k (0.45 V). All are clear of the Road codes (0.90/1.60/2.50/3.44 V). A cross-series parameter set refuses to enable the gates. FW-02's τ check is only a plausibility check since Road A.11: M10's nominal τ (2350 Ω × 303 µF ≈ 0.71 s) equals the Road 4XX value, so the M10 cap-bank/discharge kit is proven by traceability and the EOL C/R measurement (Road `dfm.md` §4). HW_ID now lands on a netlist-confirmed MCU ball (B5/PTE0) instead of a GEN3-open one — the resistor codes and thresholds are unchanged (round 14/A.13) | FW-01 mechanism; FW-02 |
| MFW-02 | **Duty class.** Each parameter set carries its ratings and its duty class: continuous (ferry) or tug. The 110 % / 60 s overload runs on an I²t budget | §3, §5 |
| MFW-03 | **Reduce power instead of tripping.** Coolant temperature, leak, module NTC, DC-window edges, BMS and PMS limits reduce power, give a pre-warning and broadcast "power limitation". Trip only for damaging faults (DESAT, OV, hardware) | DNV/BV/ABS/LR (§2) |
| MFW-04 | **BMS contract.** Obey the charge/discharge current and voltage limits; cap regeneration at the charge limit; raise an alarm on converter failure. A charge limit of 0 with the battery connected is not a lost DC path: regen ramps to zero with current control kept, and FW-06 stays armed as the backstop (Road §6, round 12). FW-06 runs the Road chain: ≤ 15.6 µs to the ASC request on a free-running V_DC slot, then ≤ 7.56 µs ASC entry (Road N9, round 7; timing re-derived round 13/A.12 for the UCC14141-Q1). The millisecond clock behind every staleness/timeout check is now a 64-bit monotonic counter (round 14/A.13): the old wrapping 32-bit µs/1000 clock gave false staleness after 71.6 min, a runtime that a ferry or tug duty cycle reaches routinely | IRS battery guidelines Rev.3; §6c |
| MFW-05 | **PMS contract.** Power-limit and ramp commands on CAN, plus a hardwired fast load-reduction input to protect gensets | §6b |
| MFW-06 | **Standstill.** Switch at 1 kHz below 2 Hz. A stall timer (≥ 50 % current at < 1 % speed for more than 10 s) warns, then derates | §6b (verified) |
| MFW-07 | **Flying start** from the resolver angle and speed | §6b |
| MFW-08 | **Safe state per set.** Pulse-off at every speed with the DC grid connected, relying on the §6b motor rule (back-EMF, and the winding energy under Road rule (b)); the cell never opens its own DC entry while its set carries current. ASC for a switch fault in that cell follows the Road matrix: LS-ASC for a shorted low-side switch; a shorted high-side switch gets pulse-off plus **automatic opening of the set's disconnector**. LS-ASC also when FW-06 finds the DC path lost with current flowing, **at any speed** — the Road matrix itself now fires this at n < n_x too (round 13/A.12, F135: the energy condition applies in both columns, so the battery-path-lost row below n_x also uses FW-06 LS-ASC as the sink), matching what Marine already required because its motor never reaches n_x under the back-EMF rule; the contactor-loss detector feeding this row was itself gated to n ≥ n_x until round 15/A.14 (F164), which now flags an OPEN/INVALID/stale contactor report at any speed and clears ordinary torque permission in the same invocation; release to pulse-off once the current is gone. Healthy sets clamp to their N−1 limits and do not trip. Towing/windmilling raises no alarm from regenerated power. Shaft-lock interlock | DNV, KR (§2), §6, §6b |
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
| Isolated 24 → 12 V, 60 W (EN 50155 / IEC 60945 class), fed from **class-grade, battery-backed 24 V**; diode-OR of two feeds where the shaft can be towed | The Road LV side is a 12 V (KL30 9–16 V) design. Since round 17/A.16 its entry TVS is a 33 V-class network (DTVSC → TPSMC33A-VR, DTVH/DTVL → TPSMC33CA-VR; knee ≈ 34.9–36.95 V, up from the TPSMC24CA-VR's 26.7–29.5 V through A.15) — dark at 24 V with more margin than before, but that was never the limit. More decisive, the power board's V15 rail is a boost (TPS55340-Q1), which cannot step 18–31 V down. It passes the input through, and the ULDO15 post-regulator then dissipates ≈ 2.8 W at 24 V into thermal shutdown (Road F51: a survival case, not an operating mode). That drops V15, which feeds the UCC14141-Q1 ASC/discharge bias (8–18 V input, A.12; V15 sits well inside) and the UCC12050 V_DC bias. The flybacks and LDOs are rated and benched at 9–16 V, 24 V only as a 60 s jump start (Road gate ⑬). Sustained ASC needs gate power (§6) | 4–8 k (+1 k second feed) |
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

**Exciter terminal fault on the Marine LV (round 19 / A.18).** The shared card's resolver-excitation protection (RSX →
SMDJ7.0A-HRA on the protected node since round 19 → MF-MSMF020/33X per line) is evaluated at the ship's voltages in
[`verification-report.md`](verification-report.md) §7: a direct short of an excitation line to the ship bus (18–31.2 V)
stays inside the PTC's 40 A only with **≥ 0.27 Ω on the fault loop** — a battery-backed bus will not present that, so it is
a **routing/segregation rule for the kit harness** (the resolver pair never runs with the 24 V bus) or a series element in
the kit, ours either way, not an OEM allocation; a sustained short to the **kit's 11.4–12 V rail with the card asleep**
leaves the tripped PTC as a thermostat passing V_BR·min(P_d/(V_S − V_BR), I_trip) into the TVS — 3.8–4.3 W with the
8.5 V class of round 18, the low-V_P case that drove the Road round-19 class change to **SMDJ7.0A-HRA** on the shared card
(2.0–2.5 W, and ≤ 132 °C junction with the PTC coupled on the TVS island, where the 8.5 V class stayed at 148–171 °C —
`xcheck19`); closed on the bench by **QP-MA-11** (the QP-RX-04 sweep at the ship's voltages, −25 / 55 °C points) with the
Road layout island (the pair's transfers) inherited on the card; the Marine operating concept keeps the card awake while
the rail is present (the awake amplifier sinks the trickle), an operating measure, not a closure.

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
   resolver per set, the §6b back-EMF and winding-energy conditions, and I_ch ≈ 1 pu. Danfoss EM-PMI DUAL/QUAD is the
   benchmark to match.

**Open gates.** Each one is a WARN in the report or an RFQ:

1. **hiitio:**
   - 1700 V and SiC cosmic-ray FIT at the class voltages;
   - terminal RMS rating of the D3 modules (M8 300 A, M10 270 A);
   - power-cycling curves, which replace the LESIT estimate;
   - the short-circuit statement at 850 V (M8) and 1100 V (M10) at the 16.9 V gate-rail corner
     (Road RR04/③, round 12);
   - the datasheet defects in §4.
2. **Faratronic:** the 15 µF / 1300 V class can (≥ 15 A).
3. **Certified barrier parts for M10:** the flyback transformer (≥ 1150 V DC working, Road gate
   ⑤) and LEM sign-off on the 1100 V busbar sleeve. Round 13 bound the bias module (TI
   UCC14141-Q1), opto (Vishay VOW3120-X017T) and Y-caps (Vishay VY1472M63Y5UQ6TV0) to parts
   already rated ≥ 1150 V DC, so gates ⑪/㉔ close at the same working-voltage ask — the residual
   is their certificate status (VDE/UL/CQC, listed "planned") at PO. Round 14 (A.13) closed the
   UCC14141-Q1 side (VDE certificate 40058888, archived); the opto and Y-cap certificates remain
   "planned."
4. **Class:**
   - current IRS Pt 4 Ch 8 and DNV texts;
   - HV test levels for 850–1200 V DC equipment;
   - BV's reading of 1.5 / 1.8 × U_P, before quoting BV-classed vessels.
5. **Motor partner:** per-set L_d/L_q/leakage, back-EMF harmonics, demagnetisation withstand,
   back-EMF at 115 % overspeed (≤ 836 / 1093 V), and N−1 thermal limits. L_d/L_q(i) and ψ_f also
   set the §6b winding energy; per vessel, the rule-(b) evidence is the IEC 61660 study and FAT.
6. **Owners:** measured ferry and tug duty profiles. They set the tug rating.
7. **Bench:**
   - CAN-FD PWM sync accuracy;
   - 6ω loop on a DUAL motor;
   - 1 kHz standstill ripple;
   - M10 double-pulse test at 1100 V (sets RG);
   - contained short-circuit test on the 1700 V IGBT (M8: the Road's, gate ③);
   - terminal-fault test on the resolver excitation and motor-temperature lines, VEXD
     off/cranking/on and both polarities, PTC/TVS/reverse-rail currents measured separately
     (Road gate ㉘, shared control card, widened round 15/A.14, re-scoped round 16/A.15, closed on
     paper round 17/A.16 for the sheet-bounded single fault; round 18/A.17 restored two release
     gates — the reverse rail current into the exciter LDO (the diversion now passes RSX) and the
     sustained-short impedance sweep with the ECU asleep, where the tripped PTC's trickle leaves
     1.5–4 W in the TVS — and the 35 V/PTC-current corner stays an accepted double event; Marine
     inherits both gates unchanged, the card being shared);
   - coldplate Rth (shared with Road).
8. **Before the Road PCB layout (which M8 inherits):** decide the creepage strategy, PD2 sealed
   or PD3.
