# Manufacturability (DFM) — rev A.6

Goal: with the hiitio module supply secured by the direct relationship, **everything else
builds at any competent CEM from LCSC + DigiKey stock** — no exotic distribution, no
single-source jellybeans, one clean flow across the four assemblies (power PCB, cap-bank
busbar assembly, bolt-on discharge PCB, control card). Grounded in two live sourcing sweeps
(LCSC exact-MPN + DigiKey/Mouser alternates, 2026-09-09) and five external review rounds
(findings F1–F76, all closed — see `verification-report.md`). Platform/SKU comparison: [`variants.md`](variants.md).

## 1. Sourcing tiers (how the buy is organized)

| Tier | Lines | Parts | Channel |
|---|---|---|---|
| T1 Direct | HCS600FH120D3C1 ×3 (SiC build) **or HCG600FH120D3E1EA ×3 (IGBT variant — same D3 pads/pins)**, HCM75S12T4K3 ×1 | 4 | hiitio (relationship in place) |
| T2 Franchise/NXP | S32K396, FS2633D (programmed per design-basis §8a OTP table), LEM HC5FW 900-S ×3 | 5 | NXP direct + LEM/Mouser — the ASIL-D anchors; order FIRST, longest lead |
| T3 DigiKey/Mouser ships-today | VGT12EEM-200S1A4 ×6, TE 770669-1 header + 770680-1 plug, TT SQP10-470RJB15 wirewounds ×4, BAT64-04, NRVBAF360T3G, NCV4276C (5.0 fixed ×1 + DTADJ ×4), TI UCC14141-Q1 ×2 + UCC12050 ×2 (**PO gate:** the UCC14141-Q1 VDE/UL certificates are "planned" in SLUSF10B §7.6 — confirm issued), Vishay VOW3120-X017T ×2, Vishay VY1 Y-caps ×2, Samtec IPL1-120-01-L-D-K ×2 + IPD1-20-D-K housings/CC79L crimps (harness), TDK ACT45B ×2, Coilcraft XAL4020-222 ×1 + XAL4040-103 ×2, BUK7Y14-80E ×2 | ~20 | one consolidated DigiKey order |
| T4 LCSC-stocked specifics | NSI6611ASC-Q1SWR (full ordering code), AMC1311B, ALM2402Q-Q1, OPA348/333/376-Q1, UCC28C40DR, TCAN1042-Q1, TPS55340-Q1, 74LVC1G11/32-Q100, SN74LVC1G74 ×2, Faratronic 20 µF/1.1 kV ×16 (4XX: C3D1U506KFAA382 50 µF/600 V, LCSC C783662), Bourns MF-LSMF polyfuses, PESD family, SMAJ13A ×2, TPSMC24CA-VR ×3 … | ~60 | LCSC/JLC |
| T5 CLASS jellybeans | all 0603/0805/1206/1210/2512 R+C, BZT52 zeners, 1N4148WS, SS34, 2N7002, studs/tabs | ~470 | LCSC basic / any |

**Rules that fell out of the sweeps and reviews:** nothing outside T1–T2 is single-channel
(every T3/T4 line carries a footprint-compatible `ALT`); **basic-insulation substitutes are
banned by name** on the reinforced-barrier bias lines (RECOM R15P05S rejected — its 6.4 kV
is a 1 s test, grade *basic* 250 VACrms working); full ordering codes only (NSI6611ASC-Q1SWR,
NCV4276CDTADJRKG — family labels caused F45/F57).

## 2. Design changes made for manufacturability (running log)

1. **Flyback controller NJW4140 → UCC28C40DR** (LCSC/DK/Mouser everywhere; the C43 grade's
   8.4 V UVLO could not start at 9 V crank — F31). No EN pin → 2-transistor default-OFF COMP
   clamp preserves the harness default-OFF rule; VCC trickle-start added (latent no-start bug).
2. **DC link 8× 40 µF → 16× Faratronic C3D 20 µF/1100 V** (in stock, ~$3.4): no 40 µF/1100 V
   can exists on LCSC in any brand. Same 320 µF, better ripple spread, BOM −₹2.8k; TDK B32778
   remains the drop-in alt (one per two positions). The cans live on the **cap-bank busbar
   assembly** (sheet 2), not FR4.
3. **Passive bleeder 9× TE CRGP (TTI-only) → 12× standard 22 k 2512 2 W in 6s×2p** (rev A.6:
   six per string keeps the worst-tolerance resistor at 154 V = 77 % of a plain 2512's 200 V
   working; five 27 k parts reached 92 %). 66 kΩ; 57 s to 60 V; any resistor vendor builds
   it. Lives on the **bolt-on discharge board** (sheet 3); the 4XX SKUs fit 15 k parts.
4. **Flyback switch BUK9Y14-80E → BUK7Y14-80E** (F56): logic-level ±10 V gate was illegal at
   the 11.8 V drive; the standard-level sibling is the same LFPAK56/price class.
5. **Value consolidation**: 4.99 k→5.1 k, 12.1 k→12 k where firmware-calibrated; MCU
   decoupling 0603 (fewer feeders, JLC-basic); both 10 µH inductors on one XAL4040-103 line.
6. **Vehicle connector made concrete and then corrected (F60)**: TE **770669-1** 23-pos
   AMPSEAL PCB header + 770680-1 plug (the earlier 776231-1 is the 35-pos header — caught
   from its own TE drawing).
7. **Resolver driver ALM2402Q-Q1** stays (LCSC-stocked, GEN3-exact), now fed from its own
   protective 12 V LDO (F55).

### Four SKUs on the same line (rev A.6)

`npm run bom:all` builds the **identical boards** for 8XX SiC / 8XX IGBT / 4XX IGBT / 4XX SiC.
IGBT SKUs swap the module and five values on existing pads (R⟨ph⟩⟨HL⟩DS 4.7 kΩ, C⟨ph⟩⟨HL⟩BL
82 pF, R⟨ph⟩⟨HL⟩ON/OFF 1.0 Ω, RF⟨HL⟩RT 8.2 kΩ); 4XX SKUs swap the 16 cans and the discharge
values (RDIS 220 Ω, RBLD 15 kΩ). Every SKU sets RHWID (its identity resistor). One SMT program,
reel changeovers per SKU; EOL reads HW_ID and rejects a unit whose parameter set disagrees.

## 3. Assembly flow (box build, four assemblies)

SMT the three PCBs (power / discharge / card; single reflow side each is the layout target;
~750 placements total) → AOI → selective/wave THT (studs, transformers, headers, TO-247,
axial wirewounds) → conformal coat card → **cap-bank assembly**: 16 cans onto the laminated
busbar (solder/clamp per busbar drawing), entry lugs + module tab pairs + discharge studs
torqued → modules onto coldplate (torque + TIM verify) → power PCB onto module aux pins
(**solder-pin vs press-fit variant of the HCS600 — decide with hiitio's outline drawing,
VERIFY item**) → busbar onto module DC tabs → **discharge board bolted across the cap-bank
studs (title-block rule: never energize without it)** → harnesses (40-way, 4-way discharge
link, LEM, vehicle) → lid + seal → EOL (hi-pot, LV functional, discharge-time check, spin
test on back-to-back rig).

## 4. Rules for the layout phase (so DFM survives it)

- 0603 minimum passive (0402 only where loop area forces it — currently none).
- The BOM generator reads each chip passive's drawn size from the built footprint and refuses a parts-db rule that orders another size (round 16), so a sheet/BOM package disagreement cannot reach the buyer.
- Fiducials 3×/board + 2× local at the MCU; testpoints on every rail, PWM, FLT/RDY, SPI,
  both VDC senses, and the discharge command (flying-probe first articles, bed-of-nails at
  volume).
- Paste/stencil: 2512 pulse resistors and LFPAK56 need reduced-aperture review; film-cap and
  stud holes on the wave side only; XAL molded inductors are fine on standard profiles.
- Panelize the card 2-up with rails; power board single-up; discharge board 4-up.
- Keep every polarized THT part in one orientation per board.
- No bottom-side THT; bottom SMT limited to chip R/C if used at all.
- CIN_TRK ≥0.5 µF effective placed at the FS26 TRKIN pin (VPRE bank) — DS placement rule.
- U5LB/U5LC (NCV4276C DPAK, ≈ 0.76 W each): ≥ 1.2 in² of 2 oz copper under each tab (DS 58.5 K/W on
  1.14 in² → Tj ≈ 129 °C at 85 °C); the hot first article measures both (round 13, A11-R04).
- UMCU 289-MAPBGA (0.8 mm pitch): via-in-pad or dog-bone per the NXP AN, 7×7 thermal via array under
  the package (DS thermal test board), all 25 VSS balls to the ground plane; the 159 open balls stay
  unconnected (docs/mcu-pin-manifest.md).
- The cap-bank / discharge kit's voltage class is proven at EOL by measurement, not inferred: LCR the
  fitted bank (320 µF ± 10 % 8XX / 800 µF 4XX) and the discharge string (1.88 k / 880 Ω) against the
  serial-linked kit record before the first HV energisation. FW-02's runtime τ check is a plausibility
  check only — a 4XX bank fitted with its own discharge board reads 0.71 s, inside the 8XX ±20 % band
  (round 12, R1-F24/R2-F28).
- ROHM ESR anti-surge parts carry fault-hold power, so their fillet (terminal) temperature is part of the
  rating (DS Fig. 2/4). RASCG (ESR03 2.2 k, 1 kΩ < R row) is full-rated to 110 °C and allows ≤ 138 °C at
  its 0.12 W; RFS4 is an **ESR18 1206 (0.5 W at 70 °C) since round 17** — 0.30 W with FS1B held at 18 V,
  0.48 W for the 24 V / 60 s jump start (0.96× of its rating; the 0603 ESR03 it replaces ran at 1.47×). Keep both off the bias-module, shunt and busbar hot spots; check on the
  thermal first article.

- **UCC14141-Q1 (PSASC/PSQD, A.12)** — TI SLUSF10B §9.5.1: the 100 nF (CxxIB) at pins 6/7–8 and the
  COUT 100 nF at pins 28/29–30/31 on the IC side with no via between cap and pin, the 10 µF bulk parts
  beside them; VEEA (35) joins VEE at a single point next to the FBVDD divider and its 330 pF; keep the
  primary/secondary copper split under the package (≥ 10 mm barrier, matching the VOW3120's ≥ 10 mm
  creepage on the same net pair). TI recommends 4 layers / 2 oz outer copper for its thermal path
  (RθJA 52.3 K/W at 1 W; ours carries a few mW).
- **MCU supply pins (A.13)** — CV25 (220 nF) within 2 mm of ball J7 on the MCU side; CBAL (1 nF) at the
  QBAL gate next to F1; H5 (V15) is fed from the V15S plane, never from the 15 V bias rail of the same name
  on the power board (the two nets are V15S and V15 — keep the labels).
- **Bias-LDO ballast R5LB/R5LC (A.13)** — 2512, 0.45 W each: on the same 2 oz copper as the LDO, ≥ 5 mm from
  the UCC12051-Q1 and the AMC1311; the LDO input cap C5Lx1 sits between the ballast and the LDO IN pin.
- **Resolver excitation protection (A.13, topology corrected A.14, unidirectional TVS A.15, 3 kW TVS + rated back-drive diodes A.16, diodes on the amplifier node + TVS island A.17)** — from the
  amplifier outwards: amplifier output node VREX_P/N (DEXP/DEXN PMEG4050EP-Q Schottky from HERE to VEXD — the rated
  back-drive path, in parallel with the ALM2402's own upper diode; **pin 1 = cathode on VEXD, pin 2 = anode on the
  amplifier node** — round 18 moved it off the protected node so the harness charging loop passes RSX; the REXB
  feedback resistors also stay here) → RSXP/RSXN (2.2 Ω 1206, **ROHM ESR18EZPF2R20 anti-surge 0.5 W** since round 18 —
  it carries the amplifier's 0.93 A source limit during a negative fault) → protected node (TVSEP/TVSEN **SMDJ7.0A-HRA**
  since round 19 (F203; SMDJ8.5A-HRA in rounds 17–18) — 3 kW, AEC-Q101, SMC — **cathode on the node, anode to the AGND
  star** with a wide short trace; the REXM monitor taps here) → FEXP/FEXN (Bourns MF-MSMF020/33X, **1812**) → the vehicle connector. The TVS must sit on the
  amplifier side of the PTC — a fault from the connector reaches the clamp only through the PTC (A13-R01).
  **Round 18/19 layout rule (F193, F203; verification-report "Sensing A.17"):** each TVS sits on its own **≥ 3 cm² 2 oz
  copper island with a thermal-via array** to the inner AGND copper, and **FEXP/FEXN are placed with their pads on that
  island**, next to the TVS. The rule is written for the PAIR, not for the TVS alone (round 19, xcheck19 §1h): the design
  numbers are a TVS-to-PTC transfer of ≈ 40 K/W (k_TP), a TVS junction-to-ambient of ≈ 55 K/W (R_thJL 15 + island 40) and
  a PTC body-to-ambient of ≈ 90 K/W. Why the pair: a sustained short of the line to a 12–16 V battery while the ECU sleeps
  leaves the tripped PTC as a thermostat at its switching temperature, passing P_d/(V_S − V_BR) (capped at I_trip) into the
  TVS indefinitely — 1.7–2.5 W with the 7.0 V class (3.8–4.3 W with the 8.5 V class of round 18). Coupled, every watt the
  island carries from the TVS into the PTC replaces a watt of the PTC's own I·(V_S − V_BR), so the trickle current falls; and
  where the PTC would never trip on current alone (< 0.4 A), its hold current extrapolates to ≈ 0 above ≈ 92–105 °C, so the
  TVS's heat trips it (the PolyZen principle). The model puts the TVS junction at ≤ 132 °C for the 7.0A at 11.4 V / 85 °C /
  T_t 125 °C (148–171 °C with the 8.5A — the reason the class moved). Caution: a LOWER island resistance alone raises the
  coupled junction (T_J − T_A → (T_t − T_A)·(1 + R_thJL·V_BR/(R_isl·V_S)) as the PTC's own path vanishes) — the pair's two
  transfers are what QP-RX-04 step 2b measures, not a TVS-alone R_thJA. The PTC body still keeps ~2 mm of free air above and
  beside it (only its pads share the island).
- **Motor-temperature protection (A.13)** — FMT (0603 fuse) first from the connector, then TVSM (SMA) to AGND,
  then the 1 k into the buffer; keep the fuse away from the LDO heat so its rating holds.
- **KL30 entry (round 17, load-dump let-through)** — from JVEH: FLVC (Bel 0680L5000-05, 2410 ceramic slow-blow fuse; it
  carries the whole inverter, keep it off the LDO/buck heat) → FCO, where DTVSC (TPSMC33A-VR, cathode on FCO) and DTVSC2
  (TPSMC18A-VR, cathode on DGND) sit in series to ground **ahead of DREVC** — a short, wide loop into the DGND entry (pulse 1
  drives 12.5 A through it) → DREVC → NRC. **CLVC3** (Panasonic EEH-ZC1H101P, φ10 × 10.2 mm G can, **10.5 mm maximum height —
  the card's component-height envelope and the housing clearance above it must allow it**; polarised, + on NRC, the sheet
  draws a plain C) sits beside DREVC and CLVC1 with the same short ground return, then LFC/CLVC2 to the FS26. DIGN (US1M,
  SMA) sits between the KL15 polyfuse and both the WAKE1 and the IGN dividers. On the power board ULDO15 is a **D2PAK-5**
  (NCV4276CDSADJR4G) since round 17: the same ≥ 1.2 in² of 2 oz copper under its tab as the other NCV4276C parts.
- **JIC/JICC (Samtec IPL1, A.12)** — confirm the header's pin-1 corner and the odd/even row numbering
  against the Samtec print before the footprint is placed; the HARNESS40 map keeps VBAT/V5GD next to
  ground under either scheme, but the silkscreen and the cable drawing must agree.

## 5. Proto vs production grade policy

Automotive (-Q1/-Q100/AEC) grades are the BOM primaries. Where LCSC stocks only the
commercial twin, the `ALT` column names it — allowed in bench/proto builds only, never in
DV/PV or production. The two NXP safety parts and the LEM sensors have no substitute at any
grade: their lead time IS the program's critical path — order at kickoff. Two PO gates
before any build: TI's VDE/UL certificates for the UCC14141-Q1 bias modules (listed "planned" in SLUSF10B §7.6;
the MGJ2 of earlier revisions is gone since A.11), and the FS26 OTP variant per design-basis §8a.
