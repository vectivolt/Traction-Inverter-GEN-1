#!/usr/bin/env node
// sheets-to-pdf.mjs — render each board sheet SVG to a single-page PDF, plus one combined
// "Schematic Set" PDF holding both sheets (the deliverable a reviewer opens first).
//
// Vector in, vector out (headless Chrome print-to-PDF), so the drawing stays zoomable and
// text stays selectable. Source of record: the AUDITED KiCad-5 sheets rendered by
// kicad5-print.mjs (calculations/out/print). Run kicad5-print before this tool.
//
// Output: boards/out-pdf/Traction Inverter <BOARD>.pdf + the combined set
// Run:    node calculations/sheets-to-pdf.mjs

import { readFileSync, writeFileSync, mkdirSync, existsSync, rmSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const OUT = join(ROOT, "boards/out-pdf");
mkdirSync(OUT, { recursive: true });
const TMP = join(ROOT, ".pdf-tmp");
mkdirSync(TMP, { recursive: true });

const CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
if (!existsSync(CHROME)) { console.error("Google Chrome not found — needed for SVG→PDF"); process.exit(1); }

const BOARDS = [
  { k5: "traction-power", name: "Traction Inverter 220kW — Power Board (SiC 3-phase)" },
  { k5: "traction-capbank", name: "Traction Inverter — Cap Bank (busbar assembly, 16x film can)" },
  { k5: "traction-disch", name: "Traction Inverter — Discharge Board (bolt-on bleeder + active discharge)" },
  { k5: "traction-card", name: "Traction Inverter — Control Card (S32K396 + FS26, ASIL D)" },
];

const PX_PER_IN = 96;
const printPdf = (htmlPath, pdfPath) => execFileSync(CHROME, [
  "--headless", "--disable-gpu", "--no-pdf-header-footer",
  `--print-to-pdf=${pdfPath}`, `file://${htmlPath}`,
], { stdio: ["ignore", "ignore", "pipe"], timeout: 180000 });

const sized = [];
for (const b of BOARDS) {
  const svgPath = join(ROOT, "calculations", "out", "print", `${b.k5}.svg`);
  if (!existsSync(svgPath)) { console.log(`!! ${b.k5}: no sheet`); continue; }
  const svg = readFileSync(svgPath, "utf8");
  const m0 = svg.match(/width="(\d+)" height="(\d+)"/);
  const [w, h] = [+m0[1], +m0[2]];
  sized.push({ ...b, svg, w, h });

  // one page, exactly the sheet's size, no margin — @page in inches keeps Chrome honest
  const html = `<!doctype html><meta charset="utf-8"><title>${b.name}</title>
<style>
  @page { size: ${(w / PX_PER_IN).toFixed(3)}in ${(h / PX_PER_IN).toFixed(3)}in; margin: 0; }
  html,body { margin:0; padding:0; background:#fff; }
  svg { display:block; width:${w}px; height:${h}px; }
</style>
${svg}`;
  const htmlPath = join(TMP, `${b.k5}.html`);
  writeFileSync(htmlPath, html);
  const pdfPath = join(OUT, `${b.name}.pdf`);
  printPdf(htmlPath, pdfPath);
  const size = existsSync(pdfPath) ? (readFileSync(pdfPath).length / 1048576).toFixed(2) : "0";
  console.log(`${b.name}.pdf  ${w}×${h}px → ${(w / PX_PER_IN).toFixed(1)}×${(h / PX_PER_IN).toFixed(1)} in · ${size} MB`);
}

// Combined set: both sheets in one PDF, each page at the LARGER sheet's aspect (Chrome honors
// only one @page size per document, so the smaller sheet is centred on the common page — the
// vector content is untouched, it simply gets margin).
if (sized.length > 1) {
  const W = Math.max(...sized.map((b) => b.w)), H = Math.max(...sized.map((b) => b.h));
  const pages = sized.map((b) =>
    `<div class="pg"><div class="in">${b.svg}</div></div>`).join("\n");
  const html = `<!doctype html><meta charset="utf-8"><title>Traction Inverter — Schematic Set</title>
<style>
  @page { size: ${(W / PX_PER_IN).toFixed(3)}in ${(H / PX_PER_IN).toFixed(3)}in; margin: 0; }
  html,body { margin:0; padding:0; background:#fff; }
  .pg { width:${W}px; height:${H}px; page-break-after: always; display:flex; align-items:center; justify-content:center; }
  .pg:last-child { page-break-after: auto; }
  svg { display:block; }
</style>
${pages}`;
  const htmlPath = join(TMP, "set.html");
  writeFileSync(htmlPath, html);
  const pdfPath = join(OUT, "Traction Inverter 220kW — Schematic Set (Power + Cap Bank + Discharge + Control Card).pdf");
  printPdf(htmlPath, pdfPath);
  console.log(`Schematic Set: ${sized.length} sheets → "${pdfPath.split("/").pop()}"`);
}
rmSync(TMP, { recursive: true, force: true });
console.log(`\n→ boards/out-pdf/`);
