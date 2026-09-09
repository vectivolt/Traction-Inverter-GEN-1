# GEN-1 silicon variants — SiC vs IGBT, end to end (rev A.5)

One platform, two silicon builds. **Identical boards, identical layout, identical pin maps**
(both modules share the D3 EconoDUAL-3 outline and 11-pin assignment); the delta is one
module MPN, two DESAT values on existing pads, and firmware constants. Everything below is
generated/verified state — sources: `bom.md` / `bom-igbt.md`, `verification-report.md`
("IGBT variant" block), `simulation-report.md` (S1b, S4, S9), `cost-rollup.md`.

## 1. Choosing at a glance

| You are building… | Pick | Why |
|---|---|---|
| 800 V pack, efficiency/range-led, premium | **SiC** | +1.1 pt efficiency @120 kW, 8–10 kHz (raisable to 16–20 kHz = inaudible), lowest losses |
| <500 V pack and/or cost-led | **IGBT** | −₹25.5k/unit BOM; power below 500 V is current-limited anyway, so the silicon efficiency gap matters less at the derated operating points |
| One SMT line, both SKUs | both | same feeders; two-reel changeover + module pick (`dfm.md` §2) |

## 2. Ratings & performance

| Parameter | SiC build | IGBT build |
|---|---|---|
| Module ×3 | HCS600FH120D3C1 (1200 V/600 A SiC) | HCG600FH120D3E1EA (1200 V/600 A IGBT) |
| DC-link voltage | 500–850 V | 500–850 V |
| Peak / continuous power | 220 kW/30 s · 120 kW | **220 kW/30 s · 120 kW** (verified — no derate needed at 6 kHz) |
| Switching frequency | 8–10 kHz SVPWM (16–20 kHz option, inaudible) | 4–6 kHz SVPWM (audible tone band — spread-spectrum PWM recommended) |
| Efficiency @120 kW | ≈98.4 % | ≈97.3 % (~1.5 kW extra silicon loss) |
| Per-switch loss, 120 kW cont | ≈121 W | ≈248 W |
| Tj, continuous (verify row) | ≈84 °C | ≈97 °C (vs 150 °C Tvjop) |
| Tj, end of 220 kW/30 s (S4 transient sim) | **89 °C** | **110 °C** |
| Dead time | 1 µs | 2.5 µs |
| Conduction drop @600 A hot | R_DS(on) 5.7 mΩ ⇒ 3.4 V | V_CE(sat) 1.82 V (lower at high current — IGBT wins above ~320 A) |
| Short-circuit protection (S9 sim) | DESAT trip 8.1 V, cleared 2.4 µs/5.6 J (3 µs class — vendor tSC letter is the gate) | DESAT trip 5.75 V, cleared 4.3 µs/6.3 J vs datasheet 1800 A/10 µs |
| Gate-power demand (S1/S1b) | 1.5 W/bank @10 kHz | 1.63 W/bank @6 kHz (same 3.87 W transformer capacity) |
| NTC | B25/85 ≈ 3435 | B25/50 = 3375 (firmware constant) |

Both builds share unchanged: gate rails +15.6/−5.1 V (IGBT legal: V_GE ±20 V abs, V_GE(th)
5.0–6.2 V + Miller clamp), drivers NSI6611ASC-Q1SWR, cap bank, discharge, sensing, safety
chain, harnesses, all four PCBs/assemblies, and the <500 V derate table (power ∝ V_bus at
the 340 A current limit — applies identically to both).

## 3. The complete hardware delta (three BOM lines)

| Designators | SiC build | IGBT build | Why |
|---|---|---|---|
| MODU/MODV/MODW | HCS600FH120D3C1 | HCG600FH120D3E1EA | same pads, same pins (DS p.8) |
| R⟨U,V,W⟩⟨H,L⟩DS | 100 Ω (trip 8.1 V) | 4.7 kΩ (trip 5.75 V) | DESAT scaled to V_CE(sat) 1.82 V hot |
| C⟨U,V,W⟩⟨H,L⟩BL | 47 pF (~0.9 µs) | 150 pF (~2.8 µs) | IGBT turn-on tail vs 10 µs SC class |

Generate: `npm run bom` (SiC) · `npm run bom:igbt` (IGBT).

## 4. Cost & pricing (₹ @1k, planning ±25 %)

| | SiC | IGBT |
|---|---|---|
| Modules (3×) | 54,000 | 28,500 |
| Electronics BOM total | **70,934** | **45,434** |
| Ex-works unit (full rollup, `cost-rollup.md`) | ≈ **1.15 L** (₹525/kW) | ≈ **0.90 L** (₹410/kW) |
| Suggested ex-works price (35–40 % GM) | 1.75–1.95 L | 1.35–1.55 L |
| Both quotes ride | one hiitio RFQ (relationship in place) | |

## 5. Variant-specific open items

- **IGBT**: bench dv/dt shoot-through check at −5.1 V off-bias; FWD (diode) recovery share in
  the thermal test; acoustic plan at 4–6 kHz (spread-spectrum PWM).
- **SiC**: vendor tSC letter (datasheet publishes none); optional 16–20 kHz quiet mode
  re-check of gate-power margin (S1 already covers 10 kHz; 20 kHz ≈ 2.6 W/bank — fits).
- Shared gates unchanged: S32K396 package binding, MGJ2 cert, measured validation list.
