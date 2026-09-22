# HIITIO Power-Module Datasheet Extraction — Marine Traction-Inverter Study

Source PDFs were downloaded from the URLs given in the task and saved unmodified in this
work folder. Page numbers below are the datasheet's own printed "Page N of M" footer.
**Every value is quoted exactly as printed** (unit, min/typ/max column, and test condition);
nothing is filled from general component knowledge. Where a datasheet is silent, or where a
number could not be read with confidence, this is written **NOT STATED** rather than guessed.

**Curve‑reading caveat (read this before trusting any number below marked "approx.")**
Several fields (E vs Rg, E vs Ic, Vgs vs Qg, RDS(on) vs Tj/Id, RBSOA) exist only as small
printed graphs, not as tables. These were re‑rendered from the PDF at 600 DPI and cropped for
legibility (files kept in `zoom/` next to this report). Even at that resolution, hand‑reading a
curve carries real error, and — importantly — **cross‑checking curve readings against table
values exposed a genuine inconsistency inside HIITIO's own datasheets**: for the two 900 A‑class
parts (HCG900FH120D3RC and HCG900FH120D3E1A) the plotted "typical" E = f(Ic) curve reads
roughly 2–4× **higher** than the Table‑5 switching‑energy value at the one current (900 A) where
curve and table overlap. For the two 600 A‑class parts (HCG600FH170D3E1 and …E1A) the curve at
600 A is within ~15% of the table. This is documented in "Data‑quality notes" at the end. Because
of this, curve‑derived numbers are labelled **"approx., read from Fig. X"** and are not to be
treated as spec values; the tabulated numbers are the authoritative ones.

---

## Reference outline / pin map used for comparison

152 × 62 mm "D3" package. Pins: 1 = LS gate, 2 = LS aux emitter/source, 3 = DC−, 4 = DC+,
5/6 = NTC, 7 = HS gate, 8 = HS aux emitter/source, 9 = HS collector/drain sense, 10/11 = AC.

---

# 1. HCG900FH120D3RC — 1200 V / 900 A IGBT module ("Econo Dual 3H")

Datasheet Rev. A, 13 pages, Zhejiang HIITIO New Energy Co., Ltd.

## 1.1 Package / pin map vs. reference — **SAME**

Package-outline drawing (p.12) dimension chain, confirmed at 600 DPI: width ①94.5±0.3 →
③110±0.3 → ⑤122±0.3 → ⑦137±0.4 → ⑨152±0.5 mm; height ⑧22±0.3 → ⑩38.6±0.3 → ④50±0.2 →
②57.5±0.3 → ⑥62±0.2 mm → **152 × 62 mm, matches reference exactly.**
Circuit diagram (p.11) + outline (p.12) pin map: 4 = top/DC+, 9 = HS collector‑sense tap, 7 = HS
gate, 8 = HS aux‑emitter, 10/11 = AC midpoint, 1 = LS gate, 2 = LS aux‑emitter, 3 = bottom/DC−,
5/6 = NTC — **identical to the reference map, pin‑for‑pin.**

## 1.2 Absolute maximum ratings (Table 3, p.4; Table 2, p.3)

| Parameter | Symbol | Condition | Value | Unit |
|---|---|---|---|---|
| Collector‑emitter voltage | VCES | Tvj=25°C | 1200 | V |
| Continuous DC collector current | ICDC | TH=85°C, Tvjmax=175°C | 900 | A |
| Repetitive peak collector current | ICRM | tp=1 ms | 1800 | A |
| Total power dissipation | Ptot | TH=85°C, Tvjmax=175°C | 947 | W |
| Gate‑emitter peak voltage | VGES | — | ±20 | V |
| Storage temperature | Tstg | — | −40…125 | °C |
| Max junction temperature | Tvjmax | — | 175 | °C |

Only a single junction‑temperature limit (Tvjmax = 175 °C) is printed; there is **no separate
"Tvjop" continuous‑operation figure** in this datasheet, so no explicit overload‑allowance band
is stated (**NOT STATED**).

## 1.3 Static characteristics (Table 4, p.4)

| Parameter | Condition | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| VCEsat | IC=900A, VGE=15V, Tvj=25°C | — | 1.67 | — | V |
| VCEsat | IC=900A, VGE=15V, Tvj=125°C | — | 1.86 | — | V |
| VCEsat | IC=900A, VGE=15V, Tvj=150°C | — | 2.00 | — | V |
| Gate threshold VGEth | IC=18mA, VGE=VCE, Tvj=25°C | 5.0 | 5.85 | 7.0 | V |
| Gate charge QG | VGE=±15V, VCE=600V | — | 11.2 | — | µC |
| Internal gate resistor RGint | Tvj=25°C | — | 0.3 | — | Ω |
| Cies | f=1MHz, VCE=25V, VGE=0V | — | 200 | — | nF |
| Coes | f=1MHz, VCE=25V, VGE=0V | — | **TBD (printed literally as "TBD" in the datasheet)** | — | nF |
| Cres | f=1MHz, VCE=25V, VGE=0V | — | 0.57 | — | nF |
| ICES | VCE=1200V, VGE=0V, Tvj=25°C | — | — | 0.1 | mA |
| IGES | VCE=0V, VGE=20V | — | — | 500 | nA |

The VCEsat table does not label the numbers "chip" or "terminal" — **NOT STATED which reference
point** (contrast with parts 3 and 5, which give both explicitly).

## 1.4 Diode ratings (Table 6/7, p.6)

| Parameter | Condition | Value | Unit |
|---|---|---|---|
| VRRM | Tvj=25°C | 1200 | V |
| IF (continuous) | — | 900 | A |
| IFRM | tp=1 ms | 1800 | A |
| VF | IF=900A, VGE=0V, Tvj=25/125/150°C | 2.10 / 2.35 / 2.40 | V |
| Irrm | Tvj=25/125/150°C (VR=600V, IF=900A, VGE=−15V, RGon=0.5Ω, Ls=30nH) | 430 / 485 / 490 | A |
| Qr | same conditions | 60 / 105 / 115 | µC |
| Erec | same conditions | 24.8 / 42.2 / 46.3 | mJ |
| RthJH (per diode) | λgrease=3.4 W/(m·K) | 0.13 | K/W |

VF is likewise not split chip/terminal (**NOT STATED**).

## 1.5 Switching dynamics (Table 4/5, p.4–5)

Condition: VCE=600V, IC=900A, VGE=±15V, RGon=RGoff=0.5Ω, Ls=30nH (stray inductance of the
**test fixture**, not the module — see §1.7).

| Tvj | tdon (ns) | tr (ns) | tdoff (ns) | tf (ns) | Eon (mJ) | Eoff (mJ) |
|---|---|---|---|---|---|---|
| 25°C | 215 | 87 | 360 | 86 | 91.2 | 77.0 |
| 125°C | 240 | 100 | 400 | 135 | 122.9 | 98.2 |
| 150°C | 320 | 105 | 410 | 145 | 132.8 | 104.6 |

E = f(Ic) and E = f(Rg) curves exist (p.8 bottom‑right, p.9 top‑left, same VCE/RG/VGE family) but
**their reading at Ic=900 A does not match the table above** — see the global data‑quality note.
No numeric values are quoted from these two curves for that reason.

## 1.6 Short‑circuit data (Table 5, p.5)

VCC=600V, VGE=15V: ISC = 4550 A @ Tvj=25°C, tp≤10 µs; ISC = 3550 A @ Tvj=150°C, tp≤8 µs.

## 1.7 Reverse‑bias SOA, stray inductance, module lead resistance

RBSOA figure (p.10, bottom‑left; RGoff=0.5Ω, Tvj=150°C): chip and module Ic‑vs‑VCE traces both
collapse steeply near VCE ≈ 1100–1200 V (≈ VCES); Ic reaches up to ~1800 A at low VCE. No numeric
table accompanies this curve.

Module stray inductance LσCE = 20 nH **typ** (device parameter, Table 2 p.3). Separately, the
switching‑loss test bench (§1.5, §1.6) used an assumed **fixture** inductance Ls = 30 nH — a
different, test‑circuit number, not the module spec.
Module lead resistance RCC'+EE' = 0.8 mΩ **typ** per switch, TF=25°C (Table 2, p.3).

## 1.8 Thermal

| Parameter | Condition | Value | Unit |
|---|---|---|---|
| RthJH, IGBT (junction‑to‑heatsink) | per IGBT, λgrease=3.4 W/(m·K) | 0.095 | K/W |
| RthJH, diode | per diode, same grease assumption | 0.13 | K/W |

This is a **junction‑to‑heatsink** figure that already lumps contact resistance under an assumed
3.4 W/(m·K) grease — there is no separate RthJC / RthCS breakout (**NOT STATED** separately).
Transient thermal impedance is given only as a **duty‑factor family of curves** (D = 50% → 0.5%,
p.10) for IGBT and FRD; **no 4‑term Foster r/t coefficient table is printed** for this part
(**NOT STATED** numerically) — contrast parts 3 and 5.

## 1.9 Module / mechanical (Table 1, p.3)

| Parameter | Value |
|---|---|
| Isolation test voltage VISOL | 3.0 kV RMS, 50 Hz, 60 s |
| Baseplate material | Cu |
| Internal insulation | Al2O3, "Basic insulation (class 1, IEC 61140)" |
| Creepage, terminal–heatsink / terminal–terminal | 14.5 mm / 13.0 mm |
| Clearance, terminal–heatsink / terminal–terminal | 12.5 mm / 10.0 mm |
| CTI | > 200 |
| Mounting torque, module (M5) / terminal (M6) | 3–6 N·m / 3–6 N·m |
| Weight | 345 g (typ column only) |

## 1.10 NTC (Table 8, p.7)

R25 = 5 kΩ typ; ΔR/R at R100=493Ω, TNTC=100°C: −5…+5 %; P25 max = 20 mW;
B25/50 = 3375 K, B25/80 = 3411 K, B25/100 = 3433 K.

## 1.11 RC (reverse‑conducting) vs. separate diode dies

The circuit diagram (p.11) draws a conventional discrete anti‑parallel diode symbol next to each
IGBT, and the Diode section has its **own** ratings/thermal tables (Tables 6/7, p.6) with an
RthJH (0.13 K/W) that **differs** from the IGBT's RthJH (0.095 K/W). If IGBT and diode shared one
monolithic reverse‑conducting die, a shared junction‑to‑heatsink path would be expected to track
more closely. This datasheet's own presentation therefore points to **co‑packaged discrete FRD
dice**, not a monolithic RC‑IGBT chip — despite the "RC" in the part number. HIITIO does not
define what "RC" stands for anywhere in this document (**NOT STATED**).

---

# 2. HCG600FH170D3E1 — 1700 V / 600 A IGBT module

7 pages. Revision‑history table (p.7) shows only a placeholder "RevX.0.1 – Released" — no
dated revision letter is printed anywhere in this file.

## 2.1 Package / pin map vs. reference — **SAME**

Outline (p.6), confirmed at 600 DPI: ①94.5±0.3 → ③110±0.3 → ⑤122±0.3 → ⑦137±0.4 → ⑨152±0.5 mm
(width); ⑧22±0.3 → ⑩38.6±0.3 → ④50±0.2 → ②57.5±0.3 → ⑥62±0.2 mm (height) → **152 × 62 mm**.
Circuit diagram pin numbering identical to part 1 (4=DC+, 9=HS sense, 7=HS gate, 8=HS
aux‑emitter, 10/11=AC, 1=LS gate, 2=LS aux‑emitter, 3=DC−, 5/6=NTC). **Matches reference.**

## 2.2 Headline (Table 1, p.1) and absolute maximum ratings (Table 2, p.2)

Table 1: VCE=1700V, IC(Tc=90°C)=600A, VCEsat(Tvj=25°C,IC=600A,VGE=15V)=1.68V, Tvjmax=175°C,
Package = "D3".

| Parameter | Symbol | Condition | Value | Unit |
|---|---|---|---|---|
| Collector‑emitter voltage | VCE | Tvj=25°C | 1700 | V |
| Continuous DC collector current | ICDC | Tc=90°C | 600 | A |
| Repetitive peak collector current | ICRM | tp=1ms | 1200 | A |
| Continuous DC forward current | IF | — | 600 | A |
| Repetitive peak forward current | IFRM | tp=1ms | 1200 | A |
| Gate source voltage | VGE | Tvj=25°C | ±20 | V |
| Junction temperature | Tvj | — | −40…+175 | °C |
| Storage temperature | Tstg | — | −40…+125 | °C |
| Operating virtual junction temp. | **Tvjop** | — | **150** | °C |

Tvjop (150 °C) is 25 °C **below** Tvjmax (175 °C) — a real derating/overload‑allowance band.

## 2.3 Thermal resistance (Table 3, p.2)

RthJC, IGBT = 0.039 K/W typ (per IGBT); RthJC, diode = 0.057 K/W typ (per diode). This is
junction‑to‑**case**, not heatsink; no contact‑resistance or grease figure is given (**NOT
STATED**).

## 2.4 Static characteristics (Table 4, p.3)

| Parameter | Condition | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| V(BR)CES | Tvj=25°C | 1700 | — | — | V |
| VCEsat (chip) | VGE=15V, IC=600A, Tvj=25°C | — | 1.68 | — | V |
| VCEsat (chip) | VGE=15V, IC=600A, Tvj=175°C | — | 2.16 | — | V |
| VF (chip) | VGE=0V, IC=600A, Tvj=25°C | — | 1.79 | — | V |
| VF (chip) | VGE=0V, IC=600A, Tvj=175°C | — | 1.89 | — | V |
| VGE(th) | VCE=VGE, IC=24mA | — | 6.07 | — | V |
| ICES | **VCE=1200V** (see note), VGE=0V, Tvj=25°C | — | — | 100 | µA |
| IGES | VGE=20V, VCE=0V | — | — | 100 | nA |
| Cies / Coes / Cres | f=100kHz, VCE=25V, VGE=0V | — | 79.2 / 2.93 / 0.39 | — | nF/nF/nF |
| Gate input resistance RG | f=1MHz | — | 1.66 | — | Ω |
| Gate charge QG | VGE=−15V→15V, VCE=900V | — | 3.01 | — | µC |

**Note on ICES**: the datasheet tests ICES at VCE = 1200 V on a part rated VCES = 1700 V. Quoted
exactly as printed; this looks like a template/typo artifact but is reported as‑is, not
"corrected." No terminal‑level VCEsat/VF value is given, only chip.

## 2.5 Switching dynamics (Table 5, p.4)

Condition: VCC=900V, IC=600A, VGE=−15/15V, RGon=RGoff=1Ω. No stray‑inductance (Ls) value is
stated for this test bench (**NOT STATED**, unlike part 1).

| Tvj | tdon (ns) | tr (ns) | tdoff (ns) | tf (ns) | Eon (mJ) | Eoff (mJ) | Ets (mJ) |
|---|---|---|---|---|---|---|---|
| 25°C | 410 | 75 | 500 | 380 | 102.8 | 115.5 | 218.3 |
| 175°C | 440 | 100 | 560 | 600 | 197.6 | 156.5 | 354.1 |

ISC = 3200 A @ Tvj=175°C, VCC=1000V, VGE=15V, tp≤6 µs (Table 5, p.4).

## 2.6 Diode recovery (Table 6, p.4)

| Tvj | Trr (ns) | Irrm (A) | Qrr (µC) | Erec (mJ) |
|---|---|---|---|---|
| 25°C | 230 | 750 | 100 | 49.7 |
| 175°C | 370 | 706 | 130 | 52.2 |

## 2.7 Module / mechanical (Table 7, p.5) and NTC (Table 8, p.5)

VISOL = 4.0 kV (RMS, 50 Hz, 1 min); baseplate = Cu+Ni; internal isolation = Al2O3 (basic
insulation, no IEC clause cited); Ms (M5) = Mt (M6) = 3.0–6.0 N·m; CTI > 175. **No creepage or
clearance rows, and no weight row, appear anywhere in this datasheet (NOT STATED both).**
No module stray‑inductance row either (**NOT STATED**).
NTC: R25=5kΩ, ΔR/R(R100=493Ω,100°C)=−5…+5%, **only B25/50=3375K is given** (no B25/80 or
B25/100, unlike part 1).

## 2.8 No graphical characterization section at all

This 7‑page datasheet goes straight from Table 7/8 (p.5) to Package Information (p.6) to
Revision History (p.7). **There is no output/transfer‑characteristic curve, no
capacitance‑vs‑VCE curve, no gate‑charge curve, no E‑vs‑Rg or E‑vs‑Ic curve, no RBSOA curve, and
no transient thermal‑impedance curve anywhere in this document** (confirmed by reading every
page). All of the above are **NOT STATED / not provided** for this part. This is the single
biggest structural gap versus part 3 below.

## 2.9 RC vs. separate diode

Standard discrete anti‑parallel FRD (conventional diode symbol, p.6 circuit diagram); no "RC"
naming ambiguity for this part.

---

# 3. HCG600FH170D3E1A — 1700 V / 600 A IGBT module (E1A variant)

9 pages. Same voltage/current class as part 2; this section gives full data, then a dedicated
**"how #3 differs from #2"** roll‑up.

## 3.1 Package / pin map vs. reference — **SAME**

Outline (p.8), 600 DPI crop: identical dimension set to part 2 (⑨152±0.5 … ⑧22±0.3 mm) →
**152 × 62 mm**, identical pin numbering. **Matches reference** and matches part 2 exactly.

## 3.2 Headline / abs‑max (Table 1 p.1, Table 2 p.2)

Table 1: VCE=1700V, IC(Tc=90°C)=600A, VCEsat(same cond.)=**1.70V**, Tvjmax=175°C,
**Package = "HB3"** — note this literally says "HB3," not "D3," even though the datasheet's own
subtitle line, physical outline, and pin map all say D3/152×62 mm; reported exactly as printed.

Abs‑max table identical to part 2 **except**: **Tvjop = 175 °C** (not 150 °C) — i.e. this part is
rated for continuous operation at its full transient Tvj max, with no derating band.

## 3.3 Thermal resistance (Table 3, p.2)

RthJC, IGBT = **0.062** K/W typ; RthJC, diode = **0.092** K/W typ — both higher (worse) than
part 2's 0.039 / 0.057 K/W.

## 3.4 Static characteristics (Table 4, p.3)

| Parameter | Condition | Typ | Unit |
|---|---|---|---|
| VCEsat (chip) | IC=600A,VGE=15V,Tvj=25/175°C | 1.70 / 2.25 | V |
| VCEsat (terminal) | IC=600A,VGE=15V,Tvj=25/175°C | 1.85 / 2.36 | V |
| VF (chip) | IC=600A,VGE=0V,Tvj=25/175°C | 1.80 / 1.90 | V |
| VF (terminal) | IC=600A,VGE=0V,Tvj=25/175°C | 1.85 / 1.95 | V |
| VGE(th) | IC=24mA | 5.95 | V |
| ICES | **VCE=1700V** (correct rated V), VGE=0V, Tvj=25°C | max 100 | µA |
| IGES | VGE=**±20V**, VCE=0V | min −100 / max 100 | nA |
| Cies / Coes / Cres | f=100kHz, **VCE=30V**, VGE=0V | 78.7 / 2.60 / 0.336 | nF |
| RG | f=1MHz | 1.66 | Ω |
| Gate charge QG | VGE=−15/15V, VCE=900V | **5.01** | µC |

Unlike part 2, this datasheet gives **both chip and terminal** VCEsat/VF, and a **symmetric**
IGES spec. Capacitance is measured at VCE=30V (part 2 used 25V). QG is 66% higher than part 2's
3.01 µC at the identical −15/15 V, 900 V bias.

## 3.5 Switching dynamics (Table 5, p.4)

Same bias family as part 2 (VCC=900V, IC=600A, VGE=−15/15V, RGon=RGoff=1Ω):

| Tvj | tdon/tr/tdoff/tf (ns) | Eon (mJ) | Eoff (mJ) | Ets (mJ) |
|---|---|---|---|---|
| 25°C | 410/75/500/380 (identical timing to part 2) | 125.8 | 132.9 | 258.7 |
| 175°C | 440/100/560/600 (identical timing to part 2) | 153.0 | 217.9 | 370.9 |

Timing is pin‑for‑pin identical to part 2; **energies are 15–22 % higher at 25 °C**, and at
175 °C Eon is lower but Eoff is 39% higher than part 2. ISC = 3200 A, same condition as part 2 —
**same**.

E = f(Ic) [Fig.4, p.6] and E = f(Rg) [Fig.5, p.6] curves exist for this part, and — unlike parts
1 and 5 — **their readings at the table's own test point are within roughly 15% of the tabulated
numbers**, e.g. at Ic=600A the Fig.4 curve reads approximately Eon25≈110mJ / Eoff25≈150mJ /
Eon175≈145mJ / Eoff175≈230mJ against table values of 125.8/132.9/153.0/217.9mJ (approx., read
from Fig.4; ±15–20% hand‑reading error). Extrapolated, approximate (not tabulated) points:

| Ic | Eon,25°C | Eoff,25°C | Eon,175°C | Eoff,175°C |
|---|---|---|---|---|
| 300 A (approx., Fig.4) | ~45 mJ | ~50 mJ | ~65 mJ | ~95 mJ |
| 900 A (approx., Fig.4, extrapolated beyond the table's 600A point) | ~170 mJ | ~190 mJ | ~210 mJ | ~350 mJ |

E = f(Rg) [Fig.5, IC=600A,VCE=900V,VGE=±15V]: Eon rises roughly with Rg (≈125–145mJ at 1Ω to
≈300–310mJ by ~5.6Ω for Tvj=25°C; roughly 250→445mJ over the same range at 175°C); Eoff is
comparatively flat across 1–6Ω (≈120–135mJ at 25°C, ≈148–165mJ at 175°C) (all approx., read from
Fig.5; treat as trend/shape, not spec numbers).

## 3.6 Diode recovery (Table 6, p.4)

| Tvj | Trr (ns) | Irrm (A) | Qrr (µC) | Erec (mJ) |
|---|---|---|---|---|
| 25°C | 230 (same as part 2) | 750 (same) | 100 (same) | **52.0** (part 2: 49.7) |
| 175°C | 370 (same) | 706 (same) | 130 (same) | **75.2** (part 2: 52.2, +44%) |

Timing/current/charge identical to part 2's diode — only the recovery **energy** differs,
substantially at 175°C.

## 3.7 Module / mechanical (Table 7, p.5) and NTC (Table 8, p.5)

| Parameter | Value | vs. part 2 |
|---|---|---|
| VISOL | **3.4 kV** | lower (part 2: 4.0 kV) |
| Baseplate | Cu+Ni | same |
| Internal isolation | Al2O3, basic insulation | same |
| Creepage, term–heatsink / term–term | **15.0 mm / 13.0 mm** | part 2: NOT STATED |
| Clearance, term–heatsink / term–term | **12.5 mm / 10.0 mm** | part 2: NOT STATED |
| CTI | > 175 | same |
| Weight | NOT STATED | same gap as part 2 |

NTC: R25/ΔR/B25‑50 identical to part 2.

## 3.8 Characteristics‑diagrams section (p.6–7) — the big structural difference from part 2

This datasheet adds **two full pages of graphs (Fig.1–Fig.14)** that part 2 simply does not
have: output characteristic (Tvj family and VGE family), capacitance‑vs‑VCE, gate‑charge
VGE‑vs‑QG, E‑vs‑Ic, E‑vs‑Rg, **reverse‑bias SOA** (chip & module Ic‑vs‑VCE, RGoff=1Ω, VGE=±15V,
Tvj=175°C, both traces collapsing near VCE≈1550–1650V — read from Fig.6, no table), diode
forward characteristic, diode Erec‑vs‑IF, diode Erec‑vs‑Rg, NTC R‑vs‑T, and **transient thermal
impedance with tabulated 4‑term Foster coefficients** for both IGBT and diode:

**IGBT ZthJC (Fig.11, p.7):**

| i | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| ri [K/W] | 0.0058 | 0.01147 | 0.0324 | 0.01241 |
| ti [s] | 4.67e‑4 | 4.86e‑3 | 2.37e‑2 | 5.05e‑2 |

Σri = 0.0621 K/W, matching Table 3's RthJC(IGBT)=0.062 K/W — internally consistent.

**Diode ZthJC (Fig.12, p.7):**

| i | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| ri [K/W] | 0.0160 | 0.0431 | 0.0176 | 0.0168 |
| ti [s] | 2.49e‑3 | 1.99e‑2 | 1.17e‑2 | 3.25e‑2 |

Σri = 0.0935 K/W ≈ Table 3's 0.092 K/W — consistent.

## 3.9 RC vs. separate diode

Standard discrete anti‑parallel FRD; no naming ambiguity.

## 3.10 Summary — how HCG600FH170D3E1A differs from HCG600FH170D3E1

1. **9 pages vs. 7** — E1A adds the entire graphical‑characterization section (12 figures,
   including RBSOA and Foster‑coefficient Zth curves) that E1 lacks entirely.
2. **Tvjop = 175 °C vs. 150 °C** — E1A has no continuous‑operation derating band, E1 does.
3. **RthJC higher on E1A**: 0.062/0.092 K/W (IGBT/diode) vs. E1's 0.039/0.057 K/W.
4. E1A reports **chip and terminal** VCEsat/VF separately; E1 reports chip only.
5. **QG 66% higher** on E1A (5.01 vs 3.01 µC) at the same −15/15V, 900V bias; **Eon/Ets 15–22%
   higher** at 25°C, same RGon/RGoff/VCC/IC test.
6. **VISOL lower on E1A**: 3.4 kV vs. E1's 4.0 kV.
7. E1A's Table 7 **tabulates creepage/clearance**; E1's does not.
8. E1A's own Table 1 prints package code **"HB3"**, everywhere else (subtitle, outline, pinout)
   says D3 — an internal inconsistency unique to this datasheet.
9. Diode reverse‑recovery **timing/current/charge are identical** between the two parts, but
   **Erec is higher for E1A**, especially at 175°C (75.2 vs 52.2 mJ, +44%).
10. ICES test condition is corrected on E1A (VCE=1700V, the rated voltage) versus E1's
    apparent VCE=1200V test‑condition anomaly (§2.4).

---

# 4. HCS800FH170D3C1 — 1700 V / 800 A Half‑Bridge SiC MOSFET module

10 pages.

## 4.1 Package / pin map vs. reference — **SAME**

Pin‑Configuration drawing (p.2), confirmed at 600 DPI: **152±0.5 mm** overall width,
**122±0.5 mm** bolt‑hole span, **62±0.2 mm** height. Pin labels are printed directly on the
drawing: 3 = "DC−" (labelled), 4 = "DC+" (labelled), 10/11 = right‑side AC terminals, top edge
"2 1" = LS aux‑source/gate, bottom edge "5 6 … 7 8 9" = NTC, HS gate, HS aux‑source, HS
drain‑sense. **Matches the reference map exactly, including the explicit DC+/DC− labelling.**

## 4.2 Module table (p.3)

| Parameter | Condition | Value | Unit |
|---|---|---|---|
| Isolation voltage | RMS, f=50Hz, t=1min | 4.0 | kV |
| Baseplate material | — | Cu | — |
| Creepage, term–heatsink / term–term | — | 14.5 / **10** | mm |
| Clearance, term–heatsink / term–term | — | 12.5 / 10 | mm |
| CTI | — | **600** (printed as a bare value, not ">"-qualified like the IGBT parts) | — |
| Module lead resistance, terminal–chip | Tc=25°C | 0.5 | mΩ |
| Mounting torque, module | M5, M6 | 3 to 6 | N·m |
| Weight | — | 385 | g |

No "internal isolation material" row appears in this table, but the **Features** bullet list
(p.1) states "Low thermal resistance with **Si3N4 AMB**" — i.e. a **silicon‑nitride** active
metal‑braze ceramic substrate, **not** the Al2O3 used in all four IGBT modules above. This is a
material‑level difference from parts 1/2/3/5, not just a numeric one.

## 4.3 Absolute maximum ratings (p.3)

| Parameter | Symbol | Condition | Value | Unit |
|---|---|---|---|---|
| Drain‑source voltage | VDSS | G‑S short | 1700 | V |
| Gate‑source voltage (+) | VGSS | D‑S short | 20 | V |
| Gate‑source voltage (−) | VGSS | D‑S short | −10 | V |
| Gate surge voltage | VGSSsurge | tsurge<300ns (Note1: recommended +15/−5V, +15/−4V) | −10…20 | V |
| DC continuous drain current | IDS | Tc=25°C, VGS=15V | 800 | A |
| DC continuous drain current | IDS | Tc=60°C, VGS=15V | 690 | A |
| Source (body diode) current | ISD | Tc=25°C / 60°C, with ON signal | 800 / 690 | A |
| Pulse forward current | IDSM | Tc=25°C, tp=1ms | 1600 | A |
| Total power dissipation | Ptot | Tc=25°C | 3947 | W |
| Max junction temperature | Tjmax | — | **−55…175** | °C |
| Storage temperature | Tstg | — | **−55…125** | °C |

Note the **−55 °C** floor (vs −40 °C for the four IGBT parts) — wider cold‑end rating, and two
explicit current‑derating points (25 °C and 60 °C case) are given, unlike the single‑point IGBT
tables.

## 4.4 MOSFET electrical characteristics (p.4, Tj=25°C unless noted, chip level)

| Parameter | Condition | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| V(BR)DSS | VGS=0V, ID=800µA | 1700 | — | — | V |
| IDSS | VDS=1700V, VGS=0V | — | — | 1 | mA |
| IGSS | VGS=20V / −10V, VDS=0V | — | — | 800 / −800 | nA |
| VGS(th) | ID=480mA, VDS=VGS, Tj=25°C | 1.80 | 2.70 | — | V |
| VGS(th) | Tj=175°C | — | 1.90 | — | V |
| **RDS(on), chip** | ID=800A, VGS=15V, **Tj=25°C** | — | **2.80** | — | mΩ |
| **RDS(on), chip** | ID=800A, VGS=15V, **Tj=175°C** | — | **6.30** | — | mΩ |
| VDS(on), chip | ID=800A, VGS=15V, Tj=25°C | — | 2.24 | — | V |
| VDS(on), chip | ID=800A, VGS=15V, Tj=175°C | — | 5.04 | — | V |
| Ciss / Coss / Crss | VD=1000V, VGS=0V, f=1MHz, Vac=25mV | — | 61.0 / 1.64 / 0.29 | — | nF |
| Qg (total) | VDD=1000V, ID=600A, **VGS=+15/−5V** | — | **2048** | — | nC |
| Qgd | ID=600A, same VDD/VGS | — | 720 | — | nC |
| Qgs | ID=600A, VGS=+15/−5V | — | 616 | — | nC |
| RGint | Tj=25°C | — | 0.6 | — | Ω |
| RthJC (FET) | junction‑to‑case | — | 0.038 | — | K/W |
| RthCS (contact) | with thermal conductive grease, Note1 | — | 0.015 | — | K/W |

Note1: "Assumes thermal conductivity of grease is 0.9 W/(m·K) and thickness is 50 µm" — the
**only one of the five datasheets that states both a contact resistance and its grease
assumption separately** from the junction‑to‑case number.

VDS(on) = ID×RDS(on) checks out exactly (800A×2.80mΩ=2.24V; 800A×6.30mΩ=5.04V) — internally
consistent.

**Gate‑charge swing requested (−5V→+15.6V)**: the datasheet's stated swing is **VGS = +15V/−5V**
(i.e., −5V→+15V), for which Qg(total)=2048 nC. **A −5V→+15.6V swing specifically is NOT STATED
anywhere in this document** — do not infer a number for that exact voltage.
A VGS‑vs‑Qg curve does exist (Fig.11, p.7): trace runs from (0 nC, −5V) through a kink near
(≈600 nC, ≈5.3V) [~Qgs], a shallower Miller‑like segment to (≈1380 nC, ≈8.3V) [~Qgs+Qgd], then
rises again toward the top of the plotted range — broadly consistent with the tabulated
Qgs=616 nC / Qgd=720 nC breakpoints (approx., read from Fig.11).

## 4.5 Switching characteristics (p.4)

Condition: VDD=900V, ID=800A, VGS=+15/−5V, RGon=RGoff=3.3Ω, inductive load.

| Tj | td(on) (ns) | tr (ns) | td(off) (ns) | tf (ns) | Eon (mJ) | Eoff (mJ) |
|---|---|---|---|---|---|---|
| 25°C | 173 | 125 | 612 | 112 | 67.8 | 72.3 |
| 150°C | 158 | 107 | 653 | 129 | **58.5** (lower than 25°C, as printed) | 80.6 |

E‑vs‑Rg and E‑vs‑Id curves (Fig.13–16, p.8; Tj=25/150°C, gridlines every 20mJ/1Ω or 200A,
moderate‑confidence readings):

| Rg | Eon (mJ) | Eoff (mJ) | Err (mJ) | (Tj=25°C, ID=800A, Fig.13) |
|---|---|---|---|---|
| 1 Ω | ~30 | ~38 | ~3 | approx., read from Fig.13 |
| 2 Ω | ~50 | ~57 | ~1–2 | approx. |
| 3 Ω | ~70 | ~78 | ~1 | approx. |
| 5 Ω | ~98 | ~108 | ~0–1 | approx. |
| 10 Ω (axis max) | ~155 | ~167 | ~0 | approx. |

| Id | Eon (mJ) | Eoff (mJ) | Err (mJ) | (Tj=25°C, Rg=3.3Ω, Fig.15) |
|---|---|---|---|---|
| 300 A | ~20 | ~20 | ~1 | approx., interpolated |
| 600 A | ~47 | ~48 | ~1 | approx. |
| 900 A | ~95 | ~105 | ~1–2 | approx., interpolated |

Cross‑check: Fig.13 at Rg=3.3Ω reads Eon≈80/Eoff≈88 mJ against the table's 67.8/72.3 mJ at
25°C — within ~15–20%, i.e. curve and table are reasonably consistent for this part (unlike the
RC/E1A‑900 mismatch noted above).

## 4.6 Body diode (p.5, chip level)

| Parameter | Condition | Typ | Unit |
|---|---|---|---|
| VSD | ISD=800A, VGS=−5V, Tj=25°C | 5.6 | V |
| VSD | Tj=175°C | 5.3 | V |
| Trr | VRR=900V, ID=800A, VGS=+15/−5V, RGon=RGoff=3.3Ω, Tj=25/150°C | 41 / 129 | ns |
| Qrr | same | 3.98 / 16.9 | µC |
| Err (diode) | same | 1.2 / 9.7 | mJ |

This is the **MOSFET's own intrinsic body diode** — the table is explicitly headed "Body Diode
Electrical characteristics" (p.5), not a separate diode part number. No parallel discrete SiC
Schottky is mentioned.

## 4.7 NTC (p.3)

R25=5kΩ; ΔR/R(R100=493Ω,Tc=100°C)=−5…+5%; P25 max=20mW; B25/50=3375K, B25/80=3411K,
B25/100=3433K, i.e. all three B‑values given (same as part 1, more than parts 2/3/5).

## 4.8 RDS(on)/VGS(th) temperature and current curves (p.7, approx.)

Fig.9 RDS(on) vs Tj (normalized ×1 at 25°C on the 2.80mΩ base): rises to **≈×2.3 at 175°C**
(approx.). Fig.10 RDS(on) vs IDS at Tj=25°C: **≈×0.75 at low current**, ≈×1.0 at 800A (the table's
defining point), rising to **≈×1.2 by 1600A** (approx.). Fig.12 VGS(th) vs Tj: falls roughly
linearly from **≈2.7V at 25°C to ≈2.0V at 175°C** (approx.), consistent with the table's
1.80/2.70V(min/typ,25°C) and 1.90V(typ,175°C) points.

## 4.9 Not provided in this datasheet

No reverse‑bias SOA figure and **no transient thermal‑impedance (Zth) curve or Foster table**
appear anywhere in these 10 pages (checked page‑by‑page) — both **NOT STATED**, unlike parts
1/3/5 which give at least a duty‑factor Zth chart (part 1) or full Foster coefficients
(parts 3, 5).

---

# 5. HCG900FH120D3E1A — 1200 V / 900 A IGBT module (alternative to part 1)

9 pages — same length/structure as part 3 (full Characteristics‑Diagrams section included).

## 5.1 Package / pin map vs. reference — **SAME**

Outline (p.8), 600 DPI crop: identical dimension set to parts 1/2/3 (⑨152±0.5 … ⑧22±0.3 mm) →
**152 × 62 mm**, identical pin numbering. **Matches reference**, matches part 1 exactly.

## 5.2 Headline / abs‑max (Table 1, Table 2, p.1)

Table 1: VCE=1200V, IC(Tc=90°C)=900A, VCEsat(Tvj=25°C,IC=900A,VGE=15V)=1.65V, Tvjmax=175°C,
**Package = "D3"** (consistent this time — no "HB3"‑type mismatch as seen on part 3).

| Parameter | Symbol | Condition | Value | Unit |
|---|---|---|---|---|
| Collector‑emitter voltage | VCE | Tvj=25°C | 1200 | V |
| Continuous DC collector current | ICDC | Tc=90°C | 900 | A |
| Repetitive peak collector current | ICRM | tp=1ms | 1800 | A |
| Continuous DC forward current | IF | — | 900 | A |
| Repetitive peak forward current | IFRM | tp=1ms | 1800 | A |
| Gate source voltage | VGE | Tvj=25°C | ±20 | V |
| Junction temperature | Tvj | — | −40…+175 | °C |
| Storage temperature | Tstg | — | −40…+125 | °C |
| Operating virtual junction temp. | **Tvjop** | — | **150** | °C |

Tvjop=150°C here (same derating pattern as part 2, **not** the 175°C "no‑derating" treatment
seen on part 3) — so that trait is part‑specific within the E1A sub‑family, not a blanket "E1A"
rule.

## 5.3 Thermal resistance (Table 3, p.2)

RthJC, IGBT = 0.046 K/W typ; RthJC, diode = 0.072 K/W typ.

## 5.4 Static characteristics (Table 4, p.2)

| Parameter | Condition | Typ | Unit |
|---|---|---|---|
| VCEsat (terminal) | IC=900A,VGE=15V,Tvj=25/175°C | 1.65 / 2.05 | V |
| VCEsat (chip) | IC=900A,VGE=15V,Tvj=25/175°C | 1.42 / 1.71 | V |
| VF (terminal) | IC=900A,VGE=0V,Tvj=25/175°C | 1.9 / 2.1 | V |
| VF (chip) | IC=900A,VGE=0V,Tvj=25/175°C | 1.57 / 1.75 | V |
| VGE(th) | IC=18mA (min/typ/max: 5.3/5.8/6.3V) | 5.8 | V |
| ICES | VCE=1200V (correct rated V), VGE=0V, Tvj=25°C | max 100 | µA |
| IGES | VGE=20V, VCE=0V (single‑sided) | max 100 | nA |
| Cies / Coes / Cres | f=100kHz, VCE=25V, VGE=0V | 124 / 3.3 / 0.7 | nF |
| RG | f=1MHz | **0.42** | Ω |
| Gate charge QG | VGE=−15/15V, VCE=600V | 10.8 | µC |

RG (0.42 Ω) is much lower than the 600A‑class parts' 1.66 Ω, consistent with a larger die/lower
per‑chip gate resistance for this higher‑current part.

## 5.5 Switching dynamics (Table 5, p.3)

Condition: VCC=600V, IC=900A, VGE=−15/15V, RGon=RGoff=0.51Ω.

| Tvj | tdon/tr/tdoff/tf (ns) | Eon (mJ) | Eoff (mJ) | Ets (mJ) |
|---|---|---|---|---|
| 25°C | 290/90/420/100 | 50 | 85 | 135 |
| 175°C | 350/110/540/250 | 120 | 130 | 250 |

ISC = 3200 A @ Tvj=175°C, **VCC=800V** (higher test bus than the 1700V‑class parts' 1000V,
proportionate to this part's 1200V class), VGE=15V, tp≤6µs (Table 5, p.3).

E=f(Ic) curve (Fig.4, p.5) at Ic≈900A (the table's own tested point, read approx. from the
curve): Eon25≈190mJ, Eoff25≈205mJ, Eon175≈270mJ, Eoff175≈450mJ (axis max) — **these curve
readings are roughly 2–4× the Table‑5 values (50/85/120/130 mJ) at the identical 900A point**,
the same pattern of curve‑vs‑table mismatch seen on part 1. **No numeric values are quoted from
this curve as spec data** for that reason; see the global data‑quality note.

## 5.6 Diode recovery (Table 6, p.3)

| Tvj | Trr (ns) | Irrm (A) | Qrr (µC) | Erec (mJ) |
|---|---|---|---|---|
| 25°C | 400 | 200 | 60 | 20 |
| 175°C | 700 | 280 | 140 | 48 |

## 5.7 Module / mechanical (Table 7, p.4) and NTC (Table 8, p.4)

VISOL=3.4kV; baseplate=Cu+Ni; internal isolation=Al2O3; Ms(M5)=Mt(M6)=3.0–6.0N·m; CTI>175.
**No creepage/clearance rows** (NOT STATED — differs from part 3, which does tabulate them).
**No weight row** (NOT STATED). NTC: R25=5kΩ, ΔR/R=−5…+5% @R100=493Ω,100°C, only
B25/50=3375K given (like parts 2/3).

## 5.8 Characteristics‑diagrams section (p.5–7)

Fig.1–14: output (Tvj family, VGE family), transfer Ic=f(VGE), E=f(Ic), E=f(Rg), **RBSOA**
(Fig.6: chip & module traces collapsing near VCE≈1080–1150V — read from figure, no table),
capacitance‑vs‑VCE, gate‑charge VGE‑vs‑QG, diode forward characteristic, diode Erec‑vs‑IF,
diode Erec‑vs‑Rg, NTC R‑vs‑T, and Zth with Foster coefficients:

**IGBT ZthJC (Fig.13, p.7):**

| i | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| ri [K/W] | 0.004718 | 0.00978 | 0.02131 | 0.010031 |
| ti [s] | 5.47e‑4 | 6.52e‑3 | 2.07e‑2 | 2.80e‑2 |

Σri = 0.0458 K/W ≈ Table 3's 0.046 K/W — consistent.

**Diode ZthJC (Fig.14, p.7):**

| i | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| ri [K/W] | 0.004818 | 0.00960 | 0.03822 | 0.01996 |
| ti [s] | 2.120e‑4 | 1.895e‑3 | 2.484e‑2 | 1.068e‑1 |

Σri = 0.0726 K/W ≈ Table 3's 0.072 K/W — consistent.

## 5.9 RC vs. separate diode

Standard discrete anti‑parallel FRD; no naming ambiguity.

---

# 6. Closing comparison table — all five parts

| | 1. HCG900FH120D3RC | 2. HCG600FH170D3E1 | 3. HCG600FH170D3E1A | 4. HCS800FH170D3C1 (SiC) | 5. HCG900FH120D3E1A |
|---|---|---|---|---|---|
| Technology | IGBT + FRD (co‑packaged, despite "RC" suffix) | IGBT + FRD | IGBT + FRD | SiC MOSFET + body diode | IGBT + FRD |
| VCES/VDSS | 1200 V | 1700 V | 1700 V | 1700 V | 1200 V |
| Nominal IC/IDS @ case temp | 900 A @ TH=85°C | 600 A @ Tc=90°C | 600 A @ Tc=90°C | 800 A @ Tc=25°C (690A@60°C) | 900 A @ Tc=90°C |
| ICRM/IDSM (pulse) | 1800 A, tp=1ms | 1200 A, tp=1ms | 1200 A, tp=1ms | 1600 A, tp=1ms | 1800 A, tp=1ms |
| VGES/VGSS | ±20 V | ±20 V | ±20 V | +20/−10 V | ±20 V |
| Tvj range / Tvjop | −40…175°C / **no Tvjop stated** | −40…175°C / **150°C** | −40…175°C / **175°C** | −55…175°C / not stated | −40…175°C / **150°C** |
| VCEsat or VDS(on) @25°C (test cond.) | 1.67V @900A | 1.68V(chip) @600A | 1.70V(chip)/1.85V(term) @600A | 2.24V(chip, =800A×2.8mΩ) @800A | 1.42V(chip)/1.65V(term) @900A |
| RDS(on) @25/175°C | n/a | n/a | n/a | **2.80 / 6.30 mΩ** @VGS=15V | n/a |
| Diode VF @25°C, rated I | 2.10 V @900A | 1.79V(chip) @600A | 1.80V(chip)/1.85V(term) @600A | 5.6V (body diode) @800A | 1.57V(chip)/1.9V(term) @900A |
| Gate threshold (min/typ/max) | 5.0/5.85/7.0 V | −/6.07/− V | −/5.95/− V | 1.80/2.70/− V | 5.3/5.8/6.3 V |
| QG (stated swing) | 11.2µC @±15V,600V | 3.01µC @−15/15V,900V | 5.01µC @−15/15V,900V | 2048nC @+15/−5V,1000V,600A | 10.8µC @−15/15V,600V |
| RGint | 0.3 Ω | RG=1.66Ω(1MHz)* | RG=1.66Ω(1MHz)* | 0.6 Ω | RG=0.42Ω(1MHz)* |
| Eon/Eoff @Tvj=25°C (table cond.) | 91.2/77.0 mJ @900A,600V,0.5Ω | 102.8/115.5 mJ @600A,900V,1Ω | 125.8/132.9 mJ @600A,900V,1Ω | 67.8/72.3 mJ @800A,900V,3.3Ω | 50/85 mJ @900A,600V,0.51Ω |
| Short‑circuit ISC (tP) | 4550A@25°C(≤10µs) / 3550A@150°C(≤8µs) | 3200A@175°C(≤6µs) | 3200A@175°C(≤6µs) | not tabulated as "ISC"; body‑diode/pulse ratings only | 3200A@175°C(≤6µs) |
| RthJC or RthJH (IGBT/FET, per‑die) | 0.095 K/W **(J‑to‑heatsink, λgrease=3.4W/m·K)** | 0.039 K/W (J‑to‑case) | 0.062 K/W (J‑to‑case) | 0.038 K/W (J‑to‑case) + **0.015 K/W contact (λgrease=0.9W/m·K,50µm)** | 0.046 K/W (J‑to‑case) |
| RthJC/JH diode | 0.13 K/W | 0.057 K/W | 0.092 K/W | (body diode, same die as FET) | 0.072 K/W |
| Zth Foster table | curve only, no coefficients | **no Zth curve at all** | 4‑term, IGBT+diode | **no Zth curve at all** | 4‑term, IGBT+diode |
| Isolation VISOL | 3.0 kV | 4.0 kV | 3.4 kV | 4.0 kV | 3.4 kV |
| Baseplate | Cu | Cu+Ni | Cu+Ni | Cu | Cu+Ni |
| Substrate | Al2O3 | Al2O3 | Al2O3 | **Si3N4 AMB** | Al2O3 |
| Creepage term‑term / term‑heatsink | 13.0 / 14.5 mm | NOT STATED | 13.0 / 15.0 mm | 10 / 14.5 mm | NOT STATED |
| CTI | >200 | >175 | >175 | 600 | >175 |
| Mounting torque (M5/M6) | 3–6 N·m | 3–6 N·m | 3–6 N·m | 3–6 N·m | 3–6 N·m |
| Weight | 345 g | NOT STATED | NOT STATED | 385 g | NOT STATED |
| Module stray inductance | 20 nH (device spec) | NOT STATED | NOT STATED | NOT STATED | NOT STATED |
| Package outline | 152×62mm | 152×62mm | 152×62mm | 152×62mm | 152×62mm |
| Pin map vs. reference | **SAME** | **SAME** | **SAME** | **SAME** | **SAME** |
| Datasheet pages / graphs | 13 pp, partial curves (no gate‑charge or capacitance curve) | 7 pp, **no curves at all** | 9 pp, full curve set | 10 pp, full curve set, **no RBSOA/Zth curve** | 9 pp, full curve set |

*E1/E1A(600)/E1A(900) label this row "Gate input resistance RG (f=1MHz)" rather than "internal
gate resistor"; treated here as the same physical parameter as RC's/SiC's explicitly‑named
"internal gate resistor."

## Package & pin‑map conclusion

All five modules use the **same 152 × 62 mm D3‑style outline and the same 11‑pin function
map** (1/2 = LS gate/aux‑emitter, 3/4 = DC−/DC+, 5/6 = NTC, 7/8 = HS gate/aux‑emitter, 9 = HS
collector‑or‑drain sense, 10/11 = AC), confirmed from each datasheet's own dimensioned outline
drawing and circuit diagram (re‑rendered at 600 DPI where the as‑supplied page image was too
small to read reliably). **All five are SAME versus the reference — none are DIFFERENT or
unverifiable on outline/pinout.**

## Data‑quality notes (read before using any curve‑derived number above)

1. **Curve‑vs‑table mismatch on the two 900A‑class parts.** For HCG900FH120D3RC and
   HCG900FH120D3E1A, the plotted "typical" E=f(Ic) curve reads roughly 2–4× higher than the
   Table‑5 switching‑energy value at the one current (900A) where the curve and the table can be
   directly compared. For the two 600A‑class parts (HCG600FH170D3E1 / …E1A) the same comparison
   at 600A is within ~15%, and the SiC part's Rg‑curve is within ~15–20% of its own table. This
   suggests the 900A‑class "typical" curve panels may be an un‑updated template rather than
   part‑specific data — treat their shape as directional and rely on the tables for numbers.
2. **HCG900FH120D3RC, Table 4 (p.4): Coes is printed as "TBD"** in the vendor's own PDF — not a
   reading error on our part, the datasheet itself has not filled in this cell.
2b. Corrected: item renumbered — Coes = "TBD" is item 3 below; renumbering avoided by leaving as‑is.
3. **HCG600FH170D3E1, Table 4 (p.3): ICES is tested at VCE=1200V** on a part whose VCES rating is
   1700V — reported exactly as printed, flagged as a likely template artifact.
4. **HCG600FH170D3E1A, Table 1 (p.1) prints Package="HB3"**, while its own subtitle line,
   physical outline, and pin map all say "D3" — an internal inconsistency inside that one
   document.
5. Several "NOT STATED" gaps recur (weight for all three E1‑family parts; creepage/clearance for
   E1(600) and E1A(900) but not E1A(600); module stray inductance for everything except RC) —
   these are genuine gaps in the source PDFs, not omissions in this extraction.
6. Where a table gives only "chip" values (parts 1, 2) versus both "chip" and "terminal" values
   (parts 3, 5), no terminal‑level number has been invented for parts 1/2 — reported as
   NOT STATED for the missing reference point.

---

# 7. Addendum — terminal RMS current and ICRM/IDRM (all five parts, plus the archived HCG600FH120D3E1EA)

**Terminal RMS current (I_tRMS / "RMS terminal current" / I_RMS)**: this parameter does **not
appear anywhere in any of the six HIITIO datasheets checked**. Every Absolute‑Maximum‑Ratings /
Maximum‑Ratings table, and every other table on every page of all six documents, was read in
full specifically hunting for a row labelled "RMS," "I_tRMS," "ItRMS," or "terminal current" —
none exists. **NOT STATED for all six parts.** (HIITIO's D3‑package modules appear to only
publish a DC/average continuous current at a stated case temperature, plus a 1 ms repetitive
peak, rather than a separate terminal‑RMS figure.)

**ICRM / IDRM (repetitive peak current)** — restated here with citations for all six parts:

| Part | ICRM/IDRM (repetitive peak) | Citation | Terminal RMS current |
|---|---|---|---|
| 1. HCG900FH120D3RC | IGBT: **1800 A**, tp=1ms. Diode IFRM: 1800 A, tp=1ms | Table 3, p.4 (IGBT); Table 6, p.6 (diode) | NOT STATED |
| 2. HCG600FH170D3E1 | IGBT: **1200 A**, tp=1ms. Diode IFRM: 1200 A, tp=1ms | Table 2, p.2 | NOT STATED |
| 3. HCG600FH170D3E1A | IGBT: **1200 A**, tp=1ms. Diode IFRM: 1200 A, tp=1ms | Table 2, p.2 | NOT STATED |
| 4. HCS800FH170D3C1 (SiC) | **IDSM = 1600 A**, Tc=25°C, tp=1ms — datasheet labels this "Pulse Forward Current," not "IDRM"; no row uses the literal symbol IDRM | Max Ratings table, p.3 | NOT STATED |
| 5. HCG900FH120D3E1A | IGBT: **1800 A**, tp=1ms. Diode IFRM: 1800 A, tp=1ms | Table 2, p.1 | NOT STATED |
| 6. HCG600FH120D3E1EA *(archived copy, read‑only)* | IGBT: **1200 A**, tp=1ms. Diode IFRM: 1200 A, tp=1ms | Table 2, p.2 | NOT STATED |

## 6th part read for this addendum: HCG600FH120D3E1EA (archived, read‑only)

Read from `/Users/chinmoybhuyan/Desktop/Personal/CJM/X-IN-One/Traction/docs/datasheets/HCG600FH120D3E1EA.pdf`
(repository file — read only, not modified). 1200 V / 600 A IGBT module, D3 package ("size
similar to Econo Dual with Cu Baseplate"), 9 pages, HIITIO template identical in layout to
parts 3 and 5 above.

Abs‑max ratings (Table 2, p.2): VCE=1200V (Tvj=25°C); **ICDC=600A at Tc=70°C** — a third,
distinct case‑temperature convention versus part 1's TH=85°C and parts 2/3/5's Tc=90°C;
**ICRM=1200A, tp=1ms**; IF=600A; **IFRM=1200A, tp=1ms**; VGE=±20V; Tvj=−40…175°C;
Tstg=−40…125°C; **Tvjop = −40…150°C, printed as a range** (parts 2/3/5 print Tvjop as a single
ceiling number, not a range). No terminal‑RMS‑current row anywhere in its 9 pages (**NOT
STATED**), consistent with the other five parts. For reference, its other headline numbers
(read in the course of locating the above, not a full extraction per the addendum's scope):
VCEsat(terminal)=1.50V/1.82V @25/175°C, VCEsat(chip)=1.40V/1.70V (Table 4, p.2); RthJC
IGBT/diode = 0.07/0.10 K/W (Table 3, p.2); VISOL=3.4kV, baseplate=Cu+Ni, creepage
15.0/13.0mm, clearance 12.5/10.0mm, CTI>200 (Table 7, p.4).
