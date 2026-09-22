# Multiphase PM propulsion motors, and what a 3-phase inverter cell needs to be one of N

Machine and drive-control view for a marine series of 0.1–3 MW per shaft, from small ferries to harbour tugs. Prepared 2026-09-23.

Out of scope here, because another note covers them: vessel types, battery and DC-grid practice, class environmental testing.

## How to read this

Every statement sits under one of three tags:

| Tag | Meaning |
|---|---|
| **S** | Sourced. The key in brackets, e.g. [Levi07], points to the source list at the end, which gives URL and title. |
| **D** | Derived. Arithmetic or algebra on sourced inputs, which you can reproduce. It has no external source of its own. |
| **J** | Engineering judgement (mine). Not sourced. |

Each entry in the source list also says how far it was read:

| Code | Meaning |
|---|---|
| FT | Full text read |
| AB | Abstract or metadata only |
| V | Vendor page, datasheet or manual |
| W | Press, encyclopaedia or secondary source |

**Research limits.** Read these before relying on any gap.

- **Search budget.** The session's web-search quota ran out early. After that I only fetched known URLs, DOIs and links found on fetched pages.
- **Paywalls.** Most IEEE and IET papers could only be read as abstracts, so their numbers are abstract-level.
- **How the research was split.** Parallel research agents gathered sources by topic. I re-checked:
  - every key full-text item against the document itself: Levi07, Zhu21, the BV/DNV/KR rules, the VACON, ABB and Siemens manuals, the Danfoss and The Switch datasheets, Kokkila15, Jatskevich06, Mueller20 and the NXP material;
  - a sample of the abstracts: Hu17, Dem23, Liao24, Kanerva09, MeyerBoecker06, Welchko03, JahnsCaliskan99, ElRJahns05, Mecrow96, Lateb05, Mitcham04, Prieto20, Arish24, Bassham03 and Sun15.
- **OEM drive documents read in full:** the VACON marine application manual (DriveSynch), the ABB ACS880 firmware manual, the Siemens S120 parallel-connection note, and a Siemens multi-winding patent.
- **Not retrieved, marked NOT FOUND where they matter:**
  - ABB multi-winding and Siemens separate-winding-system product documentation; GE MV7000; any OEM carrier-sync accuracy figure.
  - Current DNV Pt.4 Ch.8 text (behind a login).
  - LR Pt 6 Ch 2, ABS Part 4 and IRS rules (behind logins).
  - IEC 60092-501 (paid).
- **Class wording.** The BV (July 2026), KR (2025) and DNV (July 2013) clauses quoted below were read in full.

---

## 0. Summary

1. **Family.** With standard 3-phase converters at 0.1–5 MW, the practical multiphase machine is **multi-three-phase**: k star-connected 3-phase sets with isolated neutrals, shifted 60°/k (30° dual, 20° triple, 15° quad).
   - True n-phase machines (5, 15, 24 strands) appear only where the converter is custom anyway: naval AIM, Siemens Permasyn.
   - Commercial vendors already sell multi-winding designs:
     - Danfoss Editron DUAL/QUAD-winding motors, one inverter per set.
     - The Switch dual-winding MW motors.
     - ABB dual-stator, 30°-shifted MW azimuth motors, which can run on one stator.
   - Drive OEMs control multi-winding motors master/follower, e.g. VACON DriveSynch: ≤4 drives, a per-follower winding phase-shift parameter, encoder at the master only.
2. **Machines.**
   - Direct-drive PM propulsion motors: 32–40 poles, 0–220/400 rpm, 70–630 kNm, 450/500/690 V, form-wound; derived torque density is 7.6–19.2 kNm/t.
   - Geared or thruster motors: 600–2,100 rpm, with 12–16-pole SRPM or SPM rotors.
   - Electrical frequency stays at about 20–320 Hz. At these frequencies, set-to-set latency is forgiving.
3. **Control.**
   - Run per-set dq control (multiple-dq) in each cell, with 6ω resonant control (plus 12ω for N ≥ 3) and dead-time compensation.
   - 5th/7th circulating currents sit in the x–y planes, which are limited only by leakage inductance.
   - Published results show harmonic control bringing 5th/7th from ~30%/10% down to ~3%/1%.
   - Per-set control can turn unstable through mutual coupling unless the coupling is handled.
4. **PWM.**
   - Interleaving the carriers of sets on a common DC link cuts capacitor RMS current by ≈40–84% at fixed operating points (only ~26% averaged over a drive cycle in one study), but it can raise high-frequency x–y ripple.
   - No published µs synchronisation spec was found. By derivation, ≤1 µs is ample for separate winding sets.
   - Paralleling two cells on one winding set is a different problem: it needs roughly 100 ns-class matching plus coupling inductors.
5. **Class.**
   - BV accepts "double stator windings with one converter for each winding" in place of a standby converter. A single-shaft ship with one rotor still needs an independent alternative propulsion means.
   - KR requires an automatically opening disconnector between the converter and a permanently excited motor.
   - DNV (2013 text) requires a means to stop a non-de-excitable motor from water-milling, and removed the one-motor/two-winding option.
   - BV requires duplicated speed measurement.
6. **Recommendation (J).**
   - The cell needs:
     - a set-angle offset;
     - PWM SYNC in/out (≤1 µs);
     - a private CAN-FD "shaft bus" at 0.5–1 kHz plus a hard-wired fault/enable loop;
     - 6ω/12ω harmonic control;
     - per-set ASC/SPO selection from back-EMF versus Vdc;
     - N−1 torque and speed limits;
     - a disconnector interlock;
     - a speed/position sensor per set (or per pair), duplicated per BV.
   - Add winding sets rather than paralleling cells on one set.
   - Preferred set counts:

   | Shaft power | Winding sets |
   |---|---|
   | ≈0.3 MW | 1 set (2 on a single-screw vessel) |
   | ≈0.6 MW | 2 sets |
   | ≈1.2 MW | 4 sets |
   | ≈2.5 MW | 8 sets (4 × 15° groups × 2), or two tandem motors |

---

## 1. Machine families for 0.1–5 MW propulsion

### 1.1 Taxonomy and where each is used

| Family | Shift between sets | Neutrals | Converter per machine | Documented users |
|---|---|---|---|---|
| Single 3-phase | – | 1 | one VSI, or VSIs paralleled on one winding | most small thrusters; winding data rarely published |
| Dual 3-phase, **asymmetrical** (6-phase) | 30° | 2, isolated | 2 × 3-phase VSI | Danfoss EM-PMI "DUAL" [DanT4000]; The Switch "dual winding machine" [SwitchPMM1500]; ABB 18–21 MW dual-stator azimuth synchronous motors on a cruise ship, runnable on one stator [Kanerva09]; Adtranz locomotive [Levi07] |
| Dual 3-phase, symmetrical | 60° | 2, isolated | 2 × VSI | mostly academic; no 6th-harmonic torque cancellation (see 3.2) |
| Triple 3-phase (9-phase) | 20° (or 40°) | 3, isolated | 3 × VSI | 9-phase PMSM and generator rigs [Rub20; Gal19]; 36.5 MW nine-phase ship-drive study [Levi07] |
| Quad 3-phase (12-phase) | 15° | 4, isolated | 4 × VSI | Danfoss EM-PMI "QUAD" [DanT4000]; 45 MW quadruple-star synchronous motor on four VSIs (LNG compressor) [Tess10b]; 12-phase PMSM study for marine/aviation propulsion [Zh25] |
| 15-phase, 3 × 5-phase | 5-phase sets | separate | 3 independent converter channels | GE/Converteam Advanced Induction Motor (AIM). On DDG-1000 it has 3 independent channels and runs on 5, 10 or 15 phases [Riviera20]; also fitted on Type 45 and the Queen Elizabeth carriers [WikiT45; WikiQE]. A 15-phase ship induction motor built as 3 five-phase windings on H-bridge VSIs with separate DC links is described in [Levi07]. |
| 15-phase, H-bridge per phase (China) | 3 groups of 5 phases (12°/72° quoted) | – | 15 NPC five-level H-bridges | 20 MW, 200 rpm, 20 Hz propulsion induction-motor drive [Sun15] |
| 24 single-phase strands | – | – | 24 single-phase converters in 12 modules inside the motor | Siemens Permasyn submarine PM motor [SiemensWO04; Mueller20] |

**S (why multiphase at all).** Multiphase drives gained momentum from three applications: electric ship propulsion, traction and the more-electric aircraft [Levi07]. In high-power ship drives the key motive is a lower power-electronics rating per device and per phase [Levi07]. Later reviews again list ship propulsion as a main driver [Levi08; Levi16; Barrero16]. A 2021 review calls ship propulsion the first and still the main application of multiphase machines, with 5-phase PM, 6-phase PM and 15-phase machines in service [Zhu21]. The same review names 0° and 30° as the most common displacements between two sets. The 30° version gives higher torque density, lower MMF harmonics and torque ripple, and lower unbalanced magnetic force under faults [Zhu21].

**S (multi-three-phase is the natural industrial form).**
- An n = a·k phase winding can be arranged as k windings of a phases each with isolated neutrals. Zero-sequence current then cannot flow. a = 3 with k = 2 or 3 is the most common choice.
- Asymmetrical versions shift consecutive 3-phase sets by half the phase angle: 30° for 6-phase, 20° for 9-phase [Levi07].
- Feeding a dual 3-phase machine from two independent 3-phase inverters is called "a natural choice for industry applications". Small asymmetries between the two power sections then cause current imbalance, so at least four current controllers are needed [Levi07].
- Because custom multiphase converters are expensive, dual 3-phase machines on two standard converters are attractive. After a fault the faulty set is disconnected and the healthy set keeps running [Barcaro10].
- Multi-three-phase drives can be run as several 3-phase units in parallel, which reuses 3-phase technology. The asymmetrical six-phase machine cancels the 6th torque harmonic. The secondary planes have a lower equivalent impedance [Rodas21].

**S (naval).**
- DDG-1000 uses a 15-phase AIM fed by VDM25000 converters with three independent channels. Each drive train can run on 5, 10 or 15 phases, and two motors sit in tandem per shaft [Riviera20]. The ship has two 34.6 MW AIMs [NavTech].
- The 15-phase ship-propulsion induction motor studied in the literature is built from three five-phase windings, each fed by a five-phase H-bridge VSI with its own DC link [Levi07].
- Type 45 has 2 × 20 MW AIMs [WikiT45]. The Queen Elizabeth carriers have 4 × 20 MW AIMs [WikiQE] of about 110 t each [GE-QE].
- Induction was chosen for mechanical simplicity, reliability and shock design [Lewis02].
- The DRS 36.5 MW permanent-magnet motor (PMM) completed full-power land tests in 2008 [DefDaily; PTI-DRS]. DDG-1000 had already moved to the AIM in 2005 to meet schedule milestones [WikiZum].

**S (Siemens Permasyn).**
- The winding is split into 24 strands, each fed by its own single-phase converter. The converters sit in 12 two-inverter modules inside the motor, and the aim is redundancy and availability [SiemensWO04].
- Generations: about 1 MW (first), about 2 MW (Type 212A, inverters integrated in the motor), about 4 MW (2003). There are 38 orders, with redundancy "at multiple levels (e.g. cooling, control and feeding)" [Mueller20].
- The newer FLEX family covers 1.5–8 MW with motor-integrated inverters [Janes-FLEX].

**J (why multi-three-phase wins commercially at 0.1–5 MW).**
1. The 3-phase VSI is reused unchanged: modulation, protection, gate drive and type approval all stay the same.
2. Isolated neutrals block zero-sequence and 3rd-harmonic paths, so each set looks like an ordinary 3-phase motor to its inverter.
3. Power and redundancy scale in whole-converter steps.
4. Class rules already name the arrangement: BV accepts double stator windings with one converter per winding [BV-C 2.1.2].

True n-phase machines pay off only when the converter is custom anyway (an H-bridge or single-phase module per phase) and survivability or acoustics justify the cost, which is the naval case. For your cell, multi-three-phase with isolated neutrals is the only family that lets an unmodified 3-phase cell serve as one of N.

### 1.2 Isolated vs connected neutrals; distributed vs sectored sets

**S.**
- Isolated neutrals are often exploited because they help fault tolerance [Levi07].
- A Canadian naval model of a 20 MW, 15-phase induction motor uses five 3-phase groups displaced 12°. It joins all neutrals at one floating point, which the authors prefer for phase-loss cases [Jatskevich06].
- Harmonic mapping depends on the neutral configuration. With a single isolated neutral, some subspaces become coupled [Yep17].
- Barcaro et al. compare dual 3-phase winding layouts on four criteria: phase mutual coupling, overload capability, short-circuit behaviour and unbalanced radial force [Barcaro10].
- Sector-level numbers are in §5.1.

**J.**
- Sets fed by independent cells must have **isolated neutrals**. A shared neutral would create zero-sequence paths between cells that no single cell controls.
- Prefer **distributed (interspersed) sets** over sectored ones unless the motor maker proves bearing and unbalanced-magnetic-pull margins for N−1 operation.

---

## 2. How MW low-speed PMSMs are built and behave

### 2.1 Reference data

| Machine | Rotor / winding | Poles | Speed | Continuous torque | Power | Voltage | Mass → torque density | Src |
|---|---|---|---|---|---|---|---|---|
| The Switch PMM1500M | NdFeB PM; 2-layer form-wound diamond coils, mica, VPI | 32 | 0–220 rpm (58.7 Hz at 220, **D**) | 198–623 kNm | 2–4 MW typical | 450/500/690 V (MV optional) | 18.1–32.5 t (without shaft/bearings) → **10.9–19.2 kNm/t (D)** | [SwitchPMM1500; SwitchBro] |
| The Switch PMM850M | NdFeB PM; form-wound, mica; water jacket | 40 | 0–400 rpm (133 Hz at 400, **D**) | 70–231 kNm | 0.5–2 MW typical | 450/500/690 V | 9.2–17.9 t → **7.6–12.9 kNm/t (D)** | [SwitchPMM850] |
| The Switch range | PMM850M to PMM2000M | – | 0–130 … 0–400 rpm | 70–1,500 kNm | <1 to >12 MW | – | – | [SwitchBro; SwitchWeb] |
| Danfoss EM-PMI540-T4000 | Synchronous-reluctance-assisted PM (interior magnets); DUAL/QUAD options | 16 (8 pole pairs) | 600–2,400 rpm nominal (80–320 Hz, D) | 3.6–4.5 kNm (65 °C coolant) | 284–896 kW | 500 V AC nominal | 950 kg → **≈4.8 kNm/t (D)** | [DanT4000; DanUG] |
| Danfoss EM-PMI375-T800 | SRPM | 12 (6 pole pairs) | ≤4,000 rpm | ≈0.8 kNm class | 85–251 kW | 500 V (690 V variant exists) | 210 kg | [DanT800; Dan690] |
| 750 kW azimuth-thruster motor | Surface PM, segmented magnets | – | 1,200 rpm | ≈6 kNm (D) | 750 kW | 690 V | – | [Prieto20] |
| Kongsberg RD-TT rim thruster | PM in propeller rim, seawater cooled | – | 340 → 212 rpm | ≈28 → 122 kNm (D) | 1.0 → 2.7 MW | – | – | [KongRDTT] |
| DRS PMM (naval) | PM | – | ≈129 rpm (D) | 2.7 MNm | 36.5 MW | – | – | [PTI-DRS] |
| GE AIM (induction benchmark) | Induction, 15-phase | – | – | – | 20 MW | – | ≈110 t → 0.18 MW/t (D) | [GE-QE] |

### 2.2 Behaviour, sourced points

**Pole count vs speed.**
- **S:**
  - Direct-drive marine PM machines use 32–40 poles at 0–220/400 rpm [SwitchPMM1500; SwitchPMM850].
  - Naval propulsion motors run at about 0–180 rpm [Jatskevich06]. The Chinese 20 MW drive runs 200 rpm at 20 Hz [Sun15].
  - Geared azimuth units take 600–2,100 rpm input, mostly 750–1,800 rpm [SchottelSRE]. The 750 kW azimuth PM motor runs at 1,200 rpm [Prieto20].
- **D:** f = pole-pairs × rpm / 60. So 200 rpm with 24 poles gives 40 Hz, and 1,200 rpm with 8 poles gives 80 Hz.

**Surface vs interior magnets.**
- **S:**
  - Surface-mounted PMSMs are the conventional pod solution. A 5 MW PM vernier pod design is claimed to exceed 99% efficiency [Arish24].
  - The 750 kW azimuth motor is SPM, with its magnets segmented against loss and demagnetisation [Prieto20].
  - Danfoss uses interior magnets with reluctance assistance (SRPM) [DanUG].
  - High-saliency IPM is the most promising route to a wide field-weakening range [SoongMiller94].
- **J:**
  - At low speed with many poles, MW marine products pair a PM rotor with a form-wound, mica-insulated distributed stator [SwitchPMM1500]. The exact rotor magnet arrangement of these products (surface or pole-piece): NOT FOUND.
  - IPM/SRPM suits the 600–2,400 rpm geared class, where reluctance torque and field weakening pay off.

**Winding type.**
- **S:**
  - Fractional-slot concentrated windings (FSCW) offer high power density and efficiency, short end turns, high slot fill, low cogging, flux-weakening capability and fault tolerance. Their challenges include rotor and magnet losses from MMF space harmonics [ElRefaie10].
  - Suitable slot/pole combinations give negligible mutual coupling between phases and high inductance [Mitcham04].
  - The Switch's MW marine machines use 2-layer form-wound diamond coils with mica, i.e. distributed windings [SwitchPMM1500].

**Characteristic current and the "1 pu" rule.**
- **S:**
  - Fault-tolerant PM drives make each phase electrically, magnetically, thermally and physically independent [Mecrow96].
  - FSCW lets designers raise inductance enough to reach the critical condition for a wide constant-power range [ElRJahns05].
  - An optimally field-weakened design gives in principle an infinite constant-power speed range [SoongMiller94].
- **D:**
  - Characteristic current is I_ch = ψ_PM / L_d.
  - At speed (R ≪ ωL), the steady 3-phase terminal-short or active-short-circuit (ASC) current is ≈ I_ch.
  - The "critical" or optimal field-weakening condition is ψ_PM = L_d·I_rated, i.e. I_ch = 1 pu.
  - So a machine designed with I_ch ≈ 1 pu (x_d ≈ e_0 in per unit) can carry a sustained short or ASC current at about rated current, which it survives thermally, and it also has ideal field weakening. That is why fault-tolerant designs aim for ~1 pu short-circuit current.
  - The cost is a lower power factor and more converter kVA.
- **NOT FOUND:** per-unit L_d values for MW marine PM motors. Full texts with those numbers were not accessible.

**Field weakening.**
- **S:** propeller load torque in per unit is 0.0136·ω + 1.0158·ω², i.e. it goes with speed squared [Jatskevich06].
- **J:** a propeller drive needs almost no constant-power range. Design the back-EMF for maximum shaft speed with a little voltage margin, and spend the inductance on fault-current limiting rather than on field-weakening range.

**Efficiency and power factor.**
- **S:**
  - Danfoss EM-PMI540-T4000: 96% nominal efficiency. Its generator-mode power factor table reads 0.92–0.99 [DanT4000].
  - The Switch claims 2–4% higher efficiency at rated power and up to 10% below full load, versus conventional machines [SwitchEff] (vendor claim).
  - DRS claims +2–4% at full load and up to +30% at part load [PTI-DRS] (vendor claim).
  - ABB offers Azipod D with an induction motor or a "higher-performance" PM motor, chosen case by case [Kokkila15].
- **NOT FOUND:** motor-mode power factor for MW low-speed PM propulsion motors.

**Back-EMF harmonics.**
- **S:** FSCW can still deliver sinusoidal line-to-line back-EMF [ElRJahns05].
- **NOT FOUND:** measured back-EMF spectra for MW marine machines. §3.3 shows why the 5th and 7th harmonics matter.

**Demagnetisation.**
- **S:**
  - NdFeB remanence falls about 0.09–0.12%/K and coercivity about 0.40–0.65%/K. Dy/Tb grades hold coercivity at high temperature [WikiNdFeB].
  - Marine designs assess demagnetisation and segment magnets [Prieto20].
  - Short-circuit transients are the main demagnetisation threat (see §5.4).
- **J:** the worst case is the first peak of an ASC or terminal short at maximum rotor temperature. Get the motor maker's demagnetisation withstand for a 3-phase short per set, and for one set shorted while the others drive (§5).

**Torque density.**
- **S:** PMSMs can be up to 30% more compact than classical motors, though an optimised induction motor can compete [Lateb05].
- **D:** direct-drive PM machines reach 7.6–19.2 kNm/t (The Switch, table 2.1).

**PM vs induction.**
- **S:**
  - An NPS thesis found PM the best propulsion motor on mature technology, power density and acoustics [Bassham03].
  - DDG-1000 nonetheless went AIM for schedule reasons [WikiZum].
  - Schottel's SRE uses an asynchronous motor as standard and PM as an option [MarEx-SRE]. ABB Azipod D offers both [Kokkila15].
- **J:** for ferries and tugs, which spend long hours at part load, the PM efficiency gain and the smaller, shorter machine are real. The price is fault management: flux can't be switched off (§5). Induction stays attractive where towing or windmilling exposure and a single screw make PM fault handling expensive.

---

## 3. Control of multi-three-phase PMSMs

### 3.1 Vector-space decomposition vs multiple-dq

**S.**
- **VSD.** Vector-space decomposition splits a dual 3-phase machine into three orthogonal subspaces: αβ (electromechanical), x–y (5th, 7th, 17th, 19th…, loss only) and zero-sequence [ZL95].
- **Hu et al.** VSD is more flexible because each subspace gets its own gains. Two-individual control (per-set dq) can go unstable through the mutual coupling between sets; with weak coupling it matches VSD dynamics without instability [Hu17].
- **Che et al.** x–y currents are, physically, the circulating currents between the two windings. x–y control is needed to handle asymmetries and dead-time harmonics [Che14].
- **Karttunen et al.**
  - Their decoupled scheme guarantees balanced sharing between sets. It reduces harmonics but does not fully eliminate them [Kar14].
  - Two independent off-the-shelf VSIs lose some dynamics and estimate torque with an error, but produce no low-frequency current harmonics [Kar12].
- **Double d-q in practice.** A double d-q scheme (two pairs of synchronous-frame current controllers) was used for a GTO-fed high-power dual 3-phase machine [Levi07].
- **Modular variants.**
  - Independent, decoupled torque control per 3-phase unit on a 9-phase PMSM [Rub20].
  - A decoupling transform [Rub21].
  - Magnetic coupling degrades control with independent inverters, and a low-bandwidth link helps (simulation) [Sala19].
  - Centralised VSD is argued not to meet fault-tolerance needs; a droop-based distributed scheme is proposed instead [Gal19].
  - General VSD matrices for 6-, 9-, 12- and 15-phase machines [Zor17a]; generalised transformations [RL15].

**J.**
- **Default: multiple-dq in each cell.**
  1. A PI current loop per set in the set's own dq frame.
  2. Resonant terms at 6ω, plus 12ω for N ≥ 3.
  3. Feed-forward of the mutual-coupling voltage. With equal sharing, every set's dq reference is identical, so each cell can compute the coupling term from the shared reference, with no fast link.
  4. Current-loop bandwidth kept moderate (≤ ~1/10 of the sampling rate), to stay away from the coupling instability reported in [Hu17].
- **VSD only as a special case.** Use it only where one controller drives two power stages. The S32K39 is rated for "one 6-phase or two 3-phase motors" [NXP-S32K39].

### 3.2 Which harmonics go where

| Winding (isolated neutrals) | αβ: torque-producing | x–y planes: loss-only, leakage-limited | Surviving torque-ripple orders from 5/7/11/13… |
|---|---|---|---|
| Dual, 30° | 1, 11, 13, 23, 25 (12k±1) | 5, 7, 17, 19, 29, 31 | 12, 24 (6th cancels) |
| Dual, 60° (symmetrical) | all 6k±1: 1, 5, 7, 11, 13 | none of the odd non-triplen harmonics | 6, 12, 18 … (no cancellation) |
| Triple, 20° (or 40°) | 1, 17, 19, 35, 37 (18k±1) | {5, 13, 23, 31}, {7, 11, 25, 29} | 18, 36 |
| Quad, 15° | 1, 23, 25, 47, 49 (24k±1) | {5, 19, 29, 43}, {7, 17, 31, 41}, {11, 13, 35, 37} | 24, 48 |

- **S:** the dual-30° row [ZL95; Rodas21]. The mapping depends on symmetrical vs asymmetrical winding and on the neutral configuration [Yep17]. A unique mapping of all odd harmonics exists for 6/9/12/15-phase machines [Zor17a].
- **D:** the other rows. I computed them from the full phase-angle VSD transform, and an independent derivation by a second analyst agrees.
  - Assumptions: the harmonic is excited identically in every set, and plane numbering depends on how the transform is ordered.
  - Triplen harmonics are zero-sequence within each set, so they can't flow with isolated neutrals.
  - In each set's **own** synchronous frame, the 5th (negative sequence) and 7th (positive) appear at ∓6ω, and the 11th/13th at ∓12ω. A 6ω resonator in each cell's dq frame therefore covers 5/7, and a 12ω resonator covers 11/13, whatever N is.
- **D:** torque-ripple cancellation needs all sets healthy, identical and synchronised. With a set lost, the 6th returns (see §5).

### 3.3 Circulating 5th/7th (and 11th/13th) currents

**S (why they are large).**
- x–y harmonic currents are limited only by the small stator leakage impedance [Geng21; Xu20; Rodas21].
- The leakage inductance relevant to these harmonics depends on coil pitch and winding design [Had04].
- In the 20 MW naval induction-motor model, x_ls = 0.0101 pu against x_m = 1.76 pu [Jatskevich06]. **D:** the harmonic-plane impedance is about 1/175 of the magnetising reactance.
- Voltage distortion comes from dead time and inverter nonlinearity, and from non-sinusoidal back-EMF [Hu14; Geng21].
- In a 45 MW quadruple-star synchronous motor on independent VSIs, harmonic circulation also came from the non-sinusoidal air-gap flux and from switching-transient imbalance [Tess10].

**S (measured).** On a six-phase PMSM (600 V DC, 200 A rms, 12-pole, 1,200 rpm), back-EMF harmonic feed-forward plus harmonic synchronous-frame feedback gave:

| Quantity | Before | After |
|---|---|---|
| 5th harmonic | 29.98% | 2.74% |
| 7th harmonic | 9.72% | 1.21% |
| THD | 31.71% | 4.84% |

Source: [Lin25].

**S (remedies).**
- PI plus resonant controllers in the αβ and z1z2 (x–y) frames [Hu14].
- Multi-resonant controllers at multiples of 6 in one synchronous frame [Yep15].
- A disturbance observer: >99% reduction [KarDOB16].
- An extended-state observer that tolerates dead time and device drops [Xu20].
- Adaptive notch dead-time compensation [Geng21].
- Near the voltage limit full elimination may be impossible, so compensate partially [Kar17].

**D (sizing example).** The harmonic current is I_h ≈ V_h / (h·ω·L_σ). Worked for a 1,000 V link, 3 µs dead time, 3 kHz switching and a 690 V machine:

| Source of distortion | Magnitude | L_σ | Resulting I_5 | Speed dependence |
|---|---|---|---|---|
| Dead time: 5th ≈ 2.3 V (0.4% of rated phase peak) | at rated speed | 0.05 pu | ≈1.6% | – |
| Same dead time | at 20% speed | 0.05 pu | ≈8% | Rises as 1/speed |
| Same dead time | at 20% speed | 0.02 pu | ≈20% | Rises as 1/speed |
| 2% 5th in the back-EMF | – | 0.05 pu | ≈8% | Roughly constant with speed, because EMF and impedance both scale with ω |

**J.** Dead-time compensation matters most at low speed, i.e. manoeuvring and DP-like work. Back-EMF-harmonic suppression matters across the whole range. Ask the motor maker for x–y leakage (or "harmonic") inductance and for the 5th/7th back-EMF content per set; they size the 6ω loop.

### 3.4 Sharing torque and power between sets

**S.**
- Power can be shared arbitrarily between sets by imposing fundamental-frequency x–y currents, without changing flux or torque [Zor18].
- Sets do not share power the same way they share torque [Sub19].
- Unbalanced sharing shrinks the linear range of standard VSD-SVPWM [Ma24].
- In a marine hybrid SiC/Si direct-drive multiphase PMSM, shifting load between windings improves light-load efficiency. The paper reports ~30% lower drive losses than all-Si and ~35% lower cost than all-SiC [Ni24].
- Droop-based distributed sharing is shown in [Gal19].

**D.** With a split s / (1−s), copper loss scales as s² + (1−s)². A 70/30 split costs 1.16× the loss of equal sharing.

**J.** In multiple-dq control, sharing is simply a per-cell iq* weight set by the master. Use it to balance separate DC buses or battery strings. Keep id* equal unless one set is deliberately field-weakened, for example because its bus is low.

### 3.5 PWM carrier synchronisation and interleaving

**S (DC link, common bus).**
- 84% lower capacitor RMS current and 86% lower voltage ripple at static operating points, but only ~26% over a drive cycle (simulation) [Dem23].
- Capacitor ripple cut to 50–60% of a conventional 2-level VSI [Bha14].
- 60% lower capacitor current in an experiment [Sah25].
- Without interleaving, six-phase is only 7–10% better than three-phase [Tah23].
- More than 50% reduction in a modular 15-phase drive [Verk19].
- The best shift angle depends on modulation index and power factor [Bha14; Sah25].

**S (torque ripple, x–y ripple, common-mode voltage).**
- Carrier phase shift can selectively cancel PWM torque harmonics for 0°/30° windings [WX20] and for sectored triple 3-phase machines [WX19].
- Shifting the second set's carrier by π/2 halves switching-frequency vibration and acoustic noise in a segmented dual 3-phase drive [Zhu21].
- Set-to-set coupling raises high-frequency current ripple; more leakage inductance reduces it [Bai25]. The effect is made worse by interleaving [Hop25].
- A phase displacement between the two modulators lowers common-mode voltage and bearing current [Alc21].

**S (synchronisation).**
- Small crystal-frequency differences between controllers desynchronise the carriers and raise current harmonics. The usual fix is a hardware link; a self-synchronising method using sideband currents is proposed as an alternative [Liao24].
- PLL-based carrier sync without low-latency signals is shown in [Xu19].
- **NOT FOUND:** any published µs accuracy requirement.

**D (numbers).**
- Carrier phase error = 360°·f_sw·Δt. At 3 kHz, 1 µs ≈ 1.1° and 5 µs ≈ 5.4°. The cosine-law loss of cancellation is negligible.
- Free-running clocks with 100 ppm relative error at 4 kHz beat at 0.4 Hz, so the relative phase sweeps through all angles every 2.5 s. The capacitor must then be sized for the worst angle, and the interleaving benefit is lost.
- With **separate** DC buses the capacitor benefit disappears, but the torque-ripple and x–y-ripple effects of carrier phase remain.

**S (paralleling two inverters on ONE winding set).**
- Paralleled inverters on one winding risk zero-sequence circulating current. It must be blocked by transformers or separate supplies, or by a strong zero-sequence control loop [Ye02].
- A carrier phase difference drives switching-frequency circulating current that needs closed-loop suppression [ZhX18].
- In industrial practice, Siemens parallels two identical S120 Motor Modules on one winding under three conditions:
  - one control loop;
  - ≥40 µH (132 A module) or ≥26 µH (200 A module) of decoupling inductance per module, from ≥40 m or 26 m of cable or from reactors;
  - equal cable lengths.
  - Source: [S120par]
- **D:** that is L·I ≈ 5.2–5.3 mWb per module. Scaled to a ~500 A cell it gives ≈10 µH per phase.

**D.** The circulating-current step per edge mismatch is Δi = V_dc·Δt / L_c. At 1,000 V with L_c = 50 µH:

| Timing mismatch Δt | Δi per edge |
|---|---|
| 1 µs | 20 A |
| 100 ns | 2 A |

**J.** Separate winding sets bring their own built-in coupling inductance, the x–y leakage, and block zero-sequence by construction. Paralleled legs need external coupling inductors and about 10× tighter timing.

### 3.6 What passes between set controllers, and how others did it

**S.**
- Distributed inverter modules have used CAN to synchronise estimates, interleave PWM and synchronise ADC sampling [Par13].
- One distributed multi-three-phase generator needed interconnect cables to carry position; the authors propose per-set sensorless estimation to remove them [Lyu26].
- A distributed architecture with no interconnect cables has been built for redundancy [Lia23].
- Master–slave and droop control have been compared on a dual 3-phase drive [Ben21].
- A low-bandwidth link between independent inverters improves control of coupled sets (simulation) [Sala19].
- Industrial and marine precedents:
  - a 45 MW, 4-pole, 100 Hz quadruple-star synchronous motor on four multilevel VSIs, tested at full load [Tess10b];
  - a ship-propulsion synchronous motor on two PWM converters, analysed in normal and one-inverter-out operation [Bassi10].
- **OEM architectures, all master/follower:**
  - **VACON DriveSynch.** Up to 4 drives on a single-winding or multi-winding motor, with a per-follower winding phase-shift parameter of 0–360°. It uses an optical ring, a single encoder at the master, f_sw ≤ 3.6 kHz, and survives the loss of any follower but not of the master or the link [VaconMarine].
  - **Siemens patent (separate winding systems).** Only the master module reads the position sensor; slaves receive position through a central unit [SiemensUS17].
  - **ABB ACS880 master/follower.** 4 Mbit/s DDCS, ≤10 followers, references (not angle) in < 5 ms [ACS880FW].
- **NOT FOUND:** the carrier-sync accuracy of these OEM links, the internal link of GE MV7000, and how Danfoss Editron EC-C inverters coordinate on DUAL/QUAD motors.

**J.** Minimum exchange for multiple-dq cells:

| Signal | Direction | Rate / latency | Medium |
|---|---|---|---|
| Torque (iq*) reference + sharing weight + active-set mask + N−1 limits | master → cells | 0.5–1 kHz, ≤2 ms | private CAN-FD "shaft bus" |
| Status: iq, Vdc, temperatures, fault code, sync-lock, ASC/SPO state | cells → master | 0.5–1 kHz | same bus |
| Carrier sync pulse + phase offset | master → cells | every PWM period; ≤1 µs jitter | isolated differential pair or plastic fibre |
| Fault / propulsion-enable loop | all ↔ all | ≤100 µs | hard-wired wired-OR (open-collector or fibre) |
| Rotor angle | own sensor per cell (preferred) or broadcast | if broadcast: ≥10 kHz with time stamp and extrapolation | Ethernet/100BASE-T1 or dedicated serial; not CAN |

**D (link budget).** A CAN-FD frame with 16 data bytes takes ≈146 µs at 500 kbit/s arbitration and 2 Mbit/s data, or ≈64 µs at 1 and 5 Mbit/s. So 1 command + 4 status frames per 1 ms uses ≈73% of the bus at 0.5/2 Mbit/s, or ≈32% at 1/5 Mbit/s.

**J.** Run the shaft bus at 1/5 Mbit/s over the short distance inside the converter room, or at 500 Hz.

**D (angle latency).** Error = 360°·f_e·t. At f_e = 40 Hz, 100 µs gives 1.4° (negligible) and 1 ms gives 14°, which would cost 3% of torque without extrapolation. That is why a broadcast angle needs a time stamp. Timing class for fast links:
- IEEE 802.1AS (gPTP) gives sub-µs clock alignment [WikiTSN].
- TSN "Class CDT" targets 100 µs over 5 hops [WikiTSN].
- The S32K39 has one 10/100 Mbit/s Ethernet MAC with AVB/TSN and six CAN/CAN-FD channels [NXP-S32K39; NXP-TP].

---

## 4. Position sensing for N sets

### 4.1 One shared sensor vs one per set

**S (industrial practice).**
- **VACON DriveSynch, Danfoss marine application, 2020.**
  - Up to four drives in parallel, on a single-winding motor or on several-winding motors.
  - In closed loop, the encoder is wired **only to the master**. For redundancy it can also be wired to the followers through a double-encoder board (OPTA7).
  - Drives are linked by an optical ring (OPT-D2).
  - Each follower has a "follower drive winding phase shift" parameter, 0–360°, set per the motor nameplate.
  - Maximum switching frequency with DriveSynch is 3.6 kHz. Minimum recommended is 1.7 kHz open loop and 2.5 kHz closed loop.
  - The system keeps running if any follower fails. The master and the fast optical link must stay functional, and +24 V auxiliary power must reach every control unit, including failed ones. n+1 units is common practice.
  - [VaconMarine]
- **Siemens patent, motor with separately controllable winding systems.**
  - Exactly one "master module" receives the position signal directly from the sensor.
  - It passes position information to a central processing unit.
  - The processing unit sends each slave module a single item of position information.
  - [SiemensUS17]
- **Siemens S120, two Motor Modules paralleled on a shared DC link and a common winding.**
  - The modules must be identical: 132 A / 71 kW or 200 A / 107 kW.
  - Each module needs ≥40 µH (132 A) or ≥26 µH (200 A) of decoupling inductance, taken as ≈1 µH per metre of motor cable, or supplied by reactors.
  - Motor cable lengths must be equal.
  - [S120par]
- **ABB ACS880 master/follower**, for mechanically coupled drives.
  - The master broadcasts control word, speed reference and torque reference to up to 10 followers.
  - Link: DDCS at 4 Mbit/s over fibre or twisted pair, with < 5 ms reference transfer.
  - DTC motor control does not need shaft speed.
  - This shares references between drives; it does not distribute the rotor angle.
  - [ACS880FW]
- **Danfoss EM-PMI (DUAL/QUAD).** One built-in resolver option (6 or 8 pole pairs), with its signals on the motor's LV connector [DanT4000; DanUG]. **D:** a QUAD motor therefore has one resolver for four inverters, so the angle has to be shared.

**S (research).**
- A distributed multi-three-phase generator needed cables to carry position; per-set sensorless estimation is proposed to remove them [Lyu26].
- Per-set sensorless estimates are not identical. The difference tracks uneven current sharing, with errors of a few degrees [Zhu21].
- An injected pulse in one set has been used to correct both sets' estimates [Liu21].
- A six-phase fault-tolerant drive was judged weak because it depended on a single position sensor, so an estimator was added as backup [Green03].
- Decentralised, EtherCAT-synchronised current control at 5 kHz has run on an 11-phase PM machine [Marks24].

**S (class).**
- **BV:** where a speed measuring system is used for control and indication, it must be duplicated with separate sensor circuits and separate power supply [BV-C 4.3.5].
- **DNV 2013:** thrust must not increase substantially if an actual-value signal or a reference is lost. There must be an emergency stop at every control location, independent of the normal stop and separate for each propulsion line [DNV13 Sec.12 A601 d, e].

**D.** A set whose angle is off by Δθ delivers cos Δθ of its torque and a spurious d-axis current of i_q·sin Δθ. At f_e = 30 Hz:

| Angle source | Angle error | Torque lost | Spurious d-axis current |
|---|---|---|---|
| 100 µs link | 1.1° | 0.02% | 2% of i_q |
| 1 ms link | 10.8° | 1.8% | 19% of i_q |
| 5 ms link (ABB master/follower class) | 54° | – | – |

A reference-sharing link is therefore fine for torque but not for angle. A broadcast angle needs ≤100 µs, a time stamp and extrapolation.

**J.**
- One sensor per cell, or at least per pair, is the robust choice. It also satisfies BV duplication once N ≥ 2.
- A single shared sensor feeding N cells makes the sensor (or the master cell exciting it) a common-mode failure. That conflicts with the redundancy argument for splitting the winding in the first place.

### 4.2 Sensorless observers at propeller speeds

**S.**
- Model-based back-EMF and flux observers lose accuracy at low and zero speed. High-frequency injection depends on saliency [Zhu21], which makes it hard on SPM rotors [Bianchi07].
- A dual 3-phase flux + ESO observer has been shown down to 20 r/min [Gao23].
- HF-injection sensorless control has been applied to a dual-winding fault-tolerant PM motor in a ship rim-driven thruster [Bai22].
- A sensorless multiphase induction drive exists for ship propulsion [Terrien04].
- **VACON marine, open-loop PMSM.** It uses I/f control below a frequency limit; defaults are 10% of nominal frequency and 50% I/f current. Start-angle identification is available, and flying start is supported [VaconMarine].
- **ABB, PM control.**
  - An autophasing routine determines the rotor angle offset against the encoder. It has turning modes and standstill modes, the latter for a shaft that can't turn.
  - In open loop, the shaft always turns a little at start, as the rotor aligns to the remanence flux.
  - The drive can determine rotor position when started into a running motor, in open or closed loop.
  - [ACS880FW]
- **NOT FOUND:** what share of marine PM propulsion drives run sensorless versus with an encoder or resolver.

**D.** With T ∝ n², propeller load at 10% speed is about 1% of rated torque, so an I/f start at 50% current has ample margin. The hard case is catching a windmilling shaft.

**J.**
- Keep the resolver as the primary sensor in each cell.
- Run a back-EMF observer continuously as the plausibility check. It is also the limp-home angle source above ~10% speed, and it provides the flying-start angle if the resolver has failed.

---

## 5. Fault behaviour and tolerance

### 5.1 Running on N−1 sets

**S.**
- **Dual 3-phase.** The faulty set is isolated and the other runs alone, so available torque and power drop to **half** at rated current.
  - No special post-fault algorithm is needed if each set is regulated individually. The authors describe this as simple and practical, and in use in railway traction and ship propulsion.
  - 15° displacement gives the best over-rating capability with one set open (24-slot/22-pole study).
  - 30° displacement gives lower unbalanced magnetic force under faults.
  - Source: [Zhu21]
- **ABB precedent.** A cruise ship with two 18–21 MW azimuth units uses dual-stator synchronous motors, VSI-fed and shifted 30°, that can run on one stator. LNG carriers use two 12–15 MW motors per propeller, each on separate converters with double inverter units. Both designs give about 75% redundancy against common single failures [Kanerva09].
- **5 MW ship PM vernier motor.** The dual 3-phase version cuts torque ripple 44% and copper loss 7.7% against the 3-phase version, and still runs at reduced output with one set open [Arish23].
- **Rotor losses.** Post-fault current control adds current harmonics that cause significant magnet eddy loss [Ede02]. Rotor loss in faulty operation depends on the post-fault strategy and the winding [Raminosoa11].
- **Sectored vs distributed.**
  - A low "distribution level" (sector-like) gives less inter-set coupling and better fault tolerance.
  - A high distribution level gives more symmetrical MMF, less local saturation and better overload capability.
  - Source: [Hua23]
  - Modular non-overlapping windings have lower mutual inductance and higher L_d [Li19].
  - In sectored triple 3-phase SPM machines, radial force can be controlled through the sector currents [Sala17].
  - In large PM machines, eccentricity up to ~10% of the air gap is often treated as normal [Qu14].
- **Thermal.** Barcaro et al. evaluate torque, overload and thermal limits under open- and short-circuit faults [Barcaro11]. Overloading the healthy set to cancel short-circuit braking torque did not significantly affect insulation lifetime (aerospace prototype) [Giangrande19].
- **NOT FOUND:** numbers for unbalanced magnetic pull (UMP) in N−1 operation of MW marine machines.

**D.**
- With a set missing, the torque-ripple cancellation in §3.2 is lost. A dual-30° machine on one set shows its full 6th-harmonic ripple.
- On a propeller curve, N−1 gives 71% / 82% / 87% shaft speed for N = 2 / 3 / 4 (table in §7.5).

**J.**
- For N ≥ 3 insist on **distributed** sets.
- For N = 2, a sectored (half-and-half) layout needs a bearing and UMP check at N−1. Ask the motor maker for the radial-force number.

### 5.2 Inverter switch faults in one set

**S.**
- A single-phase (asymmetrical) short is worse than a symmetrical 3-phase short: high pulsating torque and demagnetisation risk. The proposed strategy deliberately converts it into a 3-phase short [Welchko03].
- Fault-tolerant drive topologies are compared for switch, leg and phase faults [Welchko04].
- ABB "reduced run" keeps parallel inverter modules running at limited current with one or more modules out. The out-of-service module must be disconnected on its AC side, or current flows through its freewheeling diodes [ACS880FW].
- VACON: a failed unit must be isolated before the system restarts [VaconMarine].
- Diagnosis of open-switch, open-phase and sensor faults in dual 3-phase PMSMs is covered in [WangX19].

**D.** For a shorted switch, gate on the other two switches of the same rail. This turns the fault into a symmetrical ASC of that set.

**J.** A PM motor keeps back-EMF on a failed set. A set that is simply "off" still rectifies through its diodes whenever the line-to-line peak exceeds its DC link. "Out of service" therefore means either SPO below that speed, or ASC or a disconnector above it.

### 5.3 Winding inter-turn and terminal shorts while the propeller keeps turning

**S.**
- The rotating magnets keep inducing voltage in shorted turns. That drives a high circulating current whose local loss spreads the fault. Reducing the flux linkage of the faulted section (field weakening) reduces the driving voltage [Cintron14].
- Fault-tolerant PM design makes phases electrically, magnetically, thermally and physically independent [Mecrow96]. An engine fuel-pump drive met its specification with any one phase faulted [Mecrow04].
- **Mitigations:**
  - A terminal short with vertical-strip coil layouts [Arumugam15].
  - A terminal short plus a zero-sequence path, or current injection into the faulted set while healthy sets keep producing torque, on a triple 3×3 machine [WangB19a; WangB19b].
- **Caveat for MW machines.** In large **bar-wound** machines, terminal-short mitigation does not work, and current injection is proposed instead [Mitcham04b].
- VACON's marine firmware offers stator-winding monitoring that flags inter-turn shorts and unbalance early [VaconMarine].
- **Class:**
  - AC machines must withstand a sudden terminal short at rated conditions [BV-C 3.3.4].
  - Motors whose excitation cannot be disconnected must have means to prevent rotation (water-milling), e.g. a shaft lock or clutch [DNV13 Sec.12 A201 d].

**J.**
- For MW form-wound, low-turn coils, assume an internal turn fault **cannot** be quenched electrically.
- Response:
  1. Detect it: x–y or negative-sequence residuals and slot temperature per set.
  2. ASC the faulted set. That helps in multi-turn coils and does not hurt otherwise.
  3. Command speed down.
  4. Stop and lock the shaft.
- This is why class wants a shaft lock or clutch, and why a twin-shaft vessel is the easy route.

### 5.4 ASC vs safe pulse-off (SPO) per set, chosen by speed and back-EMF

**S.**
- After high-speed gate removal, a PM machine becomes a generator into the diode bridge. Closed-form current and torque versus speed and Vdc are given in [JahnsCaliskan99].
- With a symmetrical short, the steady current is acceptable, but transient peaks can seriously exceed limits and risk demagnetisation.
  - The worst case is regenerative operation at rated speed, not deep field weakening.
  - The authors propose using the short only in the field-weakening range and converter shutdown at lower speed.
  - Source: [MeyerBoecker06]
- The peak demagnetising current rises with higher characteristic current, lower Rs, pre-fault q-axis current and saturation [ChoiJahns16]. Short-circuit transients can cause irreversible demagnetisation [McFarland14].
- In dual 3-phase SPM machines, the short-circuit peak depends on the rotor position at fault onset [Du21] and on the healthy set's current [Du24]. 15° displacement gives the lowest short-circuit current and braking torque [XuZhu17].
- A propeller-type n² load gives lower short-circuit currents than a constant-speed assumption [Wu20].
- **Transient-limiting methods:**
  - Bounded-transient ASC [FalkOlson24].
  - A two-phase-short transition step [Chen22].
  - A blended turn-off that avoids both ASC overcurrent and freewheel overvoltage [Chandran21].
- There is a trade-off between constant-power speed range and uncontrolled-generator risk, validated on 500 W and 1 MW IPM designs [Pellegrino11].

**D (useful formulas, SPM, per set).**

| Mode | Condition or result |
|---|---|
| Uncontrolled generation | occurs if √3·ω_e·ψ_PM > V_dc (line-to-line peak) |
| ASC steady current | \|i\| = ω_e·ψ / √(R² + ω_e²L²), which tends to I_ch = ψ/L at speed |
| ASC braking torque | T_b = (3/2)·p·ψ²·ω_e·R / (R² + ω_e²L²) |
| ASC torque peak | at ω_e = R/L, of size (3/2)·p·ψ²/(2L), i.e. ≈0.5 pu of the set's rated torque when I_ch = 1 pu |
| ASC drag at propeller speeds | ≈ R/(ωL); ≈1% of set torque for R = 0.01 pu, ω = 1 pu |

So ASC costs almost no torque at speed but brakes hard near standstill. It cannot hold a shaft at zero speed, because T_b → 0.

**J (marine specifics).**
- ASC copper loss is (3/2)·I_ch²·R. That equals rated copper loss when I_ch = 1 pu, and is 4–9× rated when I_ch = 2–3 pu.
- So continuous ASC while towed or windmilling is only acceptable on a ≈1 pu design with the motor cooling running.
- Otherwise the set must be opened with its disconnector, or the shaft stopped.

### 5.5 Overspeed and uncontrolled generation at sea: towing, windmilling, racing, crash-stop

**S.**
- **DNV (2013):**
  - Regenerated power, e.g. from water-milling propellers, must not cause alarms in planned modes or during emergency manoeuvres. Braking resistors must be fitted where necessary [DNV13 Sec.2 B205].
  - The water-milling prevention rule quoted above [DNV13 A201 d].
- **BV:** machines must withstand any overspeed that occurs in service, and winding insulation must withstand manoeuvring overvoltages [BV-C 3.3.1, 3.3.3].
- **KR:** a converter feeding a permanently excited synchronous motor needs a switch-disconnector in the motor–converter line that opens automatically on an inverter fault, plus fault-diagnosis aids [KR25 1606.1]. Start interlocks should include "shaft locking device not released" [KR25 1604.7].
- **Industry:** class societies asked that PM shaft machines be mechanically disconnectable from the shaft. The Switch answered with a quick-release coupling [Switch19].
- **Drive features:** ABB's overvoltage controller reduces generating torque at its limit, and a brake chopper can be added [ACS880FW].
- **NOT FOUND:** marine data on propeller-racing overspeed magnitude and on windmilling rpm versus towing speed for PM propulsion motors.

**J (design rules).**
1. Size the no-load back-EMF so its line-to-line peak (√2·E0,LL) at the **maximum credible shaft speed** stays below the cell's safe DC-link rating. That speed is the overspeed trip plus the racing overshoot, or the towing windmill speed, whichever is higher. Then SPO is always safe and ASC is needed only for internal faults.
   - **D (worked example, E0 = 0.9 × rated voltage):**

   | Winding | Cell / link | √2·E0,LL at rated speed | At 120% speed | Verdict |
   |---|---|---|---|---|
   | 480 V | 1,200 V IGBT, ≈850 V link | ≈611 V | ≈733 V | Rule met |
   | 690 V | 1,700 V IGBT, ≈1,100 V link | ≈878 V | ≈1,054 V | Rule met; the limit is reached at ≈25% overspeed |

   - A 690 V winding cannot be driven at rated voltage from an ≈850 V link in any case, so it belongs with the 1,700 V cell.
2. If rule 1 cannot be met, ASC must be sustainable for as long as the shaft can turn. That means:
   - gate-drive and control power from the ship's 24 V, not from the main DC link, which VACON likewise requires for all control units [VaconMarine];
   - an I_ch ≈ 1 pu motor;
   - or a per-set disconnector plus a shaft lock.
3. Handle crash-stop and racing energy through the DC grid (other note) or a brake chopper. Never rely on diode rectification into an idle cell.
4. For towing: fit a shaft lock or brake per DNV and KR practice, and interlock the cells so they can't be enabled while the lock is engaged [KR25 1604.7].

### 5.6 Class rules on PM propulsion and converter redundancy

| Topic | Requirement (paraphrased) | Rule reference | Read |
|---|---|---|---|
| Single motor on one converter | Provide a standby converter that is easy to switch over to. Double stator windings with one converter per winding are accepted as the alternative. | BV NR467 Pt C Ch 2 Sec 14 [2.1.2] (Jul 2026) | FT |
| Multiple converters or motors on one shaft | Any unit can be taken out and electrically disconnected without affecting the others. | BV [2.1.4]; DNV 2013 Pt.4 Ch.8 Sec.12 A201 d; KR 1604.3(1) | FT |
| Single shaft line, one rotor | One motor with one or two stator winding systems and one rotor needs an **alternative propulsion system**, independent of the main one, able to give safe navigable speed. | BV [2.2.5] | FT |
| Single shaft line, two rotors | May be considered if (a) each winding has a dedicated converter rated ≥50%, (b) the converters are independent, (c) each winding can be disconnected, (d) each rotor can be de-excited or de-fluxed individually. | BV [2.2.6] | FT |
| DNV single failure | A single electrical or control failure must not disable propulsion permanently. Redundancy type R1: manoeuvring power back within 30 s preferably, 45 s at most. No common-mode failures except fire and flooding. | DNV 2013 Sec.12 A201 b, c | FT |
| DNV one motor with two winding sets | The option of one propulsion motor with two sets of windings was **removed** in July 2013. | DNV 2013 change log, Sec.12 A201 | FT |
| PM motors that can't be de-excited | Means to prevent rotation (water-milling) when other propulsion motors run, e.g. a shaft lock or clutch. | DNV 2013 A201 d | FT |
| PM motor–converter disconnector | Opens automatically on an inverter fault; fault-diagnosis devices required. | KR 2025 Pt 6 Ch 1 1606.1 | FT |
| DNV PM machine clause (current) | Sec.5 [3] "additional requirements for permanent magnet machines" exists; PM propulsion motors ≥300 kW need a product certificate. | DNV-RU-SHIP Pt.4 Ch.8 (2021 table of contents) [DNV21toc]; DNV type approval TAE0000498 [DNV-TA] | AB (text NOT FOUND) |
| Speed measurement | Duplicated, with separate sensor circuits and supplies. | BV [4.3.5] | FT |
| Feedback loss | Thrust must not rise substantially if an actual-value or reference signal is lost. | DNV 2013 A601 d | FT |
| Overspeed, overvoltage, short circuit | Withstand in-service overspeed, manoeuvring overvoltage, and a sudden terminal short at rated conditions. | BV [3.3.1], [3.3.3], [3.3.4] | FT |
| Redundant-propulsion notation | RP: ≥50% propulsion power restored after a single failure. Two engines on one propeller are **not** equivalent to two lines. | DNV Pt.6 Ch.2 (Jan 2012) [DNV-RP12] | FT |
| LR, ABS, IRS, IEC 60092-501 | – | NOT FOUND (login or paywall) | – |

In the table, BV refers to [BV-C], DNV 2013 to [DNV13], and KR to [KR25].

**J.**
- **What a split winding does satisfy.** One independent cell per winding set satisfies BV's converter-redundancy clause [2.1.2], and it can be arranged to meet DNV's single-failure clause (A201 b) for converter and control faults.
- **What it does not satisfy.** On a single-screw vessel it does **not** replace an independent alternative propulsion means under BV [2.2.5], and DNV no longer lists one motor with two winding sets as an option.
- **Consequences.**
  - Twin-screw (or screw-plus-thruster) vessels are the clean fit.
  - Every PM installation needs a per-set disconnector (KR) and a shaft-lock strategy (DNV).
- **IRS.** Its rules could not be read. Get the IRS electric-propulsion chapter early, since it matters most for an Indian maker.

---

## 6. Reference products and installations (public data only)

| Product / vessel | Power | Speed | Torque | Voltage | Phases / winding sets | Converter(s) per motor | Source |
|---|---|---|---|---|---|---|---|
| **Danfoss EM-PMI540(B)-T4000** (SRPM) | 284–896 kW (540B: 119–1,000 kW family) | 600–2,400 rpm nominal | 3.6–4.5 kNm continuous; peak 5.93 kNm | 500 V AC | **DUAL = 2, QUAD = 4 galvanically isolated 3-phase systems**, separate connection boxes | peak torque quoted "with two / four 350 A inverters" (one inverter per set) | [DanT4000; Dan540B; DanPS] |
| **Danfoss EM-PMI375-T800/T1100** | ≤296 kW | ≤4,000 rpm | ≤1.1 kNm | 500 / 690 V | DUAL option ("two galvanically isolated three-phase systems") | EC-C1200/EC-C1700 inverters | [DanUG; DanT800; Dan690] |
| **The Switch PMM850M–PMM2000M** | 0.5 to >12 MW | 0–400 … 0–130 rpm | 70–1,500 kNm | 450/500/690 V; MV optional | "dual winding machine" option for converter-failure redundancy | NOT FOUND | [SwitchPMM1500; SwitchPMM850; SwitchBro] |
| The Switch tandem concept | – | – | – | – | two motors on one shaft; >50% power after a motor failure (single-screw) | – | [SwitchBro] |
| **Kongsberg RD-TT** rim tunnel thruster (PM) | 1,000 / 1,600 / 2,100 / 2,700 kW | 340 / 280 / 240 / 212 rpm | ≈28 / 55 / 84 / 122 kNm (D) | – | NOT FOUND | "AFE or 12 pulse drive" | [KongRDTT] |
| Kongsberg RD-AZ rim azimuth (PM) | 0.5–2.6 MW | 252 / 227 / 200 rpm | ≈42–124 kNm (D) | – | NOT FOUND | AFE or 12-pulse | [KongRDAZ] |
| Kongsberg Elegance PM pod | LV design up to 7.5 MW; HV up to 22 MW | – | – | LV / HV | NOT FOUND | LV: variable-frequency drives; HV: diode front end + transformer | [KongEleg] |
| Kongsberg Azipull (L-drive) | – | – | – | – | vertical-shaft PM motor integrated in the steering gear | – | [KongAzipull] |
| **ABB Azipod D** | 2–7 MW, five frames (ducted DZ 2.3–7.0 MW) | – | – | 690 V 3-phase; 3,000 V 3-phase on larger frames | induction **or** "higher-performance" PM motor | NOT FOUND | [Kokkila15] |
| ABB compact Azipod | 1–7 MW; PM motor, no rotor cooling | – | – | – | NOT FOUND | NOT FOUND | [WartsilaAzipod] |
| ABB Azipod M | 7.5–14.5 MW, "fourth generation" PM | – | – | – | NOT FOUND | NOT FOUND | [MarEx-AzM] |
| ABB cruise-ship azimuth units (2009 paper) | 18–21 MW each | – | – | – | **dual-stator** synchronous motors, 30° shift, runnable on one stator | VSIs, one per stator | [Kanerva09] |
| ABB LNG-carrier plants (2009 paper) | 2 × 12–15 MW motors per propeller | – | – | – | – | separate converters with double inverter units; half power after a fault | [Kanerva09] |
| Schottel SRE EcoPeller | 90–3,900 kW | 600–2,100 rpm input | – | – | asynchronous standard; PM optional (1–4.5 MW) | NOT FOUND | [SchottelSRE; MarEx-SRE] |
| Veth Integrated L-drive | 500–2,306 kW | – | – | – | water-cooled PM motor | – | [Veth] |
| Brunvoll RDT rim thruster | 200–1,600 kW | – | – | – | PM motor in one unit | – | [Brunvoll] |
| Voith eVSP | – | – | – | – | PM motor integrated in the Voith Schneider propeller | – | [VoithEVSP] |
| Oswald torque motors | 100–5,000 kW | 0–1,000 rpm | ≤1,000 kNm | – | NOT FOUND | – | [Oswald] |
| Siemens Permasyn (Type 212A) | ≈2 MW (2.85 MW quoted) | – | – | – | 24 single-phase strands | 24 single-phase converters in-motor | [Mueller20; SiemensWO04; WikiT212] |
| DRS PMM (navy) | 36.5 MW | ≈129 rpm (D) | 2.7 MNm | – | NOT FOUND | – | [PTI-DRS] |
| Incat "China Zorrilla" ferry | 8 × 2.4 MW PM motors on waterjets | – | – | common DC | NOT FOUND | Wärtsilä drives on a The Switch DC-Hub; faulty drive module isolated in ~10 µs | [WartsilaIncat; WikiChinaZ; ShippaxSwitch] |
| E-ferry "Ellen" | 2 × 750 kW propulsion + 2 × 250 kW | – | – | – | NOT FOUND | Danfoss Editron DC/AC inverters | [ShipTechEllen; WikiEllen] |
| MV "Ampere" | 2 × 450 kW (Siemens motors) | – | – | – | NOT FOUND | Siemens BlueDrive PlusC | [ShipTechAmpere] |
| Damen RSD Tug 2513 Electric | 70 t bollard pull, 2,800 kWh battery | – | – | – | NOT FOUND | NOT FOUND | [Damen2513] |

**S (why vendors split windings).**
- Danfoss ties the number of inverters to motor and converter current ratings. The QUAD T4000 reaches its peak torque with four 350 A inverters [DanT4000].
- The Switch cites redundancy: reduced-power operation after a converter failure [SwitchPMM1500].
- Naval and submarine systems cite redundancy and availability [Riviera20; SiemensWO04].
- **NOT FOUND:** winding-set data for Kongsberg, ABB, Schottel, Veth, Brunvoll, Voith, Hyundai, Nidec, WEG and Chinese makers (CSSC 712 Institute, CRRC).

**J.** The Danfoss DUAL/QUAD pattern is a direct commercial precedent for your concept: 500 V-class sets, one ~350 A inverter per set, a resolver option on the motor. At 0.3–1 MW it is the benchmark to beat.

---

## 7. Engineering recommendation for the 3-phase cell (all J unless tagged)

### 7.1 Architecture

**Peer cells with a master function.** Each cell is a complete controller for one 3-phase set, running multiple-dq with local harmonic control.

- **Master.** One cell, or the propulsion control system, runs the speed or thrust loop. It broadcasts torque/iq*, sharing weights, the active-set mask and N−1 limits. If the master is lost, a pre-assigned backup cell takes over; each cell holds its last reference and ramps to zero if silent for more than 50 ms.
- **Why not centralised VSD?** It puts all sets on one controller and one link, which defeats the redundancy that class and owners are buying.
- **Optional N = 2 "6-phase mode".** One S32K39 driving both power stages over fibre gate links [NXP-S32K39]. Keep it as an option, not the baseline.

### 7.2 Feature matrix

| Feature | N = 1 | N = 2 (30°) | N = 3 (20°) | N = 4 (15°) | Notes |
|---|---|---|---|---|---|
| Set angle offset θ_k (0.01° el., signed) | 0 | 0 / 30° | 0 / 20 / 40° | 0 / 15 / 30 / 45° | Plus the resolver mounting offset. Auto-calibrate at commissioning from each set's open-circuit back-EMF zero crossings against the angle sensor. Store with the motor serial. Precedents: VACON's per-follower winding phase-shift parameter [VaconMarine]; ABB autophasing of the encoder offset [ACS880FW]. |
| Harmonic loops | 6ω optional | **6ω** (5/7 in x–y) | **6ω + 12ω** (5/7/11/13 in x–y) | **6ω + 12ω** (+18ω optional for 17/19) | Resonant in own dq frame; gain-scheduled with speed; off below ~5% speed; delay-compensated. At 80 Hz fundamental, 12ω = 960 Hz, which needs ≥5 kHz sampling (double-update at 2.5 kHz switching). |
| Dead-time + device-drop compensation | yes | yes | yes | yes | Calibrated per cell. Dominant at low speed (§3.3). |
| Coupling feed-forward | – | from shared reference | same | same | Only fast data is local. |
| PWM SYNC in/out | – | **yes** | yes | yes | Isolated differential or fibre. Master sends carrier-start. Followers trim period ±0.5% to phase-lock with a programmable carrier offset. Accuracy ≤1 µs; loss of sync gives an alarm, not a trip. |
| Carrier offset | – | 0/90/180° settable | settable | settable | Choose per project: common DC link → minimise capacitor current; separate buses → minimise x–y ripple. |
| Shaft bus | vessel CAN | private CAN-FD 1/5 Mbit/s, 1 kHz | same | same | Plus redundant CAN to the propulsion control system from the master and backup. |
| Fault / enable loop | local | hard-wired, ≤100 µs | same | same | Healthy cells respond by clamping to N−1 limits, not by tripping. |
| Angle sensor | own resolver | own resolver per cell, or one per pair plus broadcast | per cell or per pair | per cell or per pair | BV requires duplicated speed measurement with separate circuits and supplies [BV-C 4.3.5]; back-EMF observer as cross-check and fallback. |
| Per-set safe state | ASC/SPO by speed | same, per set, plus coordinated response | same | same | §7.4 |
| Output disconnector interlock | – | aux-contact input, no switching under current | same | same | KR requires an auto-opening disconnector for PM motors [KR25 1606.1]. |
| Winding sensors | PT100 per set | PT100/PT1000 **per set wired to its own cell** | same | same | Danfoss offers 6–12 PT100 options [DanUG]. |
| N−1 limits | – | 50% torque | 67% | 75% | §7.5 |

**Firmware effort on the S32K396** (J, based on the sourced peripheral list [NXP-S32K39; NXP-TP]):
- **PWM.** Two eFlexPWM with NanoEdge. Check in the (NDA) reference manual that an external-sync input can reset or trim the counters. If not, use eMIOS input capture plus a period-trim loop.
- **Resolver.** Two sine-wave generators, four SDADCs and an eTPU-based resolver-to-digital converter, which means two resolver channels per MCU. This allows reading a second resolver or listening to a shared one.
- **Compute.** Rated for "one 6-phase or two 3-phase motors" at >200 kHz control loops, so the 6ω/12ω resonators are not a load problem.

### 7.3 Position sensing choice

**J.**
- **Default: one resolver per cell.** Stacked frameless resolvers on the NDE shaft cost little next to a MW motor. Each cell is then autonomous, and the BV duplication requirement is met automatically once N ≥ 2.
- **Cheaper variant for N = 4: one resolver per pair.**
  - The pair master excites the resolver.
  - The partner cell reads sin/cos through high-impedance differential inputs and samples the excitation for demodulation. This is a firmware work item on the eTPU RDC.
  - The partner cross-checks with its back-EMF observer.
- **Always: back-EMF observer in every cell** as a plausibility check. Above ~10% speed it doubles as a limp-home angle source.

### 7.4 Per-set safe-state logic

**J (with the sourced physics of §5.4).** Each cell decides its own safe state from local measurements, then reports it on the shaft bus. The master applies the N−1 limits.

- **Local measurements.** Speed comes from the cell's own sensor, or from its observer if the sensor has failed. E_LL,pk is estimated as √3·ω_e·ψ_PM(T_magnet). V_dc is the cell's own DC link.
- **Boundaries.** SPO_ok means E_LL,pk < V_dc − margin, with 5–10% hysteresis.

| Event in set k | Action on set k | Other sets |
|---|---|---|
| Control or software fault, sensor loss, comms timeout (gate power OK) | SPO if SPO_ok, else ASC; switch ASC → SPO as speed falls below the boundary (the [MeyerBoecker06] pattern) | Continue; clamp to (N−1)/N torque |
| Shorted switch (DESAT on one device) | ASC on the rail containing the shorted device [Welchko03]; never SPO | Continue; add ASC drag compensation (negligible at speed, rises near standstill) |
| Open switch or phase, or current-sensor fault | SPO if SPO_ok, else ASC | Continue |
| DC-link overvoltage in cell k | ASC (stops diode charging) | Continue |
| DC-link undervoltage or precharge loss in cell k | SPO if SPO_ok; else ASC, which needs gate power from the 24 V aux supply | Continue |
| Motor-side winding fault in set k (x–y / negative-sequence residual, slot temperature) | ASC, then request speed-down and shaft stop and lock | Reduce torque; master commands stop |
| Loss of cell auxiliary power | Hardware ASC latch held from an independent supply; otherwise SPO | Continue if speed < SPO boundary |
| Disconnector opened under fault (KR) | Only at low current: after SPO, or after ASC once the current has decayed; interlocked by the auxiliary contact | Continue |

**J (hardware points).**
- The gate-driver ASC path must be independent of the MCU and powered from the ship's 24 V as well as the DC link, because a towed PM propeller can demand ASC for hours.
- Your current Traction cell notes an ASC hold-up of about 15 ms after total LV loss, with sustained ASC depending on the KL30 feed. For marine use that feed becomes a class-grade, redundant 24 V.

### 7.5 N−1 derating on a propeller curve

**D.** On the propeller law, torque T ∝ n² [Jatskevich06]. With (N−1)/N of rated torque available at rated set current: n = √((N−1)/N) and P = ((N−1)/N)^1.5. For tugs, bollard pull scales roughly with torque.

| Sets N | Torque left | Shaft speed | Power | Notes |
|---|---|---|---|---|
| 1 | 0 | 0 | 0 | needs a second shaft or a take-home device |
| 2 | 50% | 71% | 35% | vessel speed roughly follows shaft speed on a displacement hull (J) |
| 3 | 67% | 82% | 54% | |
| 4 | 75% | 87% | 65% | |
| 8 | 88% | 94% | 82% | |

**J.**
- Allow the healthy sets short-time overload from the cell and motor thermal models: minutes, not continuous. Slot temperature in the healthy set is the limit.
- Re-enable 6ω/12ω control in N−1, because torque-ripple cancellation is lost (§3.2).

### 7.6 Cables, dv/dt and common mode with N sets

**S.**
- One IGBT drive can produce reflected-wave stress up to 2 pu of the DC bus between its own output wires.
- Where output wires of different drives touch in one tray, one can see +2 pu while the other sees −2 pu at the same instant [RockwellWG].
- PWM switching also drives bearing currents [RockwellWG].
- The Danfoss EM-PMI375 specifies ≤3 kV phase-to-phase peak-to-peak and ≤8 kV/µs rise, per IEC 60034-25 curve A [DanT800]. The DUAL/QUAD T4000 offers insulated bearings (+BIA) [DanT4000].

**J.**
1. Give each set its own symmetrical screened 3-core cable with 360° glands. Never mix conductors of different sets in one multicore. Keep the sets' cables apart, or separated, in trays.
2. Match cable lengths per set within a few metres so reflections look alike.
3. Specify inverter-duty insulation, including set-to-set insulation inside shared slots, for ≥2 pu transients. With separate IT-grounded DC buses per set, add the bus-to-bus potential.
4. Use insulated NDE bearings plus a shaft-grounding brush on MW PM motors.
5. Fit dv/dt filters per cell only above ~30–50 m of cable, or when the motor's IEC 60034-25/-18-41 rating demands it.
6. Interleaving can lower common-mode voltage [Alc21]. Set the carrier offset with bearing currents in mind when sets share a DC link.

### 7.7 Paralleling two cells on ONE winding set vs adding sets

**J: add sets. Parallel only as a special product.**

| Aspect | Two cells on one winding | One cell per winding set |
|---|---|---|
| Coupling impedance | External coupling or interphase reactors needed: Siemens asks ≥26–40 µH per module plus equal cables [S120par] (§3.5) | Winding leakage is the coupling inductor; isolated neutrals block zero-sequence |
| Timing | ~100 ns-class matching; common or master PWM; circulating-current loop [Ye02; ZhX18] | ≤1 µs sync is a nicety, not a necessity |
| Fault isolation | A shorted leg or cable takes down the whole winding | One set can be isolated (class-aligned: [BV-C 2.1.2, 2.1.4]) |
| Control | Two MCUs can't safely "negotiate" PWM; needs a master that drives both gate sets | Independent cells, slow link |
| When unavoidable | Off-the-shelf single-winding motor above one cell's current | – |

If an off-the-shelf single-winding motor above one cell's current is unavoidable, do it as a "parallel kit":
- one control card drives two power stages through fibre gate links;
- output reactors (or a coupled inductor) per phase;
- a zero-sequence/circulating-current regulator.

Low-speed MW motors have many poles and parallel paths, so extra winding sets cost mainly terminal boxes and cables. The Danfoss DUAL/QUAD and The Switch dual-winding products show that motor makers will do it.

### 7.8 Preferred number of sets (≈300–350 kW cells)

**D (cell count).** Motor input ≈ shaft/0.96. So 0.3 MW needs ≈312 kW (1 cell), 0.6 MW ≈625 kW (2 cells), 1.2 MW ≈1.25 MW (4 cells), and 2.5 MW ≈2.6 MW (8 cells at 325 kW).

| Shaft | Preferred | Alternative | Why (J) |
|---|---|---|---|
| **≈0.3 MW** | **N = 1** on twin-screw vessels (redundancy at vessel level) | **N = 2** (30°) with two ~150–175 kW cells for single-screw or DP-like duty | With only one propulsion motor, BV requires a standby converter **or** double stator windings with one converter each [BV-C 2.1.2], which N = 2 satisfies. A single-shaft, single-rotor ship still needs an independent alternative propulsion means [BV-C 2.2.5]. |
| **≈0.6 MW** | **N = 2**, dual 30° | – | 6th-harmonic torque cancellation; N−1 = 50% torque / 71% speed; matches Danfoss DUAL practice |
| **≈1.2 MW** | **N = 4**, quad 15° | N = 3 (20°) with 400 kW 1,700 V cells | N−1 = 75% torque / 87% speed; only 24th torque ripple; mirrors Danfoss QUAD |
| **≈2.5 MW** | **N = 8** = 4 phase groups (0/15/30/45°) × 2 isolated sets per group, distributed | Two tandem 1.25 MW quad motors on one shaft ([SwitchBro] tandem concept); or N = 6–7 with 400 kW 1,700 V cells | N−1 = 88% torque. Tandem also covers a motor fault, not just a converter fault, and gives the two-rotor case BV 2.2.6 considers; but it can't de-excite a PM rotor, so expect case-by-case acceptance plus a shaft lock or clutch |

### 7.9 Items to pin down with a motor partner and class (checklist)

1. Per set: L_d, L_q, x–y (leakage) inductance, and I_ch (target ≈1 pu).
2. Back-EMF harmonics per set.
3. Mutual coupling between sets, and the layout (distributed or sectored).
4. Demagnetisation withstand for:
   - a 3-phase short on one set while the others drive;
   - ASC of all sets at maximum speed.
5. Thermal time constants of each set in N−1 operation.
6. Maximum windmilling or towing speed and maximum overspeed (propeller racing). These set the SPO/ASC boundary and the DC-link rating.
7. Current class texts to obtain:
   - DNV Pt.4 Ch.8 Sec.5 [3] (PM machines) and Sec.12;
   - LR Pt 6 Ch 2;
   - ABS 4-8-5;
   - **IRS** Pt 4 Ch 8;
   - IEC 60092-501.

---

## Sources

Read levels:

| Code | Meaning |
|---|---|
| FT | Full text read |
| AB | Abstract or metadata only; IEEE/IET abstracts were read through the DOI or the OpenAlex record |
| V | Vendor document |
| W | Press, encyclopaedia or secondary |

**Multiphase machines: reviews and naval systems**
- [Levi07] Levi, Bojoi, Profumo, Toliyat, Williamson, "Multiphase induction motor drives – a technology status review", IET EPA 1(4), 2007. https://www.theiet.org/media/11279/multiphase-induction-motor-drives-a-technology-status-review.pdf (FT)
- [Levi08] Levi, "Multiphase electric machines for variable-speed applications", IEEE TIE 55(5), 2008. https://doi.org/10.1109/TIE.2008.918488 (AB)
- [Levi16] Levi, "Advances in converter control and innovative exploitation of additional degrees of freedom for multiphase machines", IEEE TIE 63(1), 2016. https://researchonline.ljmu.ac.uk/id/eprint/2767/1/TIE2434999.pdf (FT, partial)
- [Barrero16] Barrero, Duran, "Recent advances in the design, modeling, and control of multiphase machines—Part I", IEEE TIE 63(1), 2016. https://doi.org/10.1109/TIE.2015.2447733 (AB)
- [Rodas21] Rodas et al., "Recent Advances in Model Predictive and Sliding Mode Current Control Techniques of Multiphase Induction Machines", Frontiers in Energy Research, 2021. https://www.frontiersin.org/articles/10.3389/fenrg.2021.729034/full (FT)
- [Zhu21] Zhu, Wang, Shao et al., "Advances in Dual-Three-Phase Permanent Magnet Synchronous Machines and Control Techniques", Energies 14, 7508, 2021. https://eprints.whiterose.ac.uk/180427/1/energies-14-07508-v2.pdf (FT)
- [Barcaro10] Barcaro, Bianchi, Magnussen, "Analysis and tests of a dual three-phase 12-slot 10-pole permanent-magnet motor", IEEE TIA 46(6), 2010. https://doi.org/10.1109/TIA.2010.2070784 (AB)
- [Riviera20] Riviera, "First fully electric US Navy destroyer delivered", 16 Jun 2020. https://www.rivieramm.com/news-content-hub/news-content-hub/us-navy-destroyer-will-be-the-first-to-use-full-electric-power-and-propulsion-59813 (W)
- [NavTech] Naval Technology, DDG-1000 Zumwalt-class project page. https://www.naval-technology.com/projects/dd21/ (W)
- [WikiZum] Wikipedia, "Zumwalt-class destroyer". https://en.wikipedia.org/wiki/Zumwalt-class_destroyer (W)
- [WikiT45] Wikipedia, "Type 45 destroyer". https://en.wikipedia.org/wiki/Type_45_destroyer (W)
- [WikiQE] Wikipedia, "Queen Elizabeth-class aircraft carrier". https://en.wikipedia.org/wiki/Queen_Elizabeth-class_aircraft_carrier (W)
- [GE-QE] GE press release, "GE delivers last eight propulsion motors for giant UK aircraft carriers". https://www.ge.com/news/press-releases/ge-delivers-last-eight-propulsion-motors-giant-uk-aircraft-carriers-0 (W)
- [Lewis02] C. Lewis, "The Advanced Induction Motor", IEEE PES Summer Meeting, 2002. https://doi.org/10.1109/PESS.2002.1043227 (AB)
- [DefDaily] Defense Daily, "DRS wraps up successful permanent magnet motor testing", 2008. https://www.defensedaily.com/drs-wraps-up-successful-permanent-magnet-motor-testing/navy-usmc/ (W)
- [PTI-DRS] Powertrain International, article on Leonardo DRS hybrid-electric and permanent-magnet propulsion. https://www.powertraininternationalweb.com/sustainability/leonardo-drs-develops-new-approaches-for-hybrid-electric-and-permanent-magnet/ (W)
- [SiemensWO04] Siemens AG, WO2004068694A1, "Electric machine for the propulsion drive of a submarine with a permanently magnetically excited synchronous machine". https://patents.google.com/patent/WO2004068694A1/en (FT)
- [Mueller20] C. Müller (Siemens), "Flexibility Demands for Propulsion Systems", UDT 2020 extended abstract. https://cdn.asp.events/CLIENT_Clarion__96F66098_5056_B733_492B7F3A0E159DC7/sites/UDT-2020/media/libraries/draft-abstracts--slides/19-Christian-Mueller.pdf (FT)
- [Janes-FLEX] Janes, "Siemens unveils new FLEX PM modular submarine propulsion motors". https://www.janes.com/defence-intelligence-insights/defence-news/siemens-unveils-new-flex-pm-modular-submarine-propulsion-motors (W)
- [Jatskevich06] Jatskevich, Maksimcev, "Modelling of 15-phase induction motor drive for electric ship propulsion system", WSEAS, 2006. http://www.wseas.us/e-library/conferences/2006cscc/papers/532-114.pdf (FT)
- [Sun15] Sun, Ai, Hu, Chen, "The Development of a 20MW PWM Driver for Advanced Fifteen-Phase Propulsion Induction Motors", Journal of Power Electronics 15(1), 2015. https://koreascience.or.kr/article/JAKO201505458144094.page (AB)

**PM machine design**
- [Mecrow96] Mecrow, Jack, Haylock, Coles, "Fault-tolerant permanent magnet machine drives", IEE Proc. EPA 143(6), 1996. https://doi.org/10.1049/ip-epa:19960796 (AB)
- [Mecrow04] Mecrow et al., "Design and testing of a four-phase fault-tolerant permanent-magnet machine for an engine fuel pump", IEEE TEC, 2004. https://doi.org/10.1109/tec.2004.832074 (AB)
- [ElRefaie10] El-Refaie, "Fractional-slot concentrated-windings synchronous permanent magnet machines: opportunities and challenges", IEEE TIE 57(1), 2010. https://doi.org/10.1109/TIE.2009.2030211 (AB)
- [ElRJahns05] El-Refaie, Jahns, "Optimal flux weakening in surface PM machines using fractional-slot concentrated windings", IEEE TIA 41(3), 2005. https://doi.org/10.1109/TIA.2005.847312 (AB)
- [SoongMiller94] Soong, Miller, "Field-weakening performance of brushless synchronous AC motor drives", IEE Proc. EPA 141(6), 1994. https://doi.org/10.1049/ip-epa:19941470 (AB)
- [Mitcham04] Mitcham, Antonopoulos, Cullen, "Favourable slot and pole number combinations for fault-tolerant PM machines", IEE Proc. EPA, 2004. https://doi.org/10.1049/ip-epa:20040584 (AB)
- [Mitcham04b] Mitcham, Antonopoulos, Cullen, "Implications of shorted turn faults in bar wound PM machines", IEE Proc. EPA, 2004. https://doi.org/10.1049/ip-epa:20040686 (AB)
- [Prieto20] Prieto, Satrústegui, Elósegui, Gil-Negrete, "Multidisciplinary analysis of a 750 kW PMSM for marine propulsion including shock loading response", IET EPA, 2020. https://doi.org/10.1049/iet-epa.2019.0990 (AB)
- [Arish24] Arish, Kamper, Wang, "Design and Optimisation of a 5 MW Permanent Magnet Vernier Motor for Podded Ship Propulsion", World Electric Vehicle Journal, 2024. https://doi.org/10.3390/wevj15030119 (AB)
- [Arish23] Arish, Kamper, Wang, "Performance comparison of 5-MW normal and dual three-phase PM vernier motors for ship propulsion", ACEMP-OPTIM, 2023. https://doi.org/10.1109/acemp-optim57845.2023.10287025 (AB)
- [Bassham03] B. A. Bassham, "An Evaluation of Electric Motors for Ship Propulsion", Naval Postgraduate School thesis, 2003. https://api.openalex.org/works/W81010437 (AB)
- [Lateb05] Lateb et al. (Jeumont), "Performances comparison of induction motors and surface mounted PM motor for POD marine propulsion", IEEE IAS Annual Meeting, 2005. https://doi.org/10.1109/IAS.2005.1518534 (AB)
- [WikiNdFeB] Wikipedia, "Neodymium magnet". https://en.wikipedia.org/wiki/Neodymium_magnet (W)
- [Qu14] Qu, Zhang, "Practical Design Considerations for Large PM Machines", JICEMS, 2014. http://koreascience.or.kr:80/article/JAKO201418342936819.pdf (FT, read by a research agent)

**Vendor products and installations**
- [SwitchPMM1500] The Switch, "Permanent magnet machine Frame 1500M" datasheet. https://theswitch.com/wp-content/uploads/2024/03/Marine_datasheet_PMM1500M_20250313.pdf (V, FT)
- [SwitchPMM850] The Switch, "Permanent magnet machine Frame 850M" datasheet. https://theswitch.com/wp-content/uploads/2026/03/Marine_datasheet_PMM850M_20260817.pdf (V, FT)
- [SwitchBro] The Switch, "Permanent magnet machines for marine" brochure, version 4, 8/2026. https://theswitch.com/machine_brochure_en_web_20260815-2/ (V, FT)
- [SwitchWeb] The Switch, "Permanent magnet machines" (marine). https://theswitch.com/marine/permanent-magnet-machines/ (V)
- [SwitchEff] The Switch blog, "Why smaller ships need a new generation of compact electric machines", 17 Apr 2026. https://theswitch.com/2026/04/17/why-smaller-ships-need-a-new-generation-of-compact-electric-machines/ (V)
- [Switch19] The Switch blog, "Permanent magnet machines: new technology for a conservative industry", 16 Dec 2019. https://theswitch.com/2019/12/16/permanent-magnet-machines-new-technology-for-a-conservative-industry/ (V, summary)
- [DanT4000] Danfoss, "EM-PMI540-T4000 Electric Machine Data Sheet", Feb 2021 (AI269158146706en-000114). https://bnpelektromotoren.nl/wp-content/uploads/2021/03/Datasheet-EM-PMI540-T4000-BP-Elektromotoren-B.V..pdf (V, FT)
- [DanUG] Danfoss, "EM-PMI375 User Guide" (BC265856307805en). https://assets.danfoss.com/documents/latest/524170/BC265856307805en-000209.pdf (V, FT)
- [DanT800] Danfoss, "EM-PMI375-T800 Data Sheet" (AI269157546702en). https://assets.danfoss.com/documents/latest/515777/AI269157546702en-000206.pdf (V, FT)
- [Dan540B] Danfoss, "Danfoss Power Solutions launches Editron EM-PMI540B electric motor…", 4 Nov 2024. https://www.danfoss.com/en/about-danfoss/news/dps/danfoss-power-solutions-launches-editron-em-pmi540b-electric-motor-to-provide-non-stop-full-power-for-electric-machinery/ (V)
- [Dan690] Danfoss, "…launches Editron EM-PMI375 690-volt electric motor…", 4 Dec 2024. https://www.danfoss.com/en/about-danfoss/news/dps/danfoss-power-solutions-launches-editron-em-pmi375-690-volt-electric-motor-creating-a-mobile-grade-solution-for-higher-voltage-applications/ (V)
- [DanPS] Danfoss PowerSource product page, EM-PMI540B-T4000-2400-QUAD. https://powersource.danfoss.com/products/electric-converters-motors-and-systems/electric-motors-and-generators/p/11350572 (V)
- [Kokkila15] Kokkila, Aho (ABB), "Major new development on gearless thrusters: more power, less cost, improved maintainability", Dynamic Positioning Conference, 2015. https://dynamic-positioning.com/proceedings/dp2015/Thrusters_Kokkila_2015.pdf (FT)
- [Kanerva09] Kanerva, Hansen (ABB), "State of the art in electric propulsion — viewpoint on redundancy", IEEE ESTS, 2009. https://doi.org/10.1109/ESTS.2009.4906558 (AB)
- [MarEx-AzM] Maritime Executive, "ABB Expands Azipod Power Range for Ferries and RoPax Vessels", 5 Jun 2019. https://maritime-executive.com/corporate/abb-expands-azipod-power-range-for-ferries-and-ropax-vessels (W)
- [WartsilaAzipod] Wärtsilä Encyclopedia, "AZIPOD (Azimuthing Podded Drive)". https://www.wartsila.com/encyclopedia/term/azipod-(azimuthing-podded-drive) (W)
- [KongRDTT] Kongsberg Maritime, "Rim Drive Tunnel Thruster (RD-TT)". https://www.kongsbergmaritime.com/products/propulsors-and-propulsion-systems/thrusters/direct-electric-drive/rim-drive-tunnel-thruster/ (V)
- [KongRDAZ] Kongsberg Maritime, "Rim Drive Azimuth Thruster (RD-AZ)". https://www.kongsbergmaritime.com/products/propulsors-and-propulsion-systems/thrusters/direct-electric-drive/rim-drive-azimuth-thruster/ (V)
- [KongEleg] Kongsberg Maritime, "Elegance pod". https://www.kongsbergmaritime.com/products/propulsors-and-propulsion-systems/thrusters/direct-electric-drive/elegance-pod-system/ (V)
- [KongAzipull] Kongsberg Maritime, "Azipull". https://kongsbergmaritime.com/products/propulsors-and-propulsion-systems/thrusters/azipull/ (V)
- [SchottelSRE] SCHOTTEL, "SRE SCHOTTEL EcoPeller". https://www.schottel.de/en/portfolio/products/product-details/sre-schottel-ecopeller (V)
- [MarEx-SRE] Maritime Executive, "SCHOTTEL Lays Focus on Sustainability with New SRE EcoPeller", 18 May 2017. https://maritime-executive.com/corporate/schottel-lays-focus-on-sustainability-with-new-sre-ecopeller (W)
- [Veth] Twin Disc, "Veth Integrated L-Drive". https://twindisc.com/product/veth-integrated-l-drive/ (V)
- [Brunvoll] Brunvoll, "Rim Driven Tunnel Thruster". https://www.brunvoll.no/products/rim-driven-thruster-rdt (V)
- [VoithEVSP] Voith, "Electric Voith Schneider Propeller". https://www.voith.com/corp-en/products-services/drives-transmissions/electric-voith-schneider-propeller.html (V)
- [Oswald] OSWALD, "Products Torque". https://www.oswald.de/en/products-torque/ (V)
- [WartsilaIncat] Wärtsilä, "How to power this special, zero-emissions catamaran ferry" (Incat Hull 096). https://www.wartsila.com/marine/products/ship-electrification-solutions/case-incat-tasmania (V)
- [WikiChinaZ] Wikipedia, "China Zorrilla (ship)". https://en.wikipedia.org/wiki/China_Zorrilla_(ship) (W)
- [ShippaxSwitch] Shippax, "The Switch to supply DC-Hub and unique protection devices for world's largest marine battery system", 13 Oct 2023. https://www.shippax.com/en/news/the-switch-to-supply-dc-hub-and-unique-protection-devices-for-worlds-largest-marine-battery-system-.aspx (W)
- [ShipTechEllen] Ship Technology, "Ellen E-ferry: the world's glimpse of the future of ferries", 3 Sep 2019. https://www.ship-technology.com/features/ellen-e-ferry/ (W)
- [WikiEllen] Wikipedia, "Ellen (E-ferry)". https://en.wikipedia.org/wiki/Ellen_(E-ferry) (W)
- [ShipTechAmpere] Ship Technology, "Ampere Electric-Powered Ferry". https://www.ship-technology.com/projects/norled-zerocat-electric-powered-ferry/ (W)
- [WikiT212] Wikipedia, "Type 212 submarine". https://en.wikipedia.org/wiki/Type_212_submarine (W)
- [Damen2513] Damen, "RSD Tug 2513 Electric". https://www.damen.com/vessels/tugs/electric-tugs/rsd-tug-2513-electric (V)

**Control of multi-three-phase drives**
- [ZL95] Zhao, Lipo, "Space vector PWM control of dual three-phase induction machine using vector space decomposition", IEEE TIA 31(5), 1995. https://doi.org/10.1109/28.464525 (AB)
- [Hu17] Hu, Zhu, Odavic, "Comparison of Two-Individual Current Control and Vector Space Decomposition Control for Dual Three-Phase PMSM", IEEE TIA, 2017. https://doi.org/10.1109/TIA.2017.2703682 (AB)
- [Hu14] Hu, Zhu, Liu, "Current control for dual three-phase PMSMs accounting for current unbalance and harmonics", IEEE JESTPE, 2014. https://doi.org/10.1109/JESTPE.2014.2299240 (AB)
- [Che14] Che, Levi, Jones, Hew, Rahim, "Current control methods for an asymmetrical six-phase induction motor drive", IEEE TPEL, 2014. https://researchonline.ljmu.ac.uk/id/eprint/114/ (AB)
- [Kar12] Karttunen et al., "Dual three-phase permanent magnet synchronous machine supplied by two independent voltage source inverters", SPEEDAM, 2012. https://doi.org/10.1109/SPEEDAM.2012.6264448 (AB)
- [Kar14] Karttunen et al., "Decoupled vector control scheme for dual three-phase permanent magnet synchronous machines", IEEE TIE, 2014. https://doi.org/10.1109/TIE.2013.2270219 (AB)
- [KarDOB16] Karttunen et al., "Current harmonic compensation in dual three-phase PMSMs using a disturbance observer", IEEE TIE, 2016. https://doi.org/10.1109/TIE.2015.2461519 (AB)
- [Kar17] Karttunen et al., "Partial current harmonic compensation in dual three-phase PMSMs considering the limited available voltage", IEEE TIE, 2017. https://doi.org/10.1109/TIE.2016.2618786 (AB)
- [Zor17a] Zoric, Jones, Levi, "Vector space decomposition algorithm for asymmetrical multiphase machines", PEE, 2017. https://doi.org/10.1109/PEE.2017.8171682 (AB)
- [RL15] Rockhill, Lipo, "A generalized transformation methodology for polyphase electric machines and networks", IEMDC, 2015. https://doi.org/10.1109/IEMDC.2015.7409032 (AB)
- [Rub20] Rubino et al., "Modular vector control of multi-three-phase permanent magnet synchronous motors", IEEE TIE, 2020. https://doi.org/10.1109/TIE.2020.3026271 (AB)
- [Rub21] Rubino et al., "A novel matrix transformation for decoupled control of modular multiphase PMSM drives", IEEE TPEL, 2021. https://doi.org/10.1109/TPEL.2020.3043083 (AB)
- [Sala19] Sala et al., "Advantages of communication in double three-phase SPM machines fed by independent inverters", EPE, 2019. https://doi.org/10.23919/EPE.2019.8915155 (AB)
- [Gal19] Galassini et al., "Enhanced power sharing transient with droop controllers for multithree-phase synchronous electrical machines", IEEE TIE, 2019. https://doi.org/10.1109/TIE.2018.2868029 (AB)
- [Yep15] Yepes et al., "Current harmonics compensation based on multiresonant control in synchronous frames for symmetrical n-phase machines", IEEE TIE, 2015. https://doi.org/10.1109/TIE.2014.2365155 (AB)
- [Yep17] Yepes et al., "Current harmonic compensation for n-phase machines with asymmetrical winding arrangement…", IEEE TIA, 2017. https://doi.org/10.1109/TIA.2017.2722426 (AB)
- [Xu20] Xu et al., "Current harmonic suppression in dual three-phase PMSM with extended state observer", IEEE TPEL, 2020. https://doi.org/10.1109/TPEL.2020.2989624 (AB)
- [Geng21] Geng et al., "On-line dead-time compensation method for dual three phase PMSM based on adaptive notch filter", IET Power Electronics, 2021. https://doi.org/10.1049/pel2.12192 (AB)
- [Lin25] Lin et al., "Implementation of a current harmonics suppression strategy for a six-phase PMSM", Energies, 2025. https://doi.org/10.3390/en18030665 (AB)
- [Had04] Hadiouche et al., "On the modeling and design of dual-stator windings to minimize circulating harmonic currents…", IEEE TIA, 2004. https://doi.org/10.1109/TIA.2004.824511 (AB)
- [Tess10] Tessarolo, Bassi, "Stator harmonic currents in VSI-fed synchronous motors with multiple three-phase armature windings", IEEE TEC, 2010. https://doi.org/10.1109/TEC.2010.2061852 (AB)
- [Tess10b] Tessarolo et al., "Design and testing of a 45-MW 100-Hz quadruple-star synchronous motor for a liquefied natural gas turbo-compressor drive", SPEEDAM, 2010. https://doi.org/10.1109/SPEEDAM.2010.5545129 (AB)
- [Bassi10] Bassi et al., "Analysis of different system design solutions for a high-power ship propulsion synchronous motor drive with multiple PWM converters", ESARS, 2010. https://doi.org/10.1109/ESARS.2010.5665224 (AB)
- [Zor18] Zoric, Jones, Levi, "Arbitrary power sharing among three-phase winding sets of multiphase machines", IEEE TIE, 2018. https://researchonline.ljmu.ac.uk/id/eprint/6724/ (AB)
- [Sub19] Subotić et al., "Active and reactive power sharing between three-phase winding sets of a multiphase induction machine", IEEE TEC, 2019. https://doi.org/10.1109/TEC.2019.2898545 (AB)
- [Ma24] Ma et al., "An SVPWM strategy with extended linear modulation range … under unbalanced power sharing", IEEE TPEL, 2024. https://doi.org/10.1109/TPEL.2023.3347304 (AB)
- [Ni24] Ni, Li, Zheng, "Control strategy of a hybrid SiC-Si traction inverter for direct-drive multiphase PMSMs in marine propulsion", IEEE TPEL, 2024. https://doi.org/10.1109/TPEL.2024.3434703 (AB)
- [Zh25] Zhang et al., "A novel current harmonic separation and suppression method for twelve-phase PMSMs", ICIEA, 2025. https://doi.org/10.1109/ICIEA65512.2025.11149171 (AB)
- [Dem23] DeMarcos et al., "Interleaving Modulation Schemes in Asymmetrical Dual Three-Phase Machines for the DC-Link Stress Reduction", Machines, 2023. https://doi.org/10.3390/machines11020267 (AB)
- [Bha14] Bhattacharya et al., "Interleaved SVPWM and DPWM for dual three-phase inverter-PMSM", ITEC, 2014. https://doi.org/10.1109/ITEC.2014.6861785 (AB)
- [Sah25] Şahin, Dag, "Reduction in DC-link capacitor current by phase shifting method…", World Electric Vehicle Journal, 2025. https://doi.org/10.3390/wevj16010039 (AB)
- [Tah23] Taha et al., comparison of DC-link voltage and current ripples in symmetric and asymmetric six-phase VSIs, IEEE TPEL, 2023. https://doi.org/10.1109/TPEL.2022.3214096 (AB)
- [Verk19] Verkroost et al., "Module connection topologies and interleaving strategies for integrated modular motor drives", IEMDC, 2019. https://doi.org/10.1109/IEMDC.2019.8785248 (AB)
- [WX20] Wang et al., "Selective torque harmonic elimination for dual three-phase PMSMs based on PWM carrier phase shift", IEEE TPEL, 2020. https://doi.org/10.1109/TPEL.2020.2991264 (AB)
- [WX19] Wang et al., "Torque ripple reduction in sectored multi three-phase machines based on PWM carrier phase shift", IEEE TIE, 2020. https://doi.org/10.1109/TIE.2019.2931239 (AB)
- [Bai25] Bai et al., "Winding comparison of symmetrical dual-three phase PMSMs with PWM carrier phase shift considering leakage inductance", ECCE, 2025. https://doi.org/10.1109/ECCE58356.2025.11260180 (AB)
- [Hop25] Hopfensperger et al., "Investigation of the effect of magnetic coupling on the capacitor current of dual three-phase PMSM drives", ECCE Europe, 2025. https://doi.org/10.1109/ECCE-Europe62795.2025.11238664 (AB)
- [Alc21] Alcaide et al., "Common-mode voltage mitigation of dual three-phase voltage source inverters in a motor drive application", IEEE Access, 2021. https://doi.org/10.1109/ACCESS.2021.3072967 (AB)
- [Liao24] Liao et al., "A Carrier Self-Synchronization Method for Distributed Control Units in Multithree-Phase Permanent Magnet Synchronous Motor", IEEE TIE, 2024. https://doi.org/10.1109/TIE.2024.3472283 (AB)
- [Xu19] Xu et al., "A carrier synchronization method for global synchronous PWM application using PLL", IEEE TPEL, 2019. https://doi.org/10.1109/TPEL.2019.2897725 (AB)
- [Ye02] Ye et al., "Control of circulating current in two parallel three-phase boost rectifiers", IEEE TPEL, 2002. https://doi.org/10.1109/TPEL.2002.802170 (AB)
- [ZhX18] Zhang et al., "Analysis and suppression of circulating current caused by carrier phase difference in parallel voltage source inverters with SVPWM", IEEE TPEL, 2018. https://doi.org/10.1109/TPEL.2018.2826577 (AB)
- [Par13] Parker, Ran, Finney, "Distributed control of a fault-tolerant modular multilevel inverter for direct-drive wind turbine grid interfacing", IEEE TIE, 2013. https://doi.org/10.1109/TIE.2012.2186774 (AB)
- [Lyu26] Lyu et al., "Sensorless control of multithree-phase PMSG in distributed architecture", IEEE TIE, 2026. https://doi.org/10.1109/TIE.2025.3594431 (AB)
- [Lia23] Liang et al., "An enhanced distributed control architecture of multiple three-phase PMSG for improving redundancy", IEEE TPEL, 2023. https://doi.org/10.1109/TPEL.2023.3288049 (AB)
- [Ben21] Benatti et al., "Assessment of master-slave and droop control strategies in multi-three-phase drives", WEMDCD, 2021. https://doi.org/10.1109/WEMDCD51469.2021.9425685 (AB)

**Industrial drive manuals and patents (multi-inverter / multi-winding)**
- [VaconMarine] Danfoss, "VACON NX AC drives – APFIFF09 Marine Application Manual" (DPD01667B, 6 Oct 2020). https://files.danfoss.com/download/Drives/Vacon-NXP-Marine-APFIFF09V224-Application-Manual-DPD01667B-UK-V001.pdf (V, FT)
- [ACS880FW] ABB, "ACS880 primary control program – Firmware manual" (3AUA0000085967 Rev Y). https://search.abb.com/library/Download.aspx?DocumentID=3AUA0000085967&LanguageCode=en&DocumentPartId=1&Action=Launch (V, FT)
- [S120par] Siemens, "SINAMICS S120 Motor Modules Booksize parallel connection", Application Manual 01/2020 (6SL3097-5BF00-0BP0). https://support.industry.siemens.com/cs/attachments/109775623/s120_motor_modules_examples_0120_en-US.pdf (V, FT)
- [SiemensUS17] Müssenberger et al. (Siemens), US 9,571,025 B2, "Motor apparatus having separate winding systems and master module", 2017. https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/9571025 (FT, front page)

**Sensing and faults**
- [Liu21] Liu, Zhu et al., "A simple sensorless position error correction method for dual three-phase PMSMs", IEEE TEC, 2021. https://doi.org/10.1109/tec.2020.3023904 (AB)
- [Green03] Green, Atkinson, Jack, Mecrow, King, "Sensorless operation of a fault tolerant PM drive", IEE Proc. EPA, 2003. https://doi.org/10.1049/ip-epa:20030153 (AB)
- [Marks24] Marks, Summers, Betz, "Decentralised current control for a multiphase PM machine using EtherCAT", IECON, 2024. https://doi.org/10.1109/iecon55916.2024.10905860 (AB)
- [Bianchi07] Bianchi, Bolognani, Jang, Sul, "Comparison of PM motor structures and sensorless control techniques for zero-speed rotor position detection", IEEE TPEL, 2007. https://doi.org/10.1109/tpel.2007.904238 (AB)
- [Gao23] Gao, Dong, "Sensorless control of dual three-phase PMSM based on speed feedback and frequency-variable tracking", PLoS One, 2023. https://pmc.ncbi.nlm.nih.gov/articles/PMC10684025/ (FT, read by a research agent)
- [Bai22] Bai et al., "HF-based sensorless control of a FTPMM in ship shaftless rim-driven thruster system", IEEE T-ITS, 2022. https://doi.org/10.1109/tits.2022.3143208 (AB)
- [Terrien04] Terrien, Siala, Noy, "Multiphase induction motor sensorless control for electric ship propulsion", PEMD, 2004. https://doi.org/10.1049/cp:20040348 (AB)
- [Barcaro11] Barcaro, Bianchi, Magnussen, "Faulty operations of a PM fractional-slot machine with a dual three-phase winding", IEEE TIE, 2011. https://ieeexplore.ieee.org/document/5601764/ (AB)
- [Ede02] Ede, Atallah, Wang, Howe, "Effect of optimal torque control on rotor loss of fault-tolerant PM brushless machines", IEEE TMAG, 2002. https://doi.org/10.1109/tmag.2002.802294 (AB)
- [Raminosoa11] Raminosoa et al., "Rotor losses in fault-tolerant permanent magnet synchronous machines", IET EPA, 2011. https://doi.org/10.1049/iet-epa.2009.0287 (AB)
- [Hua23] Hua, Chen, Hua, "Comparative investigation of dual three-phase PMSMs with different winding configurations", IEEE TIA, 2023. https://doi.org/10.1109/tia.2023.3346835 (AB)
- [Li19] Li, Zhu et al., "Comparative study of modular dual 3-phase PM machines with overlapping/non-overlapping windings", IEEE TIA, 2019. https://doi.org/10.1109/tia.2019.2908138 (AB)
- [Sala17] Sala, Gerada, Gerada, Tani, "Radial force control for triple three-phase sectored SPM machines. Part I", WEMDCD, 2017. https://doi.org/10.1109/wemdcd.2017.7947746 (AB)
- [Giangrande19] Giangrande et al., "Braking torque compensation strategy and thermal behavior of a dual three-phase winding PMSM during short-circuit fault", IEMDC, 2019. https://doi.org/10.1109/iemdc.2019.8785164 (AB)
- [Welchko03] Welchko, Jahns, Soong, Nagashima, "IPM synchronous machine drive response to symmetrical and asymmetrical short circuit faults", IEEE TEC, 2003. https://doi.org/10.1109/tec.2003.811746 (AB)
- [Welchko04] Welchko, Lipo, Jahns, Schulz, "Fault tolerant three-phase AC motor drive topologies: a comparison of features, cost, and limitations", IEEE TPEL, 2004. https://ieeexplore.ieee.org/document/1310399/ (AB)
- [WangX19] Wang X. et al., "Comprehensive diagnosis and tolerance strategies for electrical faults and sensor faults in dual three-phase PMSM drives", IEEE TPEL, 2019. https://doi.org/10.1109/tpel.2018.2876400 (AB)
- [Cintron14] Cintron-Rivera, Foster, Strangas, "Mitigation of turn-to-turn faults in fault tolerant permanent magnet synchronous motors", IEEE TEC, 2014. https://doi.org/10.1109/tec.2014.2360813 (AB)
- [Arumugam15] Arumugam, Hamiti, Gerada, "Turn–turn short circuit fault management in permanent magnet machines", IET EPA, 2015. https://doi.org/10.1049/iet-epa.2015.0020 (AB)
- [WangB19a] Wang B. et al., "Effective turn fault mitigation by creating zero sequence current path for a triple redundant 3×3-phase PMA SynRM", IEEE TPEL, 2019. https://doi.org/10.1109/tpel.2019.2900441 (AB)
- [WangB19b] Wang B. et al., "A turn fault mitigation strategy based on current injection technique for a triple three-phase PMA SynRM", IEEE TIE, 2019. https://doi.org/10.1109/tie.2019.2908595 (AB)
- [JahnsCaliskan99] Jahns, Caliskan, "Uncontrolled generator operation of interior PM synchronous machines following high-speed inverter shutdown", IEEE TIA 35(6), 1999. https://doi.org/10.1109/28.806049 (AB)
- [MeyerBoecker06] Meyer, Böcker, "Transient peak currents in permanent magnet synchronous motors for symmetrical short circuits", SPEEDAM, 2006. https://doi.org/10.1109/speedam.2006.1649806 (AB)
- [ChoiJahns16] Choi, Jahns, "Investigation of key factors influencing the response of PMSMs to three-phase symmetrical short-circuit faults", IEEE TEC, 2016. https://doi.org/10.1109/tec.2016.2594223 (AB)
- [McFarland14] McFarland, Jahns, "Investigation of the rotor demagnetization characteristics of interior PM synchronous machines during fault conditions", IEEE TIA, 2014. https://doi.org/10.1109/tia.2013.2294997 (AB)
- [Du21] Du et al., "Influence of start rotor position on three-phase short-circuit current in dual three-phase SPM machines", IEEE TIE, 2021. https://doi.org/10.1109/tie.2021.3080207 (AB)
- [Du24] Du et al., "Influence of healthy winding group current on three-phase short-circuit and demagnetization in dual three-phase SPM machines", IEEE TIA, 2024. https://doi.org/10.1109/tia.2024.3420825 (AB)
- [XuZhu17] Xu, Zhu et al., "Analysis of dual three-phase PMSMs with different angle displacements", IEEE TIE, 2017. https://doi.org/10.1109/tie.2017.2748035 (AB)
- [Wu20] Wu et al., "Influence of load characteristics on three-phase short circuit and demagnetization of surface-mounted PM synchronous motor", IEEE TIA, 2020. https://doi.org/10.1109/tia.2020.2968036 (AB)
- [FalkOlson24] Falk Olson et al., "Active short-circuit strategy for PMSMs enabling bounded transient torque and demagnetization current", IEEE Access, 2024. https://doi.org/10.1109/access.2024.3440015 (AB)
- [Chen22] Chen et al., "An effective nontransient active short-circuit method for PMSM in electric vehicles", IEEE TIE, 2022. https://doi.org/10.1109/tie.2022.3176315 (AB)
- [Chandran21] Chandran et al., "Safe turn-off strategy for electric drives in automotive applications", IEEE TTE, 2021. https://doi.org/10.1109/tte.2021.3104461 (AB)
- [Pellegrino11] Pellegrino, Vagati, Guglielmi, "Design tradeoffs between constant power speed range, uncontrolled generator operation, and rated current of IPM motor drives", IEEE TIA, 2011. https://doi.org/10.1109/tia.2011.2161429 (AB)

**Class rules**
- [BV-C] Bureau Veritas, NR467 Rules for the Classification of Steel Ships, Part C (NR467 C DT R21), July 2026; Pt C Ch 2 Sec 14 "Electric Propulsion Plant". https://rulesexplorer-docs.bureauveritas.com/documents/nr467/jul2026/467-NR_PartC_2026-07.pdf (FT)
- [DNV13] DNV, Rules for Ships Pt.4 Ch.8 "Electrical Installations", July 2013; Sec.2 B205 and Sec.12 A201, A601 (superseded, but the latest full text obtainable). https://rules.dnv.com/docs/pdf/dnvpm/rulesship/2013-07/ts408.pdf (FT)
- [DNV-RP12] DNV, Rules for Ships Pt.6 Ch.2 "Redundant Propulsion", January 2012 (third-party copy). https://civamblog.wordpress.com/wp-content/uploads/2016/11/ts602.pdf (FT)
- [DNV21toc] DNV-RU-SHIP Pt.4 Ch.8 "Electrical installations", July 2021, table of contents only (third-party mirror). https://pdfcoffee.com/dnv-ru-ship-pt4-ch8-pdf-free.html (AB)
- [DNV-TA] DNV Type Approval Certificate TAE0000498 (PM synchronous motors, Transfluid), 2021. https://www.transfluid.eu/wp-content/uploads/2024/05/TAE0000498-DNV-TA-Electrical-machines_watermark.pdf (FT, read by a research agent)
- [KR25] Korean Register, Rules for the Classification of Steel Ships 2025, Part 6 "Electrical Equipment and Control Systems", Ch 1 Sec 16. https://www.krs.co.kr/KRRules/KRRules2025/data/data_part/english/PART%206_2025.pdf (FT)

**MCU, installation practice, networking**
- [NXP-S32K39] NXP, "S32K39/37/36 Microcontrollers for Electrification Applications". https://www.nxp.com/products/S32K39-37-36 (V)
- [NXP-TP] NXP, "S32K39 LV MC Kit MCSPTR2AK396 DFAE/CAS/TIC training". https://www.nxp.com/docs/en/training-presentation/TP-S32K396-LV-MC-KIT.pdf (V, FT)
- [RockwellWG] Rockwell Automation, "Wiring and Grounding Guidelines for Pulse-width Modulated (PWM) AC Drives", DRIVES-IN001Q, June 2019. https://literature.rockwellautomation.com/idc/groups/literature/documents/in/drives-in001_-en-p.pdf (V, FT)
- [WikiTSN] Wikipedia, "Time-Sensitive Networking". https://en.wikipedia.org/wiki/Time-Sensitive_Networking (W)
