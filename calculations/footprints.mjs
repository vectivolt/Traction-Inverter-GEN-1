// footprints.mjs — the KiCad footprint binding (round 22, F210).
//
// Every parts-db footprint CODE (R0603, SMC, DWN36, MAPBGA289, …) resolves to exactly one footprint in the
// project library shipped with the schematic sets, kicad/traction/traction.pretty, and the symbol's Footprint
// field reads "traction:<name>". Standard land patterns are copied VERBATIM from the KiCad 10 footprint libraries
// by kicad-fp-gen.mjs (their names are kept; licence: CC-BY-SA 4.0 with the KiCad libraries exception, which
// allows unrestricted use in a design — the copy carries the attribution); the rest are drawn from the archived
// datasheets and documented in traction.pretty/MANIFEST.md. A code that resolves to nothing is a GENERATOR
// FAILURE (kicad5-gen refuses to write the sheet), so no symbol can reach the layout engineer without a pattern.
//
// Resolution order (footprintOf): rule.kfp (an explicit per-part choice) → KFP[rule.fp] → the size in the MPN
// class prefix (R2512-…, R0805-…) → the size the sheet DRAWS (pages.mjs reads it from circuit.json, the same
// pad extents bom-gen's drawn-size gate uses). "OffBoard" resolves to no footprint on purpose: the part is not
// on any of the four PCBs (the LEM sensors sit on the busbar; their nets reach the card through JLEM).

export const SIZE = { "2.45x0.95": "0603", "2.85x1.40": "0805", "4.05x1.75": "1206", "4.05x2.65": "1210", "7.15x3.35": "2512" };
export const OFF_BOARD = "OffBoard";
export const LIB = "traction";

const K = (lib, name) => ({ lib, name });      // copied from KiCad's <lib>.pretty/<name>.kicad_mod
const C = (name) => ({ lib: null, name });     // drawn from the datasheet: traction.pretty/<name>.kicad_mod (MANIFEST.md)

export const KFP = {
  // chip passives (the drawn-size gate in bom-gen keeps the code and the sheet's pad extents equal)
  R0603: K("Resistor_SMD", "R_0603_1608Metric"), R0805: K("Resistor_SMD", "R_0805_2012Metric"),
  R1206: K("Resistor_SMD", "R_1206_3216Metric"), R1210: K("Resistor_SMD", "R_1210_3225Metric"),
  R2512: K("Resistor_SMD", "R_2512_6332Metric"),
  C0603: K("Capacitor_SMD", "C_0603_1608Metric"), C0805: K("Capacitor_SMD", "C_0805_2012Metric"),
  C1206: K("Capacitor_SMD", "C_1206_3216Metric"), C1210: K("Capacitor_SMD", "C_1210_3225Metric"),
  L0805: K("Inductor_SMD", "L_0805_2012Metric"),
  FB0603: K("Inductor_SMD", "L_0603_1608Metric"), FB0805: K("Inductor_SMD", "L_0805_2012Metric"), FB1206: K("Inductor_SMD", "L_1206_3216Metric"),
  F0603: K("Fuse", "Fuse_0603_1608Metric"), PTC1812: K("Fuse", "Fuse_1812_4532Metric"), PTC2920: K("Fuse", "Fuse_2920_7451Metric"),
  "1812": K("Fuse", "Fuse_1812_4532Metric"),                    // FEXP/FEXN MF-MSMF020/33X (the exciter PTCs)
  "2410": C("Bel_0680L_2410"),                                   // Bel 0680L5000-05: KiCad has no 2410 fuse pattern
  "CAP-ALU-SMD-G-10x10.2": K("Capacitor_SMD", "CP_Elec_10x10.5"), // Panasonic EEH-ZC G can (φ10 × 10.2)
  CMC1210: C("TDK_ACT45B"),                                      // TDK ACT45B-101-2P (4-terminal CMC)
  X3225: C("Kyocera_CX3225GA_2Pad"),                            // Kyocera CX3225GA is a 2-terminal 3225 (DS land pattern) — the 4-pad 3225 pattern would put the crystal on two of four pads (round 22, pin audit)
  XAL4040: K("Inductor_SMD", "L_Coilcraft_XAL4040-XXX"),        // Coilcraft: one land for XAL4020/4030/4040; the 4040 file carries the 4.1 mm height (MANIFEST)
  XAL4020: K("Inductor_SMD", "L_Coilcraft_XAL4020-XXX"),
  // diodes — pad 1 = cathode in every KiCad Diode_SMD pattern, as the shared Diode cell numbers it (F190)
  SOD123: K("Diode_SMD", "D_SOD-123"), SOD323: K("Diode_SMD", "D_SOD-323"), SOD128: K("Diode_SMD", "D_SOD-128"),
  SMA: K("Diode_SMD", "D_SMA"), SMC: K("Diode_SMD", "D_SMC"),
  "SMA-flat": C("NRVBAF360T3G_SMA-FL"),                         // onsemi SMA flat-lead: its own land pattern
  // small-signal / SOT
  SOT23: K("Package_TO_SOT_SMD", "SOT-23-3"), "SOT23-5": K("Package_TO_SOT_SMD", "SOT-23-5"),
  SOT353: K("Package_TO_SOT_SMD", "SOT-353_SC-70-5"), SOT363: K("Package_TO_SOT_SMD", "SOT-363_SC-70-6"),
  SOT223: K("Package_TO_SOT_SMD", "SOT-223"),                  // pads 1-2-3 + tab "4": QLVS numbers its tab pin 4 (DMP6023LEQ: tab = D, both on VBSW); TO-261 orientation gives 1 = G, 2 = D, 3 = S
  DPAK: K("Package_TO_SOT_SMD", "TO-252-5_TabPin3"),            // NCV4276CDT50 (U5LB/U5LC/UGDL): the NCV4276C exists ONLY as a 5-lead DPAK/D2PAK (DS p.1: "DPAK 5-PIN"; pin 2 INH, tab = pin 3 GND) — round 22, F211: the 3-lead land the code implied would have left INH and OUT without pads
  DPAK5: K("Package_TO_SOT_SMD", "TO-252-5_TabPin3"),           // NCV4276CDTADJ: tab = pin 3 (GND) — MANIFEST
  D2PAK5: K("Package_TO_SOT_SMD", "TO-263-5_TabPin3"),          // NCV4276CDSADJ (ULDO15, power board): D2PAK-5, tab = pin 3 — MANIFEST
  LFPAK56: K("Package_TO_SOT_SMD", "LFPAK56"),
  "TO247-4L": K("Package_TO_SOT_THT", "TO-247-4_Vertical"),
  // ICs
  SOIC8: K("Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"), SOP8: K("Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"),
  SOIC16W: K("Package_SO", "SOIC-16W_7.5x10.3mm_P1.27mm"),      // NSI6611 SOW-16, UCC12051 DVE
  SOIC8W: C("AMC1311B_DWV8"),                                    // TI DWV (8-pin, 5.85 mm wide body): no KiCad pattern
  SMD8W: C("VOW3120_SMD8W"),                                     // Vishay SMD-8 wide opto
  VSSOP8: K("Package_SO", "VSSOP-8_2.3x2mm_P0.5mm"),            // Nexperia DC = SOT765-1 — MANIFEST
  "HTSSOP14-PWP": C("ALM2402-Q1_PWP0014H"),                      // TI PWP0014H (ALM2402-Q1 p.27–29) — drawn, the generic Texas HTSSOP-14 mask differs (MANIFEST)
  WQFN16: K("Package_DFN_QFN", "WQFN-16-1EP_3x3mm_P0.5mm_EP1.68x1.68mm"), // TPS55340 RTE0016C: EP 1.68 × 1.68 (MANIFEST); pad 17 = the PowerPAD pin
  HLQFP48: C("FS26_HLQFP48"),                                    // NXP SOT1571-6: its own EP/mask (MANIFEST) — the TI PHP0048E land did not fit
  DWN36: C("UCC14141-Q1_DWN36"),
  MAPBGA289: K("Package_BGA", "MAPBGA-289_14x14mm_Layout17x17_P0.8mm"), // pads A1…U17 (rows skip I O Q S) = the ball map
  // magnetics, modules, THT
  XfmrEE12: C("VGT12EEM-200S1A4"),
  EconoDUAL3: C("EconoDUAL3"),                                   // HCS600FH120D3C1 / HCG600FH120D3E1EA aux pins + terminals
  AxialP38: C("Yageo_SQP10_P55.88"),                             // RDIS1…4: the SQP10 body needs a 55.88 mm pitch (MANIFEST); the sheet helper AxialFP(38) is a coarse-grid placeholder
  AxialWW10W: C("Yageo_SQP10_P55.88"),                           // the same part under the discharge-row code
  "FilmCan37.5": C("Faratronic_C3D1_P37.5"),
  "FilmP27.5": K("Capacitor_THT", "C_Rect_L31.5mm_W17.0mm_P27.50mm_MKS4"), // 1 µF / 1200 V class part — the widest 27.5 mm body
  DiscP10: K("Capacitor_THT", "C_Disc_D16.0mm_W5.0mm_P10.00mm"),               // VY1 size code 63: D 16.0 max, F 10.0 (MANIFEST)
  "disc, 10 mm pitch (FilmBoxFP(10) land)": K("Capacitor_THT", "C_Disc_D12.5mm_W5.0mm_P10.00mm"), // VY2 size code 49: D 12.5 max, F 10.0 (MANIFEST)
  StudM8: K("MountingHole", "MountingHole_8.4mm_M8_Pad"), StudM6: K("MountingHole", "MountingHole_6.4mm_M6_Pad"),
  TabM6: K("MountingHole", "MountingHole_6.4mm_M6_Pad"),
  // connectors
  "HDR2x20-2.54": C("Samtec_IPL1-120_2x20"), "IPL1-104 (1x4 2.54 mm THT)": C("Samtec_IPL1-104_1x04"), "IPL1-102 (1x2 2.54 mm THT)": C("Samtec_IPL1-102_1x02"),
  "HDR2x5-1.27": K("Connector_PinHeader_1.27mm", "PinHeader_2x05_P1.27mm_Vertical"),  // Samtec FTSH-105 (SWD)
  HDR2x5: K("Connector_PinHeader_2.00mm", "PinHeader_2x05_P2.00mm_Vertical"),         // Samtec T2M-105 (2.00 mm)
  AMPSEAL23: C("TE_770669-1"),
  [OFF_BOARD]: null,
};

// the imperial size a class MPN encodes ("R2512-3R3-2W", "R0805-4k7-1%"); MLCC classes carry none
const sizeOfMpn = (mpn) => /^(?:R|C|L|FB)?(0402|0603|0805|1206|1210|2512)\b/.exec(String(mpn ?? ""))?.[1];
const sizeCode = (kind, size) => (size ? `${kind}${size}` : undefined);

// rule: the parts-db row; drawn: the size pages.mjs read off the sheet's pads ("0603" …) or null; who: the designator
export function footprintOf(rule, drawn, who) {
  if (!rule) throw new Error(`${who}: no parts-db rule — no footprint can be bound`);
  if (rule.kfp) return parseKfp(rule.kfp);
  if (rule.fp === OFF_BOARD) return null;
  if (rule.fp && KFP[rule.fp] !== undefined) return KFP[rule.fp];
  if (rule.fp) throw new Error(`${who}: footprint code "${rule.fp}" (${rule.mpn}) is not in footprints.mjs KFP`);
  const kind = /^(?:MLCC|C)/.test(String(rule.mpn ?? "")) ? "C" : /^R/.test(String(rule.mpn ?? "")) ? "R" : undefined;
  const code = sizeCode(kind, sizeOfMpn(rule.mpn)) ?? sizeCode(kind, drawn);
  if (code && KFP[code]) return KFP[code];
  throw new Error(`${who}: rule ${rule.m} (${rule.mpn}) has no footprint code and no drawn chip size`);
}
// a per-part choice: a KFP code, "Lib:Name" (copied from that KiCad library) or a bare custom name
export const parseKfp = (kfp) => {
  if (typeof kfp !== "string") return kfp;
  if (KFP[kfp]) return KFP[kfp];
  const m = /^([^:]+):(.+)$/.exec(kfp);
  return m ? K(m[1], m[2]) : C(kfp);
};
export const fpId = (r) => (r ? `${LIB}:${r.name}` : "");
export const isOffBoard = (rule) => !!rule && rule.fp === OFF_BOARD && !rule.kfp;
