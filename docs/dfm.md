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
| T3 DigiKey/Mouser ships-today | VGT12EEM-200S1A4 ×6, TE 770669-1 header + 770680-1 plug, 470 R/10 W wirewounds ×4, BAT64-04, NRVBAF360T3G, NCV4276C (5.0 fixed ×1 + DTADJ ×2), Mornsun QA01C ×2, Murata MGJ2D150505SC ×2 (**PO gate:** verify reinforced cert @850 VDC working + SIP-7 pin map), TDK ACT45B ×2, Coilcraft XAL4020-222 ×1 + XAL4040-103 ×2, BUK7Y14-80E ×2 | ~20 | one consolidated DigiKey order |
| T4 LCSC-stocked specifics | NSI6611ASC-Q1SWR (full ordering code), AMC1311B, ALM2402Q-Q1, OPA348/333/376-Q1, UCC28C40DR, TCAN1042-Q1, TPS55340-Q1, 74LVC1G11/32-Q100, SN74LVC1G74 ×2, Faratronic 20 µF/1.1 kV ×16, Bourns MF-LSMF polyfuses, TLP152 ×2, PESD family, SMAJ13A ×2 … | ~60 | LCSC/JLC |
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
- Fiducials 3×/board + 2× local at the MCU; testpoints on every rail, PWM, FLT/RDY, SPI,
  both VDC senses, and the discharge command (flying-probe first articles, bed-of-nails at
  volume).
- Paste/stencil: 2512 pulse resistors and LFPAK56 need reduced-aperture review; film-cap and
  stud holes on the wave side only; XAL molded inductors are fine on standard profiles.
- Panelize the card 2-up with rails; power board single-up; discharge board 4-up.
- Keep every polarized THT part in one orientation per board.
- No bottom-side THT; bottom SMT limited to chip R/C if used at all.
- CIN_TRK ≥0.5 µF effective placed at the FS26 TRKIN pin (VPRE bank) — DS placement rule.
- The cap-bank / discharge kit's voltage class is proven at EOL by measurement, not inferred: LCR the
  fitted bank (320 µF ± 10 % 8XX / 800 µF 4XX) and the discharge string (1.88 k / 880 Ω) against the
  serial-linked kit record before the first HV energisation. FW-02's runtime τ check is a plausibility
  check only — a 4XX bank fitted with its own discharge board reads 0.71 s, inside the 8XX ±20 % band
  (round 12, R1-F24/R2-F28).
- ROHM ESR03 parts carry fault-hold power, so their fillet (terminal) temperature is part of the
  rating (DS Fig. 2/4). RASCG (2.2 k, 1 kΩ < R row) is full-rated to 110 °C and allows ≤ 138 °C at
  its 0.12 W; RFS4 (1 k, R ≤ 1 kΩ row) is full-rated to 130 °C and allows ≤ 132 °C at its 0.30 W
  (FS1B held at 18 V). Keep both off the bias-module, shunt and busbar hot spots; check on the
  thermal first article.

## 5. Proto vs production grade policy

Automotive (-Q1/-Q100/AEC) grades are the BOM primaries. Where LCSC stocks only the
commercial twin, the `ALT` column names it — allowed in bench/proto builds only, never in
DV/PV or production. The two NXP safety parts and the LEM sensors have no substitute at any
grade: their lead time IS the program's critical path — order at kickoff. Two PO gates
before any build: MGJ2D150505SC reinforced-cert + pin map, and the FS26 OTP variant per
design-basis §8a.
