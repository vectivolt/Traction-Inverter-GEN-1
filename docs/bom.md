# Traction Inverter — BOM (rev A.1, generated 2026-09-09)

220 kW pk / 800 V SiC traction inverter — Power board + Control card.
Generated from the built netlists by `calculations/bom-gen.mjs`; the sheets, the BOM and the
LCSC fields resolve parts through the same parts-db, so they cannot disagree.
Prices are INR planning figures at ~1k-inverter aggregate (RFQ ±30 %); hiitio module and
LEM sensor prices are quote-gated — figures below are the planning assumptions.
`CLASS` = buy to the rating printed on the sheet; `ALT` = footprint-compatible second source.

## power — 310 components, 83 BOM lines, ≈ ₹64,001 @1k

CSV: [`docs/bom-power.csv`](bom-power.csv). Top cost lines:

| Qty | MPN | Description | ₹ ext @1k | Alt |
|---|---|---|---|---|
| 3 | HCS600FH120D3C1 | SiC MOSFET half-bridge module 1200 V 600 A, EconoDUAL 3 foot | 54,000 | Infineon FF6MR12W2M1H_B11 / Starpower GD |
| 16 | C3D1M206KFSA382 | DC-link film 20 uF 1100 V p37.5 | 4,800 | TDK B32778G0406K000 40 uF 4-lead (1 per  |
| 6 | VGT12EEM-200S1A4 | gate-drive flyback transformer EE, AEC-Q200, 2.6 kVrms — Dig | 2,100 | Wurth 750318131 class / CN custom-wound  |
| 3 | FILM-1uF-1200V | 1 uF 1200 V film snubber at module DC terminals | 540 | B32774D0505K000 5 uF C3809992 (retune) / |
| 6 | NSI6611A-Q1 | iso gate driver 10 A, DESAT/Miller clamp/UVLO/RDY/FLT, SOIC- | 510 | NSI6602B / UCC21750-Q1 (map differs) |
| 1 | HCM75S12T4K3 | SiC MOSFET 1200 V 75 mR TO-247-4L | 350 | any 1200 V >=5 A SiC/Si FET, TO-247 |
| 2 | AMC1311BDWVR | iso voltage amp 0-2 V in, reinforced | 230 | NSI1311 class |
| 2 | QA01C-18 | iso 15->18 V 1 W module >=6 kVDC | 190 | B1518S-3WR3HD / YLPTEC QA01C-18 C5369865 |
| 2 | ISO5V-RFC-6K | iso 15->5 V >=1 W REINFORCED >=5 kVrms | 190 | RECOM RxxP-R certified eq |
| 5 | STUD-M8 | HV DC entry M8 stud | 140 | M10 |
| 1 | TPS55340QRTERQ1 | boost 12->15.0 V | 140 | LM5155-Q1 / commercial TPS55340RTER C169 |
| 1 | MICROFIT3-40 | 40-way Micro-Fit 3.0 harness to control card | 120 | TE MATE-N-LOK eq |

## control-card — 219 components, 81 BOM lines, ≈ ₹5,808 @1k

CSV: [`docs/bom-control-card.csv`](bom-control-card.csv). Top cost lines:

| Qty | MPN | Description | ₹ ext @1k | Alt |
|---|---|---|---|---|
| 3 | HC5FW900-S | open-loop hall 900 A busbar transducer, 5 V ratiometric | 2,100 | HAH1BVW S/08 900 A class (LEM) |
| 1 | S32K396 | lockstep M7 motor-control MCU | 1,400 | S32K388 (same family) |
| 1 | TE 776231-1 (AMPSEAL 23) | 23-pos AMPSEAL sealed header, flange mount | 780 | Aptiv GT 280 sealed family (Mouser) |
| 1 | FS2633D | ASIL-D SBC: VPRE 5.4V buck / VCORE 1.5V buck / VREF 5V / LDO | 480 | FS2630 variants |
| 8 | OPA376AQDBVRQ1 | VDC differential receiver | 224 | OPA320-Q1 |
| 1 | ALM2402QPWPRQ1 | dual power op-amp resolver driver, 8 Vpp H-bridge out | 180 | ALM2403QPWPRQ1 (DigiKey ships-today) |
| 1 | MICROFIT3-40 | 40-way Micro-Fit 3.0 harness | 120 | TE eq |
| 2 | TCAN1042HGVDRQ1 | CAN-FD 5 Mbps transceiver, VIO, AEC-Q100 — automotive grade  | 110 | TCAN1042DRQ1 C118837 (loses 70 V fault t |
| 3 | OPA348AQDBVRQ1 | VMID buffers | 75 | OPA365-Q1 / commercial OPA348AIDBVR C363 |
| 2 | OPA333AQDBVRQ1 | zero-drift buffer, motor temp | 70 | OPA388-Q1 |
| 1 | CX3225GA40000D0PTVCC | 40 MHz crystal | 45 | NX3225GA-40M / SOSET 3225 40 MHz C538031 |
| 1 | T2M-105-01-L-D-TH-WT | 2x5 2 mm hall-sensor harness header, latching | 45 | generic 2.0 mm 2x5 C64617 (no latch — de |

## BOM contribution by subsystem (both boards, ₹ @1k)

Where the money actually goes — cumulative share shows the Pareto: the first two rows are
~87 % of the electronics BOM, so those are the only two lines worth an RFQ fight.

| Subsystem | ₹ | share | cumulative | parts |
|---|---|---|---|---|
| SiC power modules | 54,000 | 77.4% | 77.4% | 3 |
| DC-link film caps | 4,800 | 6.9% | 84.2% | 16 |
| Hall sensors + AFE | 2,321 | 3.3% | 87.6% | 34 |
| Gate-power flybacks | 2,226 | 3.2% | 90.7% | 61 |
| MCU + clock + debug | 1,465 | 2.1% | 92.8% | 22 |
| Vehicle connector + prot | 785 | 1.1% | 94.0% | 6 |
| Gate drivers + networks | 656 | 0.9% | 94.9% | 120 |
| Discharge (active+passive) | 630 | 0.9% | 95.8% | 21 |
| Module snubbers | 540 | 0.8% | 96.6% | 3 |
| FS26 SBC + LV input + wake | 528 | 0.8% | 97.3% | 27 |
| VDC iso sensing + bias | 340 | 0.5% | 97.8% | 21 |
| Resolver AFE | 269 | 0.4% | 98.2% | 48 |
| Harness + pulldowns | 246 | 0.4% | 98.6% | 17 |
| LV power (prot+LDO+boost) | 238 | 0.3% | 98.9% | 24 |
| HV entry/Y-caps/HVIL/studs | 208 | 0.3% | 99.2% | 14 |
| ASC buffer | 139 | 0.2% | 99.4% | 7 |
| CAN-FD x2 | 132 | 0.2% | 99.6% | 14 |
| misc | 102 | 0.1% | 99.7% | 5 |
| Temps (module/board/motor) | 82 | 0.1% | 99.9% | 24 |
| VDC receivers (card) | 61 | 0.1% | 99.9% | 12 |
| Safety chain (EN/ASC/ILK) | 37 | 0.1% | 100.0% | 21 |
| Module NTC routing | 4 | 0.0% | 100.0% | 9 |

## Cost by category (both boards, ₹ @1k)

| Category | ₹ | share |
|---|---|---|
| power semiconductors | 55,170 | 79.0% |
| drive + control ICs | 5,397 | 7.7% |
| capacitors | 4,867 | 7.0% |
| magnetics | 2,137 | 3.1% |
| connectors + sensors | 1,182 | 1.7% |
| isolation | 421 | 0.6% |
| misc | 333 | 0.5% |
| resistors | 159 | 0.2% |
| protection + diodes | 144 | 0.2% |
| **TOTAL (electronics, ex-PCB/mech/busbar/coldplate)** | **69,809** | 100% |

The three HCS600FH120D3C1 modules dominate (as they should at this power class); every
other line is distributor-standard. Swapping the module vendor swaps one BOM line.
