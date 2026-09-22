# GEN-1 platform — one set of boards, four SKUs (rev A.6)

**Short answer to "can one board do SiC/IGBT and 4XX/8XX?" — yes.** The power PCB, control
card, discharge PCB and cap-bank busbar are the same for every SKU. A SKU is a module choice,
a film-can choice, a handful of resistor/capacitor values, an identity resistor and a firmware
parameter set. The one thing this hardware cannot do economically is **220 kW at 400 V** —
that needs ≈560 A rms, about twice the current, and current (not voltage) is what costs
money. That case has a same-footprint upgrade path (§5); it is not a second design.

Sources: generated BOMs (`bom.md`, `bom-igbt.md`, `bom-igbt4.md`, `bom-sic4.md`), the per-SKU
blocks of [`verification-report.md`](verification-report.md), S3/S4/S9 of
[`simulation-report.md`](simulation-report.md), [`firmware-contract.md`](firmware-contract.md),
[`cost-rollup.md`](cost-rollup.md).

## 1. Why one board works

- **Cap bank and discharge were already separate bolt-on assemblies.** 8XX and 4XX differ in
  exactly those two places — the 16 film cans (same 37.5 mm positions, same busbar drawing)
  and four discharge-resistor values (same discharge PCB).
- **SiC and IGBT modules share the D3 outline and the 11-pin map** (checked on both
  datasheets). The gate driver, DESAT diodes, bias supply and sensing are common; the IGBT
  build changes five values on existing pads.
- **No 650/750 V module exists in this outline** (hiitio catalogue scan, rev A.6), so 4XX uses
  the same 1200 V silicon. At a 500 V maximum that is a large voltage margin, one module
  qualification, and one volume price — worth more than the ≈0.5 pt efficiency a 750 V part
  would give at 400 V.
- **The firmware knows what it is driving:** a 1-resistor SKU identity on harness pin 40, read
  by the MCU; a mismatch with the loaded parameter set blocks the gates (FW-01/FW-02).

| Common to all SKUs | Changes per SKU |
|---|---|
| Power PCB layout, D3 footprint, gate drivers, bias flybacks, V_DC/current sensing | Module MPN (HCS600 SiC / HCG600 IGBT) |
| Control card (S32K396 + FS26), harness, CAN, resolver | IGBT: DESAT 4.7 k / 82 pF, gate 1.0/1.0 Ω, flyback RT 8.2 k |
| Discharge PCB, cap-bank busbar drawing, enclosure/coldplate footprint | 4XX: 16 × 50 µF/600 V cans, discharge 4 × 220 Ω + 12 × 15 k |
| Firmware codebase, EOL rig, SMT program | Identity resistor RHWID; firmware parameter set (§2 of the contract) |

## 2. The four SKUs (verified numbers, 65 °C coolant)

| | **8XX SiC** | **8XX IGBT** | **4XX IGBT** | 4XX SiC |
|---|---|---|---|---|
| Module ×3 | HCS600FH120D3C1 | HCG600FH120D3E1EA | HCG600FH120D3E1EA | HCS600FH120D3C1 |
| DC bus | 500–850 V | 500–850 V | 250–500 V | 250–500 V |
| f_sw | 8–10 kHz | 5 kHz | 5 kHz | 8–10 kHz |
| Phase current pk (30 s) / cont | 340 / 185 A rms | 340 / 185 A rms | 400 / 250 A rms | 400 / 250 A rms |
| Peak / continuous power | **220 / 120 kW** from 654 V | **220 / 120 kW** from 654 V | **150 / 90 kW** from 379 V | 150 / 90 kW from 379 V |
| …at the bottom of the range | 168 / 91 kW at 500 V | 168 / 91 kW at 500 V | 99 / 62 kW at 250 V | 99 / 62 kW at 250 V |
| Tj end of 30 s peak at V_max (S4) | 122 °C (175 °C max) | 127 °C (150 °C Tvjop) | 121 °C (150 °C) | 130 °C (175 °C) |
| Semiconductor efficiency, continuous point | 99.0 % | 98.6 % | 98.1 % | 98.3 % |
| Short-circuit protection | 3.1 µs reaction; SiC tSC unpublished → vendor letter | 4.5 µs vs 6 µs rating (82 pF) | same as 8XX IGBT | as 8XX SiC |
| Electronics BOM @1k | **₹70,965** | **₹45,465** | **₹45,465** | ₹70,965 |
| Ex-works cost (cost-rollup method) | ≈ ₹1.15 L | ≈ ₹0.90 L | ≈ ₹0.92 L (heavier DC busbar/connector) | ≈ ₹1.17 L |
| Cost per peak kW | ≈ ₹525/kW | **≈ ₹410/kW** | ≈ ₹610/kW | ≈ ₹780/kW |
| Status | **launch** | **launch** | **launch** | on request |

Notes. Peak power needs the voltage shown — below it firmware derates P(V_dc) (R-F09). The
4XX DC current is ≈400 A peak (vs ≈345 A for 8XX), so its busbar and HV connector are rated
up; the PCBs are not affected. The 4XX can (50 µF/600 V, ≥18 A) is a class part until the
Faratronic RFQ returns the MPN; its planning price equals the 8XX can.

## 3. Recommendation — the business-optimal path

1. **Sell voltage, not current.** The same hardware gives 220 kW at 800 V and 150 kW at 400 V,
   so the 8XX SKUs have the best ₹/kW. Lead with 8XX; offer 4XX where the customer's pack
   dictates it, priced per kW.
2. **Make the IGBT the volume SKU at both voltages** (₹25.5k/unit cheaper than SiC):
   800 V commercial vehicles, buses and cost-led passenger programs → **8XX IGBT**; 400 V
   passenger/LCV → **4XX IGBT**. It is also the robust choice at 850 V: its 200–385 ns fall
   time makes turn-off overshoot a non-issue, and its short-circuit rating is published.
3. **Keep SiC as the premium 8XX SKU** for range-/efficiency-led 800 V cars and quiet
   8–10 kHz switching (+0.45 pt at the continuous point, more on a drive cycle where SiC's
   missing knee voltage pays). Price it at ₹1.75–1.95 L against ₹1.35–1.55 L for IGBT
   (35–40 % gross margin on the rollup costs).
4. **Do not catalogue 4XX SiC.** 1200 V SiC at 400 A and 400 V has the worst ₹/kW; the
   hardware supports it on request for an efficiency-led 400 V customer.
5. **Bring-up order that spends the least money on risk:** EVT-1 on the **8XX IGBT** build
   (₹9.5k modules to risk during double-pulse and short-circuit tests; it validates every
   common block), with one SiC set dedicated to the 850 V double-pulse test that fixes RG_OFF.
   4XX follows as a BOM + parameter-set change once its cans arrive.
6. **Do not design a second power board** until a customer commits to more than ≈150 kW at
   400 V or more than 220 kW at 800 V — §5 covers both on the same PCB.

## 4. What stays open per SKU

| | 8XX SiC | 8XX IGBT | 4XX IGBT |
|---|---|---|---|
| Vendor data | module Ls, SC letter | — (SC published) | 4XX can MPN |
| Bench | DPT at 850 V cold/hot sets RG_OFF (3.3–10 Ω); contained SC test | min-blank vs turn-on tail at 82 pF; contained SC test; bias bank at worst parts (72 %) | as 8XX IGBT at 500 V; can sharing/thermal |
| Firmware | parameter set, §6 safe-state matrix with the motor's n_x | same | same, 530 V OV trip |

## 5. Upgrade path — same PCBs, when a customer pays for it

The catalogue scan found two more modules with the **same outline and pin map**:
**HCG900FH120D3RC** (1200 V/900 A IGBT, SC 3550 A for tP ≤ 8 µs at 600 V/150 °C — published)
and **HCS900FH120D3C1** (1200 V/900 A SiC, 2.8 mΩ at 175 °C). With them the platform reaches
≈220 kW at 400 V (≈560 A rms) or well above 220 kW at 800 V. What must change with them is
outside the PCBs: phase-current sensors with a larger range than ±900 A (the platform's
present ceiling is ≈480 A rms), a higher-ripple cap bank (≈360 A worst case at 560 A rms),
heavier DC/phase copper, and the bias-bank check for the larger gate charge. Get the module
quotes and extract their datasheets before committing.

Evaluated and not needed: the HCH900FH120D3ME7 SiC/Si hybrid (same pin map, but no published
short-circuit rating and no SKU the two technologies above do not already cover). Its
datasheet stays in `docs/datasheets/` as evidence.

## 6. Build commands

`npm run bom` (8XX SiC) · `npm run bom:igbt` (8XX IGBT) · `npm run bom:igbt4` (4XX IGBT) ·
`npm run bom:sic4` (4XX SiC) · `npm run bom:all`. Every BOM fails if any printed value
disagrees with its MPN.
