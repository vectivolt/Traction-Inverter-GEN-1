# Marine propulsion drive: requirements brief

**For:** the 220 kW traction-inverter platform (1 200 V SiC or IGBT modules, 500–850 V DC, liquid-cooled, S32K396 + FS26). The goal is a marine product line covering small craft, ferries and 2–5 MW harbour tugs.
**Date:** 23 Sep 2026. **Status:** research brief. Nothing here has been checked by a class society.

**How to read this brief**
- A tag like `[KEY]` means the fact is **SOURCED**. The Sources section at the end gives the title and URL for each key.
- **EJ** marks my engineering judgement. It has no source behind it.
- **NOT FOUND** marks a gap I could not source.
- "Secondary" marks trade press, Wikipedia or exam-prep sites.
- Rule editions matter:
  - The DNV rule text read is the **July 2013** edition. The current DNV portal would not serve PDFs.
  - The ABS Marine Vessel Rules read are the **2018** edition.
  - The BV NR467 text is **July 2026**, which is current.
  - Re-check the DNV and ABS clauses against the current editions before quoting them to a customer.
- The web-search budget ran out part-way through. Some gaps below reflect that, not a real absence of sources.

---

## 0. Executive summary

1. **Three power bands:**
   - Small craft: 20–300 kW per shaft at 350–850 V. The platform fits almost as-is.
   - Ferries: 0.45–1.5 MW per propulsor.
   - Harbour tugs: two thrusters of 0.9–2.1 MW each for 32–70 t bollard pull (BP). [CAND-P12][ST-AMPERE][RIV-FORSEA][BM-EWOLF][MAREX-ZEETUG]
2. **Ferries and tugs mostly sit on ≈ 1 000 V DC grids.** The grid vendors are ABB, Wärtsilä and Siemens-class integrators [ABB-DCG-TN][WARTSILA-EP][SIE-BD-CAT]. Marine battery strings reach **1 096–1 205 V at full charge** [CORVUS-ORCA][CORVUS-DOLE][LECL-MRS3]. That is above the platform's 850 V. **EJ:** keep the 1 200 V platform for systems up to about 800–850 V, and add a 1 700 V variant for 1 000 V grids.
3. **India is live:**
   - Kochi Water Metro: 100-pax boats with 122 kWh LTO batteries, IRS "Battery Prop" notation, Siemens propulsion [BM-MUZIRIS][OE-KWM].
   - GTTP Phase 1: 16 green tugs by 31 Dec 2027 [MOPSW-GTTP][SANSKRITI][GKTODAY][VAJIRAM]. The first are 60 t BP battery-electric tugs, using an ABB DC grid and Kongsberg PM thrusters [CSL-90][ML-ABB-GTTP][CLARITY].
   - No Indian propulsion-converter supplier with public specs was found.
4. **The class route for converters ≥ 100 kW:**
   - IACS UR E10 / DNV CG-0339 environmental type approval (TA).
   - A per-delivery product certificate (DNV) or witnessed tests (ABS and LR from 100 kW, BV from 50 kVA).
   - IACS E22 **Category III** software discipline for propulsion control.
   - Sources: [E10][CG0339][DNV13][ABS18][LR-P6C2-1][BV467C][E22]
5. **Behaviours class insists on:**
   - Automatic power reduction rather than a trip.
   - No single failure takes out all propulsion. ABS and BV do not accept a dual-winding motor alone as that redundancy.
   - Cooling leak, flow and resistivity alarms.
   - DC-link discharged below 60 V within 5 s.
   - The drive must work with insulation monitoring on unearthed (IT) networks.
   - Sources: [DNV13][ABS18][BV467C][DF-EC1200-DS]
6. **A BV rule may set the voltage class.** BV requires each semiconductor's repetitive peak voltage to be at least 1.5 × UP on a dedicated propulsion supply, or 1.8 × UP on a common bus [BV467C]. Taken literally, 1 200 V parts are then limited to about 800 V / 667 V of DC link. Get BV's interpretation early.
7. **EJ device choice:**
   - SiC for small craft, water taxis and pilot boats.
   - **1 700 V IGBT** for ferries and tugs on 1 000 V grids. Competitors run 1 700–1 800 V IGBTs at 1.25–4 kHz there [SIE-BD-CAT][DF-EC1700-DS].
8. **EJ thermal targets for 20 years:**
   - Continuous Tvj ≤ 125 °C (IGBT) or ≤ 135 °C (SiC).
   - ≤ 150 °C for overloads up to 60 s.
   - **ΔTj ≤ 30 K per load pulse** for SiC and for all tug duty. These come from the Semikron power-cycling model in §8.3 [AN21].
9. **Benchmark: Danfoss Editron EC-C1200-450.** It is 850 V DC, 300 kVA, IP67, glycol-cooled up to 65 °C, and uses one module as inverter, active front end (AFE) or DC-DC. It holds DNV TA at Temperature A / Vibration B / Humidity B / EMC A [DF-TA-EC1200][DF-EC1200-DS]. It is the closest analogue to this platform.

---

## 1. Vessel segments and their propulsion

### 1.1 Segment overview

| Segment | Installed propulsion (SOURCED examples) | Per shaft / thruster | Motor speed | Motor type seen | Redundancy practice |
|---|---|---|---|---|---|
| Water taxi / small passenger (10–30 m, 30–100 pax) | Candela P-12: 2 × C-POD **110 kW cont / 160 kW peak**, 378 kWh [CAND-P12]. Navalt Aditya/Indra: **2 × 20 kW** at 700 rpm, 50–80 kWh [WIKI-ADITYA][WIKI-INDRA] (secondary). Torqeedo Deep Blue 100i: 100 kW with a ≈ 350 V pack [TORQ]. RAD 120: 120 kW, 350–450 V [RAD] | 20–160 kW | 700 rpm (Navalt) up to high-speed pods | PM (Navalt's "PM asynchronous" is as written in the source) [WIKI-ADITYA] | Two independent motor + battery trains (Navalt Aditya, Indra, Limo) [PLUG-ADITYA][WIKI-INDRA][PLUG-LIMO] |
| Inland / coastal ferry (car, pax) | Ampere: 2 × 450 kW [ST-AMPERE]. Ellen: 2 × 750 kW propulsion + 2 × 250 kW thrusters [WIKI-ELLEN]. ForSea: 4 × 1.5 MW azimuths [RIV-FORSEA]. MF Hamlet retrofit: 4 × 1 530 kW PM azimuth motors [KM-HAMLET] | 0.45–1.5 MW | Geared azimuths (e.g. RR Azipull on Ampere) or direct PM | SRPM (Editron on Ellen) [DF-ELLEN]; PM azimuth motors (Hamlet) [KM-HAMLET] | Double-ended, two or more propulsors |
| Harbour / escort tug (ASD) | eWolf: 2 × Schottel SRP 430 LE 2 050 kW, 2 × 2 100 kW motors, 70 short tons (63.5 t) BP [BM-EWOLF][CROWLEY-SPEC]. Sparky: 70 t BP [DAMEN-SPARKY]. Zeetug30: 2 × 925 kW, 32 t BP [ZEETUG][MAREX-ZEETUG]. India GTTP: 60 t BP battery-electric [CSL-90][CLARITY] | 0.9–2.1 MW | Geared azimuth input 750–1 800 rpm (Schottel SRP 340–460) [SCH-SRP]; PM azimuth 187–252 rpm (Kongsberg AZ-PM) [KM-AZPM] | PM thrusters now offered for tugs (Kongsberg, Veth) [KM-AZPM][VETH] | Always two thrusters; emergency gensets on eWolf and Sparky [CROWLEY-SPEC][OE-SPARKY] |
| Pilot boat / crew transfer vessel (CTV) | **NOT FOUND** | — | — | — | — |
| OSV / CSOV (context) | Ingeteam reference: 2 × 2.2 MW + 2 × 900 kW motors, 450 kWh at 700–1 000 V DC [ING-E3]. Rem CSOVs use Kongsberg PM rim-drive thrusters [AMTI-REM] | 0.9–2.2 MW | direct PM | PM | Each thruster fed from two switchboards (BlueDrive PlusC) [SIE-PLUSC] |

**Bollard pull vs installed power:** no designer rule of thumb was found (NOT FOUND). **EJ arithmetic from sourced numbers:**
- eWolf: 63.5 t / 4 100 kW ≈ **1.55 t per 100 kW** [BM-EWOLF].
- Kongsberg AZ-PM 1900: 191 kN / 1 100 kW ≈ 1.8 t per 100 kW [KM-AZPM].
- AZ-PM 2600: 411 kN / 2 600 kW ≈ 1.6 t per 100 kW [KM-AZPM].
- So a 60–70 t tug needs about 2 × 1.9–2.3 MW. Robert Allan / Schottel tugs span 20–125 t BP [SCH-RA500].

### 1.2 Propeller and motor speeds, motor types (SOURCED)

| Item | Data |
|---|---|
| Geared azimuth (Schottel SRP) | SRP 340: 1 400 kW; 360: 1 530 kW; 430: 2 000 kW (2.5 m prop); 460: 2 350 kW (2.7 m); 490: 2 550 kW. Input speed **750–1 800 rpm**; L-drive variant ≈ 660–780 rpm [SCH-SRP] |
| Motor on top of the azimuth (LE-Drive) | Accepts any motor make or type. Dropping the upper gearbox gains about 3 % efficiency [ML-LEDRIVE]. eWolf uses SRP 430 LE [BM-EWOLF] |
| PM motor in the pod | Kongsberg AZ-PM 1900: 500–1 100 kW, **252 rpm**, 191 kN bollard, fed by an AFE or 12-pulse drive. AZ-PM 2600: 1.1–2.6 MW, **187 rpm**, 411 kN [KM-AZPM] |
| PM rim drive | Kongsberg RD-AZ: 500–2 600 kW, 200–252 rpm, motor coupled directly to the propeller [KM-RDAZ] |
| Integrated PM L-drive | Veth: 500–2 306 kW, water-cooled PM motor [VETH] |
| Low-speed direct-drive PM | The Switch: 0–130 rpm (4–12 MW), 0–220 (2–4 MW), 0–250 (1–2 MW), 0–400 rpm (0.5–2 MW) [SWITCH-PMM] |
| High-speed PM / SRPM (feeding a gearbox) | Danfoss EM-PMI540: 119–896 kW, 0–4 000 rpm, IP67 [DF-MOTORS]. EM-PMI375 is built for 690 V AC / 1 050 V DC systems [DF-PMI375] |
| Pods | Azipod D: 1.6–7 MW, induction motor, or PM at higher outputs [RIV-AZIPODD] (secondary). Compact Azipod: 0.4–5 MW, PM [ABB-COMPACT] |

### 1.3 Multi-winding motors for redundancy

- **Where it exists (SOURCED):**
  - ABB verified a "fully redundant" drive on a 1 MW lab machine: a dual-star synchronous motor with two isolated windings 30° apart, each fed by its own ACS6000 converter. It keeps running on one winding [ABB-EPE07].
  - Cruise-ship propulsion motors use two stator "half-drives" [MI-CRUISE] (secondary).
- **ABS:** a single motor with dual windings does **not** meet drive-train redundancy where electric motors are the sole propulsion [ABS18 4-8-5/5.3.1].
- **BV:**
  - A single motor on one converter needs a standby converter. Double stator windings with one converter each is an accepted alternative [BV467C Sec14 2.1.2].
  - A single-shaft, single-rotor ship still needs an independent alternative propulsion system [2.2.5].
  - Two stators with two independent rotors may be accepted, but only with independent converters each rated for at least 50 % of power [2.2.6].
- **LR:** sole electric propulsion needs **at least two independent motors** [LR-P6C2-16].
- **NOT FOUND:** any ferry, tug or small craft using a dual-winding motor.
- **What small vessels actually do:**
  - Two fully independent trains.
  - Paralleled converters per motor: two HES880 per motor on Zeetug30 [MAREX-ZEETUG]; two EC-C1200 per 370 kW motor in an Editron ferry design [DF-FEGG].

### 1.4 Reference vessels (published figures)

| Vessel | Type | Battery | DC bus | Propulsion | Suppliers / class | Src |
|---|---|---|---|---|---|---|
| Kochi Water Metro 100-pax (India, 2022 →) | 24–24.8 m aluminium catamaran | **122 kWh LTO** (Echandia, Toshiba cells); ≈ 10–15 min charge | NOT FOUND | Twin screw; 8 kn electric, 10 kn diesel (hybrid) | Siemens propulsion; Cochin Shipyard; **IRS "Battery Prop"**; fleet plan 78 (23 × 100 + 55 × 50 pax) | [BM-MUZIRIS][OE-KWM][RIV-ECHANDIA][ST-KWM][KWM] (secondary except KWM) |
| Navalt Aditya (Kerala, 2017) | 75-pax solar ferry | 50 kWh Li-ion + 20 kW solar | NOT FOUND | 2 × 20 kW at 700 rpm | IRS +IW Zone 3 | [WIKI-ADITYA] (secondary) |
| Candela P-12 (Sweden) | 12 m hydrofoil, 30 pax, 25 kn | 378 kWh nominal / 336 usable; DC charging up to 300 kW | NOT FOUND | 2 × 110 kW cont / 160 kW peak (a 2023 release said 88 kW and 252 kWh) | DNV type approval | [CAND-P12][ELEC-P12] |
| MF Ampere (Norway, 2015) | 80 m, 120 cars | ≈ 1 000–1 090 kWh (Corvus); 150–200 kWh per crossing | NOT FOUND | 2 × 450 kW, Azipull | Siemens BlueDrive PlusC | [ST-AMPERE][CORVUS-AMPERE][NORLED][SIE-PLUSC] |
| E-ferry Ellen (Denmark, 2019) | 59.4 m, 198 pax / 31 cars | 4.3 MWh Leclanché G-NMC; 4 MW charging | NOT FOUND | 2 × 750 kW + 2 × 250 kW, Editron SRPM | Danfoss Editron; 85 % grid-to-propeller | [WIKI-ELLEN][DF-ELLEN][RIV-ELLEN] |
| ForSea Tycho Brahe / Aurora (conversion 2018) | 20 min crossing | 4 160 kWh each (Tycho later 6 400 kWh); ≈ 10 MW for 6–9 min | ABB Onboard DC Grid | 4 × 1.5 MW azimuths | ABB | [MAREX-FORSEA][RIV-FORSEA][SHIPPAX] |
| Bastø Electric (Norway, 2021) | 139 m, 200 cars | 4.3 MWh (designer says 4 000 kWh); 9 MW charging (Riviera: 7.2 MW) | NOT FOUND | Azimuths at both ends | Siemens Energy | [ELEC-BASTO][RIV-BASTO][CT-BASTO] |
| Sparky (NZ, 2022) | Damen RSD-E 2513 | 2 784–2 800 kWh; ≈ 1 400 kW charging, 2 h | NOT FOUND | 2 azimuths with 3 m props; **70 t BP** | Damen | [DAMEN-SPARKY][OE-SPARKY][SS-SPARKY] |
| eWolf (USA, 2023) | Ship-assist tug | 6.2 MWh Corvus | ABB Onboard DC Grid | 2 × 2 100 kW driving SRP 430 LE; 70 short tons BP | ABS | [BM-EWOLF][ML-EWOLF][CROWLEY-SPEC] |
| HaiSea Wamis (Canada, 2023) | Robert Allan ElectRA 2800 | 5 288 kWh (other reports: 6 000 / 3 616 kWh) | NOT FOUND | L-drives with motors on top; 65 t BP through-life, ≈ 70 t at trials | Sanmar; ABS | [HAISEA][SEASPAN][MAREX-HAISEA][RIV-HAISEA] |
| Zeetug30 (Türkiye) | Harbour tug | 1 500 kWh Corvus Orca | HES880 class (730 V nominal) | 2 × 925 kW; 32 t BP; 2 inverters per motor | 14 × ABB HES880; Türk Loydu | [ZEETUG][MAREX-ZEETUG] |
| GTTP tugs (India, 2026–27) | 60 t BP battery-electric: 2 at JNPA (CSL for Polestar), 1 at Deendayal (DPA) Kandla (Atreya) | domestic batteries (JNPA pair); size NOT FOUND | ABB Onboard DC Grid (JNPA pair) | Kongsberg PM thrusters + ESS (Kandla) | Robert Allan design | [CSL-90][ML-ABB-GTTP][CLARITY] (secondary except CSL) |

**India's Green Tug Transition Programme (GTTP), SOURCED:**
- Phase 1 runs Oct 2024 to 31 Dec 2027: two tugs each at Deendayal, Paradip, JNPA and VOC, plus one at each of the other eight major ports (16 total). Budget ≈ ₹1 000 crore.
- Targets: 50 green tugs by 2030 and 100 % green by 2040.
- Tugs must follow the ASTDS-GTTP standard designs and be built in Indian yards.
- Propulsion options: battery-electric, methanol, hydrogen fuel cell, hybrid.
- Sources: [MOPSW-GTTP][GKTODAY][VAJIRAM][SANSKRITI].
- Cochin Shipyard has 17 tug orders, six of them green [WEEK-CSL], and is building battery-electric tugs for Svitzer [CSL-HOME].

---

## 2. Operating profiles, duty and life

### 2.1 Duty cycles (SOURCED)

| Segment | Profile | Src |
|---|---|---|
| Short-crossing ferry (Ampere) | 34 trips a day of 20 min, 365 days a year; 150 kWh per crossing; 10 min charge per berth; ≈ 2 GWh a year | [EPRI-AMPERE] |
| Mid-length ferry (Ellen) | Three 22 nm round trips with 10–15 min charging between, plus a 45 min midday top-up; ≈ 1 600 kWh per round trip; 4 MW (1C) charging; 12.6 kn | [BOELL-ELLEN][AERO-ELLEN][DF-ELLEN] |
| High-frequency ferry (ForSea) | 20 min at 10.5 kn; ≈ 1 175 kWh per trip; charges 6–9 min at 10.5 MW; up to 46 departures a day | [WIKI-TYCHO][FORSEA-PR] |
| Large ferry (Bastø) | ≈ 30 min crossing; up to 9 MW at berth; ≈ 36 000 departures a year; planned 20 h a day, 7 days a week | [CT-BASTO][SIE-FERRYTALE] |
| Harbour-tug time split (Foss, LA/LB) | Dock 53 %, standby 7 %, transit 16 %, assist 17 %, barge 5 %, shore power 1 % | [WRENN-THESIS] (secondary) |
| Harbour-tug power split | Under 10 % of time at full power; some tugs idle over 80 % [PROMAR]. Idle up to 50 %; transit and waiting under 20 % load [DEVARAPALI]. One profile: 65 % of time at ≈ 10 % power, 20 % at ≈ 30 %, 15 % at ≈ 90 % [MEMIS] (blog) | as cited |
| Tug hours | 260 days = 3 120 h a year including harbour standby [WARTSILA-HYTUG]; main engines 3 000–5 000 h a year at ≈ 40 % average load [MEMIS] | as cited |
| Electric tugs in service | Sparky: two or more jobs per charge; ≈ 2 h recharge at ≈ 1 400 kW [DAMEN-SPARKY][SS-SPARKY]. eWolf: a full day per charge, with morning and evening peaks [ML-CHARGE][UCI-CARB] | as cited |
| Escort tugs | California escort needs 90 short tons of **sustained** BP; ship-assist tugs are 5 000–6 800 hp | [UCI-CARB] |

**EJ:**
- Ferries give **one full thermal cycle per crossing** (≈ 1.2–1.7 × 10⁴ a year at 34–46 crossings a day), with cruise plateaus at about 50–60 % of installed power. That is arithmetic: Ampere 150 kWh per 20 min against 900 kW, and ForSea 1 175 kWh per 20 min against 6 MW.
- Tugs are mostly idle but need **full power for minutes, many times per job**.
- Both cases drive the ΔTj limits in §8.3.

### 2.2 Life

| Item | SOURCED | Src |
|---|---|---|
| Vessel life | Ellen: planned for 30 years with two complete battery replacements | [BOELL-ELLEN] |
| Battery cycle life | Ellen: 24 500 cycles to 80 % state of health at 39 % average depth of discharge, ≈ 11.7 years [AERO-ELLEN]. Sparky: ≈ 30 000 cycles expected [DAMEN-SPARKY-2]. Corvus Blue Whale NxtGen: "up to 15 years" [BW-BWNXT]. Echandia LTO: 50 000–70 000 cycles [ECHANDIA-DS] | as cited |
| Drive or converter design life (years or hours) | **NOT FOUND.** No vendor publishes it in the material reached | — |

### 2.3 Class requirements that shape a propulsion drive

| Requirement | Rule text (paraphrased, SOURCED) | Src |
|---|---|---|
| Stopping and reversing | Torque must allow the vessel to be manoeuvred, stopped or reversed from full service speed | DNV13 Sec.12 A301; BV Sec14 1.2.1 [DNV13][BV467C] |
| Overload | Plant needs enough overload capacity (torque, power, reactive power) for starting, manoeuvring and **crash stop**. Converters are rated 100 % continuous **plus a declared overload current and duration**, and must survive two consecutive starts | DNV13 Sec.12 A302, Sec.7 A201 [DNV13]; BV 1.2.4 [BV467C]; ABS: "specified overload capabilities" where the application needs them [ABS18 4-8-3/8.5.1] |
| Motor short-time capability | Induction motors: +60 % torque for 15 s. AC motors: +50 % current for ≥ 120 s. Overspeed 1.2 × for 2 min. BV lets converter current limiting replace the motor over-torque test | DNV13 Sec.5 [DNV13]; BV Ch2 Sec4 4.6.2 [BV467C] |
| Trip philosophy | Shut down only for faults that would severely damage equipment. Overcurrent settings must not act in manoeuvring or heavy seas (ABS adds floating broken ice). Automatic power reduction is accepted. Give a pre-warning before any shutdown | DNV13 Sec.12 A603 [DNV13]; BV 2.4.1–2.4.4 [BV467C]; [ABS18 4-8-5/5.7.1] |
| Power-reduction triggers | High winding temperature, fan or cooling failure, loss of converter coolant, or insufficient power → automatic reduction. Each control station shows "Power limitation" | BV 4.5.1 [BV467C]; [ABS18 4-8-5/5.13.2] |
| Cooling | A ventilation or cooling failure may take out only one propulsion line. Force-ventilated converters need at least two fans | DNV13 Sec.12 A203 [DNV13]; BV 3.1.2 [BV467C] |
| Single failure | No single electrical or control failure may permanently disable propulsion. DNV R1: manoeuvring power back within 30 s (45 s maximum) | DNV13 Sec.12 A201 [DNV13] |
| PM windmilling | Motors that cannot be de-excited (PM) need a way to stop propeller rotation. Regenerated power from a windmilling propeller must not raise alarms | DNV13 Sec.12 A201 d), Sec.2 B205 [DNV13] |
| Semiconductor voltage margin | Repetitive peak voltage of each valve ≥ **1.5 × UP** on a propeller-dedicated supply, or ≥ **1.8 × UP** on a common main supply, where UP is the peak of the rated input voltage. Add 10 % for series-connected elements | BV Sec14 3.4.1–3.4.2 [BV467C] |
| Sea trials | Crash-stop manoeuvres. ABS: a full-ahead to full-astern reversal with currents, voltages and speed recorded, plus endurance runs at full load | DNV13 Sec.12 B101 [DNV13]; [ABS18 4-8-5/5.19] |
| IMO manoeuvrability | Full-astern track reach ≤ 15 ship lengths. Applies to ships ≥ 100 m and to chemical and gas carriers, so not to most ferries and tugs in scope. SOLAS II-1/28 gives no number | [IMO-MSC137][SOLAS-28] |
| Ice | UR I3 (Polar Class) assumes electric-motor fixed-pitch propellers turn at **full** nominal speed at bollard or in ice (diesel: 0.85 ×). Shaft torque comes from the ice-milling sequence plus actual motor torque. BV: locked-rotor torque for ice must be considered | [IACS-I3] (2007 text); BV 1.2.4 [BV467C] |
| Ice motor-torque multiple ("X × nominal") | **NOT FOUND** | — |

**IEC 60092-501, Ed.6 (2025):**
- Its scope now includes battery and fuel-cell sources and podded or azimuth drives. Ed.5 (2013) excluded battery propulsion.
- It defines a **single failure criterion** and has a normative protection-and-alert matrix (Annex A).
- It normatively references IEC 61800, IEC 62477 and MSC.137(76).
- Sources: [IEC-501-2025][IEC-501-SAMPLE]. The requirement clauses themselves are behind a paywall (NOT FOUND).

---

## 3. Onboard DC grids and battery practice

### 3.1 DC bus voltages in service (SOURCED)

| System | DC bus | Src |
|---|---|---|
| ABB Onboard DC Grid | **1 000 V DC nominal**; an LV concept for ships up to at least 20 MW; replaces the 690 V AC main switchboard (≈ 40 % less cable) | [ABB-DCG-TN][ABB-DP2011][ABB-EEHB] |
| Wärtsilä | "A DC system is always in Low Voltage (1000V) setting" | [WARTSILA-EP] |
| Siemens BlueDrive / PlusC | 690 V AC propulsion drives; common-DC-bus versions on request; PlusC is the DC-grid platform. The DC-link braking chopper starts at 1 040 V DC (full at 1 070 V). Nominal DC not published | [SIE-BD-CAT] |
| Danfoss Editron | EC-C1200: 0–850 V, **750 V nominal**. EC-C1700B: 0–1 200 V, **1 050 V nominal**. One published ferry design uses a 750 V DC link | [DF-EC1200-DS][DF-EC1700-DS][DF-FEGG] |
| ABB HES880 | 730 V DC nominal (web page: 850 V DC) | [ABB-HES-TA][ABB-HES-WEB] |
| Ingeteam E3-Ship | Battery 700–1 000 V DC; bus-tie rated 1 500 V DC / 5 000 A | [ING-E3] |
| 1 500 V DC | Seen only as the upper LV-DC limit in standards and in DC shore-connection work. Every onboard battery found is ≤ 1 205 V. **No 1 500 V vessel found** | [IEEE-80005-4][ABS-DC22] |

### 3.2 What counts as low-voltage DC (SOURCED)

| Source | Definition |
|---|---|
| IEC 60092-101:2018 | Applies to **1 000 V AC and 1 500 V DC**. Extra-low voltage is ≤ 50 V AC / ≤ 120 V DC [IEC-101] |
| DNV Pt.4 Ch.8 (2013) | Low voltage includes DC systems whose **maximum** voltage is ≤ 1 500 V; high-voltage DC is > 1 500 V [DNV13] |
| ABS DC Power Distribution Requirements (2022) | LVDC is up to and including 1 500 V DC; optional LVDC-DIST notation [ABS-DC22] |
| IEC 60092-503 / IACS UR E11 | High-voltage parts are AC-only; no DC high-voltage part exists [IEC-503][IACS-ELEC] |
| IEC PAS 63108:2017 | "Primary DC distribution – System design architecture" [IEC-PAS63108] |
| DC supply quality | ±10 % continuous, 5 % cyclic, 10 % ripple. Battery systems: +30/−25 % while charging, +20/−25 % otherwise [IACS-ELEC][ABS-DC22]. The 2013 DNV text said −15/+30 % [DNV13] |

### 3.3 Chemistries and vendor voltage windows (SOURCED)

| Product | Chemistry | Voltage and modules | Rate | Approvals | Src |
|---|---|---|---|---|---|
| Corvus Orca | NMC / graphite | Pack 38–136 kWh at **350–1 200 V DC**; 5.6 kWh / 128 Ah modules in 50 V steps | ≤ 3C / 3C | DNV, BV, ABS, RINA | [CORVUS-ORCA] |
| Corvus Dolphin NxtGen Energy | NCA | String 33–197 kWh at **130–1 205 V DC** | 0.5C continuous, 1C for 10 s | pending | [CORVUS-DOLE] |
| Corvus Dolphin NxtGen Power | NCA | ≤ 1 200 V DC; 6.56 kWh modules; liquid-cooled; listed for tugs | 1.5C continuous, 2.5C for 10 s | DNV pending | [CORVUS-DOLP] |
| Corvus Blue Whale | LFP | Pack 336–5 472 kWh at **560–1 120 V DC**; 48.2 kWh modules in 80 V steps | 0.7C continuous, 1C for 20 min | DNV / LR targeted | [CORVUS-BW] |
| Leclanché Navius MRS-3 | G/NMC, G/NMCA | String ≤ 1 200 V DC; modules 33.5–67.1 V; example strings 756 / 937 / **1 096 V** and 513 / 636 / 744 V (min / nom / max) | 2.7C continuous, 4.6C peak | DNV, BV, LR | [LECL-MRS3] |
| Echandia E-LTO | LTO (Toshiba cells) | Examples at 1 000 V max; integrated pre-charge; string breaker sized for the maximum short-circuit contribution | 160 A continuous / 400 A for 10 s (Energy) | DNV, BV | [ECHANDIA-DS][RIV-ECHANDIA] |
| Toshiba SCiB | LTO | 2.3 V cells; 2P12S modules at 27.6 V | — | — | [TOSHIBA-SCIB] |

- **Trend (secondary):**
  - NMC leads, but LFP is gaining.
  - ≈ 1 000 battery vessels were in operation in mid-2025, with ≈ 550 on order. The split is 65 % hybrid, 17 % plug-in hybrid, 17 % fully electric [ML-BATT26].
  - Corvus is moving toward LFP [WB-CORVUS].
- **Architecture:**
  - Modules of 12 V to over 100 V are wired into series strings at system voltage, and strings are paralleled.
  - A DC-DC converter usually sits between battery and grid [DNVGL-HB16].
  - Kongsberg instead markets a floating DC link with no battery DC-DC stage [KM-DCHYB].
- **EJ consequence:** full-charge string voltages of 1.1–1.2 kV rule out a direct 850 V connection. The platform needs one of three things:
  - (a) a DC-DC stage in front of it;
  - (b) a battery string configured to ≤ 850 V (Orca and Leclanché scale in 50–67 V steps, so a 600–800 V string is buildable);
  - (c) a 1 700 V variant.

### 3.4 DC short-circuit protection, pre-charge, IT earthing (SOURCED)

| Topic | Practice | Src |
|---|---|---|
| ABB | Fuses, controlled semiconductor turn-off and isolators, with no main AC breakers. Faults cleared within **40 ms**. Solid-state breakers and high-speed fuses in the DC-link zone | [ABB-DCG-TN][ABB-EEHB] |
| Siemens | In a DP2 test, two paralleled 2 000 A DC bus-tie units (ILC type) tripped in **20 µs**, and semiconductor fuses cleared after 300 µs. Busbar rated 100 kA for 1 s. Inverters connect through semiconductor fuses | [SIE-DP2016][SIE-BD-CAT] |
| The Switch | Electronic bus link clears a fault in ≈ 10 µs; a failed drive module disconnects in 10 µs; DNV approved (DP3) | [SWITCH-DCHUB] |
| Physics | Capacitor fault currents rise in µs to hundreds of µs. Solid-state breakers act in under 1 ms, SiC types in 10–20 µs | [ABB-IEEE17] |
| ABS rules | Allowed devices: DC breakers, fault-limiting converters, solid-state switches or fuses. Selectivity required. Short-circuit calculation to IEC 61660 / IEEE 946, including converter capacitors and ESS. **Fast semiconductor fuses must back up each converter.** The DC bus is split into at least two sections where propulsion depends on it | [ABS-DC22] |
| DNV rules (2013) | Discriminative protection. Battery short-circuit protection as close to the battery as practical. Converters used as supplies must give enough short-circuit current for selective tripping | [DNV13] |
| Pre-charge | ABS requires converter capacitors to be pre-charged before connecting to a live bus. Siemens pre-charges "within a few seconds". Pre-charge also sits at string level [DNVGL-HB16] | [ABS-DC22][SIE-BD-CAT][DNVGL-HB16][ECHANDIA-DS] |
| IT systems | Insulated systems need continuous insulation monitoring (IMD circulating current ≤ 30 mA). DNV warns that **EMC filters and line-to-earth capacitors can mislead insulation monitors**. ABS: ungrounded DC needs detection and alarm; propulsion circuits need an earth-leakage alarm, and a trip if the fault current could cause damage | [DNV13][ABS-DC22][ABS18 4-8-3/T5, 4-8-5/5.9] |
| IMD technology | IT systems with IMDs are typical for propeller drives. Pure-DC measuring IMDs fail with drives, so pulse-code methods are used. Bender iso1685DP covers 0–1 500 V DC | [BENDER-SHIPS][BENDER-1685] |
| Converter design for IT | Editron's marine "-L" build uses **3.3 nF Y-capacitors instead of 330 nF**, and 240 MΩ isolation-measurement resistors. On IT networks ABB removes its EMC-filter and varistor links | [DF-EC1200-DS][ABB-ACS880-QIG][ABB-GND] |

---

## 4. Class and standards

### 4.1 What each rulebook asks of a propulsion-converter supplier (SOURCED)

| Body / document | Main asks | Src |
|---|---|---|
| **IACS UR E10 Rev.10** | Common type-approval test specification used by all IACS members. Applies to TA applications from 1 Jan 2026. Levels in §4.3 | [E10] |
| **IACS UR E22 Rev.3** | Propulsion control and the electric power system (including PMS) are **Category III**. Requires an ISO 9001 QMS that also incorporates ISO/IEC 90003, plus documentation and FAT. Applies to ships contracted from 1 Jul 2024 | [E22] |
| IACS UR E27 Rev.1 | Cyber resilience. Mandatory for passenger ships and cargo ships ≥ 500 GT on international voyages contracted from 1 Jul 2024; guidance only for smaller or domestic craft | [E27] |
| IACS UR E24 | THD ≤ 8 % where harmonic filters are on the main busbars; continuous monitoring | [E24] |
| **DNV** Pt.4 Ch.8 | Motor-drive converters ≥ 100 kW need a **product certificate for each delivery**, which can rest partly on TA of the power modules. TA tests cover supply failure and variation, vibration, dry and damp heat, insulation, HV, EMC immunity and emission, temperature rise and **short circuit** (Sec.7 Table 4), to IEC 62477-1 / IEC 61800-5-1. Major software changes must be approved before installation | [DNV13][DNV-TA-IC7][DF-TA-EC1200] |
| DNV batteries (Pt.6 Ch.2 Sec.1) | Battery(Safety) above 50 kWh. Battery(Power) when batteries drive propulsion in normal operation or are the redundant source. Product certificate for each system ≥ 50 kWh. TA via CP-0418 (IEC 62619 / 62620 + CG-0339), including a propagation test. A new "Battery ready" notation takes effect 1 Jan 2027 | [DNVGL-HB16][DNV-TA-ROYPOW][DNV-TA-FLASH][CP0418][DNV-NEWS26] |
| DNV redundancy | Legacy RP: ≥ 50 % propulsion plus steering restored after a single failure; 6 kn into BF 8. FMEA (Z071) and FMEA test procedure (Z140). Supplier FMEAs are merged into the yard's; methods DNV-RP-D102 / IEC 60812. 2022 added qualifier B and the AP (alternative propulsion) notation | [DNV-RP12][DNV-NEWS22] |
| **LR** Pt 6 Ch 2 | Surveyor attends converters ≥ 100 kW and lithium batteries ≥ 50 kWh. Converters (from 5 kW) to IEC 60146. Load cut or reduced on cooling failure. Liquid cooling needs a leak alarm, containment, and resistivity alarm with shutdown for main propulsion. Sole electric propulsion needs **≥ 2 independent motors**. FAT of propulsion control and PMS. Up to 10 % THD on dedicated propulsion circuits. Battery FMEA mandatory. Hybrid Power / Hybrid Power (+) notations. TS No.1 covers electronics, TS No.5 lithium batteries | [LR-P6C2-1][LR-P6C2-10][LR-P6C2-12][LR-P6C2-16][LR-P6C2-24][LR-TS] (July 2022 mirror) |
| **ABS** | MVR 4-8-3/8: converters ≥ 100 kW for essential service designed to IEC 61800-5-1 / IEC 60146-1-1, with **ABS-witnessed type and routine tests** (insulation, impulse, cooling system, component breakdown, rated current, temperature rise, capacitor discharge). Required alarms: overcurrent, overload, overvoltage, ground fault, loss of cooling, coolant resistivity, over-temperature, loss of communication, loss of speed feedback. Li-ion Requirements (2024): ≥ 20 kWh, IEC 62619/62620, propagation test, 2 × 100 % fans ≥ 6 ACH, gas alarm at 30 % LEL. HYBRID and All-Electric notations. MIL/SIL/HIL testing accepted | [ABS18][ABS-LI24][ABS-HYB24][ABS-DC22] |
| **BV** NR467 (Jul 2026) | Converters to IEC 60146-1-1:2024 / IEC 61800. Surveyor attends tests for essential-service converters ≥ 50 kVA. Temperature rise at 45 °C air / 32 °C sea water. Coolant resistivity monitoring; creepage to IEC 61800-5-1:2023. Batteries > 20 kWh to IEC 62619/62620, with packs over 500 V DC at least IP4X. Propulsion rules in §2.3, including the 1.5 / 1.8 × UP margin. ELECTRIC HYBRID (PM / PB / ZE MODE) notations; AVM-APS/DPS/IPS redundancy notations | [BV467C][BV467F] |
| **IRS** (India) | Guidelines on Battery Powered Vessels **Rev 3 (Sep 2026, for contracts from 1 Jan 2027)**. Notations BATTERY PROP and BATTERY (PROPULSION SUPPORT). Used alongside the Steel Ships, HSC, **Inland Waterways** and Pleasure Craft rules. References IEC 62619/62620 and IACS E10. Two battery systems, either able to reach port. **BMS sends voltage and current limits to the converter**, and converter failure raises an alarm. CN 13 covers electronics TA, harmonised with IEC 60092-504 / IEC 60533. CN 23a covers Li-ion approval | [IRS-G-BATT][IRS-CN13][IRS-CN23A] |
| IEC 60092-501:2025 | Electric propulsion, now including batteries and pods; single failure criterion; Annex A protection and alert matrix | [IEC-501-2025][IEC-501-SAMPLE] |
| IEC 60533:2015 | EMC for metallic hulls. Zones: deck and bridge, general power distribution, **special power distribution (propulsion, bow thrusters; emissions exceed general limits)**, and accommodation | [IEC-60533] |
| IEC 62619 | Battery safety basis common to ABS, BV, IRS and DNV CP-0418 | [ABS-LI24][BV467C][IRS-G-BATT][CP0418] |
| EMSA BESS guidance (2023, non-binding) | Converter uses voltage sensors independent of the BMS, has reverse-current protection, and raises alarms | [EMSA-BESS] |
| Norway NMA RSV 12-2016 | Li-ion on all ships except non-commercial vessels under 24 m. Class approval and a module-to-module propagation test | [RISE-2017] (secondary); DNV TAs cite it [DNV-TA-FLASH] |
| SOLAS Safe Return to Port | Passenger ships ≥ 120 m or with three or more main vertical zones | [SOLAS-SRTP] |

### 4.2 Redundancy and FMEA, condensed

- **Single failure:**
  - DNV R1: propulsion restored within 30–45 s [DNV13].
  - ABS: no single failure may completely disable propulsion; a dual winding is not enough for sole electric propulsion [ABS18].
  - LR: two independent motors [LR-P6C2-16].
  - BV: a standby converter or double windings, plus alternative propulsion on single-shaft ships [BV467C].
  - IRS: two battery systems; fire or collision must not cause total loss [IRS-G-BATT].
- **FMEA deliverables:**
  - Drive-level FMEA merged into the yard FMEA, plus an FMEA test procedure for quay and sea trials [DNV-RP12].
  - LR: battery FMEA and overall hybrid FMEA are mandatory [LR-P6C2-12][LR-P6C2-24].
  - BV: FMEA on request [BV467C].
  - ABS: MIL/SIL/HIL accepted as evidence [ABS-HYB24].
- DNV offers independent HIL testing [DNV-HIL]. The name of a DNV HIL class notation was **NOT FOUND**.

### 4.3 Numeric type-approval test levels

**IACS UR E10 Rev.10 (Aug 2024)** [E10]:

| Test | Level |
|---|---|
| Power supply, AC | +6/−10 % voltage and ±5 % frequency permanent; ±20 % voltage (1.5 s) and ±10 % frequency (5 s) transient |
| Power supply, DC | ±10 % continuous, 5 % cyclic, 10 % ripple. Battery-fed: +30/−25 % if on a charging battery, +20/−25 % otherwise |
| Supply failure | Three interruptions in 5 min, 30 s off each (plus one during boot) |
| Dry heat | 55 ± 2 °C for 16 h, operating. **70 °C for 16 h** for electronics inside consoles or housings with other heat-dissipating power equipment |
| Damp heat | IEC 60068-2-30 Db: 55 °C, 95 % RH, two 24 h cycles (12 + 12 h) |
| Vibration, general | 2–13.2 Hz ±1 mm; 13.2–100 Hz ±0.7 g; 90 min at each resonance (Q ≥ 2), otherwise 90 min at 30 Hz; three axes |
| Vibration, on engines or compressors | 2–25 Hz ±1.6 mm; 25–100 Hz ±4.0 g |
| Inclination | 22.5° static and 22.5° dynamic (10 s period, at least 15 min per direction); usually waived for equipment with no moving parts |
| Insulation resistance | Un > 65 V: 500 V DC test; ≥ 100 MΩ before, ≥ 10 MΩ after the climatic and HV tests |
| HV withstand | 501–690 V → 2 500 V AC for 1 min. **The E10 and CG-0339 tables stop at 690 V.** Test levels for 850–1 200 V DC equipment must be agreed with the society (NOT FOUND in these documents). BV Sec 6 uses 2U + 1 000 V, minimum 2 000 V, for converters [BV467C] |
| Cold | +5 °C for 2 h, or −25 °C where not weather-protected |
| Salt mist | IEC 60068-2-52 Kb: four spray periods, each followed by 7 days' storage (weather-exposed equipment) |
| ESD / RF field / conducted RF | 6 kV contact, 8 kV air / 10 V/m from 80 MHz to 6 GHz / 3 V rms from 150 kHz to 80 MHz (10 V at bridge-zone spot frequencies) |
| Burst / surge | 2 kV on power ports, 1 kV on I/O / 1 kV line-to-earth, 0.5 kV line-to-line |
| Conducted low-frequency immunity | AC: 10 % of supply up to the 15th harmonic, falling to 1 % at the 100th. DC: 10 % of supply from 50 Hz to 10 kHz (max 2 W) |
| Conducted emission, general power distribution zone | 10–150 kHz: 120 → 69 dBµV; 150–500 kHz: 79 dBµV; 0.5–30 MHz: 73 dBµV |
| Radiated emission, general power distribution zone (3 m) | 0.15–30 MHz: 80 → 50 dBµV/m; 30–100 MHz: 60 → 54; 100–1 000 MHz: 54; **156–165 MHz: 24 dBµV/m**; 1–6 GHz: 54 (average) |

**DNV CG-0339 location classes** [CG0339]. Read from the Nov 2015 edition; the current edition is Aug 2021 [CG0339-ED], so confirm the tables against it.

| Parameter | Classes |
|---|---|
| Temperature | **A**: machinery spaces and control rooms, 0 to +45 °C (tested at 55 °C). **B**: inside cubicles with ≥ 5 K internal rise, 0 to +55 °C (tested at 70 °C). C: −25 to +45 °C. D: open deck, −25 to +55 °C |
| Humidity | A: ≤ 96 % RH, non-condensing (40 °C / 93 % for 4 days). **B**: up to 100 % RH, condensing (cyclic test). Built-in anti-condensation heaters may run during the test |
| Vibration | **A**: 2–13.2 Hz 1.0 mm, 13.2–100 Hz 0.7 g. **B**: on machinery, 2–25 Hz 1.6 mm, 25–100 Hz 4.0 g. C: masts, 2.5 mm / 2.3 g |
| EMC | **A**: everywhere except bridge and open deck. B: including bridge and deck |
| Enclosure | A: IP20. **B: IP44 (engine room)**. C: IP56 (open deck, below floor plates; salt mist test). D: IP68 |
| Machinery-space default | Temperature A (B inside cubicles), Humidity **B**, Vibration A (B if mounted on machinery), EMC A, Enclosure B |

**Precedent certificates:**
- Editron EC-C1200-450-L: **Temp A, Vib B, Hum B, EMC A-locations only, 0–45 °C, IP67**. Leak detection for the liquid cooling must be provided at commissioning. The certificate is valid to 2026-10-06, so check whether it has been renewed [DF-TA-EC1200].
- ABB HES880 (2017–2022, now expired): Temp D, Vib A, Enclosure B, EMC A [ABB-HES-TA].

---

## 5. Environment and installation

| Item | Value | Status / Src |
|---|---|---|
| Engine-room air | **0 to +45 °C**; electronics must work at **+55 °C** | SOURCED [M40]. ABS allows the actual ambient where a space is cooler than 45 °C, with a minimum of +40 °C [ABS18 4-1-1/T8] |
| Open deck | −25 to +45 °C | SOURCED [M40] |
| Sea water | **+32 °C** design | SOURCED [M40] |
| Fresh-water low-temperature (LT) loop | MAN: design LT fresh water max **36 °C** ("a convenient choice"); LT inlet ≥ 10 °C. Siemens BlueDrive: cooling water **20–38 °C** | SOURCED [MAN][SIE-BD-CAT] |
| Coolant in marine-approved liquid drives | Editron: 50/50 water-glycol, −40 to +65 °C, derating to 75 °C [DF-EC1200-DS]. HES880: −40 to +70 °C [ABB-HES-FF]. ACS880 liquid-cooled modules: 0 to +55 °C ambient [ABB-ACS880-MAR] | SOURCED |
| Converter cooling rules | Design on 45 °C ambient. No power, or reduced load, without cooling. Coolant over-temperature raises an alarm (ABS: then shutdown). Liquid cooling needs leak detection and containment, and conductivity monitoring where the coolant touches live parts. Cooling failure → alarm and **automatic current reduction** | SOURCED [ABS18 4-8-3/8.5.8][BV467C][LR-P6C2-10] |
| Humidity / condensation | Machinery spaces are Humidity class B (condensing); damp heat 55 °C / 95 %. ABS requires anti-condensation means on propulsion converters. BV requires heating where condensation can build up | SOURCED [CG0339][E10][ABS18 4-8-5/5.17.3][BV467C] |
| Salt mist | Tested only for weather-exposed or below-floor-plate (IP56) locations | SOURCED [E10][CG0339] |
| Inclination (UR M46 Rev.4) | Machinery: 15° static / 22.5° dynamic athwartships, 5° / 7.5° fore-and-aft. Electrical and electronic equipment: 22.5° / 22.5° athwartships, 10° / 10° fore-and-aft. ABS: switches must hold their position up to 45° | SOURCED [M46][ABS18 4-1-1/T7] |
| Vibration | 0.7 g general; 4 g on machinery | SOURCED [E10][CG0339] |
| Enclosure (power equipment) | ABS minimum for converters in machinery spaces above floor plates is **IP22**; converters are not recommended below floor plates. CG-0339 engine-room class for control electronics is **IP44**. Siemens: IP22 standard, up to IP44. Editron and HES880: IP67 | SOURCED [ABS18 4-8-3/T2][CG0339][SIE-BD-CAT][DF-EC1200-DS] |
| Clearance / creepage | IEC 61800-5-1 at **OVC III, pollution degree 3**: clearance 8.0 mm at 1 000 V; creepage 12.5 mm at 800 V rms, 16 mm at 1 000 V | SOURCED [ABS18 4-8-3/8.5.11] |
| Capacitor discharge | Below 60 V (or 50 µC) within **5 s** of power removal, otherwise warning labels | SOURCED [DNV13][ABS18 4-8-3/8.5.7] |
| Propulsion cables | EPR, XLPE or silicone insulation (PVC only where ambient ≤ 50 °C); no splices; supports less than 900 mm apart | SOURCED [ABS18 4-8-5/5.15.3, 5.17.11] |
| Typical cable length, drive room to thruster | **NOT FOUND** | — |

---

## 6. Motor–drive interface issues specific to ships

| Issue | SOURCED numbers | Mitigation seen in sources | Src |
|---|---|---|---|
| Reflected wave | Critical length ≈ v·t_r / 2. Motor-terminal peak up to **2 × V_DC**, more with ringing. Pulses under 235 ns almost double on 20 m of cable. Measured example: 1 300 V at 30 m on a 672 V bus | Output reactor, dv/dt filter or sine filter; measure at the terminals | [ABB-TG102][GAMBICA][ABB-TN176] |
| SiC edges | 1 200 V SiC at 600 V with 45–47 ns edges: motor-end peak rises from 743 V to **1 163 V with only 4 m of cable**. Undamped reflections can exceed 2 pu. Filters are "bulky and costly" | A parallel L-R filter gives the lowest loss; an L-only filter still leaves 1.4 pu | [NARAYAN-TPEL] |
| Motor size | Reflection coefficient falls with size: ≈ 0.95 below 3.7 kW, 0.82 at 90 kW, 0.6 at 355 kW | — | [DF-OUTFILT] |
| Marine cable path | Azipod power passes through **slip rings** in the steering module. The LV Azipod motor uses form-wound VPI coils to cope with converter stress | — | [ABB-DP2001][ABB-AZICO] |
| Motor insulation | IEC 60034-18-41 Type I (≤ 700 V rms, random-wound, partial-discharge-free, impulse classes A/B/C/D/S). Type II (form-wound, -18-42). Converter rise times are 0.05–2 µs, and the integrator must tell the motor maker the terminal voltage | Specify the stress category or impulse class | [IEC-18-41][IEC-18-41-A1][IRIS-IEC] |
| Motor limits | ABB: 1 300 V peak phase-to-ground standard, 1 800 V with special insulation; at 690 V, special insulation **and** a dU/dt filter | — | [ABB-MOTCAT] |
| dv/dt filter | Cuts dv/dt to ≈ 500 V/µs, with 0.5–1.5 % loss or voltage drop. No effect beyond ≈ 150 m. **Standard units are rated for 1.5–4 kHz switching only** | Use a sine filter beyond ≈ 300 m; an 8–10 kHz SiC drive needs a purpose-designed filter | [DF-OUTFILT][DF-MCC102][GAMBICA][ABB-TN176] |
| Sine filter | Limits motor voltage to about 90 % of supply. Differential-mode only, so no help with common-mode or bearing currents | Add HF common-mode cores | [GAMBICA][DF-OUTFILT] |
| Bearing currents | EDM damage fails bearings within 1–6 months; two-level PWM always creates common-mode voltage. ABB: insulated non-drive-end bearing from IEC frame 280 (or ≥ 100 kW), plus a common-mode filter above 350 kW | Symmetrical cable with 3 PE or concentric shield, 360° glands, HF bonding, insulated or hybrid bearings, shaft grounding | [ABB-TG5][ABB-BRG18][ABB-MOTCAT][DF-OUTFILT] |
| Marine drive cable | IEC 60092-353 0.6/1 kV drive cable with three earth cores, copper-tape screen and braid (Polycab, an Indian maker) | Shield conductivity at least 1/10 of the phase conductor | [POLYCAB][ABB-GND] |
| Harmonics on the AC grid | THD ≤ 8 % (IACS E24). ABS adds ≤ 5 % for any single harmonic, requires a harmonic calculation for all electric propulsion, and continuous monitoring. LR accepts up to 10 % on dedicated propulsion circuits | AFE or multi-pulse front ends | [E24][ABS18 4-8-2/7.21, 4-8-5/5.3.5][LR-P6C2-16] |
| EMC zones | The special power distribution zone (propulsion, thrusters) may exceed general-zone emission limits | Segregate cables and zones | [IEC-60533] |
| Interaction with IT earthing | EMC filters and varistors are unsuitable on IT networks. Long motor cables cause nuisance earth-fault trips. Pure-DC IMDs misread with drives | Small Y-caps; pulse-code IMDs | [ABB-GND][ABB-TN176][BENDER-GFM] |
| Class rule mandating filters or a motor insulation class | **NOT FOUND** | — | — |

---

## 7. Competitive landscape (public material only)

| Vendor / product | Architecture | Power per module | DC range | Semis / switching freq. | Cooling / IP | Approvals | Src |
|---|---|---|---|---|---|---|---|
| **Danfoss Editron EC-C1200-450** | One hardware unit works as inverter, AFE, microgrid or bidirectional DC-DC, chosen by option. Paralleled per motor (2 × 300 A on a 370 kW motor) | 350 A rms / **300 kW** at 500 V AC; DC-DC 400 A / 240 kW. **No overload above nominal published** | 0–850 V (750 V nominal) | not published / 8 kHz | Water-glycol **up to +65 °C**; **IP67**; 14 kg | DNV TA: Temp A / Vib B / Hum B / EMC A; product certificate per delivery ≥ 100 kW; CCS | [DF-TA-EC1200][DF-EC1200-DS][DF-FEGG][DF-CCS] |
| Danfoss Editron EC-C1700B-420 | Inverter, AFE or microgrid (no DC-DC) | 210 A rms / 250 kVA at 690 V | 0–1 200 V (1 050 V nominal) | not published / **4 kHz** | Coolant ≤ 65 °C; IP67 | no marine TA found | [DF-EC1700-DS] |
| ABB Onboard DC Grid | Pre-engineered source and load modules on 1 000 V DC; fuses, semiconductor turn-off, solid-state breakers | — | 1 000 V | — | — | Used on ForSea, eWolf and the GTTP JNPA tugs | [ABB-EEHB][ABB-DCG-TN][ML-ABB-GTTP] |
| ABB HES880 | One power module serves as inverter, line converter or DC-DC depending on firmware; two paralleled per motor on Zeetug30 | 303 / 520 / 779 kVA; DC-DC 332 / 518 / 776 kW | 730 V nominal | not published | Water-glycol, −40 to +70 °C; IP67 | DNV TA 2017–2022 (expired) | [ABB-HES-TA][ABB-HES-FF][MAREX-ZEETUG] |
| ABB ACS880 marine | Industrial drive family | Air 0.55–5 600 kW; liquid 250–6 000 kW | 208–690 V AC | — | Liquid-cooled, 0 to +55 °C ambient | ABS, BV, CCS, ClassNK, DNV, KR, LR, RINA | [ABB-ACS880-MAR] |
| Siemens BlueDrive (SINAMICS S120) | 690 V AC propulsion drives; diode, 12/24-pulse or AFE front end; common DC bus on request; CES DC-DC for batteries | 560–5 700 kW per drive | ≈ 1 000 V class (chopper at 1 040 V) | **1 800 V / 600 A IGBTs at 1.25–2.5 kHz** | Fresh water 20–38 °C; IP22, up to IP44 | Class-certified (DNV named) | [SIE-BD-CAT][SIE-SISHIP] |
| Kongsberg | DC hybrid with the ESS on a floating DC link (no battery DC-DC); integrated drive line-up; PM thrusters | — | not published | — | — | 45+ DC hybrid projects | [KM-DCHYB][KM-DCYACHT] |
| Wärtsilä | DC Hub (switchboard + converter modules); Low Loss 690 V AC scheme | — | 1 000 V | — | — | 200+ Low Loss installations | [WARTSILA-EP][WARTSILA-SES] |
| The Switch (BEMAC) DC-Hub | Inverter, AFE, DC-DC chopper, electronic bus link, fast DC breaker | 800 A, 2 × 800 A, 1 600 A modules | not published | — | Closed IP44 module on ship fresh-water cooling; crew-swappable | DNV (DP3 bus link) | [SWITCH-DCHUB] |
| Ingeteam INGEDRIVE / E3-Ship | Parallel AFEs (master-slave or droop), IGBT inverters, DC-DC for the ESS | LV200 / LV800 | 700–1 000 V; bus-tie rated 1 500 V | IGBT | Water or air | 1 000+ vessels claimed; Indian subsidiary | [ING-E3][ING-NAVAL] |
| Automotive-derived drives in small craft | Cascadia CM350SiC: 200–850 V, 500 A rms continuous / 900 A for 30 s, SiC. Dana TM4: 320–900 V, lists marine. Torqeedo: 100 kW at ≈ 350 V. RAD: 120 kW at 350–450 V. Evoy: up to 300 / 600 kW | — | 200–900 V | SiC (Cascadia) | — | **No marine class approval found** | [CASCADIA][DANATM4][TORQ][RAD][EVOY] |

**EJ observations:**
1. The EC-C1200 is the template. It is an automotive-grade heavy-duty converter (it cites ISO 16750-3 vibration and IP6K9K) that won DNV TA at the location classes in §4.3.
2. The 1 kV class runs on 1 700–1 800 V IGBTs at 1.25–4 kHz.
3. Every vendor sells **one module in three roles** (inverter, AFE, DC-DC) and parallels modules rather than scaling one up.
4. Nobody publishes semiconductor technology or life figures.
5. Indian projects currently buy Siemens, ABB and Kongsberg. No domestic converter competitor was found.

---

## 8. Engineering recommendation

*This whole section is ENGINEERING JUDGEMENT. The source tags only anchor the inputs.*

### 8.1 DC bus voltage classes to support

| Class | DC range | Devices | Serves | Why |
|---|---|---|---|---|
| **LV-800** (existing platform) | 400–850 V **maximum** continuous, including battery top-of-charge | 1 200 V SiC or IGBT | Small craft, water taxis, pilot/crew boats; ferries or tugs whose integrator runs a ≤ 800 V link (Editron 750 V, HES880 730 V) | Cosmic-ray failures of 1 200 V silicon rise steeply above ≈ 850 V (data below) [AN17]. Small-craft packs are 350–450 V [TORQ][RAD]. Editron's 1 200 V class also stops at 850 V [DF-EC1200-DS] |
| **LV-1000** (new variant) | 600–1 200 V maximum (900–1 050 V nominal) | **1 700 V IGBT** baseline; 1 700 V SiC optional | Ferries and tugs on ABB, Wärtsilä or Siemens 1 000 V grids, which includes GTTP (ABB grid), eWolf and ForSea | 1 000 V is the de-facto marine DC grid [ABB-DCG-TN][WARTSILA-EP]. Batteries reach 1 096–1 205 V [CORVUS-ORCA][LECL-MRS3][CORVUS-DOLE]. Competitors use 1 700–1 800 V parts [DF-EC1700-DS][SIE-BD-CAT] |
| LV-1500 | defer | 2 kV-class or three-level | none found | Only a standards limit so far [IEC-101][ABS-DC22]. A three-level NPC with 1 200 V dies is effectively immune to cosmic rays at 1 500 V [AN17] |

**Cosmic-ray data (SOURCED, Semikron AN 17-003) [AN17]:**
- For 1 200 V modules the note gives 1 000 V as the "typical upper limit", but that case is a PV inverter that spends little time near its maximum.
- A SEMiX603GB12E4p (1 200 V IGBT4) switch is ≈ 41 FIT at 900 V.
- The note's 12-module inverter table gives 31 / 175 / 984 / 11 520 FIT at ≤ 800 / 850 / 900 / 1 000 V. Dividing by its 24 switches (my arithmetic) gives ≈ **1.3 / 7 / 41 / 480 FIT per switch**.
- A SKiiP38GB12E4V1 is 230 FIT per switch at 1 000 V.
- The rate roughly doubles for every 1 000 m of altitude, which does not matter at sea.
- **SiC FIT data: NOT FOUND.** Get it from the module vendor (hiitio / Starpower) before committing SiC above 800 V.

**BV voltage-margin caveat (SOURCED rule, EJ reading):**
- BV requires each valve's repetitive peak voltage to be ≥ 1.5 × UP on a dedicated propulsion supply, or ≥ 1.8 × UP on a common bus [BV467C Sec14 3.4.1].
- If UP is read as the rated DC-link voltage:
  - 1 200 V parts are limited to UP ≤ 800 V (dedicated) / ≤ 667 V (common);
  - 1 700 V parts to UP ≤ 1 133 V / ≤ 944 V;
  - a 1 000 V common DC grid would need ≥ 1 800 V parts, which is what Siemens uses [SIE-BD-CAT].
- Get a written BV interpretation before freezing the classes. No equivalent rule was found for DNV, LR, ABS or IRS.

### 8.2 IGBT or SiC, by segment

| Segment | Recommendation | Reason |
|---|---|---|
| Water taxi, small passenger craft, pilot boats, fast crew boats (20–300 kW per shaft; 350–850 V) | **SiC** (existing platform) | Efficiency directly buys range on small packs (122 kWh Kochi, 336 kWh P-12) [BM-MUZIRIS][CAND-P12]. Low annual hours. But with SiC edges even **4 m of cable nearly doubles the motor-terminal voltage** (743 → 1 163 V on a 600 V bus) [NARAYAN-TPEL], so ship a dv/dt-limited gate-drive mode and specify the motor's impulse class to IEC 60034-18-41 [IEC-18-41] |
| Ferries on ≤ 850 V links (0.25–0.75 MW per shaft) | SiC **only if** ΔTj ≤ 30 K per crossing and there is a filter plan; otherwise IGBT | 34–46 crossings a day [EPRI-AMPERE][WIKI-TYCHO]; SiC carries a 0.33× power-cycling factor [AN21] |
| Ferries and tugs on 1 000 V grids (0.9–2.1 MW per thruster) | **1 700 V IGBT**, 2–4 kHz, paralleled modules | Voltage class is set by the 1.1–1.2 kV battery maximum. Competitors switch at 1.25–4 kHz, where SiC saves little [SIE-BD-CAT][DF-EC1700-DS]. Off-the-shelf dv/dt filters stop at 4 kHz [DF-MCC102]. Slip rings and long cables to thrusters [ABB-DP2001] |
| Harbour / escort tugs | **IGBT** (1 700 V on 1 kV grids; 1 200 V on ≤ 800 V grids) | Under 10 % of time at full power [PROMAR], so efficiency is worth little; the power bursts dominate ΔTj (§8.3); escort needs sustained pull [UCI-CARB] |
| OSV / CSOV dynamic positioning (DP) | IGBT | Continuous DP duty on an established 690 V AC / 1 kV DC base [ABB-DP2011][ING-E3] |

### 8.3 Junction-temperature and ΔTj targets for 20 years

**Model:** Semikron Danfoss AN 21-001, parameter set for copper-baseplate modules with soldered chips and Al wire bonds (the EconoDUAL class).
- Load pulses of 60 s.
- Results are **B15 life** (15 % failure probability), the model's own definition.
- The model reproduces the note's own example: ≈ 880 k cycles at 60 K between 40 and 100 °C, and ≈ 260 k between 90 and 150 °C.
- Chip factors: SiC ≤ 1 200 V × 0.33; 1 700 V IGBT × 0.65 [AN21].

| Tj,max | ΔTj | Si IGBT ≤ 1 200 V | Si IGBT 1 700 V | SiC ≤ 1 200 V |
|---|---|---|---|---|
| 125 °C | 30 K | 1.5 × 10⁷ | 9.5 × 10⁶ | 4.8 × 10⁶ |
| 125 °C | 40 K | 2.1 × 10⁶ | 1.3 × 10⁶ | 6.8 × 10⁵ |
| 150 °C | 30 K | 8.7 × 10⁶ | 5.7 × 10⁶ | 2.9 × 10⁶ |
| 150 °C | 40 K | 1.2 × 10⁶ | 7.8 × 10⁵ | 4.0 × 10⁵ |
| 150 °C | 60 K | 1.7 × 10⁵ | 1.1 × 10⁵ | 5.7 × 10⁴ |

**Illustrative 20-year demand (EJ; replace with owners' measured profiles):**
- Ferry: 30 crossings a day × 350 days × 20 years ≈ **2.1 × 10⁵** major cycles.
- Tug: 6 jobs a day × 20 high-power pulses × 330 days × 20 years ≈ **7.9 × 10⁵** pulses.
- At ΔTj = 40 K and 150 °C, SiC covers only 0.5 × the tug demand and 1 700 V IGBT 1.0 ×.
- At ΔTj = 30 K every option has at least 3.6 × margin.

| Target (EJ) | Si IGBT | SiC |
|---|---|---|
| Rating point | 40 °C coolant inlet (36 °C LT loop [MAN] + 4 K margin), 45 °C air [M40]; linear derating to 50 °C coolant | same |
| Continuous Tvj at rated continuous current | **≤ 125 °C** | **≤ 135 °C** |
| Short-time Tvj (≤ 60 s: bollard push, crash stop, ice torque) | ≤ 150 °C | ≤ 150 °C |
| ΔTj per load pulse (≥ 10 s on-time, rainflow-counted) | ≤ 40 K ferries, **≤ 30 K tugs** | **≤ 30 K** everywhere |
| ΔTj at low output frequency (< 5 Hz: crash-stop reversal, DP) | ≤ 20 K, checked separately (no source found for a limit) | ≤ 15 K |
| Life criterion | B10 ≥ 20 years on the measured mission profile, i.e. ≥ 3 × margin on the model's B15 cycles | same |
| Evidence to prepare | Mission-profile loss and thermal model, plus **the module vendor's own** power- and thermal-cycling curves (AN 21-001 is Semikron's model) | same, plus gate-threshold drift and high-temperature reverse-bias (HTRB) data |

### 8.4 The five things a class surveyor will check first

1. **Certificate trail and test evidence.**
   - A TA certificate listing the declared location classes. Precedent: Temp A / Vib B / Hum B / EMC A [DF-TA-EC1200].
   - A per-delivery product certificate at ≥ 100 kW (DNV) [DNV13][DNV-TA-IC7], or witnessed tests (ABS and LR ≥ 100 kW, BV ≥ 50 kVA) [ABS18][LR-P6C2-1][BV467C].
   - The short-circuit type test [DF-TA-EC1200], and temperature rise at 45 °C air / 32 °C sea water [BV467C].
2. **Single-failure behaviour and FMEA.**
   - No single failure disables propulsion. DNV requires power back within 30–45 s. ABS, LR and BV limit reliance on one motor [DNV13][ABS18][LR-P6C2-16][BV467C].
   - A drive-level FMEA that merges into the yard FMEA and is proven at trials [DNV-RP12].
   - Crash-stop and reversal trials with recordings [DNV13][ABS18].
3. **DC-bus fault behaviour.**
   - Declared short-circuit contribution and withstand. Editron declares 750 A for 1 ms and 350 A continuous [DF-TA-EC1200].
   - A semiconductor fuse per converter and an IEC 61660 selectivity study that includes DC-link capacitors, plus pre-charge [ABS-DC22].
   - Insulation-monitor compatibility (small Y-caps) [DNV13][DF-EC1200-DS].
   - Discharge below 60 V within 5 s [DNV13][ABS18].
4. **Cooling and power-reduction logic.**
   - Alarms for coolant temperature, flow, leakage and resistivity, with containment.
   - A cooling failure costs at most one propulsion line.
   - **Power reduction rather than a trip**, shown as "power limitation" [DNV13][BV467C][ABS18][LR-P6C2-10].
5. **Software and control integrity.**
   - E22 Category III: ISO 9001 plus ISO/IEC 90003, documentation, FAT [E22].
   - Major software changes re-approved [DF-TA-EC1200].
   - Losing a reference or feedback signal must not increase thrust; a hard-wired E-stop at every station [DNV13][ABS18].
   - The converter obeys the BMS voltage and current limits [IRS-G-BATT][EMSA-BESS].
   - E27 cyber resilience where mandatory [E27].

A close sixth: harmonic and EMC impact on the ship's grid [E24][E10][IEC-60533].

### 8.5 Platform changes from automotive to marine

Platform facts in the second column come from the project's own design notes (rev A.4.x), not a web source.

| Area | Platform today (project notes) | Marine requirement (SOURCED) | Action (EJ) |
|---|---|---|---|
| Rating basis | 220 kW for 30 s / 120 kW continuous | 100 % continuous plus a declared overload [DNV13]; basis 45 °C air / 32 °C sea water [M40][BV467C] | Publish a continuous rating at 40 °C coolant and a separate 60 s rating from the thermal model. Editron publishes peak = nominal [DF-EC1200-DS], so an honest overload rating is a differentiator |
| Control supply | 12 V automotive (KL15, 9 V crank) | E10 DC input: ±10 %, 10 % ripple; battery-fed +30/−25 % [E10] | Design for 24 V nominal (18–31.2 V by that arithmetic). "24 V is the marine norm" is EJ; no source found |
| DC-link discharge | Passive bleeder ≈ 57 s; active path ≈ 1.6–1.8 s | < 60 V within 5 s, otherwise warning labels [DNV13][ABS18] | Make the passive path meet 5 s, or show the active path is single-fault-tolerant **and** label |
| Y-caps / IMD | Automotive values | IMDs must not be misled [DNV13]; Editron marine build uses 3.3 nF [DF-EC1200-DS] | Keep total HV-to-chassis capacitance in the low-nF range on the marine SKU |
| Creepage and clearance | Automotive layout | OVC III / PD3: 12.5 mm at 800 V, 16 mm at 1 000 V [ABS18]. Editron declares PD2 inside IP67 and still holds DNV TA [DF-EC1200-DS] | Either argue PD2 inside a sealed IP67 enclosure, or re-lay out for PD3 |
| Humidity | Automotive | Condensing class B [CG0339]; heating where condensation can build up [BV467C] | Conformal coating plus a heater or breather |
| EMC | Automotive EMC basis | E10 limits, including **24 dBµV/m at 156–165 MHz** [E10] | Test the complete drive; agree the IEC 60533 zone with the integrator [IEC-60533] |
| Coolant | Hot automotive glycol | Ship LT water ≤ 36–38 °C [MAN][SIE-BD-CAT]; leak, containment and resistivity rules [ABS18][BV467C][LR-P6C2-10] | Use the colder coolant to raise the continuous rating; add a leak sensor and drip tray |
| Safety / software | ISO 26262 ASIL D (FS26) | E22 Category III [E22]. No class document citing ISO 26262 was found (NOT FOUND) | Map the ASIL D artefacts onto E22 deliverables |
| Voltage class | 500–850 V, 1 200 V parts | 1 kV grids with batteries up to 1.2 kV [CORVUS-ORCA]; BV 1.5 / 1.8 × UP [BV467C] | Add the LV-1000 variant with 1 700 V parts (§8.1) |
| Motor interface | Short cables, dedicated motor | Long cables, slip rings, third-party motors [ABB-DP2001][IEC-18-41] | Offer a dv/dt-limited mode and an output filter rated for 8–10 kHz; standard filters stop at 4 kHz [DF-MCC102] |

---

## 9. Gaps (NOT FOUND) worth closing next

- Pilot boats and CTVs: any sourced electric or hybrid example.
- DC bus voltages of specific vessels (Kochi Water Metro, Ampere, Ellen, Sparky, HaiSea); Kochi motor kW and charger power.
- Sizes of the GTTP standard designs (ASTDS) beyond 60 t, and GTTP battery sizes.
- Drive or converter design life (years or hours) from any vendor; manufacturer overload ratings (for example 150 % for 60 s) for marine drives.
- An ice-class motor-torque multiple; the current Finnish-Swedish and DNV ice clauses.
- Current texts of DNV Pt.4 Ch.8 / Pt.6 Ch.2 and ABS MVR 4-8-3/8 and 4-8-5 (the 2013 and 2018 editions were used); LR Test Specification No.1 contents; the DNV HIL notation name.
- HV test levels for 850–1 200 V DC equipment under E10 / CG-0339.
- SiC cosmic-ray FIT and power-cycling data for the chosen modules; class positions on SiC.
- Any class interpretation of BV's 1.5 / 1.8 × UP rule for DC-fed IGBT or SiC inverters, and any DNV / LR / ABS / IRS equivalent.
- Drive-room-to-thruster cable lengths; any class rule mandating dv/dt or sine filters.

---

## Sources

**Rules, standards and regulators**
- [E10] IACS UR E10 Rev.10 (Aug 2024), Test Specification for Type Approval: https://iacs.s3.af-south-1.amazonaws.com/wp-content/uploads/2024/08/29145615/UR-E10-Rev.10-Aug-2024-CLN.pdf
- [E22] IACS UR E22 Rev.3 Corr.1 (Sep 2025), Computer-based systems: https://iacs.s3.af-south-1.amazonaws.com/wp-content/uploads/2025/10/08115518/UR-E22-Rev.3-Corr.1-Sep-2025-CLN.pdf
- [E24] IACS UR E24 Rev.1 (Dec 2018), Harmonic Distortion for Ship Electrical Distribution System including Harmonic Filters: https://iacs.s3.af-south-1.amazonaws.com/wp-content/uploads/2022/02/16160520/ur-e24rev1.pdf
- [E27] IACS UR E27 Rev.1 (Sep 2023), Cyber resilience of on-board systems and equipment: https://iacs.s3.af-south-1.amazonaws.com/wp-content/uploads/2022/05/29103853/UR-E27-Rev.1-Sep-2023-CLN.pdf
- [M40] IACS UR M40, Ambient conditions – Temperatures: https://iacs.s3.af-south-1.amazonaws.com/wp-content/uploads/2022/02/17112652/ur-m40.pdf
- [M46] IACS UR M46 Rev.4 (Aug 2024), Ambient conditions – Inclinations and Ship Accelerations and Motions: https://iacs.s3.af-south-1.amazonaws.com/wp-content/uploads/2024/08/29134020/UR-M46-Rev.4-Aug-2024-CLN.pdf
- [IACS-ELEC] IACS Requirements concerning Electrical and Electronic Installations (2023-09 compilation): https://safety4sea.com/wp-content/uploads/2023/10/IACS-Requirements-concerning-electrical-and-electronic-installations-2023_09.pdf
- [IACS-I3] IACS UR I3 (2007 text), Machinery requirements for Polar Class ships: https://www.engr.mun.ca/~cdaley/9093/UR_I_pdf410.pdf
- [CG0339] DNVGL-CG-0339 (Nov 2015), Environmental test specification for electrical, electronic and programmable equipment and systems (third-party copy): https://sandith.in/wp-content/uploads/2018/11/dnvgl-cg-0339-enviroment-testing.pdf
- [CG0339-ED] Accuris listing, DNV-CG-0339 (current edition Aug 2021): https://store.accuristech.com/standards/dnv-dnv-cg-0339?product_id=2842682
- [DNV13] DNV Rules for Ships Pt.4 Ch.8, Electrical Installations (July 2013): https://rules.dnv.com/docs/pdf/dnvpm/rulesship/2013-07/ts408.pdf
- [DNV-RP12] DNV Rules for Ships Pt.6 Ch.2, Redundant Propulsion (Jan 2012, third-party copy): https://civamblog.wordpress.com/wp-content/uploads/2016/11/ts602.pdf
- [DNV-NEWS22] DNV, "DNV Rules for Ships – July 2022 edition": https://www.dnv.com/news/2022/dnv-rules-for-ships-july-2022-edition-227477/
- [DNV-NEWS26] DNV, "Now available: The July 2026 edition of the DNV class rules…": https://www.dnv.com/news/2026/standards-now-available-the-july-2026-edition-of-the-dnv-class-rules-and-standards-for-ship-and-offshore/
- [DNV-HIL] DNV, HIL testing of marine systems: https://www.dnv.com/services/hil-testing-of-marine-systems-83385/
- [CP0418] DNVGL-CP-0418, Lithium batteries (Dec 2015, third-party copy): https://sandith.in/wp-content/uploads/2018/11/dnvgl-cp-0418-lithium-batteries.pdf
- [DNVGL-HB16] DNV GL, Handbook for Maritime and Offshore Battery Systems (2016): https://sustainableworldports.org/wp-content/uploads/DNV-GL_2016-Handbook-maritime-offshore-battery-systems-report.pdf
- [DNV-TA-IC7] DNV TA certificate TAE00004S2 (Danfoss/Vacon iC7-60): https://files.danfoss.com/download/Drives/ID459733982208-0101.pdf
- [DNV-TA-ROYPOW] DNV TA certificate TAE0000523 (RoyPow Li-ion): https://www.roypow.com/uploads/DNV-CERTIFICATE.pdf
- [DNV-TA-FLASH] DNV TA certificate TAE00003SJ Rev 2 (Flash Battery): https://www.transfluid.eu/wp-content/uploads/2024/05/TAE00003SJ-DNV-TA-Battery-Systems-extension-2_watermark.pdf
- [ABS18] ABS Rules for Building and Classing Marine Vessels 2018, Part 4: https://ww2.eagle.org/content/dam/eagle/rules-and-guides/archives/other/1000_marinevessels_2018/mvr-part-4-aug-18.pdf
- [ABS-DC22] ABS, Requirements for DC Power Distribution Systems for Marine and Offshore Applications (Jul 2022): https://ww2.eagle.org/content/dam/eagle/rules-and-guides/current/other/293-requirements-for-direct-current-(dc)-power-distribution-systems-for-marine-and-offshore-applications/293-dc-power-reqts-july22.pdf
- [ABS-LI24] ABS, Requirements for Use of Lithium-ion Batteries in the Marine and Offshore Industries (Apr 2024, third-party copy): https://maritimecyprus.com/wp-content/uploads/2024/04/ABS-RequirementsforUseofLithium-ionBatteries.pdf
- [ABS-HYB24] ABS, Requirements for Hybrid and All-Electric Power Systems (Apr 2024): https://ww2.eagle.org/content/dam/eagle/rules-and-guides/current/conventional_ocean_service/319-requirements-for-hybrid-electric-power-systems-for-marine-and-offshore-applications-2024/319-hybrid-electric-power-systems-reqts-Apr24.pdf
- [LR-P6C2-1] LR Rules Pt 6 Ch 2 Sec 1 (Jul 2022, imorules mirror): https://www.imorules.com/LRSHIP_PT6_CH2_1.html
- [LR-P6C2-10] LR Rules Pt 6 Ch 2 Sec 10, Converter equipment: https://www.imorules.com/LRSHIP_PT6_CH2_10.html
- [LR-P6C2-12] LR Rules Pt 6 Ch 2 Sec 12, Batteries: https://www.imorules.com/LRSHIP_PT6_CH2_12.html
- [LR-P6C2-16] LR Rules Pt 6 Ch 2 Sec 16, Electric propulsion: https://www.imorules.com/LRSHIP_PT6_CH2_16.html
- [LR-P6C2-24] LR Rules Pt 6 Ch 2 Sec 24, Hybrid electrical power systems: https://www.imorules.com/GUID-F26B54AD-CA6B-483A-925D-974D0F5FE001.html
- [LR-TS] LR, Type Approval Test Specifications: https://www.lr.org/en/knowledge/lloyds-register-rules/type-approval-test-specifications2/type-approval-test-specifications/
- [BV467C] Bureau Veritas NR467, Rules for the Classification of Steel Ships, Part C (Jul 2026): https://rulesexplorer-docs.bureauveritas.com/documents/nr467/jul2026/467-NR_PartC_2026-07.pdf
- [BV467F] Bureau Veritas NR467, Part F (Jul 2026): https://rulesexplorer-docs.bureauveritas.com/documents/nr467/jul2026/467-NR_PartF_2026-07.pdf
- [IRS-G-BATT] IRS, Guidelines on Battery Powered Vessels, Rev 3 (Sep 2026): https://www.irclass.org/media/8778/guidelines_battery-powered-vessels_rev03_sept-2026.pdf
- [IRS-CN23A] IRS CN 23a, Approval of Lithium-ion Battery Systems, Rev 2 (Sep 2026): https://www.irclass.org/media/8774/23a-cn-approval-of-lithium-ion-battery-systems_rev2_sept-2026.pdf
- [IRS-CN13] IRS CN 13 (Mar 2025), type approval of electrical equipment for control, protection and safety: https://www.irclass.org/media/7785/13a-cn-electr-equip-cntrl-prote-safy-envnr_march-2025.pdf
- [IEC-101] IEC 60092-101:2018 preview: https://cdn.standards.iteh.ai/samples/21328/f815aed8028342119da0d1fcd71fd1fe/IEC-60092-101-2018.pdf (webstore: https://webstore.iec.ch/en/publication/29989)
- [IEC-501-2025] IEC 60092-501:2025, IEC webstore: https://webstore.iec.ch/en/publication/92837 (Ed.5: https://webstore.iec.ch/en/publication/703)
- [IEC-501-SAMPLE] IEC 60092-501:2025 preview: https://cdn.standards.iteh.ai/samples/iec/iec-60092-501-2025/bd40e24641d940af8355de3dd2639ab6/iec-60092-501-2025.pdf
- [IEC-503] IEC 60092-503:2021 preview: https://cdn.standards.iteh.ai/samples/104044/dc76c7984a784d7eae581ab4602c83e2/IEC-60092-503-2021.pdf
- [IEC-PAS63108] IEC PAS 63108:2017, Primary DC distribution: https://webstore.iec.ch/publication/59753
- [IEC-60533] IEC 60533:2015 preview: https://cdn.standards.iteh.ai/samples/17759/d3b05ed567de4c2ea35e793d25b547bb/IEC-60533-2015.pdf
- [IEC-18-41] IEC 60034-18-41:2014 preview: https://cdn.standards.iteh.ai/samples/18905/b95c2f0bc77e4b658894b3e6629e3aa2/IEC-60034-18-41-2014.pdf
- [IEC-18-41-A1] IEC 60034-18-41:2014/AMD1:2019 preview: https://cdn.standards.iteh.ai/samples/23527/a1d9bbea464c4242a53f644c696783a0/IEC-60034-18-41-2014-AMD1-2019.pdf
- [IEEE-80005-4] IEEE P80005-4, DC shore connection: https://standards.ieee.org/ieee/80005-4/11275/
- [IMO-MSC137] IMO MSC.137(76), Standards for Ship Manoeuvrability: https://wwwcdn.imo.org/localresources/en/KnowledgeCentre/IndexofIMOResolutions/MSCResolutions/MSC.137(76).pdf
- [SOLAS-28] SOLAS II-1 Reg.28 (imorules): https://www.imorules.com/GUID-F47DF1A7-8BBA-4E93-A894-8C380214D99C.html
- [SOLAS-SRTP] SOLAS II-2 Reg.21 (imorules): https://imorules.com/GUID-5A6A972E-F1BF-470E-9E64-BD99966E3E02.html
- [EMSA-BESS] EMSA, Guidance on the Safety of BESS on board ships v1.0 (Nov 2023): https://www.emsa.europa.eu/publications/inventories/download/7643/5061/23.html
- [RISE-2017] RISE, Safe introduction of battery propulsion at sea (2017): https://publications.lib.chalmers.se/records/fulltext/250628/250628.pdf
- [MOPSW-GTTP] MoPSW Sagar Vidya Kosh, GTTP: https://mopsw.nic.in/sagarvidyakosh/index.php?title=GTTP

**Manufacturers, certificates and technical papers**
- [AN17] Semikron Danfoss AN 17-003 Rev.01 (2024), Cosmic Ray Failures in Power Electronics: https://assets.danfoss.com/documents/latest/465381/AB501641368473en-000201.pdf
- [AN21] Semikron Danfoss AN 21-001 Rev.02 (2024), Power Cycle Model for IGBT Product Lines: https://assets.danfoss.com/documents/latest/444233/AB501650017495en-000201.pdf
- [MAN] MAN Energy Solutions, L23/30DF Project Guide (2024-08-23): https://man-es.com/applications/projectguides/4stroke/manualcontent/PG_M-III_L2330DF.pdf
- [DF-TA-EC1200] DNV TA certificate TAE00004BJ, Danfoss Editron EC-C1200-450-L: https://assets.danfoss.com/approvals/latest/396866/ID488443225810-0101.pdf
- [DF-EC1200-DS] Danfoss, EC-C1200-450 data sheet (BIBUS-hosted): https://bibus-pim-production-storage.sos-ch-dk-2.exoscale-cdn.com/media/filer_public/d5/2d/d52d5d81-0e67-4468-8256-94be58225388/electric_converter_ec-c1200-450_data_sheet_danfoss_en.pdf
- [DF-EC1700-DS] Danfoss, EC-C1700B-420 data sheet: https://assets.danfoss.com/documents/latest/357478/AI454140063189en-000201.pdf
- [DF-FEGG] Danfoss Editron single-line diagram, "Feggesund Ferry Parallel Hybrid H2" (Den Danske Maritime Fond annex): https://dendanskemaritimefond.dk/wp-content/uploads/2020/06/Bilag-9-Danfoss-1.pdf
- [DF-CCS] Danfoss, "Danfoss Editron obtains marine certification in China": https://www.danfoss.com/en-us/about-danfoss/news/dps/danfoss-editron-obtains-marine-certification-in-china/
- [DF-MOTORS] Danfoss, Electric motors: https://www.danfoss.com/en/products/dps/electric-converters-motors-and-systems/electric-motors-and-generators/electric-motors/
- [DF-PMI375] Danfoss, Editron EM-PMI375 690 V motor launch: https://www.danfoss.com/en/about-danfoss/news/dps/danfoss-power-solutions-launches-editron-em-pmi375-690-volt-electric-motor-creating-a-mobile-grade-solution-for-higher-voltage-applications/
- [DF-ELLEN] Danfoss case story, "Bringing to life the future of the marine industry": https://www.danfoss.com/en-us/service-and-support/case-stories/dps/bringing-to-life-the-future-of-the-marine-industry
- [DF-OUTFILT] Danfoss, Output Filters Design Guide MG.90.N4.02: https://assets.danfoss.com/documents/277620/AJ361178726334en-000101.pdf
- [DF-MCC102] Danfoss, VLT dU/dt Filter MCC 102 fact sheet: https://files.danfoss.com/download/Drives/LoRes_DKDDPFO615A202_dUdt_Filter_MCC102.pdf
- [SIE-BD-CAT] Siemens Energy, BlueDrive Propulsion Catalogue 2021-04: https://assets.siemens-energy.com/dam/2bc03f4f-46d1-4764-bd68-b0670081dbba/BlueDrivePropulsion-Catalogue-V1-pdf_Original%20file.pdf
- [SIE-PLUSC] Siemens Energy, BlueDrive PlusC brochure: https://assets.siemens-energy.com/dam/6173a663-c272-4b7a-9ddc-b0670081ee06/BlueDrivePlusCBrochure-V1-pdf_Original%20file.pdf
- [SIE-SISHIP] Siemens press, "Siship BlueDrive: Scalable electric drive for reduced emissions": https://press.siemens.com/global/en/pressrelease/siship-bluedrive-scalable-electric-drive-reduced-emissions
- [SIE-DP2016] Settemsdal et al. (Siemens), Fault Ride-Through Testing of BlueDrive PlusC, DP Conference 2016: https://dynamic-positioning.com/proceedings/dp2016/EnvironmentTesting_Settemsdal_pp_2016.pdf
- [SIE-FERRYTALE] Siemens Energy, "Norway's 'ferrytale' on green waves": https://www.siemens-energy.com/global/en/home/stories/electrifying-the-sea.html
- [ABB-DCG-TN] ABB, Onboard DC Grid technical note (2011): https://resources.news.e.abb.com/attachments/published/13107/en-US/A79B8B042988/12-10-onboarddcgrid-technical-information.pdf
- [ABB-DP2011] Hansen, Lindtjørn, Vanska (ABB), Onboard DC Grid for enhanced DP operation, DP Conference 2011: https://dynamic-positioning.com/proceedings/dp2011/power_hansen.pdf
- [ABB-EEHB] ABB Energy Efficiency Handbook 5.7, Onboard DC Grid: https://library.e.abb.com/public/8a79a50eed6e4cf1ac673988bd448b32/Detailed%20Description%20_%20Onboard%20DC%20Grid.pdf
- [ABB-IEEE17] Qi et al. (ABB), DC Power Distribution: New Opportunities and Challenges (IEEE 2017): https://library.e.abb.com/public/6eee07fdd30546018651c06f26ab6373/08001020.pdf
- [ABB-HES-TA] DNV GL TA certificate TAE000027E, ABB HES880: https://library.e.abb.com/public/f1748c0c84da4b62a0d377bbc751dd9e/HES880_DNV-GL_Type_Approval_Certificate_TAE000027E.pdf?x-sign=ZZ5Rx0uxCPnu1PyaK5g0scD/zroVtRa1ekoJwpP4Imr3tR/paqPNNwYxJqaAICSY
- [ABB-HES-FF] ABB HES880 factfile v7: https://cdn-documents.yodify.com//documents/ABB-Group/HES880-Mobile-Drive/egmjG/7c1b590b-e084-4531-bc1f-3dc587f6d42d/HES880_factfile_v7.pdf
- [ABB-HES-WEB] ABB HES880 web page (search-result excerpt only): https://www.abb.com/global/en/areas/motion/traction/mobile-inverter/hes880
- [ABB-ACS880-MAR] ABB, Marine certified ACS880 drives flyer (2018): https://library.e.abb.com/public/cfc75c37bb5f4464ad6552f1607ab7e2/20988_ABB_ACS880_Marine_flyer_3AUA0000189301_REVC_LR.pdf
- [ABB-ACS880-QIG] ABB, ACS880-01 quick installation guide: https://library.e.abb.com/public/fb5c22d92afd42178aa498c4e6ed79f9/EN_ACS880-01_QISG_D_B5.pdf?x-sign=7LFBtKvYfQ9vHycG53z7B0h1gjnXs4aw9vejvFufjaHFHRWtJbnCyDlf3LzTu5V4
- [ABB-GND] ABB, Grounding and cabling of drive systems (2024): https://library.e.abb.com/public/9fbb4caf4e7b4bf7970839b083ba5396/EN_Drive_systems_grounding_and_cabling_REF_D_A4.pdf
- [ABB-TG102] ABB Technical Guide No.102, Effects of AC Drives on Motor Insulation: https://library.e.abb.com/public/5df66cce8dd649689ab7c2c66abd52db/voltstress.pdf?x-sign=0Rp9IiNFm7ar%2FbVLnJCZfcrnxpaWWzZnWyydcaedIxcDvRYNlxx38cQscNNQYtU1
- [ABB-TN176] ABB Technical Note 176, Motor cable distance from drives (2024): https://library.e.abb.com/public/6704602d55da42778215ff96c7bd3d7c/Technical_Note_176_MotorCableDistanceFromDrives.pdf?x-sign=XzMAVImetMBKgA8gd7wD6CFmP2kdczd6zBnxVbxmUIcio2hIMgmejCiauHIbexZq
- [ABB-TG5] ABB Technical guide No.5, Bearing currents in modern AC drive systems: https://library.e.abb.com/public/8c253c2417ed0238c125788f003cca8e/ABB_Technical_guide_No5_RevC.pdf
- [ABB-BRG18] ABB, Bearing currents and their mitigation (2018): https://library.e.abb.com/public/619767be6f9340009060c208fe3c2d45/Bearing%20currents%20options-9AKK107336-EN-06-2018.pdf
- [ABB-MOTCAT] ABB, LV Process performance motors catalog (2010): https://library.e.abb.com/public/28e5206d6766e183c1257b130056f4f6/Catalog_process%20performance%20motors_EN_04_2010_lowres.pdf
- [ABB-DP2001] Ylitalo & Ådnanes (ABB), New Thruster Concept for Station Keeping and Electric Propulsion, DP 2001: https://dynamic-positioning.com/proceedings/dp2001/drives_adnanes.pdf
- [ABB-AZICO] ABB, Azipod CO Product Introduction (2010): https://library.e.abb.com/public/933fa7cfa4392993c1257b1a005b78e9/Azipod%20CO_Product%20Introduction.pdf
- [ABB-COMPACT] ABB Review 4/2001, Compact Azipod: https://library.e.abb.com/public/4cd3a27c08106322c1256ddd00346fbd/10-13%20M729.pdf
- [ABB-EPE07] ABB (EPE 2007), Redundant Drive with DTC and Dual-Star Synchronous Machine: https://library.e.abb.com/public/6ecce91d66ee4088a3dc05283cd59d82/Redundant_Drive_with_DTC_and_Dual-Star_Synchronous_Machine.pdf
- [KM-AZPM] Kongsberg, The new permanent-magnet-driven azimuth thruster (AZ-PM): https://www.kongsberg.com/contentassets/830f65761a9b4307ac32c10d35236243/20.azimuth_2p_09.04.21.pdf
- [KM-RDAZ] Kongsberg Maritime, Rim Drive Azimuth Thruster: https://www.kongsbergmaritime.com/products/propulsors-and-propulsion-systems/thrusters/direct-electric-drive/rim-drive-azimuth-thruster/
- [KM-HAMLET] Kongsberg Maritime, "Ferry goes all-electric with Kongsberg Maritime upgrade": https://kongsbergmaritime.com/feature_articles/2026/1/ferry-hits-the-electric-switch/
- [KM-DCHYB] Kongsberg Maritime, DC hybrid solution: https://www.kongsbergmaritime.com/products/electrical-power-system/energy-solutions/dc-hybrid-solution/
- [KM-DCYACHT] Kongsberg, DC hybrid solutions for yachts: https://www.kongsbergmaritime.com/contentassets/b6206103b3424caa918c641f76e033c0/54.propsyst-2p-15.09.21.pdf
- [WARTSILA-EP] Wärtsilä, Electric propulsion systems: https://www.wartsila.com/marine/products/ship-electrification-solutions/electric-propulsion-systems
- [WARTSILA-SES] Wärtsilä, Ship electrification solutions: https://www.wartsila.com/marine/products/ship-electrification-solutions
- [WARTSILA-HYTUG] Wärtsilä, The Wärtsilä HY Tug system: https://www.wartsila.com/insights/article/the-wartsila-hy-tug-system-electric-hybrid-designs-to-save-costs-and-reduce-emissions
- [SWITCH-DCHUB] The Switch, DC-Hub brochure (v4, 8/2026): https://theswitch.com/wp-content/uploads/2024/05/Marine_Brochure_EN_DC-Hub_web_20260815.pdf
- [SWITCH-PMM] The Switch, Permanent Magnet Machines for Marine: https://theswitch.com/marine/permanent-magnet-machines/
- [ING-E3] Ingeteam, INGEDRIVE E3-Ship catalogue: https://www.ingeteam.com/sites/default/files/2025-04/EN_C_INGEDRIVETM_eVESSEL.pdf
- [ING-NAVAL] Ingeteam, Naval: https://www.ingeteam.com/es/naval
- [SCH-SRP] Schottel, SRP RudderPropeller: https://www.schottel.de/en/portfolio/products/product-details/srp-schottel-rudderpropeller
- [SCH-RA500] Schottel, "Robert Allan and SCHOTTEL celebrate milestone of 500 tugs": https://www.schottel.de/en/media-events/press-releases/press-detail/robert-allan-and-schottel-celebrate-milestone-of-500-tugs
- [VETH] Twin Disc, Veth Integrated L-Drive: https://twindisc.com/product/veth-integrated-l-drive/
- [CORVUS-ORCA] Corvus Orca ESS datasheet (2025): https://old.corvusenergy.com/wp-content/uploads/2019/04/datasheets_Corvus-Energy_Orca-ESS_2025.02.23.pdf
- [CORVUS-BW] Corvus Blue Whale ESS datasheet (2025): https://old.corvusenergy.com/wp-content/uploads/2019/05/datasheets_Corvus-Energy_Blue-Whale-ESS_2025.02.23.pdf
- [CORVUS-DOLE] Corvus Dolphin NxtGen Energy datasheet: https://old.corvusenergy.com/wp-content/uploads/2019/04/datasheets_Corvus-Energy_Dolphin-NxtGen-ESS-Energy_2025.02.23.pdf
- [CORVUS-DOLP] Corvus Dolphin NxtGen Power datasheet: https://old.corvusenergy.com/wp-content/uploads/2024/07/datasheets_Corvus-Energy_Dolphin-NxtGen-ESS-Power_2025.02.242.pdf
- [CORVUS-AMPERE] Corvus, "MF Ampere – World's first fully electric ferry sailing for over 10 years": https://corvusenergy.com/sucess-stories/mf-ampere-world-s-first-fully-electric-ferry-sailing-for-over-10-years
- [BW-BWNXT] BW Group, "Corvus Energy launches next-generation marine LFP energy storage system": https://bw-group.com/newsroom/articles/2025/12/corvus-energy-launches-next-generation-marine-lfp-energy-storage-system/
- [LECL-MRS3] Leclanché, Navius MRS-3 datasheet: https://www.leclanche.com/wp-content/uploads/2023/10/NAVIUS-MRS3.pdf
- [ECHANDIA-DS] Echandia, Technical Data Sheet V03.100 (2022): https://19496325.fs1.hubspotusercontent-na1.net/hubfs/19496325/Echandia-Technical-datasheet-June22-V03.100.pdf
- [TOSHIBA-SCIB] Toshiba, SCiB module lineup: https://www.global.toshiba/ww/products-solutions/battery/scib/product/module.html
- [BENDER-SHIPS] Bender, "A safe trip all the time – with reliable power supply": https://www.benderinc.com/fileadmin/content/BenderGroup/Documents/Article/en/Schiffe_2013_en_8er.pdf
- [BENDER-1685] Bender, ISOMETER iso1685DP: https://www.benderinc.com/products/ground-fault-monitoring-ungrounded/isometer-iso1685dp-isohv1685d-isolr1685dp/
- [BENDER-GFM] Bender, Ground-fault monitoring: https://www.benderinc.com/know-how/technology/ungrounded-system/ground-fault-monitoring/
- [GAMBICA] GAMBICA/BEAMA, Technical report on motor winding insulation (4th ed., 2016): https://www.gambica.org.uk/asset/5E97C960-870E-4C67-82BC1017EEC7089A/
- [IRIS-IEC] Stone & Lloyd, IEC Standards for Variable Speed Drives and Motor Winding Insulation (2014): https://irispower.com/wp-content/uploads/2018/06/IEC-Standards-for-Variable-Speed-Drives-and-Motor-Winding-Insulation.pdf
- [NARAYAN-TPEL] Narayanasamy et al., Reflected Wave Phenomenon in SiC Motor Drives, IEEE TPEL 35(10) 2020: https://par.nsf.gov/servlets/purl/10241024
- [POLYCAB] Polycab, Polymarine IEC 60092-353 0.6/1 kV VFD cable: https://polycab.com/polymarine-iec-60092-353-0610kv-vfd-cable/a-5038/p-54892
- [CASCADIA] Cascadia Motion, CM350SiC: https://www.cascadiamotion.com/cm-350sic
- [DANATM4] Dana TM4, inverters: https://www.danatm4.com/inverters.html
- [TORQ] Torqeedo, Deep Blue 100i 900: https://www.torqeedo.com/en/products/inboards/deep-blue-100i-900/000-01059.html
- [RAD] RAD Propulsion, RAD 120: https://radpropulsion.com/propulsion/rad-120-electric-outboard/
- [EVOY] Evoy, inboard: https://www.evoy.no/evoy-inboard/
- [CAND-P12] Candela, P-12 Shuttle: https://candela.com/pro-series/p-12-shuttle/
- [CROWLEY-SPEC] Crowley, eWolf spec sheet: https://www.crowley.com/wp-content/uploads/sites/7/2024/08/24-05635-eWolf-Spec-Sheet-V3.pdf
- [DAMEN-SPARKY] Damen, "All-electric Tug Sparky delivered to Ports of Auckland": https://www.damen.com/insights-center/news/damen-s-first-all-electric-tug-sparky-delivered-to-ports-of-auckland
- [DAMEN-SPARKY-2] Damen, "RSD-E Tug 2513 Sparky nominated for Tug of the Year": https://www.damen.com/insights-center/news/damen-rsd-e-tug-2513-sparky-short-listed-in-its-awards-2022
- [HAISEA] HaiSea Marine, Battery Electric Harbour Tug: https://haiseamarine.com/our-vessels/harbour-tugs/
- [SEASPAN] Seaspan, HaiSea Wamis release: https://www.seaspan.com/press-release/haisea-marine-warmly-welcomes-the-worlds-first-fully-electric-tugboat-the-haisea-wamis/
- [ZEETUG] Zeetug, "World's first rechargeable all-electric harbour tugboat": https://www.zeetug.com/post/world-s-first-rechargeable-all-electric-harbour-tugboat
- [NORLED] Norled, "MF Ampere marks its 10th anniversary": https://www.norled.no/en/nyhet/mf-ampere-marks-its-10th-anniversary-a-world-first-that-started-a-green-revolution/
- [FORSEA-PR] ForSea press release (24 Jun 2021), Tycho Brahe battery upgrade: https://www.mynewsdesk.com/forsea/pressreleases/forsea-to-upgrade-tycho-brahe-with-the-worlds-largest-battery-pack-extending-the-vessel-lifetime-and-cutting-emissions-3112242
- [SHIPPAX] Shippax, "ForSea's TYCHO BRAHE is now upgraded with the world's largest battery pack": https://www.shippax.com/en/press-releases/forseas-tycho-brahe-is-now-upgraded-with-the-worlds-largest-battery-pack.aspx
- [AERO-ELLEN] Abrahamsen (Ærø EnergyLab), "The E-ferry Ellen" slides (2021): https://eu.boell.org/sites/default/files/2021-07/The%20E-ferry%20Ellen,%20by%20Halfdan%20Abrahamsen,%20%C3%86r%C3%B8%20EnergyLab_2021-06-22.pdf
- [BOELL-ELLEN] Heinrich Böll Stiftung, interview on E-ferry Ellen (2021): https://eu.boell.org/en/2021/07/07/all-aboard-e-ferry-ellen-and-future-electric-shipping
- [EPRI-AMPERE] EPRI, Battery Electric Ferry: The Ampere in Norway (2015): https://restservice.epri.com/publicdownload/000000003002007147/0/Product
- [UCI-CARB] UCI for CARB, Commercial Harbor Craft Technology Assessment (Oct 2024): https://ww2.arb.ca.gov/sites/default/files/2024-11/ADA%20PDF_UCI_CARB_CHCReport_10062024_updated%20(1).pdf
- [KWM] Kochi Water Metro, About: https://watermetro.co.in/about
- [CSL-90] Cochin Shipyard news (GTTP tugs for Polestar / JNPA): https://cochinshipyard.in/news/view/90
- [CSL-HOME] Cochin Shipyard home page: https://cochinshipyard.in/

**Secondary (trade press, Wikipedia, exam-prep sites)**
- [BM-MUZIRIS] Baird Maritime, Vessel Review: Muziris: https://www.bairdmaritime.com/work-boat-world/passenger-vessel-world/ferries/vessel-review-muziris-indian-metro-ferry-operator-goes-hybrid-electric-with-100-pax-newbuild
- [OE-KWM] Offshore Energy, "India: 1st 100 pax hybrid electric ferry classed": https://www.offshore-energy.biz/1st-100-pax-hybrid-electric-catamaran-classed/
- [RIV-ECHANDIA] Riviera, "Batteries selected for world's largest fleet of electric ferries": https://www.rivieramm.com/news-content-hub/news-content-hub/batteries-selected-for-worldrsquos-largest-fleet-of-electric-ferries-62442
- [ST-KWM] Ship Technology, Kochi Water Metro Project: https://www.ship-technology.com/projects/kochi-water-metro-project-india/
- [WIKI-ADITYA] Wikipedia, Aditya (boat): https://en.wikipedia.org/wiki/Aditya_(boat)
- [WIKI-INDRA] Wikipedia, Indra (boat): https://en.wikipedia.org/wiki/Indra_(boat)
- [PLUG-ADITYA] Plugboats, "This solar ferry in India runs on $2.60 a day": https://plugboats.com/this-solar-ferry-in-india-runs-on-2-60-a-day/
- [PLUG-LIMO] Plugboats, Navalt boat delivered to Canada: https://plugboats.com/indian-solar-electric-ferry-delivered-canada/
- [ELEC-P12] electrive, "Candela kicks off series production of the P-12": https://www.electrive.com/2023/11/17/candela-kicks-off-series-production-of-the-p-12-water-taxi/
- [ST-AMPERE] Ship Technology, Ampere Electric-Powered Ferry: https://www.ship-technology.com/projects/norled-zerocat-electric-powered-ferry/
- [WIKI-ELLEN] Wikipedia, Ellen (E-ferry): https://en.wikipedia.org/wiki/Ellen_(E-ferry)
- [RIV-ELLEN] Riviera, "E-ferry Ellen: a 'milestone' in commercial marine propulsion": https://www.rivieramm.com/editors-choice-brand/e-ferry-ellen-a-lsquomilestonersquo-in-commercial-marine-propulsion-55958
- [WIKI-TYCHO] Wikipedia, MF Tycho Brahe: https://en.wikipedia.org/wiki/MF_Tycho_Brahe
- [MAREX-FORSEA] Maritime Executive, "ForSea Converts World's Largest Battery Ferries, Powered by ABB": https://maritime-executive.com/corporate/forsea-converts-world-s-largest-battery-ferries-powered-by-abb
- [RIV-FORSEA] Riviera, "ForSea Ferries battery conversion: a 'big little journey'": https://www.rivieramm.com/news-content-hub/news-content-hub/forsea-ferries-battery-conversion-a-lsquobig-little-journeyrsquo-57394
- [ELEC-BASTO] electrive, "World's largest electric ferry launches in Norway": https://www.electrive.com/2021/03/02/worlds-largest-electric-ferry-yet-goes-into-service-in-norway/
- [RIV-BASTO] Riviera, "Bastø Electric: a milestone for sustainable shipping": https://www.rivieramm.com/news-content-hub/news-content-hub/bastoslash-electric-a-milestone-for-sustainable-shipping-65049
- [CT-BASTO] CleanTechnica (21 Sep 2026), Bastø Electric five years on: https://cleantechnica.com/2026/09/21/norways-basto-electric-shows-what-five-years-of-ferry-electrification-looks-like/
- [OE-SPARKY] Offshore Energy, Port of Antwerp-Bruges RSD-E tug: https://www.offshore-energy.biz/europes-1st-all-electric-tug-on-the-horizon-as-part-of-port-of-antwerp-bruges-tug-renewal-deal-with-damen/
- [SS-SPARKY] Sustainable Ships, "Damen's Sparky": https://www.sustainable-ships.org/stories/2023/sparky-damen
- [BM-EWOLF] Baird Maritime, Vessel Review: eWolf: https://www.bairdmaritime.com/tugs/harbour-tugs-and-operation/vessel-review-ewolf-first-us-built-all-electric-tug-joins-crowleys-ship-assist-fleet
- [ML-EWOLF] MarineLink, "On Board the eWolf": https://www.marinelink.com/news/board-ewolf-first-electric-tugboat-us-512741
- [ML-CHARGE] MarineLink, "Leading The Charge": https://www.marinelink.com/news/leading-charge-492228
- [MAREX-HAISEA] Maritime Executive, "Electric Tug HAISEA WAMIS Delivered": https://maritime-executive.com/corporate/electric-tug-haisea-wamis-delivered-to-eco-friendly-haisea-marine
- [RIV-HAISEA] Riviera, "World's first ElectRA battery-powered tug delivered": https://www.rivieramm.com/news-content-hub/news-content-hub/worlds-first-electra-battery-powered-tug-delivered-76434
- [MAREX-ZEETUG] Maritime Executive, "ABB Drives Provide the Pulling Power for Zeetug": https://maritime-executive.com/corporate/abb-drives-provide-the-pulling-power-for-zeetug
- [ML-LEDRIVE] MarineLink, "SCHOTTEL Introduces Embedded L-Drive": https://www.marinelink.com/news/schottel-introduces-embedded-ldrive-487063
- [RIV-AZIPODD] Riviera, "ABB launches new Azipod D": https://www.rivieramm.com/opinion/opinion/abb-launches-new-azipod-d-36534
- [AMTI-REM] AMTI, "Kongsberg Maritime to supply permanent magnet thrusters for Rem Offshore vessels": https://www.advancedmaritimetechnologyinternational.com/news/power-and-propulsion/kongsberg-maritime-to-supply-permanent-magnet-thrusters-for-rem-offshore-vessels.html
- [MI-CRUISE] Marine Insight, "How Are Cruise Ships Powered?": https://www.marineinsight.com/cruise-ships-powered/
- [ML-ABB-GTTP] MarineLink, "ABB to Equip Electric Tugs Under India Green Fleet Plan": https://www.marinelink.com/news/abb-equip-electric-tugs-india-green-fleet-539954
- [WEEK-CSL] The Week, "Cochin Shipyard wins major EV green tug order for JNPA deployment": https://www.theweek.in/news/maritime/2026/01/31/maritime-cochin-shipyard-wins-major-ev-green-tug-order-for-jnpa-deployment.html
- [CLARITY] Clarity UPSC, "Green Tug Transition Programme – India's First Fully Electric Tug": https://clarityupsc.com/news/article/green-tug-transition-programme-indias-first-fully-electric-tug-1375
- [GKTODAY] GKToday, GTTP question page: https://www.gktoday.in/question/green-tug-transition-programme-gttp-is-an-initiative-of-which-ministry-2
- [VAJIRAM] Vajiram & Ravi, Green Tug Transition Programme: https://vajiramandravi.com/current-affairs/green-tug-transition-programme/
- [SANSKRITI] Sanskriti IAS, Green Tug Transition Program: https://www.sanskritiias.com/current-affairs/green-tug-transition-program
- [ML-BATT26] MarineLink, "Batteries at Sea: Not All-Electric, Not Everywhere but Increasingly Essential" (Jul 2026): https://www.marinelink.com/news/batteries-sea-not-allelectric-not-541405
- [WB-CORVUS] WorkBoat, "Corvus Energy expands LFP battery portfolio" (Aug 2026): https://www.workboat.com/corvus-energy-expands-lfp-battery-portfolio-for-workboats-ferries
- [WRENN-THESIS] Wrenn, "Diesel-Electric Hybrid Tugboats Using Lithium-Ion Batteries" (MS thesis, 2022): https://etd.ohiolink.edu/acprod/odb_etd/ws/send_file/send?accession=case1654878038516448&disposition=inline
- [PROMAR] Professional Mariner, hybrid tug designs (2008): https://professionalmariner.com/hybrid-tug-designs-aim-for-efficiencies-during-stop-and-go-harbor-operations/
- [DEVARAPALI] Devarapali et al., Electric tugboat deployment, Maritime Business Review (2024): https://www.emerald.com/mabr/article/9/3/263/1234638/Electric-tugboat-deployment-in-maritime
- [MEMIS] Memis, "A Terminal Tug's Annual Duty Profile" (LinkedIn, 2014): https://www.linkedin.com/pulse/20141010084248-90166035-terminal-tug-s-annual-duty-profile-and-fuel-consumptions-on-high-and-medium-speed-engines
