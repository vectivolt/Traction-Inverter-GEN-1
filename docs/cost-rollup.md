# Traction Inverter — Full unit cost @1,000 units (rev A.5)

Complete ex-works build cost per inverter at a 1,000-unit run (India assembly, India+China
supply chain), on top of the generated electronics BOM (`docs/bom.md`, **₹70,934 @1k** across
the four assemblies). Figures are planning numbers (RFQ ±25 %); the two lines that move the
answer are flagged in §3.

## 1. Per-unit rollup (₹ @1k)

| # | Block | Low | Baseline | High | Notes |
|---|---|---|---|---|---|
| 1 | Electronics BOM (power + cap bank + discharge + card) | 65,000 | 70,934 | 95,000 | `bom.md`; low/high = module + film-cap quote swing (§3) |
| 2 | PCBs (power 6L 2–3 oz ~0.12 m² + card 6L + discharge 2L) | 4,500 | 5,300 | 6,100 | India/China fab @1k |
| 3 | PCBA (SMT ~750 placements + TH + AOI, 3 boards) | 2,500 | 3,300 | 4,100 | discharge board panelized 4-up |
| 4 | Liquid coldplate (FSW/gun-drilled Al, 3× EconoDUAL footprint, fittings) | 4,500 | 6,000 | 8,000 | ~360×160 mm, ≤0.05 K/W per switch to coolant (the S4 thermal-sim assumption — verify at thermal test) |
| 5 | Busbars (laminated DC bus + cap-bank assembly labor + 3 phase bars, plated) | 5,000 | 6,200 | 8,200 | the cap-bank sheet (2 of 4) is the busbar vendor's electrical drawing |
| 6 | Housing — **CNC** Al 6061 body + lid, seals, hard anodize | 6,500 | 8,500 | 11,000 | billet CNC as requested; gravity-cast + CNC faces ≈ ₹4–5.5k/unit + ₹4–7 L tooling (breaks even ~250 units) |
| 7 | HV connectors (DC-in 2P 300 A + HVIL; 3-phase out 3P 400 A) + coolant QCs | 4,000 | 5,200 | 6,500 | RADSOK-class; stud-through-gland saves ~₹2.5k if acceptable |
| 8 | Seals, Gore vent, EMC gasket, TIM (3 modules), fasteners, internal harnesses | 2,500 | 3,300 | 4,100 | incl. the 4-way discharge link + LEM harness |
| 9 | Assembly + EOL test (hi-pot, discharge-time, spin/power on B2B rig) + conformal coat | 2,500 | 3,200 | 4,000 | incl. rig amortization over 1k |
| 10 | Yield / scrap / warranty reserve (3 %) | 2,900 | 3,400 | 4,400 | |
| | **Ex-works cost per unit** | **≈ 99,900** | **≈ 115,300** | **≈ 151,400** | |

**Baseline: ≈ ₹1.15 lakh / unit ≈ $1,390 ≈ ₹525/kW ($6.3/kW).**
1,000 units ⇒ **≈ ₹11.5 Cr** parts + build (range ₹10.0–15.1 Cr).

### IGBT cost SKU (same boards — full comparison in [`variants.md`](variants.md))

Swapping to the HCG600FH120D3E1EA IGBT variant (₹9.5k/module planning vs ₹18k SiC) drops
line 1 to **₹45,434** and the ex-works baseline to **≈ ₹90k/unit (≈ ₹410/kW)** — at
~1.1 pt efficiency and 4–6 kHz switching. Everything else in the rollup is unchanged; the
natural pairing is the <500 V-pack derated SKU. Both quotes ride the same hiitio RFQ.

## 2. One-time NRE (not in the per-unit number)

| Item | ₹ |
|---|---|
| Coldplate + busbar + housing tooling/fixtures | 8–14 L |
| EOL back-to-back test rig (220 kW) | 15–25 L |
| DVT: CISPR 25 EMC + ISO 16750 environmental + thermal/durability | 35–70 L |
| **Hardware NRE subtotal** | **≈ 60–110 L → ₹6–11k/unit amortized over 1k** |
| ISO 26262 ASIL-D work products + assessment (if formal certification is pursued) | 1.5–4 Cr, program-level — the schematics carry the required hardware mechanisms, the paperwork is the cost |

## 3. What actually moves the number

1. **The hiitio module quote is 76 % of the electronics BOM.** Baseline assumes ₹18k (~$215)
   per HCS600FH120D3C1 at 3,000 pcs (3 × 18k = ₹54k of the ₹70.9k BOM). Chinese 600 A SiC
   EconoDUAL parts land $180–260 at this volume — get the written quote first. If forced onto
   Infineon FF6MR12W2M1H (~₹40–50k each at 1k), add **+₹70–95k/unit** and the inverter
   becomes a ₹2 L product.
2. **DC-link film caps**: 16 × Faratronic 20 µF at ₹300/can baseline (₹4.8k). Distributor
   pricing for Western 4-lead 40 µF alternates runs 2–4× that — stay on the Faratronic/
   Jianghai volume channel to hold baseline.
3. **CNC vs cast housing**: at exactly 1k units billet CNC costs ~₹4k/unit more than
   casting+machining; casting pays for itself after ~250 units. Keep CNC for the first
   hundreds, tool the casting when the schedule firms.

## 4. Pricing guidance (if "price" = selling price)

Low-volume 200–300 kW/800 V SiC inverters (Cascadia CM350-class) sell at $4,000–7,000.
SiC SKU: at ₹1.15 L cost, ex-works **₹1.75–1.95 L** (35–40 % GM). IGBT SKU: at ≈₹0.90 L
cost, ex-works **₹1.35–1.55 L** at the same margin — it undercuts imported IGBT drives
(Curtis/Sevcon class tops out far below this power) while the SiC SKU undercuts
every imported option by ~2× while funding the NRE inside the first 1,000 units:

- Revenue @1k ≈ ₹17.5–19.5 Cr · COGS ≈ ₹11.5 Cr · gross ≈ ₹6–8 Cr vs ≈ ₹1.2 Cr hardware NRE.

Excluded throughout: motor/resolver, coolant pump/loop (vehicle-side), packaging & freight,
duties, software beyond the safety concept, and the formal ISO 26262 certification program.
