# Traction Inverter — 4XX · IGBT (HCG600FH120D3E1EA), 250–500 V bus — BOM (rev A.12, generated 2026-09-24)

220 kW pk / 800 V SiC traction inverter — Power board + Cap-bank busbar + bolt-on Discharge board + Control card.
Generated from the built netlists by `calculations/bom-gen.mjs`; the sheets, the BOM and the
LCSC fields resolve parts through the same parts-db, so they cannot disagree.
Prices are INR planning figures at ~1k-inverter aggregate (RFQ ±30 %); hiitio module and
LEM sensor prices are quote-gated — figures below are the planning assumptions.
`CLASS` = buy to the rating printed on the sheet; `ALT` = footprint-compatible second source.
Same PCBs for every SKU — this BOM differs from the others only in the rows listed in `parts-db.mjs` SKUS.igbt4.
All SKUs: [8XX SiC](bom.md) · [8XX IGBT](bom-igbt.md) · [4XX IGBT](bom-igbt4.md) · [4XX SiC](bom-sic4.md) — comparison in [`variants.md`](variants.md).

## power — 322 components, 97 BOM lines, ≈ ₹34,387 @1k

CSV: [`docs/bom-power-igbt4.csv`](bom-power-igbt4.csv). Top cost lines:

| Qty | MPN | Description | ₹ ext @1k | Alt |
|---|---|---|---|---|
| 3 | HCG600FH120D3E1EA | IGBT half-bridge module 1200 V 600 A, SAME D3 EconoDUAL 3 fo | 28,500 | - |
| 6 | VGT12EEM-200S1A4 | gate-drive flyback transformer EE, AEC-Q200, 2.6 kVrms — Dig | 2,100 | Wurth 750318131 class / CN custom-wound  |
| 2 | UCC12050DVER | iso 5 V -> 5.0 V 500 mW DC/DC, SOIC-16W | 720 | PRODUCTION: UCC12051QDVERQ1 (AEC-Q100 G1 |
| 3 | FILM-1uF-1200V | 1 uF 1200 V film snubber at module DC terminals | 540 | B32774D0505K000 5 uF C3809992 (retune) / |
| 6 | NSI6611ASC-Q1SWR | iso gate driver 10 A | 510 | NSI6602B / UCC21750-Q1 (map differs) |
| 1 | UCC14141QDWNRQ1 | reinforced isolated bias, 8-18 V in, single-output configura | 450 | UCC14240QDWNRQ1 (24 V-in bin, same pins) |
| 1 | IPL1-120-01-L-D-K | 40-way | 245 | TE AMPMODU Mod II 6-102618-8 header / 3- |
| 2 | AMC1311BDWVR | iso voltage amp 0-2 V in, reinforced | 230 | NSI1311 class |
| 1 | VOW3120-X017T | opto buffer: latched ASC_CMD -> LS driver ASC pins | 190 | Broadcom HCNW3120-500E (widebody, V_IORM |
| 5 | STUD-M8 | HV DC entry M8 stud | 140 | M10 |
| 1 | TPS55340QRTERQ1 | boost 12->15.0 V | 140 | LM5155-Q1 (controller — a redesign, not  |
| 3 | NCV4276CDT50RKG | 5 V LDO from V15 for one UCC12050 | 96 | NCV4949 class |

## capbank — 26 components, 4 BOM lines, ≈ ₹3,340 @1k

CSV: [`docs/bom-capbank-igbt4.csv`](bom-capbank-igbt4.csv). Top cost lines:

| Qty | MPN | Description | ₹ ext @1k | Alt |
|---|---|---|---|---|
| 16 | C3D1U506KFAA382 | DC-link film 50 uF 600 V | 3,200 | Vishay MKP1848C65060JP5 (verified, talle |
| 2 | STUD-M8 | cap-bank busbar HV entry lug | 56 | M10 |
| 6 | TAB-M6 | cap-bank busbar DC tab onto EconoDUAL module terminal | 48 | per busbar drawing |
| 2 | STUD-M6 | cap-bank busbar stud for the bolt-on discharge board | 36 | M8 |

## discharge — 35 components, 17 BOM lines, ≈ ₹1,188 @1k

CSV: [`docs/bom-discharge-igbt4.csv`](bom-discharge-igbt4.csv). Top cost lines:

| Qty | MPN | Description | ₹ ext @1k | Alt |
|---|---|---|---|---|
| 1 | UCC14141QDWNRQ1 | reinforced isolated bias, 8-18 V in, single-output configura | 450 | UCC14240QDWNRQ1 (24 V-in bin, same pins) |
| 1 | HCM75S12T4K3 | SiC MOSFET 1200 V 75 mR TO-247-4L | 350 | 1200 V >=5 A SiC FET in TO-247-4L with t |
| 1 | VOW3120-X017T | opto gate driver | 190 | Broadcom HCNW3120-500E (widebody, V_IORM |
| 4 | SQP10-220RJB15 | 220 R 10 W axial ceramic wirewound, fail-open/flameproof cla | 112 | - |
| 2 | STUD-M6 | discharge-board bolt terminal onto the cap bank / busbar | 36 | M8 |
| 12 | R2512-15k-2W | 15 k 2512 2 W thick film | 36 | - |
| 1 | HDR-1x4-2.54 | discharge-board control link | 6 | JST-XH 4p |
| 2 | MLCC-10uF-25V-X7R | UCC14141-Q1 VIN bulk 2 x 10 uF | 3 | any X7R 25 V |
| 1 | MLCC-10uF-50V | UCC14141-Q1 COUT1 10 uF at VDD-VEE | 2 | any X7R 50 V |
| 2 | MLCC-100nF-50V | UCC14141-Q1 VIN high-frequency bypass, at pins 6/7-8 | 1 | any |
| 2 | R0603-10k | UCC14141-Q1 ENA divider top from V15 | 1 | any |
| 1 | R0603-62k-1% | UCC14141-Q1 FBVDD divider top: 2.5 V x | 0 | any 1 % |

## control-card — 288 components, 96 BOM lines, ≈ ₹6,167 @1k

CSV: [`docs/bom-control-card-igbt4.csv`](bom-control-card-igbt4.csv). Top cost lines:

| Qty | MPN | Description | ₹ ext @1k | Alt |
|---|---|---|---|---|
| 3 | HC5FW900-S | open-loop hall 900 A busbar transducer, 5 V ratiometric | 2,100 | HAH1BVW S/08 900 A class (LEM) |
| 1 | S32K396EHT1MJBST | lockstep M7 motor-control MCU | 1,400 | S32K388 (same family, different ball map |
| 1 | TE 770669-1 (AMPSEAL 23) | 23-pos AMPSEAL PCB header, right-angle shrouded | 780 | 1-770669-x plating variants / Aptiv GT 2 |
| 1 | FS2633D | ASIL-D SBC: VPRE 5.4V buck / VCORE 1.5V buck / VREF 5V / LDO | 480 | FS2630 variants |
| 9 | OPA376AQDBVRQ1 | VDC differential receiver | 252 | OPA320-Q1 |
| 1 | IPL1-120-01-L-D-K | 40-way | 245 | TE AMPMODU Mod II 6-102618-8 / 2-826634- |
| 1 | ALM2402QPWPRQ1 | dual power op-amp resolver driver, 8 Vpp H-bridge out | 180 | ALM2403QPWPRQ1 (DigiKey ships-today) |
| 2 | TCAN1042HGVDRQ1 | CAN-FD 5 Mbps transceiver, VIO, AEC-Q100 — automotive grade  | 110 | TCAN1042DRQ1 C118837 (loses 70 V fault t |
| 3 | OPA348AQDBVRQ1 | VMID buffers | 75 | OPA365-Q1 / commercial OPA348AIDBVR C363 |
| 2 | OPA333AQDBVRQ1 | zero-drift buffer, motor temp | 70 | OPA388-Q1 |
| 1 | CX3225GA40000D0PTVCC | 40 MHz crystal | 45 | NX3225GA-40M / SOSET 3225 40 MHz C538031 |
| 1 | FTSH-105-01-L-DV-K | 10-pin 1.27 mm ARM Cortex debug header | 45 | Harwin M50-3500542 / CNC Tech 3220-10-03 |

## BOM contribution by subsystem (all boards, ₹ @1k)

Where the money actually goes — cumulative share shows the Pareto: the first two rows are
~87 % of the electronics BOM, so those are the only two lines worth an RFQ fight.

| Subsystem | ₹ | share | cumulative | parts |
|---|---|---|---|---|
| SiC power modules | 28,500 | 63.2% | 63.2% | 3 |
| DC-link film caps | 3,200 | 7.1% | 70.3% | 16 |
| Hall sensors + AFE | 2,322 | 5.2% | 75.5% | 38 |
| Gate-power flybacks | 2,252 | 5.0% | 80.5% | 77 |
| MCU + clock + debug | 1,494 | 3.3% | 83.8% | 22 |
| Discharge (active+passive) | 1,146 | 2.5% | 86.3% | 33 |
| VDC iso sensing + bias | 1,036 | 2.3% | 88.6% | 30 |
| Vehicle connector + prot | 785 | 1.7% | 90.4% | 6 |
| Gate drivers + networks | 659 | 1.5% | 91.8% | 129 |
| ASC buffer | 650 | 1.4% | 93.3% | 18 |
| FS26 SBC + LV input + wake | 608 | 1.3% | 94.6% | 38 |
| Module snubbers | 540 | 1.2% | 95.8% | 3 |
| Harness + pulldowns | 495 | 1.1% | 96.9% | 17 |
| LV power (prot+LDO+boost) | 274 | 0.6% | 97.5% | 28 |
| misc | 273 | 0.6% | 98.1% | 44 |
| Resolver AFE | 273 | 0.6% | 98.7% | 53 |
| HV entry/Y-caps/HVIL/studs | 208 | 0.5% | 99.2% | 14 |
| CAN-FD x2 | 132 | 0.3% | 99.5% | 14 |
| Safety chain (EN/ASC/ILK) | 88 | 0.2% | 99.7% | 43 |
| Temps (module/board/motor) | 82 | 0.2% | 99.9% | 24 |
| VDC receivers (card) | 61 | 0.1% | 100.0% | 12 |
| Module NTC routing | 4 | 0.0% | 100.0% | 9 |

## Cost by category (all boards, ₹ @1k)

| Category | ₹ | share |
|---|---|---|
| power semiconductors | 30,242 | 67.1% |
| drive + control ICs | 10,238 | 22.7% |
| magnetics | 2,183 | 4.8% |
| connectors + sensors | 1,282 | 2.8% |
| misc | 437 | 1.0% |
| capacitors | 292 | 0.6% |
| isolation | 231 | 0.5% |
| protection + diodes | 149 | 0.3% |
| resistors | 30 | 0.1% |
| **TOTAL (electronics, ex-PCB/mech/busbar/coldplate)** | **45,082** | 100% |

The three HCS600FH120D3C1 modules dominate (as they should at this power class); every
other line is distributor-standard. Swapping the module vendor swaps one BOM line.
