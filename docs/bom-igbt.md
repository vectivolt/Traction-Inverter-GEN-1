# Traction Inverter — IGBT VARIANT (HCG600FH120D3E1EA) — BOM (rev A.5, generated 2026-09-09)

220 kW pk / 800 V SiC traction inverter — Power board + Cap-bank busbar + bolt-on Discharge board + Control card.
Generated from the built netlists by `calculations/bom-gen.mjs`; the sheets, the BOM and the
LCSC fields resolve parts through the same parts-db, so they cannot disagree.
Prices are INR planning figures at ~1k-inverter aggregate (RFQ ±30 %); hiitio module and
LEM sensor prices are quote-gated — figures below are the planning assumptions.
`CLASS` = buy to the rating printed on the sheet; `ALT` = footprint-compatible second source.

## power — 292 components, 87 BOM lines, ≈ ₹33,891 @1k

CSV: [`docs/bom-power-igbt.csv`](bom-power-igbt.csv). Top cost lines:

| Qty | MPN | Description | ₹ ext @1k | Alt |
|---|---|---|---|---|
| 3 | HCG600FH120D3E1EA | IGBT half-bridge module 1200 V 600 A, SAME D3 EconoDUAL 3 fo | 28,500 | - |
| 6 | VGT12EEM-200S1A4 | gate-drive flyback transformer EE, AEC-Q200, 2.6 kVrms — Dig | 2,100 | Wurth 750318131 class / CN custom-wound  |
| 2 | MGJ2D150505SC | iso 15->+5/-5 V 2 W SIP-7 gate-drive-grade bias, +5 V used | 960 | NO basic-insulation substitutes; Mornsun |
| 3 | FILM-1uF-1200V | 1 uF 1200 V film snubber at module DC terminals | 540 | B32774D0505K000 5 uF C3809992 (retune) / |
| 6 | NSI6611ASC-Q1SWR | iso gate driver 10 A | 510 | NSI6602B / UCC21750-Q1 (map differs) |
| 2 | AMC1311BDWVR | iso voltage amp 0-2 V in, reinforced | 230 | NSI1311 class |
| 5 | STUD-M8 | HV DC entry M8 stud | 140 | M10 |
| 1 | TPS55340QRTERQ1 | boost 12->15.0 V | 140 | LM5155-Q1 / commercial TPS55340RTER C169 |
| 1 | MICROFIT3-40 | 40-way Micro-Fit 3.0 harness to control card | 120 | TE MATE-N-LOK eq |
| 1 | QA01C-18 | iso 15 V-in SiC-driver bias module, OUTPUTS +20/-4 V per DS | 95 | B1518S-3WR3HD / YLPTEC QA01C-18 C5369865 |
| 2 | BUK7Y14-80E | 80 V logic-level NFET LFPAK | 50 | SQJ850EP class |
| 2 | Y1-4.7nF-500VAC | Y1-class 4.7 nF disc | 44 | 2x Y2 in series / TDK CeraLink 1.5 kVdc |

## capbank — 26 components, 4 BOM lines, ≈ ₹4,940 @1k

CSV: [`docs/bom-capbank-igbt.csv`](bom-capbank-igbt.csv). Top cost lines:

| Qty | MPN | Description | ₹ ext @1k | Alt |
|---|---|---|---|---|
| 16 | C3D1M206KFSA382 | DC-link film 20 uF 1100 V p37.5 | 4,800 | TDK B32778G0406K000 40 uF 4-lead (1 per  |
| 2 | STUD-M8 | cap-bank busbar HV entry lug | 56 | M10 |
| 6 | TAB-M6 | cap-bank busbar DC tab onto EconoDUAL module terminal | 48 | per busbar drawing |
| 2 | STUD-M6 | cap-bank busbar stud for the bolt-on discharge board | 36 | M8 |

## discharge — 24 components, 11 BOM lines, ≈ ₹672 @1k

CSV: [`docs/bom-discharge-igbt.csv`](bom-discharge-igbt.csv). Top cost lines:

| Qty | MPN | Description | ₹ ext @1k | Alt |
|---|---|---|---|---|
| 1 | HCM75S12T4K3 | SiC MOSFET 1200 V 75 mR TO-247-4L | 350 | any 1200 V >=5 A SiC/Si FET, TO-247 |
| 4 | WW-470R-10W-AX | 470 R 10 W axial ceramic wirewound, >=100 J single pulse | 112 | TE SQP500JB / Vishay AC10 / RX27-1 |
| 1 | QA01C-18 | iso 15 V-in SiC-driver bias module >=6 kVDC, OUTPUTS +20/-4  | 95 | B1518S-3WR3HD / YLPTEC QA01C-18 C5369865 |
| 1 | TLP152 | opto gate driver | 42 | TLP2745 / EL3182 |
| 2 | STUD-M6 | discharge-board bolt terminal onto the cap bank / busbar | 36 | M8 |
| 10 | R2512-27k-2W | 27 k 2512 2 W standard thick-film | 30 | TE CRGP2512F68K C2073426 (101 pcs LCSC)  |
| 1 | HDR-1x4-2.54 | discharge-board control link | 6 | JST-XH 4p |
| 1 | R0603-470R | discharge opto LED series | 0 | any |
| 1 | R0603-47R | discharge gate resistor | 0 | any |
| 1 | R0603-10k | discharge gate pulldown to DCN | 0 | any |
| 1 | MLCC-100nF-25V | discharge bias decoupling | 0 | any |

## control-card — 250 components, 92 BOM lines, ≈ ₹5,931 @1k

CSV: [`docs/bom-control-card-igbt.csv`](bom-control-card-igbt.csv). Top cost lines:

| Qty | MPN | Description | ₹ ext @1k | Alt |
|---|---|---|---|---|
| 3 | HC5FW900-S | open-loop hall 900 A busbar transducer, 5 V ratiometric | 2,100 | HAH1BVW S/08 900 A class (LEM) |
| 1 | S32K396 | lockstep M7 motor-control MCU | 1,400 | S32K388 (same family) |
| 1 | TE 770669-1 (AMPSEAL 23) | 23-pos AMPSEAL PCB header, right-angle shrouded | 780 | 1-770669-x plating variants / Aptiv GT 2 |
| 1 | FS2633D | ASIL-D SBC: VPRE 5.4V buck / VCORE 1.5V buck / VREF 5V / LDO | 480 | FS2630 variants |
| 9 | OPA376AQDBVRQ1 | VDC differential receiver | 252 | OPA320-Q1 |
| 1 | ALM2402QPWPRQ1 | dual power op-amp resolver driver, 8 Vpp H-bridge out | 180 | ALM2403QPWPRQ1 (DigiKey ships-today) |
| 1 | MICROFIT3-40 | 40-way Micro-Fit 3.0 harness | 120 | TE eq |
| 2 | TCAN1042HGVDRQ1 | CAN-FD 5 Mbps transceiver, VIO, AEC-Q100 — automotive grade  | 110 | TCAN1042DRQ1 C118837 (loses 70 V fault t |
| 3 | OPA348AQDBVRQ1 | VMID buffers | 75 | OPA365-Q1 / commercial OPA348AIDBVR C363 |
| 2 | OPA333AQDBVRQ1 | zero-drift buffer, motor temp | 70 | OPA388-Q1 |
| 1 | CX3225GA40000D0PTVCC | 40 MHz crystal | 45 | NX3225GA-40M / SOSET 3225 40 MHz C538031 |
| 1 | T2M-105-01-L-D-TH-WT | 2x5 2 mm hall-sensor harness header, latching | 45 | generic 2.0 mm 2x5 C64617 (no latch — de |

## BOM contribution by subsystem (all boards, ₹ @1k)

Where the money actually goes — cumulative share shows the Pareto: the first two rows are
~87 % of the electronics BOM, so those are the only two lines worth an RFQ fight.

| Subsystem | ₹ | share | cumulative | parts |
|---|---|---|---|---|
| SiC power modules | 28,500 | 62.7% | 62.7% | 3 |
| DC-link film caps | 4,800 | 10.6% | 73.3% | 16 |
| Hall sensors + AFE | 2,321 | 5.1% | 78.4% | 35 |
| Gate-power flybacks | 2,227 | 4.9% | 83.3% | 66 |
| MCU + clock + debug | 1,465 | 3.2% | 86.5% | 22 |
| Vehicle connector + prot | 785 | 1.7% | 88.3% | 6 |
| misc | 754 | 1.7% | 89.9% | 45 |
| VDC iso sensing + bias | 725 | 1.6% | 91.5% | 21 |
| Gate drivers + networks | 658 | 1.4% | 93.0% | 126 |
| Discharge (active+passive) | 630 | 1.4% | 94.3% | 21 |
| FS26 SBC + LV input + wake | 575 | 1.3% | 95.6% | 31 |
| Module snubbers | 540 | 1.2% | 96.8% | 3 |
| Resolver AFE | 272 | 0.6% | 97.4% | 48 |
| LV power (prot+LDO+boost) | 266 | 0.6% | 98.0% | 28 |
| Harness + pulldowns | 245 | 0.5% | 98.5% | 17 |
| HV entry/Y-caps/HVIL/studs | 208 | 0.5% | 99.0% | 14 |
| ASC buffer | 139 | 0.3% | 99.3% | 7 |
| CAN-FD x2 | 132 | 0.3% | 99.6% | 14 |
| Temps (module/board/motor) | 82 | 0.2% | 99.8% | 24 |
| VDC receivers (card) | 61 | 0.1% | 99.9% | 12 |
| Safety chain (EN/ASC/ILK) | 45 | 0.1% | 100.0% | 24 |
| Module NTC routing | 4 | 0.0% | 100.0% | 9 |

## Cost by category (all boards, ₹ @1k)

| Category | ₹ | share |
|---|---|---|
| power semiconductors | 30,678 | 67.5% |
| drive + control ICs | 5,464 | 12.0% |
| capacitors | 4,873 | 10.7% |
| magnetics | 2,183 | 4.8% |
| connectors + sensors | 1,230 | 2.7% |
| misc | 456 | 1.0% |
| isolation | 231 | 0.5% |
| resistors | 162 | 0.4% |
| protection + diodes | 159 | 0.4% |
| **TOTAL (electronics, ex-PCB/mech/busbar/coldplate)** | **45,434** | 100% |

The three HCS600FH120D3C1 modules dominate (as they should at this power class); every
other line is distributor-standard. Swapping the module vendor swaps one BOM line.
