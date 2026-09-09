# Traction Inverter — Full unit cost @1,000 units (rev A.1, 2026-09-09)

Complete ex-works build cost per inverter at a 1,000-unit run (India assembly, India+China
supply chain), on top of the generated electronics BOM (`docs/bom.md`). Figures are planning
numbers (RFQ ±25 %); the two lines that move the answer are flagged in §3.

## 1. Per-unit rollup (₹ @1k)

| # | Block | Low | Baseline | High | Notes |
|---|---|---|---|---|---|
| 1 | Electronics BOM (both boards) | 65,000 | 70,800 | 95,000 | `bom.md`; low/high = module + film-cap quote swing (§3) |
| 2 | PCBs (power 6L 2–3 oz ~0.12 m² + card 6L) | 4,500 | 5,200 | 6,000 | India/China fab @1k |
| 3 | PCBA (SMT ~700 placements + TH + AOI) | 2,500 | 3,200 | 4,000 | both boards |
| 4 | Liquid coldplate (FSW/gun-drilled Al, 3× EconoDUAL footprint, fittings) | 4,500 | 6,000 | 8,000 | ~360×160 mm, ≤0.05 K/W to coolant |
| 5 | Busbars (DC laminated + link-cap bus + 3 phase bars, plated) | 5,000 | 6,000 | 8,000 | laminated DC bus is what buys the ≤15 nH loop |
| 6 | Housing — **CNC** Al 6061 body + lid, seals, hard anodize | 6,500 | 8,500 | 11,000 | billet CNC as requested; gravity-cast + CNC faces ≈ ₹4–5.5k/unit + ₹4–7 L tooling (breaks even ~250 units) |
| 7 | HV connectors (DC-in 2P 300 A + HVIL; 3-phase out 3P 400 A) + coolant QCs | 4,000 | 5,200 | 6,500 | RADSOK-class (Chinese Amphenol-compatible); stud-through-gland saves ~₹2.5k if acceptable |
| 8 | Seals, Gore vent, EMC gasket, TIM (3 modules), fasteners, internal harnesses | 2,500 | 3,200 | 4,000 | |
| 9 | Assembly + EOL test (hi-pot, spin/power test on B2B rig) + conformal coat | 2,500 | 3,200 | 4,000 | incl. rig amortization over 1k |
| 10 | Yield / scrap / warranty reserve (3 %) | 2,900 | 3,300 | 4,400 | |
| | **Ex-works cost per unit** | **≈ 99,900** | **≈ 114,600** | **≈ 150,900** | |

**Baseline: ≈ ₹1.15 lakh / unit ≈ $1,380 ≈ ₹520/kW ($6.3/kW).**
1,000 units ⇒ **≈ ₹11.5 Cr** parts + build (range ₹10.0–15.1 Cr).

## 2. One-time NRE (not in the per-unit number)

| Item | ₹ |
|---|---|
| Coldplate + busbar + housing tooling/fixtures | 8–14 L |
| EOL back-to-back test rig (220 kW) | 15–25 L |
| DVT: CISPR 25 EMC + ISO 16750 environmental + thermal/durability | 35–70 L |
| **Hardware NRE subtotal** | **≈ 60–110 L → ₹6–11k/unit amortized over 1k** |
| ISO 26262 ASIL-D work products + assessment (if formal certification is pursued) | 1.5–4 Cr, program-level — the schematics carry the required hardware mechanisms, the paperwork is the cost |

## 3. What actually moves the number

1. **The hiitio module quote is 47 % of the BOM.** Baseline assumes ₹18k (~$215) per
   HCS600FH120D3C1 at 3,000 pcs. Chinese 600 A SiC EconoDUAL parts land $180–260 at this
   volume — get the written quote first. If you are forced onto Infineon FF6MR12W2M1H
   (~₹40–50k each at 1k), add **+₹70–95k/unit** and the inverter becomes a ₹2L product.
2. **DC-link film caps**: BOM carries ₹950/can; distributor reality for 40 µF/1100 V
   (B32776/MKP1848C) is ₹1,300–2,100 @1k → the high column. A China film-cap house
   (Faratronic/Jianghai) at volume gets back near baseline.
3. **CNC vs cast housing**: at exactly 1k units billet CNC costs ~₹4k/unit more than
   casting+machining; casting pays for itself after ~250 units. Keep CNC for the first
   hundreds, tool the casting when the schedule firms.

## 4. Pricing guidance (if "price" = selling price)

Low-volume 200–300 kW/800 V SiC inverters (Cascadia CM350-class) sell at $4,000–7,000.
At ₹1.15L cost, ex-works pricing at **₹1.75–1.95L (~$2,100–2,350, 35–40 % GM)** undercuts
every imported option by ~2× while funding the NRE inside the first 1,000 units:

- Revenue @1k ≈ ₹17.5–19.5 Cr · COGS ≈ ₹11.5 Cr · gross ≈ ₹6–8 Cr vs ≈ ₹1.2 Cr hardware NRE.

Excluded throughout: motor/resolver, coolant pump/loop (vehicle-side), packaging & freight,
duties, software beyond the safety concept, and the formal ISO 26262 certification program.
