#!/usr/bin/env node
// kicad5-gen.mjs — emit the hand-placed Traction schematic sheets in KiCad 5.1 LEGACY format
// (importable into EasyEDA Pro, opens in KiCad 5.1). Ported from the DC-Modules
// power-module-platform generator so the Traction set carries the identical house style:
// every pin gets a short stub ending in a net label, components sit on a 50 mil grid in
// uniform columns, sections are framed with notes lines and titled "GROUP / SECTION",
// sheet-index + net-naming + safety panels fill the voids, title block bottom-right.
//
// Input:  calculations/out/pages/<side>-<PAGE>.json   (from pages.mjs)
// Output: kicad5/traction/*.sch + traction-r1.lib + traction.pro (+ SHIP zip)
// Run:    node calculations/kicad5-gen.mjs

import { readFileSync, writeFileSync, appendFileSync, mkdirSync, readdirSync, existsSync, unlinkSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { DB, OVERRIDES, SAFETY_ROWS, DISCHARGE_ROWS } from "./parts-db.mjs";
import { footprintOf, fpId, LIB as FP_LIB } from "./footprints.mjs";
// round 22 (F210): every symbol's F2 is "traction:<footprint>" — footprintOf THROWS when a rule resolves to nothing,
// so a sheet cannot be written with an unbound part; off-board parts (OffBoard) get an empty field on purpose
const fpField = (c) => fpId(footprintOf(DB.find((r) => r.m.test(c.designator)), c.size, c.designator));

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const SRC = join(ROOT, "calculations/out/pages");
const OUT = join(ROOT, "kicad5/traction");
// Library name is revision-stamped: EasyEDA will NOT overwrite an existing library of the same
// name, so a re-import would silently mix new sheets with stale pin geometry. Bump on symbol change.
const LIB_NAME = "traction-r1";
import { REV } from "./rev.mjs";
// Pinned, NOT new Date(): a release sheet carries its release date; bump with REV.
const DATE = "2026-09-23";
mkdirSync(OUT, { recursive: true });

// ---- geometry in mils (50 mil grid) ------------------------------------------------------
const YGRID = 250;            // frame tops AND frame heights land on this grid
const G = 50;                 // 1.27 mm
const snap = (v) => Math.round(v / G) * G;
const PITCH = 100;            // pin pitch (2.54 mm)
const STUB = 200;             // pin stub before its label
const ROW = 400;              // vertical pitch between stacked 2-pin parts
const COLGAP = 400;
const SECPAD = 200;
const SECTITLE = 250;
const SECGAP = 400;
const MARGIN = 500;
const CHW = 27;               // label glyph advance at 40 mil text size

let tsid = 0x5e000000;
const nextId = () => (++tsid).toString(16).toUpperCase().padStart(8, "0");

// ---- part classification -----------------------------------------------------------------
function pinSide(name) {
  if (/^(VDD|VCC|VIN|VP|VDD1|VDD2|VCC1|VCC2|V\+|COM)$/i.test(name)) return "top";
  if (/^(GND|GND1|GND2|VEE|VEE2|VSS|VN|V-|EP)$/i.test(name)) return "bottom";
  if (/^(OUT|OUTP|OUTN|OUTH|OUTL|Y\d?|DRV|GATE|SW|VO|Q\d|CANH|CANL|TXD|RXD|P5|P15|P18|\+VO|-VO|0V|AC|D|CLAMP|DESAT|ASC)$/i.test(name)) return "right";
  if (/^OUT/i.test(name) || /^Q\d/.test(name)) return "right";
  return "left";
}
function groupPins(pins) {
  // Pins ordered by NAME FAMILY, not by pin number, so PA0..PA15 and COIL1/COIL2 sit together.
  // Pin NUMBERS are still printed on every pin, so nothing is lost for datasheet trace-back.
  const famKey = (p) => {
    const m = String(p.name ?? "").match(/^(.*?)(\d*)$/);
    return [m ? m[1] : "", m && m[2] ? +m[2] : -1];
  };
  const byFamily = (a, b) => {
    const [af, an] = famKey(a), [bf, bn] = famKey(b);
    return af.localeCompare(bf) || an - bn || (Number(a.pin_number) - Number(b.pin_number)) || String(a.pin_number).localeCompare(String(b.pin_number), undefined, { numeric: true });
  };
  const g = { left: [], right: [], top: [], bottom: [] };
  for (const p of pins) g[pinSide(p.name)].push(p);
  for (const k of Object.keys(g)) g[k].sort(byFamily);
  if (g.left.length > 16 && g.right.length * 2 < g.left.length) {
    const half = Math.ceil(g.left.length / 2);
    g.right = g.left.slice(half).concat(g.right).sort(byFamily);
    g.left = g.left.slice(0, half);
  }
  return g;
}
const CAT = (value, mpn, pins, designator = "") => {
  if (pins.length === 1) return "TERM";        // stud / tab: a terminal, not a chip
  if (pins.length !== 2) return "IC";
  const m = String(mpn || value);
  if (/^(R-|R\d|HV73|CER-|WW-|SQP-|PULSE-|SHUNT2|R0805|R1206|R2512|R0603)/.test(m) || /^\d+(\.\d+)?(k|M|R|Ω)?$/.test(value)) return "R";
  if (/^(MLCC|PP-|FILM-|X2-|Y2-|Y1-|C1812|EL-|ELH-|EEH-|CLINK)/.test(m)) return /^(EL-|ELH-|EEH-)/.test(m) ? "CP" : "C";   // round 17: EEH- (Panasonic hybrid polymer CLVC3) is polarised
  if (/^(IND-|DM-|FB-)/.test(m)) return "L";
  if (/^(US\d|UF-|1N4148|SMBJ|SMCJ|SMAJ|SMDJ|5\.0SMDJ|FAST-|SICJBS|STTH|BZT52|BAS|PMEG)/.test(m)) return "D";   // round 17 (F184): SMDJ (the 3 kW exciter TVS) and PMEG (the diversion Schottky) are diodes too
  if (/^FUSE-/.test(m)) return "F";
  if (/^(S20K|MOV)/.test(m)) return "MOV";
  if (/^GDT-/.test(m)) return "GDT";
  if (/^(CT-|ACX-|AS-\d)/.test(m)) return "CT";
  if (/^TACT-/.test(m) || /^SW/.test(designator)) return "SW";
  if (/^(CX3225|NX3225|XTAL)/.test(m)) return "Y";
  if (designator[0] === "J") return "J2";
  // Designator letter is the honest fallback the schematic itself guarantees.
  const d0 = designator[0];
  if (d0 === "C") return "C";
  if (d0 === "L") return "L";
  if (d0 === "D") return "D";
  if (d0 === "F") return "F";
  return "R";
};

// ---- legacy .lib symbol library ----------------------------------------------------------
const lib = new Map();
const libName = (s) => String(s).replace(/[^A-Za-z0-9_.+-]/g, "_");

// EasyEDA's KiCad-legacy importer places a symbol's pins at (ux+px, uy+py) — it does NOT apply
// the "1 0 0 -1" orientation matrix. So the library is written pre-mirrored about Y: EasyEDA
// mirrors it back and the sheet is correct. Mirroring twice is the identity — exact round trip.
const mirrorLibY = (text) => text.split("\n").map((l) => {
  const t = l.split(" ");
  const neg = (i) => { t[i] = String(-Number(t[i])); };
  const ang = (i) => { t[i] = String(((-Number(t[i])) % 3600 + 3600) % 3600); };
  switch (t[0]) {
    case "X": neg(4); if (t[6] === "U") t[6] = "D"; else if (t[6] === "D") t[6] = "U"; break;
    case "S": neg(2); neg(4); break;
    case "C": neg(2); break;
    case "P": { const n = Number(t[1]); for (let k = 0; k < n; k++) neg(6 + 2 * k); break; }
    case "A": { neg(2); const s0 = t[4], e0 = t[5]; t[4] = e0; t[5] = s0; ang(4); ang(5);
                neg(11); neg(13);
                const sx = t[10], sy = t[11]; t[10] = t[12]; t[11] = t[13]; t[12] = sx; t[13] = sy; break; }
    case "F0": case "F1": case "F2": case "F3": neg(3); break;
    default: return l;
  }
  return t.join(" ");
}).join("\n");

// Largest empty rectangle on the sheet, as a fraction of sheet area (drives the void gate).
const largestVoid = (rects, W, H) => {
  const TBW = 9000, TBH = 2600;                       // title-block corner, reserved
  rects = [...rects, { x0: W - TBW, y0: H - TBH, x1: W, y1: H }];
  const NX = 64, NY = 44, cw = W / NX, ch = H / NY;
  const occ = Array.from({ length: NY }, () => new Uint8Array(NX));
  for (const r of rects) {
    const x0 = Math.max(0, Math.floor(r.x0 / cw)), x1 = Math.min(NX - 1, Math.ceil(r.x1 / cw) - 1);
    const y0 = Math.max(0, Math.floor(r.y0 / ch)), y1 = Math.min(NY - 1, Math.ceil(r.y1 / ch) - 1);
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) occ[y][x] = 1;
  }
  const hgt = new Int32Array(NX);
  let best = 0, bx0 = 0, bx1 = 0, by0 = 0, by1 = 0;
  for (let y = 0; y < NY; y++) {
    for (let x = 0; x < NX; x++) hgt[x] = occ[y][x] ? 0 : hgt[x] + 1;
    const st = [];
    for (let x = 0; x <= NX; x++) {
      const h = x === NX ? 0 : hgt[x];
      let start = x;
      while (st.length && st[st.length - 1].h >= h) {
        const t = st.pop();
        const a = t.h * (x - t.x);
        if (a > best) { best = a; bx0 = t.x; bx1 = x; by0 = y - t.h + 1; by1 = y; }
        start = t.x;
      }
      st.push({ x: start, h });
    }
  }
  return { frac: best / (NX * NY), x0: bx0 * cw, y0: by0 * ch, x1: bx1 * cw, y1: (by1 + 1) * ch };
};

// Top-N empty rectangles, greedily: notes panels want a consistent corner, not just the biggest hole.
const voidCandidates = (rects, W, H, n) => {
  const NX = 64, NY = 44, cw = W / NX, ch = H / NY;
  const occ = Array.from({ length: NY }, () => new Uint8Array(NX));
  const mark = (r) => {
    const x0 = Math.max(0, Math.floor(r.x0 / cw)), x1 = Math.min(NX - 1, Math.ceil(r.x1 / cw) - 1);
    const y0 = Math.max(0, Math.floor(r.y0 / ch)), y1 = Math.min(NY - 1, Math.ceil(r.y1 / ch) - 1);
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) occ[y][x] = 1;
  };
  rects.forEach(mark);
  const out = [];
  for (let k = 0; k < n; k++) {
    const hgt = new Int32Array(NX);
    let best = 0, bx0 = 0, bx1 = 0, by0 = 0, by1 = 0;
    for (let y = 0; y < NY; y++) {
      for (let x = 0; x < NX; x++) hgt[x] = occ[y][x] ? 0 : hgt[x] + 1;
      const st = [];
      for (let x = 0; x <= NX; x++) {
        const h = x === NX ? 0 : hgt[x];
        let start = x;
        while (st.length && st[st.length - 1].h >= h) {
          const t = st.pop(), a = t.h * (x - t.x);
          if (a > best) { best = a; bx0 = t.x; bx1 = x; by0 = y - t.h + 1; by1 = y; }
          start = t.x;
        }
        st.push({ x: start, h });
      }
    }
    if (!best) break;
    const r = { x0: bx0 * cw, y0: by0 * ch, x1: bx1 * cw, y1: (by1 + 1) * ch, frac: best / (NX * NY) };
    out.push(r);
    mark(r);
  }
  return out;
};

// Sheet, BOM and LCSC map resolve the part the same way: designator vs parts-db, first match wins.
const partOf = (designator, value) => {
  const ov = OVERRIDES[designator];
  const rule = ov ?? DB.find((r) => r.m.test(designator));
  if (!rule?.mpn) return { mpn: "", lc: { status: "UNMAPPED" } };
  return { mpn: rule.mpn, lc: { lcsc: rule.lcsc, status: rule.lcsc ? "ORDERABLE" : (rule.cls ?? "CLASS") } };
};

// What gets PRINTED under a symbol: no internal "-class" taxonomy on the drawing, and bare
// ohm numbers take a trailing R so 4.7 can never read as 4.7 k.
const valueText = (designator, value) => {
  const ov = OVERRIDES[designator]?.mpn;
  const v = String(ov ?? value).replace(/-class$/i, "");
  return (/^R/.test(designator) && /^\d+(\.\d+)?$/.test(v)) ? `${v}R` : v;
};

function termLib(pinNum, pinName) {
  const nm = `TERM_${pinNum}`;
  if (lib.has(nm)) return nm;
  lib.set(nm, `#\n# ${nm}\n#\nDEF ${nm} J 0 40 N N 1 F N\n`
    + `F0 "J" 0 130 50 H V C CNN\nF1 "${nm}" 0 -130 50 H V C CNN\n`
    + `F2 "" 0 0 50 H I C CNN\nF3 "" 0 0 50 H I C CNN\nDRAW\n`
    + `C 50 0 35 0 1 8 N\n`
    + `X ${pinName.replace(/\s+/g, "_")} ${pinNum} -100 0 115 R 50 50 1 1 P\n`
    + `ENDDRAW\nENDDEF\n`);
  return nm;
}

function passiveLib(kind, n1, n2) {
  const nm = `${kind}${n1 === "1" && n2 === "2" ? "" : `_${n1}${n2}`}`;
  if (lib.has(nm)) return nm;
  const ref = kind === "D" ? "D" : kind === "L" ? "L" : kind === "Y" ? "Y" : kind === "J2" ? "J" : kind.startsWith("C") ? "C" : "R";
  let draw = "";
  if (kind === "R") draw = "P 9 0 1 10 -80 0 -65 0 -50 40 -25 -40 0 40 25 -40 50 40 65 0 80 0 N\n";
  else if (kind === "F") draw = "S -70 30 70 -30 0 1 10 N\nP 2 0 1 10 -80 0 80 0 N\n";
  else if (kind === "MOV") draw = "S -60 45 60 -45 0 1 10 N\nP 3 0 1 10 -85 -70 60 45 85 45 N\n";
  else if (kind === "GDT") draw = "C 0 0 80 0 1 10 N\nP 2 0 1 12 -35 45 -35 -45 N\nP 2 0 1 12 35 45 35 -45 N\nC 0 -55 8 0 1 0 F\n";
  else if (kind === "CT") draw = "A -40 0 40 -899 899 0 1 10 N -40 -40 -40 40\nA 40 0 40 -899 899 0 1 10 N 40 -40 40 40\nP 2 0 1 14 -90 90 90 90 N\nC -75 60 8 0 1 0 F\n";
  else if (kind === "SW") draw = "C -40 0 10 0 1 10 N\nC 40 0 10 0 1 10 N\nP 2 0 1 12 -55 45 55 45 N\nP 2 0 1 10 0 45 0 75 N\n";
  // Plates PERPENDICULAR to the pins; height matches the R zigzag envelope so columns pack alike.
  else if (kind === "C") draw = "P 2 0 1 12 -20 -40 -20 40 N\nP 2 0 1 12 20 -40 20 40 N\n";
  // CP: straight plate = pin 1 = +, marked with a "+" (authored at +y — the lib pre-mirror lands it above).
  else if (kind === "CP") draw = "P 2 0 1 12 -20 -40 -20 40 N\nA 6 0 44 -646 646 0 1 12 N 25 -40 25 40\n"
    + "P 2 0 1 8 -85 55 -55 55 N\nP 2 0 1 8 -70 40 -70 70 N\n";
  else if (kind === "L") draw = "A -40 0 40 1 1799 0 1 8 N 0 0 -80 0\nA 40 0 40 1 1799 0 1 8 N 80 0 0 0\n";
  else if (kind === "D") draw = "P 4 0 1 8 -50 50 -50 -50 50 0 -50 50 F\nP 2 0 1 12 50 50 50 -50 N\n";
  // crystal: plates + resonator rect
  else if (kind === "Y") draw = "P 2 0 1 12 -35 -40 -35 40 N\nP 2 0 1 12 35 -40 35 40 N\nS -20 45 20 -45 0 1 10 N\n";
  // 2-pin connector: open body + two contact squares
  else if (kind === "J2") draw = "S -70 40 70 -40 0 1 10 N\nS -45 15 -15 -15 0 1 10 F\nS 15 15 45 -15 0 1 10 F\n";
  const len1 = kind === "C" || kind === "CP" ? 230 : kind === "SW" ? 200 : 170;
  const len2 = kind === "C" ? 230 : kind === "CP" || kind === "SW" ? 200 : 170;
  lib.set(nm, `#\n# ${nm}\n#\nDEF ${nm} ${ref} 0 40 N N 1 F N\n`
    + `F0 "${ref}" 0 130 50 H V C CNN\nF1 "${nm}" 0 -130 50 H V C CNN\n`
    + `F2 "" 0 0 50 H I C CNN\nF3 "" 0 0 50 H I C CNN\nDRAW\n${draw}`
    + `X ${n1} ${n1} -250 0 ${len1} R 50 50 1 1 P\nX ${n2} ${n2} 250 0 ${len2} L 50 50 1 1 P\n`
    + `ENDDRAW\nENDDEF\n`);
  return nm;
}

function icLib(rawKey, pins) {
  const nm = libName(rawKey);
  if (lib.has(nm)) return nm;
  const g = groupPins(pins);
  const rows = Math.max(g.left.length, g.right.length, 1);
  const halfH = Math.max(rows * PITCH / 2 + PITCH, 2 * PITCH);
  const nameW = Math.max(...pins.map((p) => p.name.length), 4) * 30;
  const halfW = Math.max(snap(nameW + 150), 300);
  let draw = `S ${-halfW} ${halfH} ${halfW} ${-halfH} 0 1 10 f\n`;
  // Function glyph centred in the body: a power FET / opto / iso-barrier drawn as a bare box
  // "reads like a dummy". Pins and body size unchanged; only inks function into empty body.
  const GLYPH = {
    NMOS: "P 2 0 1 10 -160 0 -60 0 N\nP 2 0 1 12 -60 90 -60 -90 N\n"
        + "P 2 0 1 12 -25 110 -25 40 N\nP 2 0 1 12 -25 30 -25 -30 N\nP 2 0 1 12 -25 -40 -25 -110 N\n"
        + "P 2 0 1 10 -25 75 120 75 N\nP 2 0 1 10 -25 -75 120 -75 N\nP 2 0 1 10 120 75 120 -75 N\n"
        + "P 4 0 1 8 -25 0 35 25 35 -25 -25 0 F\nP 2 0 1 10 35 0 120 0 N\n",
    NPN: "C 0 0 130 0 1 10 N\nP 2 0 1 12 -45 80 -45 -80 N\nP 2 0 1 10 -120 0 -45 0 N\n"
       + "P 2 0 1 10 -45 35 70 105 N\nP 2 0 1 10 -45 -35 70 -105 N\nP 3 0 1 8 40 -70 70 -105 25 -95 F\n",
    XFMR: "A -35 -90 45 1 1799 0 1 12 N -80 -90 10 -90\nA -35 0 45 1 1799 0 1 12 N -80 0 10 0\nA -35 90 45 1 1799 0 1 12 N -80 90 10 90\n"
        + "P 2 0 1 12 25 140 25 -140 N\nP 2 0 1 12 45 140 45 -140 N\n"
        + "A 105 -90 45 -1799 -1 0 1 12 N 60 -90 150 -90\nA 105 0 45 -1799 -1 0 1 12 N 60 0 150 0\nA 105 90 45 -1799 -1 0 1 12 N 60 90 150 90\n"
        + "C -95 -120 9 0 1 0 F\nC 165 -120 9 0 1 0 F\n",
    CMC: "A -60 -70 40 1 1799 0 1 12 N -100 -70 -20 -70\nA -60 30 40 1 1799 0 1 12 N -100 30 -20 30\n"
       + "A 60 -70 40 1 1799 0 1 12 N 20 -70 100 -70\nA 60 30 40 1 1799 0 1 12 N 20 30 100 30\n"
       + "P 2 0 1 14 -130 -15 130 -15 N\nP 2 0 1 14 -130 -25 130 -25 N\n",
    OPTO: "P 4 0 1 8 -140 30 -140 -30 -90 0 -140 30 F\nP 2 0 1 12 -90 30 -90 -30 N\n"
        + "P 2 0 1 10 -60 15 -10 40 N\nP 3 0 1 8 -25 42 -10 40 -18 28 F\n"
        + "P 2 0 1 10 -60 -15 -10 10 N\nP 3 0 1 8 -25 12 -10 10 -18 -2 F\n"
        + "P 2 0 1 12 30 60 30 -60 N\nP 2 0 1 12 45 60 45 -60 N\n",
    ISO: "P 2 0 1 8 0 __H__ 0 __H2__ N\nP 2 0 1 8 0 __H3__ 0 __H4__ N\nP 2 0 1 8 0 __H5__ 0 __H6__ N\nP 2 0 1 8 0 __H7__ 0 __H8__ N\n",
    SHUNT: "P 2 0 1 20 -120 0 120 0 N\nP 2 0 1 10 -70 0 -70 -60 N\nP 2 0 1 10 70 0 70 -60 N\n",
    DCDC: "P 2 0 1 10 __DGA__ N\nA -70 30 40 1 1799 0 1 10 N -110 30 -30 30\nP 2 0 1 12 40 60 40 -60 N\nP 2 0 1 12 60 60 60 -60 N\nP 2 0 1 10 90 30 130 30 N\nP 2 0 1 10 90 -30 130 -30 N\n",
  };
  const GLYPH_OF = [
    [/^(HCS|HCM|C3M|IMZ|NVH|SCT|SIC-|BUK)/, "NMOS"],   // SiC modules/FETs (hiitio + alternates) + flyback NFET
    [/^(S8050|MMBT|2N7002)/, "NPN"],
    [/^(XFMR-|VGT)/, "XFMR"],
    [/^(CMC-|ACT45)/, "CMC"],
    [/^(TLP1|VOM1271|PC817|TLP2)/, "OPTO"],
    [/^(NSI6|NSI1|NSI2|NSI8|AMC1|ISO77|UCC217|1ED3)/, "ISO"],
    [/^SHUNT4-/, "SHUNT"],
    [/^(QA01C|ISO5V|B0515|B1505|MGJ2)/, "DCDC"],
  ];
  {
    const hit = GLYPH_OF.find(([re]) => re.test(nm));
    if (hit) {
      let gg = GLYPH[hit[1]];
      const H = Math.min(halfH - 120, 700);
      gg = gg.replace("__H__", String(H)).replace("__H2__", String(Math.round(H * 0.55)))
           .replace("__H3__", String(Math.round(H * 0.3))).replace("__H4__", String(-Math.round(H * 0.05)))
           .replace("__H5__", String(-Math.round(H * 0.3))).replace("__H6__", String(-Math.round(H * 0.55)))
           .replace("__H7__", String(-Math.round(H * 0.8))).replace("__H8__", String(-H))
           .replace("__DGA__", "-130 -30 -110 30");
      draw += gg;
    }
  }
  const px = (p, x, y, orient) => {
    // power pins sit on top/bottom edges; "~" suppresses the in-body name collision.
    const side = pinSide(p.name);
    const pn = (side === "top" || side === "bottom") ? "~" : p.name.replace(/\s+/g, "_");
    return `X ${pn} ${p.pin_number} ${x} ${y} 150 ${orient} 50 40 1 1 ${
      /^(GND|VEE|VSS|EP|VDD|VCC|VIN|VP|COM)/i.test(p.name) ? "W" : "P"}\n`;
  };
  g.left.forEach((p, i) => { draw += px(p, -halfW - 150, halfH - PITCH - i * PITCH, "R"); });
  g.right.forEach((p, i) => { draw += px(p, halfW + 150, halfH - PITCH - i * PITCH, "L"); });
  g.top.forEach((p, i) => { draw += px(p, -halfW + PITCH + i * PITCH, halfH + 150, "D"); });
  g.bottom.forEach((p, i) => { draw += px(p, -halfW + PITCH + i * PITCH, -halfH - 150, "U"); });
  lib.set(nm, `#\n# ${nm}\n#\nDEF ${nm} U 0 40 Y Y 1 F N\n`
    + `F0 "U" ${-halfW - 50} ${halfH + 100} 50 H V R CNN\nF1 "${nm}" ${-halfW} ${-halfH - 100} 50 H V L CNN\n`
    + `F2 "" 0 0 50 H I C CNN\nF3 "" 0 0 50 H I C CNN\nDRAW\n${draw}ENDDRAW\nENDDEF\n`);
  return nm;
}

// ---- shape used by the packer (must mirror the library geometry) --------------------------
const PIN_UNION = new Map();
for (const f of readdirSync(SRC).filter((x) => x.endsWith(".json"))) {
  const pg = JSON.parse(readFileSync(join(SRC, f), "utf8"));
  for (const c of pg.chunks.flat()) {
    if (c.pins.length === 2) continue;
    const key = c.mpn || c.value;
    if (!PIN_UNION.has(key)) PIN_UNION.set(key, new Map());
    const u = PIN_UNION.get(key);
    for (const p of c.pins) if (!u.has(String(p.pin_number))) u.set(String(p.pin_number), { ...p });
  }
}
const unionPins = (c) => PIN_UNION.get(c.mpn || c.value) ? [...PIN_UNION.get(c.mpn || c.value).values()] : c.pins;

function shapeOf(c) {
  const cat = CAT(c.value, partOf(c.designator, c.value).mpn, c.pins, c.designator);
  if (cat === "TERM") {
    const lw = (c.pins[0]?.signal_name?.length ?? 0) * CHW;
    return { cat, w: 200 + STUB + lw, h: ROW, lw, rw: 0 };
  }
  if (cat !== "IC") {
    let ns = c.pins.map((p) => String(p.pin_number)).sort((a, b) => Number(a) - Number(b));
    let l = c.pins.find((p) => String(p.pin_number) === ns[0]);
    let r = c.pins.find((p) => String(p.pin_number) === ns[1]);
    // SEAT POLARIZED PINS SEMANTICALLY, NOT NUMERICALLY: the D glyph draws its anode on the
    // LEFT seat; the anode-named pin takes it regardless of number. Symbol variant (e.g. D_21)
    // keeps pin numbers truthful.
    if (c.pins.length === 2) {
      const a = c.pins.find((p) => /^(A|anode|\+)$/i.test(p.name ?? ""));
      const k = c.pins.find((p) => /^(C|K|cathode|-)$/i.test(p.name ?? ""));
      if (a && k) { l = a; r = k; ns = [String(a.pin_number), String(k.pin_number)]; }
      else if (cat === "D")
        throw new Error(`polarity seating: diode ${c.designator} payload has no named anode/cathode pins`);
    }
    const lw = (l?.signal_name?.length ?? 0) * CHW, rw = (r?.signal_name?.length ?? 0) * CHW;
    return { cat, nums: ns, w: 500 + STUB * 2 + lw + rw, h: ROW, lw, rw };
  }
  const all = unionPins(c);
  const g = groupPins(all);
  const rows = Math.max(g.left.length, g.right.length, 1);
  const halfH = Math.max(rows * PITCH / 2 + PITCH, 2 * PITCH);
  const nameW = Math.max(...all.map((p) => p.name.length), 4) * 30;
  const halfW = Math.max(snap(nameW + 150), 300);
  const lw = Math.max(0, ...g.left.map((p) => (p.signal_name || "").length)) * CHW;
  const rw = Math.max(0, ...g.right.map((p) => (p.signal_name || "").length)) * CHW;
  const topExtra = g.top.length ? 150 + STUB + 150 : 0;    // 500: on the G grid
  const bottomExtra = g.bottom.length ? 150 + STUB + 150 : 0;
  const TEXT = 125;                                        // 2*TEXT + PITCH = 350: on the G grid
  return { cat, w: halfW * 2 + (150 + STUB) * 2 + lw + rw,
    h: TEXT + topExtra + halfH * 2 + bottomExtra + TEXT + PITCH,
    halfW, halfH, topExtra, TEXT, lw, rw, groups: g };
}

// ---- hand section compositions (function order where it matches the packed frame) ---------
// A hand plan replaces the column search but reuses the same column geometry. "#" = instance
// index stripped from the title; anything not named falls into a final column (never drops a part).
const HAND = {
  // A drive channel reads: driver + its DESAT sense | the gate network | the floating bias.
  "PHASE-U / DRIVE-UH": [
    ["UUHG", "DUHSB", "RUHDS", "DUHS1", "DUHS2", "CUHBL"],
    ["RUHON", "RUHOFF", "RUHMC", "RUHGS", "RUHPD", "DUHZ1", "DUHZ2"],
    ["DUHR", "ZUHV", "CUHV1", "CUHE1", "RUHBL", "CUHB1", "CUHB2"],
  ],
  "PHASE-U / DRIVE-UL": [
    ["UULG", "DULSB", "RULDS", "DULS1", "DULS2", "CULBL"],
    ["RULON", "RULOFF", "RULMC", "RULGS", "RULPD", "DULZ1", "DULZ2"],
    ["DULR", "ZULV", "CULV1", "CULE1", "RULBL", "CULB1", "CULB2"],
  ],
  "PHASE-U / MODULE-U": [
    ["MODU"],
    ["CUSN", "JMU", "RUTS", "CUTF", "DUTP"],
  ],
  "PHASE-V / DRIVE-VH": [
    ["UVHG", "DVHSB", "RVHDS", "DVHS1", "DVHS2", "CVHBL"],
    ["RVHON", "RVHOFF", "RVHMC", "RVHGS", "RVHPD", "DVHZ1", "DVHZ2"],
    ["DVHR", "ZVHV", "CVHV1", "CVHE1", "RVHBL", "CVHB1", "CVHB2"],
  ],
  "PHASE-V / DRIVE-VL": [
    ["UVLG", "DVLSB", "RVLDS", "DVLS1", "DVLS2", "CVLBL"],
    ["RVLON", "RVLOFF", "RVLMC", "RVLGS", "RVLPD", "DVLZ1", "DVLZ2"],
    ["DVLR", "ZVLV", "CVLV1", "CVLE1", "RVLBL", "CVLB1", "CVLB2"],
  ],
  "PHASE-V / MODULE-V": [
    ["MODV"],
    ["CVSN", "JMV", "RVTS", "CVTF", "DVTP"],
  ],
  "PHASE-W / DRIVE-WH": [
    ["UWHG", "DWHSB", "RWHDS", "DWHS1", "DWHS2", "CWHBL"],
    ["RWHON", "RWHOFF", "RWHMC", "RWHGS", "RWHPD", "DWHZ1", "DWHZ2"],
    ["DWHR", "ZWHV", "CWHV1", "CWHE1", "RWHBL", "CWHB1", "CWHB2"],
  ],
  "PHASE-W / DRIVE-WL": [
    ["UWLG", "DWLSB", "RWLDS", "DWLS1", "DWLS2", "CWLBL"],
    ["RWLON", "RWLOFF", "RWLMC", "RWLGS", "RWLPD", "DWLZ1", "DWLZ2"],
    ["DWLR", "ZWLV", "CWLV1", "CWLE1", "RWLBL", "CWLB1", "CWLB2"],
  ],
  "PHASE-W / MODULE-W": [
    ["MODW"],
    ["CWSN", "JMW", "RWTS", "CWTF", "DWTP"],
  ],
  // DC link: the can bank in two columns; the XM3 bleeder as its three strings.
  "CAP-BANK / BUS-STUDS": [
    ["JCBEP", "JCBEN", "JCBDP", "JCBDN"],
    ["JCBUP", "JCBUN", "JCBVP", "JCBVN", "JCBWP", "JCBWN"],
  ],
  "CAP-BANK / CAN-ARRAY": [
    ["CDC1", "CDC2", "CDC3", "CDC4", "CDC5", "CDC6", "CDC7", "CDC8"],
    ["CDC9", "CDC10", "CDC11", "CDC12", "CDC13", "CDC14", "CDC15", "CDC16"],
  ],
  "DISCHARGE / BLEED": [
    ["RBLD1", "RBLD2", "RBLD3", "RBLD4", "RBLD5", "RBLD6"],
    ["RBLD7", "RBLD8", "RBLD9", "RBLD10", "RBLD11", "RBLD12"],
  ],
  "DISCHARGE / ENTRY": [["JDCP", "JDCN", "JCTL"]],
  // Discharge in work order: bias, opto, LED/gate network | the switch with its string.
  "DISCHARGE / ACTIVE": [
    ["PSQD", "CQDI1", "CQDI2", "CQDIB", "RQDE1", "RQDE2", "RQDF1", "RQDF2", "CQDF", "CQDO"],
    ["UQD", "RQDL", "RQDG", "RQDPD", "CQD"],
    ["QDIS", "RDIS1", "RDIS2", "RDIS3", "RDIS4"],
  ],
  // An isolated voltage sense is a divider feeding an amplifier — string first, then the tap.
  "HV-SENSING / SENSE-VDC": [
    ["RVDD1", "RVDD2", "RVDD3", "RVDD4", "RVDD5", "RVDD6"],
    ["RVDDL", "CVDDF", "UIVDC"],
  ],
  "HV-SENSING / SENSE-VDC#": [
    ["RVBD1", "RVBD2", "RVBD3", "RVBD4", "RVBD5", "RVBD6"],
    ["RVBDL", "CVBDF", "UIVB"],
  ],
  "HV-SENSING / ASC-BUFFER": [
    ["PSASC", "CASCI1", "CASCI2", "CASCIB", "RASCE1", "RASCE2", "RASCF1", "RASCF2", "CASCF", "CASCO"],
    ["UASC", "RASCL", "RASCG", "RASCPD", "CASC"],
  ],
  // A flyback chain: controller + its comp/CT network | switch + sense + snubber |
  // regulation | the three transformers.
  "GATE-POWER / FLY-HS": [
    ["UFH", "RFHRT", "CFHVR", "RFHG", "RFHGO", "DFHG", "ZFHG", "RFHGS", "CFHCT", "CFHCO", "CFHCF"],
    ["QFH", "RFHCS", "RFHSI", "CFHSI", "RFHSN", "CFHSN", "DFHSN"],
    ["QFHE1", "QFHE2", "RFHEN", "RFHST", "DFHA", "CFHA", "DFHVZ", "RFHFB1", "RFHFB2"],
    ["TFH1", "TFH2", "TFH3"],
  ],
  "GATE-POWER / FLY-LS": [
    ["UFL", "RFLRT", "CFLVR", "RFLG", "RFLGO", "DFLG", "ZFLG", "RFLGS", "CFLCT", "CFLCO", "CFLCF"],
    ["QFL", "RFLCS", "RFLSI", "CFLSI", "RFLSN", "CFLSN", "DFLSN"],
    ["QFLE1", "QFLE2", "RFLEN", "RFLST", "DFLA", "CFLA", "DFLVZ", "RFLFB1", "RFLFB2"],
    ["TFL1", "TFL2", "TFL3"],
  ],
  // Power enters at the fuse, so the chain reads fuse -> reverse -> clamp -> filter.
  "LV-POWER / PROT-H": [["FH1", "DRH", "DTVH", "LFH1", "CLVH1", "CLVH2"]],
  "LV-POWER / PROT-L": [["FL1", "DRL", "DTVL", "LFL1", "CLVL1", "CLVL2"]],
  "LV-POWER / BOOST-#V": [
    ["CB15I", "UB15", "CB15C"],
    ["LB15", "DB15", "CB15O1", "CB15O2", "RB15F1", "RB15F2"],
  ],
  "DC-INPUT / ENTRY": [["JHVP", "JHVN", "JPE"]],
  "CONTROL-IF / HARNESS": [
    ["JIC"],
    ["RPD0", "RPD1", "RPD2", "RPD3", "RPD4", "RPD5"],
    ["RPD6", "RPD7", "RPD8", "RPD9", "RPD10", "RHWID"],
  ],
  // ---- card ----
  "CONTROL / MCU": [
    ["UMCU"],
    ["Y1", "CYA", "CYB", "RMRST", "CMA1", "CMA2"],
    ["CV25", "CMD1", "CMD2", "CMD3", "CMD4", "CMD5", "CMD6", "CMD7", "CMD8", "CMD9", "CMD10", "CMD11", "CMD12"],
  ],
  "SBC / FS26": [
    ["USBC"],
    ["DBAT", "LSBC", "LCOR", "QBAL", "CBAL", "CBTP", "CBTC", "CVDIG", "CVBOS"],
    ["RSB1", "RSB2", "RSB3", "RSB4", "RAGT", "RDBG"],
    ["CSB1", "CSB2", "CSB3", "CSB3B", "CSB4", "CSB5", "CSB6", "CSB7", "CSB8"],
  ],
  "SBC / LV-INPUT": [["FLVC", "DREVC", "DTVSC", "LFC", "CLVC1", "CLVC2"], ["QLVS", "RLVSG", "ZLVS", "RLVSD", "RLVSM", "CLVSM", "QLVN", "FVBH", "FVBL"]],
  "SBC / WAKE": [["DIGN", "RIGN1", "RIGN2", "CIGN"], ["RIGNS1", "RIGNS2", "CIGNS"]],
  // Safety reads: the AND chain with its pull-ups | the fault/ready conditioning.
  "SAFETY / GATE-EN": [
    ["RENP1", "RENP2", "UAND1", "UAND2", "RGPD", "CAND1", "CAND2"],
    ["USCH3", "CSCH3", "RRDB"],
    ["RFLTP1", "RFLTP2", "RRDYP1", "RRDYP2", "RV5GP", "RV5GS", "CFLTF", "CFLTF2"],
    ["ULAT2", "DFLT1", "DFLT2", "RFLTC", "RLAT2", "CLAT2"],
    ["RFLTD", "CFLTD", "CCLR", "DCLR"],
  ],
  "SAFETY / ASC-LATCH": [
    ["ULAT", "RLAT1", "CLAT", "RASCP"],
    ["RFS1", "RFS2", "RFS3", "RFS4"],
  ],
  "SAFETY / INTERLOCK": [["RILK1", "RILK2", "RILK3", "RILK4", "CILK"]],
  "VDC-RECEIVE / CH-#": [
    ["RVD#A", "RVD#B", "RVD#C", "RVD#D"],
    ["UVD#", "CVD#"],
    ["UVOF", "ROF1", "ROF2", "COF1"],
  ],
  // Exciter in signal order: SWG filter -> op-amp -> power driver -> monitor dividers.
  "RESOLVER / EXCITER": [
    ["CEXA4", "REXA1", "CEXA3", "REXA2", "CEXA2", "REXA3", "CEXA1", "REXA4", "UEXF"],
    ["UEXD", "REXB1", "REXB2", "REXB3", "REXB4", "CEXD", "RSDN"],
    ["ULDOEX", "RLDE1", "RLDE2", "CLDEC", "CLDE"],
    ["REXM1", "REXM2", "REXM3", "REXM4", "CEXM"],
    ["RSXP", "TVSEP", "FEXP", "RSXN", "TVSEN", "FEXN"],
  ],
  "RESOLVER / VMID": [
    ["RVM1", "RVM2", "CVM1", "UVMB1"],
    ["RVM3", "RVM4", "CVM2", "UVMB2"],
  ],
  "RESOLVER / SIN": [
    ["RSIN1", "RSIN2", "RSINF1", "RSINF2", "DSINP", "CSIND", "CSINF1", "CSINF2"],
    ["RSINR1", "RSINR2", "CSINA1", "CSINA3", "CSINA2"],
  ],
  "RESOLVER / COS": [
    ["RCOS1", "RCOS2", "RCOSF1", "RCOSF2", "DCOSP", "CCOSD", "CCOSF1", "CCOSF2"],
    ["RCOSR1", "RCOSR2", "CCOSA1", "CCOSA3", "CCOSA2"],
  ],
  // A hall channel: supply filtering | the two-buffer chain in signal order.
  "PHASE-SENSE / SENSE-IU": [
    ["LUB", "CUS1", "CUS2", "RUB0", "RUB1", "CUB1"],
    ["UUB1", "RUB2", "CUB2", "UUB2", "RUB3"],
  ],
  "PHASE-SENSE / SENSE-IV": [
    ["LVB", "CVS1", "CVS2", "RVB0", "RVB1", "CVB1"],
    ["UVB1", "RVB2", "CVB2", "UVB2", "RVB3"],
  ],
  "PHASE-SENSE / SENSE-IW": [
    ["LWB", "CWS1", "CWS2", "RWB0", "RWB1", "CWB1"],
    ["UWB1", "RWB2", "CWB2", "UWB2", "RWB3"],
  ],
  "PHASE-SENSE / HALL-CONN": [["JLEM", "USNSU", "USNSV", "USNSW"]],
  // Two motor-temp channels, each complete: fuse, clamp, bias, filter, buffer.
  "TEMP / MOTOR-TEMP": [
    ["FMT1", "DMT1", "RMT1P", "RMT1S", "CMT1F", "UMT1"],
    ["FMT2", "DMT2", "RMT2P", "RMT2S", "CMT2F", "UMT2"],
  ],
  "TEMP / MOD-NTC": [
    ["RSNUP", "CSNUF", "RSNVP", "CSNVF", "RSNWP", "CSNWF"],
  ],
  // CAN from the MCU outward: transceiver | choke, termination, ESD.
  "COMMS / CAN-FD#": [
    ["UCAN#", "CCAN#"],
    ["LCAN#", "RCT#A", "RCT#B", "CCT#", "TVSC#"],
  ],
};

// ---- emit ---------------------------------------------------------------------------------
const files = [];
let totalComps = 0, totalLabels = 0;

const BOARDS = { power: [], capbank: [], disch: [], card: [] };
const PAGE_ORDER = [
  "power-DC-INPUT", "power-DC-LINK", "power-PHASE-U", "power-PHASE-V", "power-PHASE-W",
  "capbank-CAP-BANK",
  "power-GATE-POWER", "power-HV-SENSING", "power-LV-POWER", "power-CONTROL-IF",
  "disch-DISCHARGE",
  "card-CONTROL", "card-SBC", "card-SAFETY", "card-RESOLVER", "card-PHASE-SENSE",
  "card-VDC-RECEIVE", "card-TEMP", "card-COMMS", "card-VEHICLE-IF", "card-CARD-IF",
];
for (const file of readdirSync(SRC).filter((f) => f.endsWith(".json")).sort()) {
  const pg = JSON.parse(readFileSync(join(SRC, file), "utf8"));
  const side = pg.page.split("-")[0];   // power / capbank / disch / card — side-prefixed
  if (BOARDS[side]) BOARDS[side].push(pg);
}
for (const side of Object.keys(BOARDS))
  BOARDS[side].sort((a, b) => PAGE_ORDER.indexOf(a.page) - PAGE_ORDER.indexOf(b.page));
const SIDE_TITLE = {
  power: "Traction Inverter 220 kW pk - Power board (SiC 3-phase, 2-level)",
  capbank: "Traction Inverter - Cap bank (laminated busbar + 16x 20 uF film can array)",
  disch: "Traction Inverter - Discharge board (bolt-on bleeder + active discharge)",
  card: "Traction Inverter - Control Card (S32K396 + FS26, ASIL D)",
};
const SHEET_TITLES = {
  power: "Traction Inverter 220 kW — Power board (3x EconoDUAL 3 SiC)",
  capbank: "Traction Inverter — Cap bank (busbar assembly, 320 uF)",
  disch: "Traction Inverter — Discharge board (bolt-on, XM3 pattern)",
  card: "Traction Inverter — Control Card (S32K396 + FS26, ASIL D)",
};
const SHEET_IDENT = {
  power: { sku: "220 kW pk", board: "Power (HV)", sheet: "1 of 4", cells: "3x HCS600FH120D3C1 (1200 V/600 A EconoDUAL 3) + gate drive + iso sensing (link cans: sheet 2)" },
  capbank: { sku: "220 kW pk", board: "Cap bank (HV, busbar)", sheet: "2 of 4", cells: "laminated busbar + 16x 20 uF/1100 V film cans = 320 uF — NOT an FR4 PCB; discharge board bolts across it" },
  disch: { sku: "220 kW pk", board: "Discharge (HV, bolt-on)", sheet: "3 of 4", cells: "passive 66k bleeder (56 s nom / 65 s worst) + commanded active path (1.88k, 1.6 s) — NEVER energize without this board fitted" },
  card: { sku: "220 kW pk", board: "Control card (LV)", sheet: "4 of 4", cells: "S32K396 lockstep MCU + FS2633D ASIL-D SBC + resolver AFE + hall AFE + CAN-FD + safety chain" },
};

for (const [side, pgs] of Object.entries(BOARDS)) {
  if (!pgs.length) continue;
  const SINGLE = side === "disch" || side === "capbank";   // small sheets use the compact footer
  const page = { page: `traction-${side}`, title: SIDE_TITLE[side],
    total: pgs.reduce((a, p) => a + p.total, 0),
    nc: Object.assign({}, ...pgs.map((p) => p.nc)) };
  const blocks = pgs.flatMap((p) => p.block_order.map((b, i) => ({
    title: `${p.page.replace(/^(power|capbank|disch|card)-/, "")} / ${b}`, comps: p.chunks[i] })));

  for (const b of blocks) {
    b.items = b.comps.map((c) => ({ c, s: shapeOf(c) }));
    b.items.sort((a, z) => (z.s.cat === "IC") - (a.s.cat === "IC") || a.c.designator.localeCompare(z.c.designator));
    // Try every sensible column count with items balanced across them; keep the smallest frame
    // area (mildly penalising extreme aspect ratios).
    const totalH = b.items.reduce((a, i) => a + i.s.h, 0);
    let bestL = null;
    for (let k = 1; k <= Math.min(6, b.items.length); k++) {
      const target = Math.ceil(totalH / k);
      const cols = []; let col = [], h = 0;
      for (const it of b.items) {
        if (col.length && h + it.s.h > target && cols.length < k - 1) { cols.push(col); col = []; h = 0; }
        col.push(it); h += it.s.h;
      }
      if (col.length) cols.push(col);
      let x = 0, maxH = 0; const placed = [];
      for (const c of cols) {
        // Every symbol in a column starts at the SAME x, set by the column's widest left label.
        const mlw = Math.max(...c.map((i) => i.s.lw));
        const cw = mlw + Math.max(...c.map((i) => i.s.w - i.s.lw));
        let y = 0;
        for (const it of c) { placed.push({ it, x, y, clw: mlw }); y += it.s.h; }
        maxH = Math.max(maxH, y); x += cw + COLGAP;
      }
      const w = x - COLGAP + 2 * SECPAD, hh = maxH + 2 * SECPAD + SECTITLE;
      const score = w * hh * (1 + Math.abs(Math.log((w / hh) / 1.3)) * 0.15);
      if (!bestL || score < bestL.score) bestL = { placed, w, h: hh, score };
    }
    const plan = HAND[b.title.replace(/\d+/g, "#")] ?? HAND[b.title];
    if (plan) {
      const idx = (b.title.match(/(\d+)\s*$/) || [])[1] ?? "";
      const left = new Map(b.items.map((i) => [i.c.designator, i]));
      const cols = plan.map((col) => col.map((pat) => left.get(pat.replace(/#/g, idx))).filter(Boolean));
      for (const col of cols) for (const it of col) left.delete(it.c.designator);
      if (left.size) cols.push([...left.values()]);          // never drop a part
      let x = 0, maxH = 0;
      for (const c of cols) {
        if (!c.length) continue;
        const mlw = Math.max(...c.map((i) => i.s.lw));
        const cw = mlw + Math.max(...c.map((i) => i.s.w - i.s.lw));
        let y = 0;
        for (const it of c) { it.x = x; it.y = y; it.clw = mlw; y += it.s.h; }
        maxH = Math.max(maxH, y); x += cw + COLGAP;
      }
      bestL = { w: x - COLGAP + 2 * SECPAD, h: maxH + 2 * SECPAD + SECTITLE };
    } else
    for (const { it, x, y, clw } of bestL.placed) { it.x = x; it.y = y; it.clw = clw; }
    // Height rounded UP to the Y grid: grid-aligned tops AND one uniform vertical gap.
    b.w = bestL.w; b.h = Math.ceil(bestL.h / YGRID) * YGRID;
  }

  // Skyline placement on a COLUMN GRID, frames in logical order so the signal flow survives.
  const GRID = 500;
  const gsnap = (v) => Math.ceil(v / GRID) * GRID;
  const widths = blocks.map((b) => b.w).sort((m, n) => m - n);
  const COLW = Math.max(gsnap(widths[Math.floor(widths.length * 0.4)] + SECGAP), 2500);
  for (const b of blocks) {
    b.span = Math.max(1, Math.ceil((b.w + SECGAP) / COLW));
    const full = b.span * COLW - SECGAP;
    b.pad = Math.round((full - b.w) / 2);          // centre the content in its widened frame
    b.w = full;
  }
  const RAGGED_DIV = 2000;
  const BAND = 16000;
  const famCount = new Map(), famSpan = new Map();
  for (const b of blocks) {
    const f = String(b.title).split(" / ")[0];
    famCount.set(f, (famCount.get(f) ?? 0) + 1);
    famSpan.set(f, Math.max(famSpan.get(f) ?? 1, b.span));
  }
  const capFor = (mul) => new Map([...famCount].map(([f, n]) =>
    [f, Math.max(famSpan.get(f), Math.round(Math.sqrt(n) * mul))]));
  const orderings = [
    blocks,
    (() => {
      const byFam = new Map();
      for (const b of blocks) {
        const f = String(b.title).split(" / ")[0];
        if (!byFam.has(f)) byFam.set(f, []);
        byFam.get(f).push(b);
      }
      return [...byFam.values()].flatMap((g) => [...g].sort((m, n) => n.h - m.h));
    })(),
    (() => {
      const byFam = new Map();
      for (const b of blocks) {
        const f = String(b.title).split(" / ")[0];
        if (!byFam.has(f)) byFam.set(f, []);
        byFam.get(f).push(b);
      }
      return [...byFam.values()].flatMap((g) => [...g].sort((m, n) => n.w - m.w || n.h - m.h));
    })(),
  ];
  const runPack = (NC, famCap, order) => {
    const colH = new Array(NC).fill(MARGIN);
    const out = [], famAt = new Map();
    for (const b of order) {
      const fam = String(b.title).split(" / ")[0];
      const cands = [];
      for (let c = 0; c + b.span <= NC; c++) cands.push({ c, y: Math.max(...colH.slice(c, c + b.span)) });
      const minY = Math.min(...cands.map((k) => k.y));
      const seen = famAt.get(fam);
      const cap = famCap.get(fam) ?? 1;
      const dist = (c) => {
        if (!seen) return c;
        const lo = Math.min(seen.lo, c), hi = Math.max(seen.hi, c + b.span - 1);
        if (hi - lo + 1 <= cap) return 0;
        return c < seen.lo ? seen.lo - c : c - seen.hi;
      };
      const best = cands.filter((k) => k.y <= minY + BAND).sort((m, n) =>
        dist(m.c) - dist(n.c) || m.y - n.y || m.c - n.c)[0];
      famAt.set(fam, seen
        ? { lo: Math.min(seen.lo, best.c), hi: Math.max(seen.hi, best.c + b.span - 1) }
        : { lo: best.c, hi: best.c + b.span - 1 });
      const Y = Math.ceil(best.y / YGRID) * YGRID;
      out.push({ b, X: MARGIN + best.c * COLW, Y });
      for (let k = best.c; k < best.c + b.span; k++) colH[k] = Y + b.h + SECGAP;
    }
    const H = Math.max(...colH), W = MARGIN + NC * COLW - SECGAP + MARGIN;
    const ragged = H - Math.min(...colH);
    const sheetArea = W * (H + MARGIN + 800);
    const frameArea = blocks.reduce((a2, b2) => a2 + b2.w * b2.h, 0);
    const aspect = W / (H + MARGIN + 800);
    const famX = new Map();
    for (const { b: bb, X } of out) {
      const f = String(bb.title).split(" / ")[0];
      const e = famX.get(f) ?? [Infinity, -Infinity];
      famX.set(f, [Math.min(e[0], X), Math.max(e[1], X)]);
    }
    const famArea = new Map();
    for (const { b: bb } of out) {
      const f = String(bb.title).split(" / ")[0];
      famArea.set(f, (famArea.get(f) ?? 0) + bb.w * bb.h);
    }
    const spreadOver = Math.max(...[...famX].map(([f, [lo, hi]]) =>
      (hi - lo) / W - (0.35 + 0.9 * (famArea.get(f) / frameArea))));
    const seq = [...out].sort((a, b) => (a.X - b.X) || (a.Y - b.Y));
    let runs = 0, prevFam = null;
    for (const { b: bb } of seq) {
      const f = String(bb.title).split(" / ")[0];
      if (f !== prevFam) runs++;
      prevFam = f;
    }
    const frag = runs - famX.size;
    const voidFrac = largestVoid(out.map(({ b: bb, X, Y }) =>
      ({ x0: X, y0: Y, x1: X + bb.w, y1: Y + bb.h })), W, H + MARGIN + 800).frac;
    // Denser is only better while the notes panels still fit somewhere (a gate, not a weight).
    const notesFits = voidCandidates(out.map(({ b: bb, X, Y }) =>
      ({ x0: X, y0: Y, x1: X + bb.w, y1: Y + bb.h })), W, H + MARGIN + 800, 4)
      .some((v) => v.x1 - v.x0 > 5200 && v.y1 - v.y0 > 4200 && v.y1 > (H + MARGIN + 800) * 0.4);
    // void gate: 9 % was calibrated when the tallest symbol was ~34 pins; the full 49-pin
    // FS26 monolith (rev A.4) makes <=9 % unreachable at any NC — 12 % keeps the intent
    // (notes panels still land in the void) without an impossible bar.
    const usable = aspect >= 1.15 && aspect <= 1.95 && spreadOver <= 0 && voidFrac <= 0.12
      && (notesFits || SINGLE);
    const soft = (sheetArea / frameArea) * 10 + ragged / RAGGED_DIV + voidFrac * 60
      + frag * 1
      + Math.max(0, spreadOver) * 300
      + (notesFits ? 0 : 500)
      + (aspect >= 1.15 && aspect <= 2.1 ? 0 : 1000);
    const score = usable ? soft : Infinity;
    return { NC, out, colH, W, H, score, soft, aspect };
  };
  const minNC = Math.max(...blocks.map((b) => b.span));
  let pick = null, fallback = null;
  for (const order of orderings) {
    for (const mul of [1.0, 1.3, 1.6, 2.0, 2.5, 3.0, 4.0]) {
      const famCap = capFor(SINGLE ? 1e9 : mul);
      for (let NC = minNC; NC <= minNC + 32; NC++) {
        const r = runPack(NC, famCap, order);
        if (r.score < Infinity && (!pick || r.score < pick.score)) pick = r;
        if (!fallback || r.soft < fallback.soft) fallback = r;
      }
    }
  }
  pick = pick ?? fallback;
  {
    const fa = blocks.reduce((a, b) => a + b.w * b.h, 0);
    const sa = pick.W * (pick.H + MARGIN + 800);
    console.error(`   [layout] ${page.page.padEnd(15)} NC=${String(pick.NC).padStart(2)} `
      + `fill=${(100 * fa / sa).toFixed(1)}% ragged=${pick.H - Math.min(...pick.colH)} `
      + `aspect=${pick.aspect.toFixed(2)} ${pick.score < Infinity ? "gated" : "FALLBACK"}`);
  }
  const NC = pick.NC;
  for (const { b, X, Y } of pick.out) { b.X = X; b.Y = Y; }
  const sheetW = snap(MARGIN + NC * COLW - SECGAP + MARGIN);
  let sheetH = snap(Math.max(...blocks.map((b) => b.Y + b.h)) + MARGIN + 800);

  const ident = SHEET_IDENT[side];

  let body = "", nLabels = 0, nNC = 0;
  const GL = (net, x, y, dir) => {                     // dir: 0 right, 2 left, 1 up, 3 down
    nLabels++;
    // Plain "Text Label", NOT GLabel: EasyEDA renders imported global labels as fixed-width net
    // ports that overflow long names. Each board is one sheet; labels merge by name across it.
    return `Text Label ${x} ${y} ${dir}    45   ~ 0\n${net}\n`;
  };

  // Notes panels fill the top voids: sheet index, net-naming legend, and (power sheet) the
  // ASIL-D safety concept + discharge verification rows.
  {
    const used = blocks.map((b) => ({ x0: b.X, y0: b.Y, x1: b.X + b.w, y1: b.Y + b.h }));
    const PAD = 500, LH = 300, CW = 4200, HEAD = 160 + 260 + Math.round(LH * 1.4);
    const drawPanel = (V, title, sub, rows, footer, fixed) => {
      const px0 = snap(V.x0 + PAD), py0 = snap(V.y0 + PAD);
      const px1 = snap(V.x1 - PAD), maxY = snap(V.y1 - PAD);
      const perCol = Math.floor((maxY - py0 - HEAD - LH - Math.round(PAD / 2)) / LH);
      const cw = Math.max(2600, Math.min(CW, px1 - px0 - 460));
      const maxCols = Math.floor((px1 - px0 - 400) / cw);
      if (perCol < 2 || maxCols < 1) return null;
      const ncols = Math.min(maxCols, Math.ceil(rows.length / perCol));
      const cap = ncols * perCol, over = rows.length > cap;
      const shown = rows.slice(0, over ? cap - 1 : rows.length);
      const cell = [...shown, ...(over ? [`+ ${rows.length - shown.length} more`] : [])];
      const nRow = Math.min(perCol, Math.max(1, Math.ceil(cell.length / ncols)));
      const pw = 60 + ncols * cw + 200;
      // ABUT the notes to the CONTENT side of their void: slack goes to the paper edge.
      const bx0 = fixed ? fixed.x0 : px0;
      const px1b = fixed ? fixed.x1 : snap(Math.min(px1, bx0 + pw));
      const py1 = snap(py0 + HEAD + nRow * LH + LH + Math.round(PAD / 2));
      body += `Wire Notes Line\n\t${bx0} ${py0} ${px1b} ${py0}\nWire Notes Line\n\t${px1b} ${py0} ${px1b} ${py1}\n`
        + `Wire Notes Line\n\t${px1b} ${py1} ${bx0} ${py1}\nWire Notes Line\n\t${bx0} ${py1} ${bx0} ${py0}\n`;
      body += `Text Notes ${bx0 + 60} ${py0 + 160} 0    79   ~ 16\n${title}\n`;
      body += `Text Notes ${bx0 + 60} ${py0 + 420} 0    60   ~ 0\n${sub}\n`;
      cell.forEach((t, i) => {
        const cx = bx0 + 60 + Math.floor(i / nRow) * cw, cy = py0 + HEAD + (i % nRow) * LH;
        body += `Text Notes ${snap(cx)} ${snap(cy)} 0    60   ~ 0\n${t}\n`;
      });
      if (footer) body += `Text Notes ${bx0 + 60} ${snap(py1 - 200)} 0    60   ~ 0\n${footer}\n`;
      return { x0: bx0, y0: py0, x1: px1b, y1: py1 };
    };
    const fams = new Map();
    for (const b of blocks) {
      const f = String(b.title).split(" / ")[0];
      fams.set(f, (fams.get(f) ?? 0) + 1);
    }
    const famRows = [...fams].sort((a, b2) => b2[1] - a[1] || a[0].localeCompare(b2[0]))
      .map(([f, n]) => `${f}   -   ${n} section${n > 1 ? "s" : ""}`);
    const pickVoid = (occupied) => {
      let cands = voidCandidates(occupied, sheetW, sheetH, 4)
        .filter((v) => v.x1 - v.x0 > 5200 && v.y1 - v.y0 > 2200 && v.y1 > sheetH * 0.4);
      if (!cands.length) return { x0: 0, y0: 0, x1: 0, y1: 0 };
      const tall = cands.filter((v) => v.y1 - v.y0 > 6400);
      cands = tall.length ? tall : cands;
      const right = cands.filter((v) => v.x1 > sheetW * 0.55);
      return (right.length ? right : cands).sort((a, b) =>
        (2 * b.x1 / sheetW + b.y1 / sheetH) - (2 * a.x1 / sheetW + a.y1 / sheetH))[0];
    };
    if (SINGLE) {
      body += `Text Notes ${MARGIN + 100} ${sheetH - 700} 0    60   ~ 12\n`
        + `${ident.sku} ${ident.board} - ${ident.sheet}   ·   rev ${REV}   ·   ${blocks.length} sections   ·   ${page.total} components\n`;
      body += `Text Notes ${MARGIN + 100} ${sheetH - 400} 0    50   ~ 0\n`
        + `NET NAMING: U<ref>_<PIN> = node at that IC pin   ·   R<stem>_M = series-pair midpoint   ·   R<stem>_<nm> = tap between R<stem>n/m   ·   all others are explicit design nets   ·   same name on another sheet = same net, joined only at the named connector/stud/tab\n`;
    } else {
      const v1 = pickVoid(used);
      const p1 = drawPanel(v1, "SHEET INDEX",
        `${ident.sku} ${ident.board} - ${ident.sheet}`, famRows,
        `rev ${REV}   -   ${blocks.length} sections   -   ${page.total} components`);
      if (p1) {
        used.push(p1);
        const below = { x0: p1.x0 - PAD, y0: p1.y1 + Math.round(PAD / 2), x1: p1.x1 + PAD, y1: v1.y1 };
        const legend = (V, fixed) => drawPanel(V, "NET NAMING",
          "internal junctions are named for what they join", [
            "U<ref>_<PIN>     node at that IC pin        e.g. UIVDC_VINP",
            "R<stem>_M        midpoint of a series pair  e.g. RBLA_M",
            "R<stem>_<nm>     tap between R<stem>n/m     e.g. RVDD_12",
            "all others are explicit design nets",
            "same name on another sheet = same net, joined ONLY at the",
            "named connector/stud/tab (see INTERBOARD LINKS, design basis)",
          ], "", fixed);
        const l1 = legend(below, { x0: p1.x0, x1: p1.x1 }) || legend(pickVoid(used));
        if (l1) used.push(l1);
        // Safety + discharge verification: the two review-critical stories, printed ON the sheet
        // so the drawing is not the only artifact a reviewer holds.
        // One combined panel: the safety concept AND the discharge verification share a frame so
        // neither can silently vanish when voids run short — the discharge check is a review
        // requirement, not decoration.
        // F62: a single filtered void pick let this panel vanish silently when the sheet
        // grew. Now every void candidate is tried largest-first, and a power sheet WITHOUT
        // the panel refuses to build — the review panel is a ship requirement.
        let s1 = null;
        if (side === "power") {
          const sRows = [...SAFETY_ROWS, "--- DISCHARGE ---", ...DISCHARGE_ROWS];
          const cands = voidCandidates(used.map((u) => ({ x0: u.x0, y0: u.y0, x1: u.x1, y1: u.y1 })), sheetW, sheetH + MARGIN + 800, 16)
            .filter((v) => v.x1 - v.x0 > 5200 && v.y1 - v.y0 > 2200)
            .sort((p, q) => (q.x1 - q.x0) * (q.y1 - q.y0) - (p.x1 - p.x0) * (p.y1 - p.y0));
          for (const v of [pickVoid(used), ...cands]) {
            s1 = drawPanel(v,
              "ASIL-D SAFETY CONCEPT + DC-LINK DISCHARGE VERIFICATION",
              "safe state = 3-phase-open (ASC above overspeed); discharge vs ECE R100 / ISO 6469",
              sRows, "");
            if (s1) break;
          }
          if (!s1) {
            // No interior void fits: extend the sheet downward and anchor the panel there.
            // The $Descr size is emitted after this point, so the growth is consistent.
            const need = 160 + 260 + Math.round(300 * 1.4) + 9 * 300 + 2 * 500;
            const v = { x0: MARGIN + 400, y0: sheetH - 400, x1: MARGIN + 400 + 10600, y1: sheetH - 400 + need + 700 };
            sheetH = snap(v.y1 + 900);
            s1 = drawPanel(v,
              "ASIL-D SAFETY CONCEPT + DC-LINK DISCHARGE VERIFICATION",
              "safe state = 3-phase-open (ASC above overspeed); discharge vs ECE R100 / ISO 6469",
              sRows, "");
          }
          if (!s1) throw new Error("power sheet: ASIL-D + DISCHARGE panel does not fit any void (F62 gate)");
        }
        if (s1) used.push(s1);
      }
    }
  }

  for (const b of blocks) {
    const x0 = b.X, y0 = b.Y, x1 = snap(b.X + b.w), y1 = snap(b.Y + b.h);
    body += `Wire Notes Line\n\t${x0} ${y0} ${x1} ${y0}\nWire Notes Line\n\t${x1} ${y0} ${x1} ${y1}\n`
      + `Wire Notes Line\n\t${x1} ${y1} ${x0} ${y1}\nWire Notes Line\n\t${x0} ${y1} ${x0} ${y0}\n`;
    body += `Text Notes ${x0 + 60} ${y0 + 160} 0    79   ~ 16\n${b.title}\n`;

    for (const it of b.items) {
      const { c, s } = it;
      const ox = snap(b.X + SECPAD + (b.pad ?? 0) + it.x + (it.clw ?? s.lw) + STUB);
      const oy = snap(b.Y + SECTITLE + SECPAD + it.y);
      const { mpn, lc } = partOf(c.designator, c.value);
      if (s.cat === "TERM") {
        const p0 = c.pins[0];
        const cx = snap(ox + 100), cyy = snap(oy + ROW / 2);
        const nm = termLib(p0.pin_number, p0.name);
        body += `$Comp\nL ${LIB_NAME}:${nm} ${c.designator}\nU 1 1 ${nextId()}\nP ${cx} ${cyy}\n`
          + `F 0 "${c.designator}" H ${cx} ${cyy - 160} 50  0000 C CNN\n`
          + `F 1 "${valueText(c.designator, c.value)}" H ${cx} ${cyy + 170} 50  0000 C CNN\n`
          + `F 2 "${fpField(c)}" H ${cx} ${cyy} 50  0001 C CNN\nF 3 "~" H ${cx} ${cyy} 50  0001 C CNN\n`
          + `F 4 "${lc.lcsc ?? lc.status}" H ${cx} ${cyy} 50  0001 C CNN "LCSC"\n`
          + `F 5 "${mpn}" H ${cx} ${cyy} 50  0001 C CNN "MPN"\n`
          + `\t1    ${cx} ${cyy}\n\t1    0    0    -1  \n$EndComp\n`;
        if (p0.signal_name) {
          const pxx = snap(cx - 100), ex = snap(pxx - STUB);
          body += `Wire Wire Line\n\t${pxx} ${cyy} ${ex} ${cyy}\n` + GL(p0.signal_name, ex, cyy, 2);
        }
      } else if (s.cat !== "IC") {
        const cx = snap(ox + 250), cyy = snap(oy + ROW / 2);
        const nm = passiveLib(s.cat, s.nums[0] ?? "1", s.nums[1] ?? "2");
        body += `$Comp\nL ${LIB_NAME}:${nm} ${c.designator}\nU 1 1 ${nextId()}\nP ${cx} ${cyy}\n`
          + `F 0 "${c.designator}" H ${cx} ${cyy - 160} 50  0000 C CNN\n`
          + `F 1 "${valueText(c.designator, c.value)}" H ${cx} ${cyy + 170} 50  0000 C CNN\n`
          + `F 2 "${fpField(c)}" H ${cx} ${cyy} 50  0001 C CNN\nF 3 "~" H ${cx} ${cyy} 50  0001 C CNN\n`
          + `F 4 "${lc.lcsc ?? lc.status}" H ${cx} ${cyy} 50  0001 C CNN "LCSC"\n`
          + `F 5 "${mpn}" H ${cx} ${cyy} 50  0001 C CNN "MPN"\n`
          + `\t1    ${cx} ${cyy}\n\t1    0    0    -1  \n$EndComp\n`;
        const sides = [[s.nums[0], snap(cx - 250), -1], [s.nums[1], snap(cx + 250), 1]];
        for (const [pn, pxx, dir] of sides) {
          const p = c.pins.find((q) => String(q.pin_number) === String(pn));
          if (!p || !p.signal_name) continue;
          const ex = snap(pxx + dir * STUB);
          body += `Wire Wire Line\n\t${pxx} ${cyy} ${ex} ${cyy}\n`;
          body += GL(p.signal_name, ex, cyy, dir < 0 ? 2 : 0);
        }
      } else {
        const cx = snap(ox + s.halfW + 150), cyy = snap(oy + s.TEXT + s.topExtra + s.halfH);
        const nm = icLib(c.mpn || c.value, unionPins(c));
        body += `$Comp\nL ${LIB_NAME}:${nm} ${c.designator}\nU 1 1 ${nextId()}\nP ${cx} ${cyy}\n`
          + `F 0 "${c.designator}" H ${cx - s.halfW - 100} ${cyy - s.halfH - 100} 50  0000 R CNN\n`
          + `F 1 "${valueText(c.designator, c.value)}" H ${cx - s.halfW - 100} ${cyy + s.halfH + 130} 50  0000 R CNN\n`
          + `F 2 "${fpField(c)}" H ${cx} ${cyy} 50  0001 C CNN\nF 3 "~" H ${cx} ${cyy} 50  0001 C CNN\n`
          + `F 4 "${lc.lcsc ?? lc.status}" H ${cx} ${cyy} 50  0001 C CNN "LCSC"\n`
          + `F 5 "${mpn}" H ${cx} ${cyy} 50  0001 C CNN "MPN"\n`
          + `\t1    ${cx} ${cyy}\n\t1    0    0    -1  \n$EndComp\n`;
        const bound = new Map(c.pins.map((p) => [String(p.pin_number), p.signal_name]));
        const g = s.groups;
        const noConn = (x, y) => { body += `NoConn ~ ${x} ${y}\n`; nNC++; };
        g.left.forEach((p, i) => {
          const pxx = snap(cx - s.halfW - 150), py = snap(cyy - s.halfH + PITCH + i * PITCH);
          const sig = bound.get(String(p.pin_number));
          if (!sig) return noConn(pxx, py);
          const ex = snap(pxx - STUB);
          body += `Wire Wire Line\n\t${pxx} ${py} ${ex} ${py}\n` + GL(sig, ex, py, 2);
        });
        g.right.forEach((p, i) => {
          const pxx = snap(cx + s.halfW + 150), py = snap(cyy - s.halfH + PITCH + i * PITCH);
          const sig = bound.get(String(p.pin_number));
          if (!sig) return noConn(pxx, py);
          const ex = snap(pxx + STUB);
          body += `Wire Wire Line\n\t${pxx} ${py} ${ex} ${py}\n` + GL(sig, ex, py, 0);
        });
        g.top.forEach((p, i) => {
          const sig = bound.get(String(p.pin_number)); if (!sig) return;
          const pxx = snap(cx - s.halfW + PITCH + i * PITCH), py = snap(cyy - s.halfH - 150);
          const ey = snap(py - STUB);
          body += `Wire Wire Line\n\t${pxx} ${py} ${pxx} ${ey}\n` + GL(sig, pxx, ey, 1);
        });
        g.bottom.forEach((p, i) => {
          const sig = bound.get(String(p.pin_number)); if (!sig) return;
          const pxx = snap(cx - s.halfW + PITCH + i * PITCH), py = snap(cyy + s.halfH + 150);
          const ey = snap(py + STUB);
          body += `Wire Wire Line\n\t${pxx} ${py} ${pxx} ${ey}\n` + GL(sig, pxx, ey, 3);
        });
      }
      totalComps++;
    }
  }
  totalLabels += nLabels;

  const sch = `EESchema Schematic File Version 4\nEELAYER 30 0\nEELAYER END\n`
    + `$Descr User ${sheetW} ${sheetH}\nencoding utf-8\nSheet 1 1\n`
    + `Title "${SHEET_TITLES[side]}"\n`
    + `Date "${DATE}"\nRev "${REV}"\n`
    + `Comp "Traction Inverter ${ident.sku} - ${ident.board}, sheet ${ident.sheet}"\n`
    + `Comment1 "Inverter = Power board + Cap bank busbar + bolt-on Discharge board (all HV) + Control card (LV); safe state holds with any link lost"\n`
    + `Comment2 "Content: ${ident.cells}"\n`
    + `Comment3 "${blocks.length} functional sections - ${page.total} components - cross-section links are net labels; wires are pin stubs only"\n`
    + `Comment4 "Every component carries MPN + LCSC fields (CLASS = buy to class spec, ALT footprint-compatible second source in BOM)"\n$EndDescr\n${body}$EndSCHEMATC\n`;
  writeFileSync(join(OUT, `${page.page}.sch`), sch);
  files.push(page.page);
  console.log(`${page.page.padEnd(18)} ${String(page.total).padStart(3)} comps · ${blocks.length} sections · ${nLabels} labels · ${nNC} no-connects · ${sheetW}×${sheetH} mil`);
}

// Root sheet referencing both pages, so EasyEDA imports the set in one go.
{
  let root = "", sx = 1000, sy = 1000;
  files.forEach((name, i) => {
    const col = i % 2, rowi = Math.floor(i / 2);
    const X = sx + col * 5200, Y = sy + rowi * 2200;
    root += `$Sheet\nS ${X} ${Y} 4200 1400\nU ${nextId()}\n`
      + `F0 "${SIDE_TITLE[name.replace("traction-", "")] ?? name}" 70\nF1 "${name}.sch" 70\n$EndSheet\n`;
  });
  const rootSch = `EESchema Schematic File Version 4\nEELAYER 30 0\nEELAYER END\n`
    + `$Descr User 12000 8000\nencoding utf-8\nSheet 1 1\n`
    + `Title "Traction Inverter 220 kW pk - schematic set"\nDate "${DATE}"\nRev "${REV}"\n`
    + `Comp "Traction Inverter"\nComment1 "Power board sheet 1 - Cap bank sheet 2 - Discharge board sheet 3 - Control card sheet 4"\n`
    + `Comment2 "800 V-class, 120 kW cont / 220 kW pk, ASIL-D-capable architecture"\nComment3 ""\nComment4 ""\n$EndDescr\n`
    + `${root}$EndSCHEMATC\n`;
  writeFileSync(join(OUT, "traction.sch"), rootSch);
}

const rawLib = `EESchema-LIBRARY Version 2.4\n#encoding utf-8\n${[...lib.values()].join("")}#\n#End Library\n`;
const proText = `update=Date\nversion=1\nlast_client=eeschema\n[general]\nversion=1\n[eeschema]\nversion=1\nLibDir=\n[eeschema/libraries]\nLibName1=${LIB_NAME}\n`;
writeFileSync(join(OUT, `${LIB_NAME}.lib`), mirrorLibY(rawLib));
writeFileSync(join(OUT, `${LIB_NAME}.dcm`), `EESchema-DOCLIB  Version 2.0\n#\n#End Doc Library\n`);
writeFileSync(join(OUT, "traction.pro"), proText);
// Round-16 gap closure (F163 residual): KiCad 5 opens a project without a sym-lib-table through its remap dialog
// and KiCad 6+ / kicad-cli cannot resolve "traction-r1:" at all — ship the table and the cache library that
// every KiCad since 5.0 reads first, so an unattended open or a CLI netlist export resolves every symbol.
const SYM_TABLE = `(sym_lib_table\n  (lib (name ${LIB_NAME})(type Legacy)(uri \${KIPRJMOD}/${LIB_NAME}.lib)(options "")(descr "Traction Inverter GEN-1 symbols (generated)"))\n)\n`;
writeFileSync(join(OUT, "sym-lib-table"), SYM_TABLE);
writeFileSync(join(OUT, "traction-cache.lib"), mirrorLibY(rawLib));
writeFileSync(join(OUT, "README.txt"),
  `EASYEDA-IMPORT VARIANT (round 15, A13-R05). The symbol library is pre-mirrored about Y because EasyEDA's\nKiCad-legacy importer places pins at (ux+px, uy+py) and ignores the "1 0 0 -1" orientation matrix.\nDo NOT open this folder in native KiCad: use ../traction-native/ (same sheets, un-mirrored library).\nBoth variants are verified pin-by-pin with their consumer's placement rule (calculations/kicad5-verify.mjs).\n`);
// Round 15 (A13-R05): native KiCad 5 applies the orientation matrix itself, so it needs the UN-mirrored library
// with the very same sheets; ship it as a separate, clearly named folder and zip.
const OUT_NATIVE = join(ROOT, "kicad5/traction-native");
mkdirSync(OUT_NATIVE, { recursive: true });
for (const f of files) writeFileSync(join(OUT_NATIVE, `${f}.sch`), readFileSync(join(OUT, `${f}.sch`)));
writeFileSync(join(OUT_NATIVE, "traction.sch"), readFileSync(join(OUT, "traction.sch")));
writeFileSync(join(OUT_NATIVE, `${LIB_NAME}.lib`), rawLib);
writeFileSync(join(OUT_NATIVE, `${LIB_NAME}.dcm`), `EESchema-DOCLIB  Version 2.0\n#\n#End Doc Library\n`);
writeFileSync(join(OUT_NATIVE, "traction.pro"), proText);
writeFileSync(join(OUT_NATIVE, "sym-lib-table"), SYM_TABLE);
writeFileSync(join(OUT_NATIVE, "traction-cache.lib"), rawLib);
writeFileSync(join(OUT_NATIVE, "README.txt"),
  `NATIVE KICAD 5 VARIANT (round 15, A13-R05). Same sheets as ../traction/, un-mirrored symbol library: KiCad 5\napplies each component's orientation matrix to the library pin before adding its position. Open traction.sch\nin KiCad 5.1.x. For EasyEDA import use ../traction/ instead. Both are verified by calculations/kicad5-verify.mjs.\n`);
const FP_NOTE = `\nFOOTPRINTS (round 22, F210): every symbol's F2 field is "${FP_LIB}:<name>" and resolves in ${FP_LIB}.pretty, shipped here\nwith fp-lib-table (patterns copied verbatim from the KiCad 10 libraries or drawn from the archived datasheets — see\n${FP_LIB}.pretty/README.md, SOURCES.json and MANIFEST.md). Off-board parts (the LEM sensors USNSU/V/W on the busbar) carry\nno footprint on purpose. calculations/kicad-sch-verify.mjs proves every symbol pin has a pad of its number.\n`;
for (const d of [OUT, OUT_NATIVE]) appendFileSync(join(d, "README.txt"), FP_NOTE);
// Package for EasyEDA import as part of GENERATING (zips must not drift behind the sheets).
{
  const { execFileSync } = await import("node:child_process");
  const zip = join(ROOT, "kicad5", "Traction-Inverter-SHIP.zip");
  try { unlinkSync(zip); } catch {}
  const members = [...files.map((f) => join(OUT, `${f}.sch`)),
    join(OUT, `${LIB_NAME}.lib`), join(OUT, `${LIB_NAME}.dcm`),
    join(OUT, "traction.pro"), join(OUT, "traction.sch"), join(OUT, "sym-lib-table"), join(OUT, "traction-cache.lib")];
  const pretty = (d) => readdirSync(join(d, `${FP_LIB}.pretty`)).map((f) => join(d, `${FP_LIB}.pretty`, f));
  execFileSync("touch", ["-t", `${DATE.replace(/-/g, "")}0000`, ...members, join(OUT, "fp-lib-table"), ...pretty(OUT)]);
  execFileSync("zip", ["-qX", "-r", zip, ...members.map((m) => m.slice(OUT.length + 1)), "README.txt", "fp-lib-table", `${FP_LIB}.pretty`], { cwd: OUT });
  console.log("   packaged → kicad5/Traction-Inverter-SHIP.zip (EasyEDA-import variant)");
  const zipN = join(ROOT, "kicad5", "Traction-Inverter-KiCad5-native.zip");
  try { unlinkSync(zipN); } catch {}
  const membersN = [...files.map((f) => join(OUT_NATIVE, `${f}.sch`)), join(OUT_NATIVE, `${LIB_NAME}.lib`), join(OUT_NATIVE, `${LIB_NAME}.dcm`),
    join(OUT_NATIVE, "traction.pro"), join(OUT_NATIVE, "traction.sch"), join(OUT_NATIVE, "sym-lib-table"), join(OUT_NATIVE, "traction-cache.lib"), join(OUT_NATIVE, "README.txt")];
  execFileSync("touch", ["-t", `${DATE.replace(/-/g, "")}0000`, ...membersN, join(OUT_NATIVE, "fp-lib-table"), ...pretty(OUT_NATIVE)]);
  execFileSync("zip", ["-qX", "-r", zipN, ...membersN.map((m) => m.slice(OUT_NATIVE.length + 1)), "fp-lib-table", `${FP_LIB}.pretty`], { cwd: OUT_NATIVE });
  console.log("   packaged → kicad5/Traction-Inverter-KiCad5-native.zip (native KiCad 5 variant)");
}
console.log(`\n${files.length} sheets · ${totalComps} components · ${totalLabels} labels · ${lib.size} symbols → kicad5/traction/`);
