# Manufacturability (DFM) — rev A.2, 2026-09-09

Goal: with the hiitio module supply secured by the direct relationship, **everything else
builds at any competent CEM from LCSC + DigiKey stock** — no exotic distribution, no
single-source jellybeans, one clean assembly flow. This revision was driven by two live
sourcing sweeps (LCSC exact-MPN check + DigiKey/Mouser/alternates check, 2026-09-09; reports
in the BOM notes).

## 1. Sourcing tiers (how the buy is organized)

| Tier | Lines | Parts | Channel |
|---|---|---|---|
| T1 Direct | HCS600FH120D3C1 ×3, HCM75S12T4K3 ×1 | 4 | hiitio (relationship in place) |
| T2 Franchise/NXP | S32K396, FS2633D, LEM HC5FW 900-S ×3 | 5 | NXP direct + LEM/Mouser — the ASIL-D anchors; order FIRST, longest lead. (LCSC marketplace lists both NXP parts in tens — proto builds only) |
| T3 DigiKey/Mouser ships-today | VGT12EEM-200S1A4 ×6, TE AMPSEAL 776231-1, 560R/10W wirewounds, BAT64-04, NRVBAF360T3G, NCV4276C, genuine Mornsun QA01C-18 ×2, TDK ACT45B ×2 | ~16 | one consolidated DigiKey order |
| T4 LCSC-stocked specifics | NSI6611A-Q1 (C7470934), AMC1311B (C456277), ALM2402Q-Q1 (C544754), OPA348-Q1 (C2877787), OPA333-Q1 (C2058544), UCC28C43 (C111694), TCAN1042-Q1 (C132550), TPS55340-Q1 (C2070860), 74LVC1G11/32-Q100H (C548242/C548334), SN74LVC1G74 (C70285), BUK9Y14 (C86732), Faratronic 20 µF/1.1 kV ×16 (C2840809), Bourns polyfuses (C719172), TLP152, PESD family, STPS5L60S, BAT46Z, MF crystal … | ~60 | LCSC/JLC |
| T5 CLASS jellybeans | all 0603/0805/1206/2512 R+C, zeners, SS34, 2N7002… | ~440 | LCSC basic / any |

**Rule that fell out of the sweeps:** nothing outside T1–T2 is single-channel anymore. Every
T3/T4 line carries a footprint-compatible alternate in the BOM `ALT` column.

## 2. Design changes made for manufacturability (rev A.2)

1. **Flyback controller NJW4140 → UCC28C43** (LCSC/DK/Mouser everywhere, TI+ST+onsemi
   multi-source vs. LCSC stock of 0–2 pcs for the Nisshinbo part). It has no EN pin, so a
   2-transistor default-OFF COMP clamp implements the enable — the harness default-OFF rule
   is preserved. A VCC trickle-start resistor from the 12 V rail was added at the same review
   (the aux-only VCC wiring could never have started — latent bug found by the DFM pass).
2. **DC link 8× 40 µF → 16× Faratronic C3D 20 µF/1100 V** (C2840809, in stock, ~$3.4):
   no 40 µF/1100 V can exists on LCSC in any brand, and the TDK 4-lead part is a single line
   at DigiKey. Same 320 µF, better ripple spread, **BOM −₹2.8k**; TDK B32778 remains the
   drop-in alt (one per two positions).
3. **Passive bleeder 9× TE CRGP (500 V specialty, TTI-only in volume) → 10× standard 27 k
   2512 2 W in 5s×2p** (170 V/resistor at 850 V — inside plain-2512 200 V working). Same
   67.5 kΩ / 58 s discharge as the XM3 network; any resistor vendor builds it.
4. **Resolver driver confirmed as ALM2402Q-Q1** — the sweep found it LCSC-stocked (C544754),
   so the GEN3-exact part stays (ALM2403-Q1 at DigiKey is the alt).
5. **Value consolidation**: 4.99 k→5.1 k, 49.9 k→51 k, 12.1 k→12 k where scaling is
   firmware-calibrated; MCU decoupling 0402→0603 (fewer feeders, easier rework, JLC-basic).
6. **Vehicle connector made concrete**: TE AMPSEAL 23-pos 776231-1 + 770680-1 plug
   (DigiKey ~$9), replacing the abstract "sealed 23-way" line.

## 3. Assembly flow (box build)

SMT both boards (single reflow side each is the layout target; ~740 placements total)
→ AOI → selective/wave THT (film caps, studs, transformers, headers, TO-247, axials)
→ conformal coat card → module mount on coldplate (torque + TIM verify) → power PCB onto
module terminals (**specify the solder-pin aux variant of the HCS600 or press-fit — decide
with hiitio's outline drawing, VERIFY item**) → laminated busbar + link caps → harnesses
→ lid + seal → EOL (hi-pot, LV functional, spin test on back-to-back rig).

## 4. Rules for the layout phase (so DFM survives it)

- 0603 minimum passive (0402 only where RF/loop area forces it — currently none).
- Fiducials 3×/board + 2× local at the MCU; testpoints on every rail, PWM, FLT/RDY, SPI,
  and both VDC senses (flying-probe first articles, bed-of-nails at volume).
- Paste/stencil: 2512 pulse resistors and LFPAK need reduced-aperture review; film-cap and
  stud holes on the wave side only.
- Panelize the card 2-up with rails; power board runs single-up.
- Keep every polarized THT part in one orientation per board.
- No bottom-side THT; bottom SMT limited to chip R/C if used at all.

## 5. Proto vs production grade policy

Automotive (-Q1/-Q100/AEC) grades are the BOM primaries. Where LCSC stocks only the
commercial twin, the `ALT` column names it — allowed in bench/proto builds only, never in
DV/PV or production. The two NXP safety parts and the LEM sensors have no substitute at any
grade: their lead time IS the program's critical path — order at kickoff.
