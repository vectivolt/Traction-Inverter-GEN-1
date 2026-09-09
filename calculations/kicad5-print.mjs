// kicad5-print.mjs — PRINT-GRADE full-sheet SVG of an emitted KiCad-5 sheet.
//
// Ported from DC-Modules power-module-platform (same drawing conventions, same palette) so the
// Traction set renders in the identical house style: real library glyphs (zigzag R, plates,
// diode triangles, FET/opto/iso function glyphs), every pin number and name, net-label text,
// section frames, notes panels and a title block. sheets-to-pdf consumes these.
//
// Run: node calculations/kicad5-print.mjs   ->  calculations/out/print/<sheet>.svg
import { readFileSync, writeFileSync, mkdirSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const SCH = join(ROOT, "kicad5/traction");
const OUT = join(ROOT, "calculations/out/print");
mkdirSync(OUT, { recursive: true });

// ---- library: pins + ALL draw primitives (incl. polygon fill flag) ----
const libFile = readdirSync(SCH).find((f) => f.endsWith(".lib"));
const LIB = new Map();
for (const blk of readFileSync(join(SCH, libFile), "utf8").split(/^DEF /m).slice(1)) {
  const name = blk.split(/\s+/)[0];
  const pins = [], shapes = [];
  for (const l of blk.split("\n")) {
    const t = l.split(/\s+/);
    if (l.startsWith("X ")) pins.push({ name: t[1], num: t[2], x: +t[3], y: +t[4], len: +t[5], o: t[6] });
    else if (l.startsWith("S ")) shapes.push({ k: "S", x0: +t[1], y0: +t[2], x1: +t[3], y1: +t[4], fill: t[8] });
    else if (l.startsWith("C ")) shapes.push({ k: "C", x: +t[1], y: +t[2], r: +t[3], fill: t[7] });
    else if (l.startsWith("P ")) {
      const n = +t[1], pts = [];
      for (let i = 0; i < n; i++) pts.push([+t[5 + 2 * i], +t[6 + 2 * i]]);
      shapes.push({ k: "P", pts, w: +t[4] || 8, fill: t[5 + 2 * n] });
    } else if (l.startsWith("A "))
      shapes.push({ k: "A", r: +t[3], sx: +t[10], sy: +t[11], ex: +t[12], ey: +t[13], w: +t[8] || 8 });
  }
  LIB.set(name, { pins, shapes });
}

const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
const INK = "#1a1a1a", WIRE = "#00695c", LBL = "#8c2b3d", REF = "#333333", VAL = "#00626e",
  PINNO = "#8a8a8a", FRAME = "#9b8a66", NOTE = "#4a3f35";

for (const file of readdirSync(SCH).filter((f) => /^traction-(power|capbank|disch|card)\.sch$/.test(f)).sort()) {
  const lines = readFileSync(join(SCH, file), "utf8").split("\n");
  const [W, H] = lines.find((l) => l.startsWith("$Descr")).split(/\s+/).slice(2).map(Number);
  const grab = (k) => (lines.find((l) => l.startsWith(k)) ?? "").split('"')[1] ?? "";
  const title = grab("Title "), comp = grab("Comp "), rev = grab("Rev "), date = grab("Date ");

  const wires = [], labels = [], notes = [], texts = [], syms = [], ncs = [];
  for (let i = 0; i < lines.length; i++) {
    const L = lines[i];
    if (L === "Wire Wire Line") wires.push(lines[++i].trim().split(/\s+/).map(Number));
    else if (L === "Wire Notes Line") notes.push(lines[++i].trim().split(/\s+/).map(Number));
    else if (L.startsWith("NoConn ~")) { const t = L.split(/\s+/); ncs.push([+t[2], +t[3]]); }
    else if (L.startsWith("Text Label ") || L.startsWith("Text GLabel ")) {
      const t = L.split(/\s+/);
      labels.push({ x: +t[2], y: +t[3], dir: +t[4], size: +t[5], net: lines[++i] });
    } else if (L.startsWith("Text Notes ")) {
      const t = L.split(/\s+/); texts.push({ x: +t[2], y: +t[3], size: +t[5], s: lines[++i] });
    } else if (L === "$Comp") {
      let lib = "", ref = "", val = "", x = 0, y = 0, rp = null, vp = null;
      for (let k = i + 1; lines[k] !== "$EndComp"; k++) {
        const t = lines[k].split(/\s+/);
        if (lines[k].startsWith("L ")) { lib = t[1].split(":").pop(); ref = t[2]; }
        else if (lines[k].startsWith("P ")) { x = +t[1]; y = +t[2]; }
        else if (lines[k].startsWith("F 0 ")) rp = [+t[4], +t[5], +t[6]];
        else if (lines[k].startsWith("F 1 ")) { val = lines[k].split('"')[1]; vp = [+t[4], +t[5], +t[6]]; }
      }
      syms.push({ lib, ref, val, x, y, rp, vp });
    }
  }

  const frames = [];
  for (let i = 0; i + 3 < notes.length; i += 4) {
    const seg = notes.slice(i, i + 4);
    const xs = seg.flatMap((s) => [s[0], s[2]]), ys = seg.flatMap((s) => [s[1], s[3]]);
    frames.push({ x0: Math.min(...xs), y0: Math.min(...ys), x1: Math.max(...xs), y1: Math.max(...ys) });
  }

  let g = "";
  for (const f of frames)
    g += `<rect x="${f.x0}" y="${f.y0}" width="${f.x1 - f.x0}" height="${f.y1 - f.y0}" fill="#f7f3e8" fill-opacity=".45" stroke="${FRAME}" stroke-width="10" stroke-dasharray="85 55"/>`;
  for (const t of texts)
    g += `<text x="${t.x}" y="${t.y}" font-family="Helvetica,Arial" font-size="${t.size}" font-weight="700" fill="${NOTE}">${esc(t.s)}</text>`;
  for (const [a, b, c, d] of wires)
    g += `<line x1="${a}" y1="${b}" x2="${c}" y2="${d}" stroke="${WIRE}" stroke-width="7"/>`;
  for (const [x, y] of ncs)
    g += `<g stroke="#2e7d32" stroke-width="8"><line x1="${x - 30}" y1="${y - 30}" x2="${x + 30}" y2="${y + 30}"/><line x1="${x - 30}" y1="${y + 30}" x2="${x + 30}" y2="${y - 30}"/></g>`;

  for (const c of syms) {
    const s = LIB.get(c.lib);
    if (!s) continue;
    for (const sh of s.shapes) {
      const fill = sh.fill === "F" ? INK : sh.fill === "f" ? "#fbfaf4" : "none";
      if (sh.k === "S") g += `<rect x="${c.x + Math.min(sh.x0, sh.x1)}" y="${c.y + Math.min(sh.y0, sh.y1)}" width="${Math.abs(sh.x1 - sh.x0)}" height="${Math.abs(sh.y1 - sh.y0)}" fill="${fill === "none" ? "#ffffff" : fill}" stroke="${INK}" stroke-width="9"/>`;
      else if (sh.k === "C") g += `<circle cx="${c.x + sh.x}" cy="${c.y + sh.y}" r="${sh.r}" fill="${fill === "none" ? "none" : fill}" stroke="${INK}" stroke-width="9"/>`;
      else if (sh.k === "P") g += `<${sh.fill === "F" ? "polygon" : "polyline"} points="${sh.pts.map(([a, b]) => `${c.x + a},${c.y + b}`).join(" ")}" fill="${sh.fill === "F" ? INK : "none"}" stroke="${INK}" stroke-width="${Math.max(7, sh.w)}"/>`;
      else if (sh.k === "A") g += `<path d="M ${c.x + sh.sx} ${c.y + sh.sy} A ${sh.r} ${sh.r} 0 0 1 ${c.x + sh.ex} ${c.y + sh.ey}" fill="none" stroke="${INK}" stroke-width="${Math.max(7, sh.w)}"/>`;
    }
    for (const p of s.pins) {
      const px = c.x + p.x, py = c.y + p.y;
      const dx = p.o === "R" ? p.len : p.o === "L" ? -p.len : 0;
      // The lib is authored in the EasyEDA-importer convention: Y pre-negated AND U/D swapped.
      // Positions parse correctly under plain ADD, but the orientation letters are in the
      // mirrored sense — so U runs DOWN (+y) and D runs UP here.
      const dy = p.o === "U" ? p.len : p.o === "D" ? -p.len : 0;
      g += `<line x1="${px}" y1="${py}" x2="${px + dx}" y2="${py + dy}" stroke="${INK}" stroke-width="6"/>`;
      g += `<circle cx="${px}" cy="${py}" r="8" fill="#b03030"/>`;
      if (p.name && p.name !== "~" && s.pins.length > 2) {
        const ix = px + (p.o === "R" ? p.len + 40 : p.o === "L" ? -p.len - 40 : 0);
        const iy = py + (p.o === "U" ? p.len + 40 : p.o === "D" ? -p.len - 40 : 18);
        const a = p.o === "R" ? "start" : p.o === "L" ? "end" : "middle";
        g += `<text x="${ix}" y="${iy}" font-family="Helvetica,Arial" font-size="50" fill="#4a5a63" text-anchor="${a}">${esc(p.name)}</text>`;
      }
      if (s.pins.length > 2) {
        const nx = px + (p.o === "R" ? p.len / 2 : p.o === "L" ? -p.len / 2 : 0);
        const ny = py + (p.o === "U" ? -p.len / 2 : p.o === "D" ? p.len / 2 : -25);
        g += `<text x="${nx}" y="${ny}" font-family="Helvetica,Arial" font-size="40" fill="${PINNO}" text-anchor="middle">${esc(p.num)}</text>`;
      }
    }
    if (c.rp) g += `<text x="${c.rp[0]}" y="${c.rp[1]}" font-family="Helvetica,Arial" font-size="${c.rp[2]}" font-weight="600" fill="${REF}" text-anchor="middle">${esc(c.ref)}</text>`;
    if (c.vp) g += `<text x="${c.vp[0]}" y="${c.vp[1]}" font-family="Helvetica,Arial" font-size="${c.vp[2]}" fill="${VAL}" text-anchor="middle">${esc(c.val)}</text>`;
  }

  for (const l of labels) {
    const vert = l.dir === 1 || l.dir === 3;
    const anchor = (l.dir === 2 || l.dir === 3) ? "end" : "start";
    const off = (l.dir === 2 || l.dir === 3) ? -20 : 20;
    const attrs = `font-family="Helvetica,Arial" font-size="${l.size}" fill="${LBL}" text-anchor="${anchor}"`;
    g += vert
      ? `<text ${attrs} transform="translate(${l.x + l.size * 0.36} ${l.y + off}) rotate(-90)">${esc(l.net)}</text>`
      : `<text x="${l.x + off}" y="${l.y + l.size * 0.36}" ${attrs}>${esc(l.net)}</text>`;
  }

  // border + title block
  g += `<rect x="60" y="60" width="${W - 120}" height="${H - 120}" fill="none" stroke="#7a6a52" stroke-width="16"/>`;
  const tbW = 9600, tbH = 900, tbX = W - 60 - tbW, tbY = H - 60 - tbH;
  g += `<rect x="${tbX}" y="${tbY}" width="${tbW}" height="${tbH}" fill="#fbf8ee" stroke="#7a6a52" stroke-width="12"/>`
    + `<text x="${tbX + 120}" y="${tbY + 320}" font-family="Helvetica,Arial" font-size="230" font-weight="700" fill="#2e4a1f">${esc(title)}</text>`
    + `<text x="${tbX + 120}" y="${tbY + 620}" font-family="Helvetica,Arial" font-size="180" fill="#4a3f35">${esc(comp)}</text>`
    + `<text x="${tbX + tbW - 120}" y="${tbY + 620}" font-family="Helvetica,Arial" font-size="180" text-anchor="end" fill="#4a3f35">${esc(date)}   ·   rev ${esc(rev)}</text>`;

  const w = Math.round(W / 10), h = Math.round(H / 10);
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${w}" height="${h}">`
    + `<rect width="${W}" height="${H}" fill="#ffffff"/>${g}</svg>`;
  const name = file.replace(".sch", ".svg");
  writeFileSync(join(OUT, name), svg);
  console.log(`${name}: ${syms.length} symbols, ${labels.length} labels, ${wires.length} stubs → out/print/`);
}
