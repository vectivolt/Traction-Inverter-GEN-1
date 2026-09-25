#!/usr/bin/env node
// kicad-sch-gen.mjs — the Traction schematic set in the KiCad 6+ s-expression format (.kicad_sch / .kicad_pro,
// file version 20250114 = KiCad 9, read by KiCad 9 and 10). It TRANSLATES the native KiCad 5 set that
// kicad5-gen.mjs has just written (kicad5/traction-native/: un-mirrored library + sheets) item for item —
// symbols with every property, pin stubs, labels, no-connect flags, section frames, notes panels, title blocks —
// so the legacy and modern sets share one layout engine and one page payload (nothing is re-packed here).
// 1 mil = 0.0254 mm = 254 KiCad IU exactly, so every pin still lands on its stub end.
//
// Output: kicad/traction/  traction.kicad_pro · traction.kicad_sch (root, four sheet instances) ·
//         traction-{power,capbank,disch,card}.kicad_sch · traction.kicad_sym + sym-lib-table · README.txt
//         kicad/Traction-Inverter-KiCad-modern.zip
// Run:    node calculations/kicad-sch-gen.mjs        (after kicad5-gen.mjs; proved by kicad-sch-verify.mjs)
// Self-test only: --out <dir> writes there (no zip); --mutate mcu-mirror|mcu-swap corrupts the MCU on purpose.

import { readFileSync, writeFileSync, mkdirSync, unlinkSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { REV } from "./rev.mjs";
import { loadCircuit } from "./circuit-net.mjs";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const SRC = join(ROOT, "kicad5/traction-native");       // the un-mirrored library is the one KiCad reads
const arg = (k) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : undefined; };
const OUT = resolve(arg("--out") ?? join(ROOT, "kicad/traction"));
const SHIP = !arg("--out");
const MUTATE = arg("--mutate");
if (MUTATE && (!["mcu-mirror", "mcu-swap"].includes(MUTATE) || SHIP)) throw new Error("--mutate mcu-mirror|mcu-swap needs --out <scratch dir>");
const PROJECT = "traction", LIB = "traction";
const MCU = "UMCU";                                     // the one part numbered by BGA ball (calculations/mcu-ballmap.json)
const BOARD_OF = { "traction-power": "power", "traction-capbank": "capbank", "traction-disch": "discharge", "traction-card": "control-card" };

// ---- s-expression helpers ------------------------------------------------------------------------------------
const mm = (mil) => String(Math.round(mil * 254) / 10000);          // <= 4 decimals, no float noise, no -0
const q = (s) => `"${String(s).replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/\n/g, "\\n")}"`;
const uuid = (seed) => {
  const h = createHash("sha1").update(seed).digest("hex");
  return `${h.slice(0, 8)}-${h.slice(8, 12)}-4${h.slice(13, 16)}-${(8 | (parseInt(h[16], 16) & 3)).toString(16)}${h.slice(17, 20)}-${h.slice(20, 32)}`;
};
const font = (size, bold) => `(font (size ${mm(size)} ${mm(size)})${bold ? " (bold yes)" : ""})`;
const effects = (size, hj, vj, hide) => {
  const j = [{ L: "left", R: "right" }[hj], { T: "top", B: "bottom" }[vj]].filter(Boolean).join(" ");
  return `(effects ${font(size)}${j ? ` (justify ${j})` : ""}${hide ? " (hide yes)" : ""})`;
};
const prop = (f) => `(property ${q(f.name)} ${q(f.text)} (at ${mm(f.x)} ${mm(f.y)} ${f.rot}) ${effects(f.size, f.hj, f.vj, f.hide)})`;
const need = (cond, msg) => { if (!cond) throw new Error(msg); };

// ---- legacy library (KiCad 5 .lib) ---------------------------------------------------------------------------
const FIELD = ["Reference", "Value", "Footprint", "Datasheet"];
const SYMS = new Map();
for (const block of readFileSync(join(SRC, "traction-r1.lib"), "utf8").split(/^DEF /m).slice(1)) {
  const lines = block.split("\n");
  const d = lines[0].split(/\s+/);                     // name ref 0 text_offset draw_num draw_name units locked power
  const s = { name: d[0], off: +d[3], showNum: d[4] === "Y", showName: d[5] === "Y", fields: [], draw: [] };
  need(d[6] === "1" && d[8] === "N", `${s.name}: multi-unit or power symbols are not translated`);
  for (const l of lines.slice(1)) {
    const m = l.match(/^F(\d) "([^"]*)" (-?\d+) (-?\d+) (\d+) ([HV]) ([VI]) ([LRC]) ([TBC])[IN][BN]$/);
    if (m) s.fields.push({ name: FIELD[+m[1]], text: m[2], x: +m[3], y: +m[4], size: +m[5], rot: m[6] === "V" ? 90 : 0, hide: m[7] === "I", hj: m[8], vj: m[9] });
    else if (/^[SPCAX] /.test(l)) s.draw.push(l.trim().split(/\s+/));
    else need(/^(DRAW|ENDDRAW|ENDDEF|#.*|)$/.test(l), `${s.name}: unhandled library line "${l}"`);
  }
  SYMS.set(s.name, s);
}

const FILL = { N: "none", F: "outline", f: "background" };
const ETYPE = { P: "passive", W: "power_in", I: "input", O: "output", B: "bidirectional", T: "tri_state", U: "unspecified", w: "power_out", C: "open_collector", E: "open_emitter", N: "no_connect" };
const ORIENT = { R: 0, U: 90, L: 180, D: 270 };
const stroke = (w) => `(stroke (width ${mm(+w)}) (type default))`;
const fill = (f) => { need(FILL[f], `unknown fill ${f}`); return `(fill (type ${FILL[f]}))`; };
// one legacy DRAW line -> { unit_convert, s-expression }
function drawItem(t) {
  switch (t[0]) {
    case "S": return [`${t[5]}_${t[6]}`, `(rectangle (start ${mm(+t[1])} ${mm(+t[2])}) (end ${mm(+t[3])} ${mm(+t[4])}) ${stroke(t[7])} ${fill(t[8])})`];
    case "C": return [`${t[4]}_${t[5]}`, `(circle (center ${mm(+t[1])} ${mm(+t[2])}) (radius ${mm(+t[3])}) ${stroke(t[6])} ${fill(t[7])})`];
    case "P": {
      const n = +t[1], pts = [];
      for (let k = 0; k < n; k++) pts.push(`(xy ${mm(+t[5 + 2 * k])} ${mm(+t[6 + 2 * k])})`);
      return [`${t[2]}_${t[3]}`, `(polyline (pts ${pts.join(" ")}) ${stroke(t[4])} ${fill(t[5 + 2 * n])})`];
    }
    case "A": {
      // KiCad 5 draws an arc the short way between its stored end points; the s-expression arc needs a third
      // point ON the arc, taken at the middle of that short sweep (angles in 0.1 degree, library Y up).
      const [cx, cy, r, a1, a2] = t.slice(1, 6).map(Number);
      const sweep = (((a2 - a1) % 3600) + 3600) % 3600;
      const mid = (sweep <= 1800 ? a1 + sweep / 2 : a1 - (3600 - sweep) / 2) * Math.PI / 1800;
      return [`${t[6]}_${t[7]}`, `(arc (start ${mm(+t[10])} ${mm(+t[11])}) (mid ${mm(cx + r * Math.cos(mid))} ${mm(cy + r * Math.sin(mid))}) (end ${mm(+t[12])} ${mm(+t[13])}) ${stroke(t[8])} ${fill(t[9])})`];
    }
    case "X": {
      need(ETYPE[t[11]] && ORIENT[t[6]] !== undefined && t.length === 12, `pin ${t[1]}: unsupported type/orientation/shape`);
      return [`${t[9]}_${t[10]}`, `(pin ${ETYPE[t[11]]} line (at ${mm(+t[3])} ${mm(+t[4])} ${ORIENT[t[6]]}) (length ${mm(+t[5])}) `
        + `(name ${q(t[1])} (effects ${font(+t[8])})) (number ${q(t[2])} (effects ${font(+t[7])})))`];
    }
  }
}
function libSymbol(s, prefix) {
  const units = new Map();
  for (const t of s.draw) { const [k, item] = drawItem(t); if (!units.has(k)) units.set(k, []); units.get(k).push(item); }
  return [`\t(symbol ${q(prefix + s.name)}`,
    ...(s.showNum ? [] : ["\t\t(pin_numbers (hide yes))"]),
    `\t\t(pin_names (offset ${mm(s.off)})${s.showName ? "" : " (hide yes)"})`,
    "\t\t(exclude_from_sim no) (in_bom yes) (on_board yes)",
    ...s.fields.map((f) => `\t\t${prop(f)}`),
    ...[...units].sort(([a], [b]) => a.localeCompare(b)).flatMap(([k, items]) =>
      [`\t\t(symbol ${q(`${s.name}_${k}`)}`, ...items.map((i) => `\t\t\t${i}`), "\t\t)"]),
    "\t\t(embedded_fonts no)", "\t)"].join("\n");
}

// ---- legacy sheets (KiCad 5 .sch) ----------------------------------------------------------------------------
function parseSheet(file) {
  const L = readFileSync(join(SRC, file), "utf8").split("\n");
  const sh = { tb: {}, comps: [], wires: [], labels: [], nc: [], notes: [], texts: [], sheets: [] };
  for (let i = 0; i < L.length; i++) {
    const l = L[i], t = l.trim().split(/\s+/);
    let m;
    if (l.startsWith("$Descr ")) { need(t[1] === "User", `${file}: paper ${t[1]}`); sh.w = +t[2]; sh.h = +t[3]; }
    else if ((m = l.match(/^(Title|Date|Rev|Comp|Comment[1-4]) "(.*)"$/))) sh.tb[m[1]] = m[2];
    else if (l === "$Comp") {
      const c = { fields: [] };
      for (i++; L[i] !== "$EndComp"; i++) {
        const k = L[i], u = k.trim().split(/\s+/);
        if (k.startsWith("L ")) { c.lib = u[1].split(":").pop(); c.ref = u[2]; }
        else if (k.startsWith("U ")) c.unit = +u[1];
        else if (k.startsWith("P ")) { c.x = +u[1]; c.y = +u[2]; }
        else if ((m = k.match(/^F (\d+) "([^"]*)" ([HV]) (-?\d+) (-?\d+) (\d+) +(\d{4}) ([LRC]) ([TBC])[IN][BN](?: "([^"]*)")?$/)))
          c.fields.push({ name: FIELD[+m[1]] ?? m[10], text: m[2], rot: m[3] === "V" ? 90 : 0, x: +m[4], y: +m[5], size: +m[6], hide: m[7].endsWith("1"), hj: m[8], vj: m[9] });
        else if (u.length === 4) need(u.join(" ") === "1 0 0 -1", `${file}: ${c.ref} orientation matrix ${u.join(" ")} (only the identity is emitted)`);
        else need(u.length === 3 && u[0] === "1", `${file}: unhandled component line "${k}"`);
      }
      need(c.lib && c.ref && c.unit === 1 && c.fields.length >= 6, `${file}: incomplete component ${c.ref}`);
      sh.comps.push(c);
    }
    else if (l === "Wire Wire Line") sh.wires.push(L[++i].trim().split(/\s+/).map(Number));
    else if (l === "Wire Notes Line") sh.notes.push(L[++i].trim().split(/\s+/).map(Number));
    else if (l.startsWith("Text Label ")) sh.labels.push({ x: +t[2], y: +t[3], dir: +t[4], size: +t[5], net: L[++i] });
    else if (l.startsWith("Text Notes ")) sh.texts.push({ x: +t[2], y: +t[3], dir: +t[4], size: +t[5], bold: +t[7] !== 0, text: L[++i] });
    else if (l.startsWith("NoConn ")) sh.nc.push([+t[2], +t[3]]);
    else if (l === "$Sheet") {
      const s = {};
      for (i++; L[i] !== "$EndSheet"; i++) {
        const u = L[i].split(/\s+/);
        if (u[0] === "S") Object.assign(s, { x: +u[1], y: +u[2], w: +u[3], h: +u[4] });
        else if ((m = L[i].match(/^F([01]) "([^"]*)" (\d+)/))) s[m[1] === "0" ? "name" : "file"] = m[2], s.size = +m[3];
      }
      sh.sheets.push(s);
    }
    else need(/^(EESchema Schematic File Version 4|EELAYER .*|encoding utf-8|Sheet 1 1|\$EndDescr|\$EndSCHEMATC|)$/.test(l), `${file}: unhandled line "${l}"`);
  }
  need(sh.tb.Rev === REV, `${file}: rev ${sh.tb.Rev} but rev.mjs says ${REV} — run kicad5-gen.mjs first`);
  return sh;
}

const rootSch = parseSheet("traction.sch");
const boards = rootSch.sheets.map((s) => {
  const name = s.file.replace(/\.sch$/, "");
  need(BOARD_OF[name], `root sheet ${s.file}: not a known board`);
  return { ...s, title: s.name, name, sh: parseSheet(s.file), sheetUuid: uuid(`${PROJECT}:sheet:${name}`), fileUuid: uuid(`${PROJECT}:file:${name}`) };
});
need(boards.length === 4, `root lists ${boards.length} sheets, want 4`);

// ---- pin numbers: only the MCU is numbered by ball --------------------------------------------------------------
// pages.mjs gives a pin the ball ID of its "<ball>_<signal>" label (the round-14 MCU rule). The 40-way harness
// labels "P<n>_<net>" fit that pattern as well, so the legacy library numbers JIC/JICC P1..P40 although their pads
// are 1..40 (circuit.json pin_number, the footprint's pad numbers). Here every pin so renumbered on any part other
// than the MCU gets its circuit.json number back; kicad-sch-verify proves the result against circuit.json.
{
  const portNum = new Map();
  for (const b of Object.values(BOARD_OF)) for (const p of loadCircuit(b).ports) portNum.set(`${p.ref}\0${p.name}`, String(p.pin_number));
  const users = new Map();
  for (const { sh } of boards) for (const c of sh.comps) users.set(c.lib, [...(users.get(c.lib) ?? []), c.ref]);
  let n = 0;
  for (const [name, s] of SYMS) {
    if ((users.get(name) ?? []).includes(MCU)) continue;
    for (const t of s.draw) {
      if (t[0] !== "X" || !/^[A-U]\d{1,2}$/.test(t[2]) || !t[1].startsWith(`${t[2]}_`)) continue;
      const nums = new Set((users.get(name) ?? []).map((r) => portNum.get(`${r}\0${t[1]}`)).filter(Boolean));
      need(nums.size === 1, `${name} pin ${t[1]}: circuit.json numbers [${[...nums]}]`);
      t[2] = [...nums][0]; n++;
    }
  }
  console.log(`   pin numbers: ${n} harness pins renumbered from their ball-style label to the circuit.json pad number`);
}
if (MUTATE === "mcu-swap") {                           // two MCU balls trade numbers; geometry untouched
  const mcuLib = boards.flatMap((b) => b.sh.comps).find((c) => c.ref === MCU).lib;
  const pins = SYMS.get(mcuLib).draw.filter((t) => t[0] === "X" && (t[2] === "H5" || t[2] === "J7"));
  need(pins.length === 2, "mutation precondition: MCU balls H5 and J7");
  [pins[0][2], pins[1][2]] = [pins[1][2], pins[0][2]];
}

// ---- emit ------------------------------------------------------------------------------------------------------
const HEAD = (fileUuid, w, h) => ["(kicad_sch", "\t(version 20250114)", '\t(generator "traction_kicad_sch_gen")', '\t(generator_version "9.0")',
  `\t(uuid ${q(fileUuid)})`, `\t(paper "User" ${mm(w)} ${mm(h)})`];
const titleBlock = (tb) => ["\t(title_block", `\t\t(title ${q(tb.Title)})`, `\t\t(date ${q(tb.Date)})`, `\t\t(rev ${q(REV)})`,
  `\t\t(company ${q(tb.Comp)})`, ...[1, 2, 3, 4].filter((k) => tb[`Comment${k}`]).map((k) => `\t\t(comment ${k} ${q(tb[`Comment${k}`])})`), "\t)"];
const LABEL_ANGLE = { 0: [0, "left"], 2: [180, "right"], 1: [90, "left"], 3: [270, "right"] };   // KiCad 5 local-label spin

mkdirSync(OUT, { recursive: true });
const ROOT_UUID = uuid(`${PROJECT}:root`);
const stats = [];
for (const b of boards) {
  const { sh } = b, f = b.name;
  let k = 0;
  const id = (kind) => uuid(`${f}:${kind}:${k++}`);
  // KiCad 9/10 draws its border (10 mm) and title block (34 mm above it, bottom right) INSIDE the page, and the
  // KiCad 5 pages reserve no room for them (the cap-bank block covered two can values): the page grows below the
  // unchanged content, and to the right where a notes line is longer than the page (glyph ~0.85 em measured, 0.9 used).
  const right = Math.max(0, ...sh.texts.map((t) => t.x + t.text.length * t.size * 0.9));
  const out = [...HEAD(b.fileUuid, Math.max(sh.w, Math.ceil((right + 600) / 50) * 50), sh.h + 1500), ...titleBlock(sh.tb), "\t(lib_symbols"];
  for (const name of [...new Set(sh.comps.map((c) => c.lib))].sort()) { need(SYMS.has(name), `${f}: no symbol ${name}`); out.push(libSymbol(SYMS.get(name), `${LIB}:`)); }
  out.push("\t)");
  for (const c of sh.comps) {
    const paths = [`/${ROOT_UUID}/${b.sheetUuid}`, `/${b.fileUuid}`];   // in the hierarchy, and the board opened on its own
    out.push(`\t(symbol (lib_id ${q(`${LIB}:${c.lib}`)}) (at ${mm(c.x)} ${mm(c.y)} 0)${MUTATE === "mcu-mirror" && c.ref === MCU ? " (mirror y)" : ""} (unit 1)`,
      "\t\t(exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)", `\t\t(uuid ${q(uuid(`${f}:sym:${c.ref}`))})`,
      ...c.fields.map((fl) => `\t\t${prop(fl)}`),
      `\t\t(instances (project ${q(PROJECT)}${paths.map((p) => ` (path ${q(p)} (reference ${q(c.ref)}) (unit 1))`).join("")}))`, "\t)");
  }
  for (const [x1, y1, x2, y2] of sh.wires)
    out.push(`\t(wire (pts (xy ${mm(x1)} ${mm(y1)}) (xy ${mm(x2)} ${mm(y2)})) (stroke (width 0) (type default)) (uuid ${q(id("wire"))}))`);
  for (const l of sh.labels) {
    need(LABEL_ANGLE[l.dir], `${f}: label ${l.net} spin ${l.dir}`);
    const [ang, just] = LABEL_ANGLE[l.dir];
    out.push(`\t(label ${q(l.net)} (at ${mm(l.x)} ${mm(l.y)} ${ang}) (effects ${font(l.size)} (justify ${just} bottom)) (uuid ${q(id("label"))}))`);
  }
  for (const [x, y] of sh.nc) out.push(`\t(no_connect (at ${mm(x)} ${mm(y)}) (uuid ${q(id("nc"))}))`);
  // section frames: kicad5-gen draws each as four notes lines round a rectangle — emit one rectangle
  let frames = 0;
  for (let i = 0; i < sh.notes.length; i++) {
    const s = sh.notes.slice(i, i + 4);
    const closed = s.length === 4 && s.every((g, j) => g[2] === s[(j + 1) % 4][0] && g[3] === s[(j + 1) % 4][1] && (g[0] === g[2] || g[1] === g[3]));
    if (closed) {
      const xs = s.map((g) => g[0]), ys = s.map((g) => g[1]);
      out.push(`\t(rectangle (start ${mm(Math.min(...xs))} ${mm(Math.min(...ys))}) (end ${mm(Math.max(...xs))} ${mm(Math.max(...ys))}) (stroke (width 0) (type dash)) (fill (type none)) (uuid ${q(id("frame"))}))`);
      frames++; i += 3;
    } else out.push(`\t(polyline (pts (xy ${mm(s[0][0])} ${mm(s[0][1])}) (xy ${mm(s[0][2])} ${mm(s[0][3])})) (stroke (width 0) (type dash)) (uuid ${q(id("line"))}))`);
  }
  for (const t of sh.texts) {
    need(t.dir === 0, `${f}: rotated note "${t.text}"`);
    out.push(`\t(text ${q(t.text)} (exclude_from_sim no) (at ${mm(t.x)} ${mm(t.y)} 0) (effects ${font(t.size, t.bold)} (justify left bottom)) (uuid ${q(id("text"))}))`);
  }
  out.push("\t(embedded_fonts no)", ")", "");
  writeFileSync(join(OUT, `${f}.kicad_sch`), out.join("\n"));
  stats.push(`${f.padEnd(18)} ${String(sh.comps.length).padStart(3)} symbols · ${sh.labels.length} labels · ${sh.wires.length} stubs · ${sh.nc.length} no-connects · ${frames} frames · ${sh.texts.length} notes`);
}

// root: the four boards as sheet instances (local labels keep each board its own netlist)
{
  const out = [...HEAD(ROOT_UUID, rootSch.w, rootSch.h), ...titleBlock(rootSch.tb), "\t(lib_symbols)"];
  boards.forEach((b, i) => {
    out.push(`\t(sheet (at ${mm(b.x)} ${mm(b.y)}) (size ${mm(b.w)} ${mm(b.h)})`,
      "\t\t(exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)",
      "\t\t(stroke (width 0.1524) (type solid)) (fill (color 0 0 0 0.0000))", `\t\t(uuid ${q(b.sheetUuid)})`,
      `\t\t${prop({ name: "Sheetname", text: b.title, x: b.x, y: b.y - 30, size: b.size, rot: 0, hj: "L", vj: "B" })}`,
      `\t\t${prop({ name: "Sheetfile", text: `${b.name}.kicad_sch`, x: b.x, y: b.y + b.h + 30, size: b.size, rot: 0, hj: "L", vj: "T" })}`,
      `\t\t(instances (project ${q(PROJECT)} (path ${q(`/${ROOT_UUID}`)} (page ${q(String(i + 2))}))))`, "\t)");
  });
  out.push('\t(sheet_instances (path "/" (page "1")))', "\t(embedded_fonts no)", ")", "");
  writeFileSync(join(OUT, "traction.kicad_sch"), out.join("\n"));
}

// project, and a project library identical to the embedded lib_symbols: KiCad's ERC checks every symbol against
// its library (lib_symbol_issues / lib_symbol_mismatch); the sheets still open with no library at all
writeFileSync(join(OUT, "traction.kicad_pro"), `${JSON.stringify({ meta: { filename: "traction.kicad_pro", version: 3 } }, null, 2)}\n`);
writeFileSync(join(OUT, "traction.kicad_sym"), ["(kicad_symbol_lib", "\t(version 20241209)", '\t(generator "traction_kicad_sch_gen")', '\t(generator_version "9.0")',
  ...[...SYMS.values()].map((s) => libSymbol(s, "")), ")", ""].join("\n"));
writeFileSync(join(OUT, "sym-lib-table"), `(sym_lib_table\n\t(version 7)\n\t(lib (name ${q(LIB)})(type "KiCad")(uri "\${KIPRJMOD}/traction.kicad_sym")(options "")(descr "Traction Inverter symbols (generated; identical to the lib_symbols embedded in every sheet)"))\n)\n`);
writeFileSync(join(OUT, "README.txt"), `KICAD 9/10 SET (rev ${REV}). Open traction.kicad_pro in KiCad 9 or 10: the root sheet holds the four boards
(power, cap bank, discharge, control card) as sheet instances. Each board is its own PCB: its labels are local,
so a net joins other boards only through the named connector/stud/tab, never by name across sheets.
Every sheet embeds its symbols (no library needed to open it); traction.kicad_sym + sym-lib-table are an identical
project library so KiCad's symbol-library checks pass. The MCU (UMCU) numbers its pins by BGA ball (H5, J7, ...);
every other part by its footprint pad. Footprint fields name the package (R0603, MAPBGA289, ...); no footprint
library is shipped. Generated from the same sheets as the KiCad 5 set (kicad5/ in the repository, for KiCad 5-8 and EasyEDA;
KiCad 10 resolves none of its symbols) by calculations/kicad-sch-gen.mjs, and proved with kicad-cli (ERC + netlist
export compared net by net with circuit.json) by calculations/kicad-sch-verify.mjs.
`);
const FILES = ["traction.kicad_pro", "traction.kicad_sch", ...boards.map((b) => `${b.name}.kicad_sch`), "traction.kicad_sym", "sym-lib-table", "README.txt"];
if (SHIP) {                                             // packaged while generating, so the zip never drifts
  const zip = join(ROOT, "kicad", "Traction-Inverter-KiCad-modern.zip");
  const members = FILES.map((f) => join(OUT, f));
  try { unlinkSync(zip); } catch {}
  execFileSync("touch", ["-t", `${rootSch.tb.Date.replace(/-/g, "")}0000`, ...members]);
  execFileSync("zip", ["-qX", "-j", zip, ...members]);
}
stats.forEach((s) => console.log(s));
console.log(`\n${FILES.length} files · ${boards.length} board sheets + root · ${SYMS.size} symbols → ${OUT}${SHIP ? " + kicad/Traction-Inverter-KiCad-modern.zip" : ""}${MUTATE ? ` (MUTATED: ${MUTATE})` : ""}`);
