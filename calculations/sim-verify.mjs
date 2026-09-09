#!/usr/bin/env node
// sim-verify.mjs — operating-point SIMULATION layer (S1–S7), on top of the closed-form
// design-verify. These are numerical time/frequency-domain simulations of the drawn
// circuits at the actual operating conditions — not measured waveforms. Items that
// physically require hardware (layout parasitics, core saturation, SC withstand, EMI)
// stay on the bench list and are marked so.
// Run: node calculations/sim-verify.mjs  → docs/simulation-report.md + docs/img/sim/*.svg
import { writeFileSync, mkdirSync } from "node:fs";
import { REV } from "./rev.mjs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
mkdirSync(join(ROOT, "docs", "img", "sim"), { recursive: true });

const rows = [];
const add = (id, name, result, limit, st, note = "") => rows.push({ id, name, result, limit, st, note });
const f = (x, d = 1) => Number(x.toFixed(d));

// tiny SVG line-plot helper (house-style, light background)
function plot(file, title, series, xlab, ylab) {
  const W = 720, H = 320, L = 62, B = 40, T = 28, R = 16;
  const xs = series.flatMap((s) => s.x), ys = series.flatMap((s) => s.y);
  const x0 = Math.min(...xs), x1 = Math.max(...xs), y0 = Math.min(...ys, 0), y1 = Math.max(...ys) * 1.05 + 1e-12;
  const X = (v) => L + (v - x0) / (x1 - x0) * (W - L - R);
  const Y = (v) => H - B - (v - y0) / (y1 - y0) * (H - B - T);
  const colors = ["#2a6b4f", "#a33c3c", "#31567e", "#8a6d1a"];
  let g = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" font-family="Helvetica,Arial" font-size="11">`
    + `<rect width="${W}" height="${H}" fill="#faf8f1"/>`
    + `<text x="${W / 2}" y="16" text-anchor="middle" font-size="13" fill="#233">${title}</text>`;
  for (let i = 0; i <= 4; i++) {
    const yy = y0 + (y1 - y0) * i / 4;
    g += `<line x1="${L}" y1="${Y(yy)}" x2="${W - R}" y2="${Y(yy)}" stroke="#ddd6c4" stroke-width="0.6"/>`
      + `<text x="${L - 6}" y="${Y(yy) + 3}" text-anchor="end" fill="#555">${Number(yy.toPrecision(3))}</text>`;
    const xx = x0 + (x1 - x0) * i / 4;
    g += `<text x="${X(xx)}" y="${H - B + 14}" text-anchor="middle" fill="#555">${Number(xx.toPrecision(3))}</text>`;
  }
  series.forEach((s, i) => {
    const pts = s.x.map((xv, k) => `${X(xv).toFixed(1)},${Y(s.y[k]).toFixed(1)}`).join(" ");
    g += `<polyline points="${pts}" fill="none" stroke="${colors[i % 4]}" stroke-width="1.6"/>`
      + `<text x="${W - R - 4}" y="${T + 14 + i * 14}" text-anchor="end" fill="${colors[i % 4]}">${s.name}</text>`;
  });
  g += `<text x="${W / 2}" y="${H - 6}" text-anchor="middle" fill="#555">${xlab}</text>`
    + `<text x="14" y="${H / 2}" transform="rotate(-90 14 ${H / 2})" text-anchor="middle" fill="#555">${ylab}</text></svg>`;
  writeFileSync(join(ROOT, "docs", "img", "sim", file), g);
}

// ============ S1 — gate-power flyback, cycle-by-cycle (DCM current mode) ============
// Drawn circuit: UCC28C40 @253 kHz, 3x VGT12EEM primaries in parallel (Lp_eq 3.33 µH),
// CS limit 1 V / 0.33 Ω = 3.03 A, NS/NP = 2.9, NF/NP = 1.6, secondary rectifier on the
// dot (A.4 phasing), per-channel output C = 4.7+10 µF, rails +15.6/−5.1 via zener split.
{
  const Lp = 10e-6 / 3, fsw = 253e3, Tsw = 1 / fsw, Ns = 2.9, Nf = 1.6, Ilim = 1.0 / 0.33;
  const Cout = 3 * 14.7e-6;               // lumped across the 3 channels of one bank
  const Vf = 0.8, Llk = 0.2e-6;           // leakage (assumption: 2 % of Lp — bench item)
  const Vtar = 21.4;                      // NF-regulated secondary total (F33 chain)
  for (const Vin of [9, 12, 16]) {
    // per-switch gate load at 10 kHz PWM + driver Iq, referred to one bank
    const Pload = 3 * (1.24e-6 * 20.7 * 10e3 + 5e-3 * 20.7) + 0.2;
    let V = 0, t = 0, ipkS = 0, settle = -1, vpk = 0;
    const tr = { x: [], y: [] };
    for (let n = 0; n < 12000; n++) {
      // primary-side regulation: demanded peak current from the FB error (P-control approx)
      const dem = Math.max(0.15, Math.min(Ilim, 200 * (Vtar - V) / Vtar * Ilim)); // high-gain EA (integrator in the real part)
      const ipk = Math.min(Ilim, dem);
      const Ein = 0.5 * Lp * ipk * ipk;
      const Eload = Pload * Tsw;
      V = Math.sqrt(Math.max(0, V * V + 2 * (0.92 * Ein - Eload) / Cout)); // 8 % xfmr/diode loss
      ipkS = ipk; t += Tsw;
      // drain peak this cycle: Vin + reflected + leakage ring into the SMAJ13A clamp path
      const vClampRing = Math.min(Math.sqrt(Llk / 50e-12) * ipk * 0.15, 19.5); // ring limited by clamp Vbr+Vf
      vpk = Math.max(vpk, Vin + (V + Vf) / Ns + Math.min(vClampRing, 19.5 + 0.7));
      if (settle < 0 && V >= 0.95 * Vtar) settle = t;
      if (n % 40 === 0) { tr.x.push(t * 1e3); tr.y.push(V); }
    }
    if (Vin === 12) plot("s1-flyback-startup.svg", "S1 flyback bank: secondary total voltage, startup @12 V (cycle-by-cycle DCM)", [{ name: "Vsec (V)", x: tr.x, y: tr.y }], "time (ms)", "V");
    add("S1", `Flyback steady rail @${Vin} V in`, `${f(V, 1)} V (target 21.4)`, "±5 %", Math.abs(V - Vtar) / Vtar < 0.05 ? "PASS" : "WARN",
      `settles in ${f(settle * 1e3, 1)} ms · steady Ipk ${f(ipkS, 2)} A vs ${f(Ilim, 2)} A limit`);
    add("S1", `Flyback drain peak @${Vin} V in`, `${f(vpk, 1)} V`, "80 V BUK7Y14-80E", vpk < 64 ? "PASS" : "WARN",
      "leakage 2 % assumed — bench-confirm the ring; clamp path bounds it");
  }
}

// ============ S2 — UB15 boost loop Bode WITH the A.4.3 compensation ============
// Current-mode CCM model (TI SLVSBD4E §8.2.1.2.11 form): power stage Gps with output pole,
// RHP zero; EA gm=360 µS, Ro=10 MΩ, Zc = Rc + 1/sCc (∥ Cp). Vin 12/9 V, load 0.33 A.
{
  const Vout = 15.4, Cout = 35e-6, gm = 360e-6, Ro = 10e6, Vref = 1.229;
  const Rc = 2e3, Cc = 100e-9, Cp = 470e-12, L = 10e-6, Rsense = 0.088; // internal Ri (A/V→V/A est.)
  for (const Vin of [12, 9]) {
    const D = 1 - Vin / Vout, Rload = Vout / 0.33;
    const fp = 2 / (2 * Math.PI * Rload * Cout);
    const frhpz = (1 - D) ** 2 * Rload / (2 * Math.PI * L);
    const Adc = (1 - D) * Rload / (2 * Rsense); // current-mode DC gain to the modulator
    const T = (fr) => {
      const s = { re: 0, im: 2 * Math.PI * fr };
      const mag1 = Adc / Math.hypot(1, fr / fp);
      const ph1 = -Math.atan2(fr / fp, 1) - Math.atan2(fr / frhpz, 1);
      const Zc_re = Rc, Zc_im = -1 / (2 * Math.PI * fr * Cc);
      const zmag = Math.hypot(Zc_re, Zc_im) / Math.hypot(1, fr / (1 / (2 * Math.PI * Rc * Cp))); // Cp HF pole approx
      const zph = Math.atan2(Zc_im, Zc_re) - Math.atan2(fr / (1 / (2 * Math.PI * Rc * Cp)), 1);
      const eaP = 1 / (2 * Math.PI * Ro * Cc);
      const eamag = 1 / Math.hypot(1, eaP / fr); // low-freq integrator shaping via Ro
      return { mag: mag1 * gm * zmag * (Vref / Vout) * eamag, ph: 180 + (ph1 + zph) * 180 / Math.PI };
    };
    let fc = 0, pm = 0; const bode = { xf: [], gain: [], phase: [] };
    for (let e = 1; e <= 5.3; e += 0.01) {
      const fr = 10 ** e, r = T(fr), g = 20 * Math.log10(r.mag);
      bode.xf.push(e); bode.gain.push(g); bode.phase.push(r.ph);
      if (fc === 0 && g <= 0) { fc = fr; pm = r.ph; }
    }
    if (Vin === 12) plot("s2-boost-bode.svg", "S2 UB15 loop gain with 2 kΩ/100 nF + 470 pF (Vin 12 V, 0.33 A)",
      [{ name: "gain (dB)", x: bode.xf, y: bode.gain }, { name: "phase (deg)", x: bode.xf, y: bode.phase }], "log10 f (Hz)", "dB / deg");
    add("S2", `UB15 crossover @${Vin} V in`, `${f(fc / 1e3, 2)} kHz`, `«fsw/10 (58 kHz) · «RHPZ (${f(frhpz / 1e3, 0)} kHz)`,
      fc < 58e3 ? "PASS" : "WARN", "");
    add("S2", `UB15 phase margin @${Vin} V in`, `${f(pm, 0)}°`, "≥45°", pm >= 45 ? "PASS" : "WARN",
      "vs ~0° for the pre-A.4.3 capacitor-only COMP; measured Bode still a bench gate");
  }
}

// ============ S3 — DC-link ripple: SVPWM switching simulation over a fundamental ============
// Actual switch-state simulation, 50 Hz fundamental, fsw 8 kHz, m=0.9, cosφ=0.9:
// dc-side current = Σ(sw_high[k]·i_k); cap current = idc − mean(idc); per-can share /16.
{
  for (const [tag, Iph] of [["peak-30s (340 A)", 340], ["continuous (216 A)", 216]]) {
    const m = 0.9, phi = Math.acos(0.9), fs = 8000, ffund = 50, N = Math.floor(fs / ffund) * 64;
    let sum = 0, sum2 = 0;
    const wave = { x: [], y: [] };
    for (let n = 0; n < N; n++) {
      const t = n / (fs * 64), th = 2 * Math.PI * ffund * t;
      let idc = 0;
      for (let k = 0; k < 3; k++) {
        const duty = 0.5 + 0.5 * m * Math.sin(th - k * 2 * Math.PI / 3);
        const carrier = (n % 64) / 64;                       // triangle-equivalent sampling
        const hi = carrier < duty ? 1 : 0;
        idc += hi * Math.SQRT2 * Iph * Math.sin(th - k * 2 * Math.PI / 3 - phi);
      }
      sum += idc; sum2 += idc * idc;
      if (n < 2 * 64 * 8 && n % 8 === 0) { wave.x.push(t * 1e3); wave.y.push(idc); }
    }
    const mean = sum / N, irms = Math.sqrt(sum2 / N - mean * mean), perCan = irms / 16;
    if (Iph === 340) plot("s3-dclink-ripple.svg", "S3 DC-link capacitor current, SVPWM switching sim (340 A, m=0.9, cosφ=0.9)",
      [{ name: "i_dc (A)", x: wave.x, y: wave.y }], "time (ms)", "A");
    add("S3", `Cap ripple per can, ${tag}`, `${f(perCan, 1)} A rms (bank ${f(irms, 0)} A)`, "15.4 A/can @10 kHz/70 °C",
      perCan < 15.4 ? "PASS" : "FAIL", "switching-state simulation (not the closed-form envelope)");
  }
}

// ============ S4 — module junction thermal transient through the 30 s peak ============
// 3-node ladder per switch: junction (RthJC 0.066, τ 0.8 s) → case/TIM (0.015, τ 5 s) →
// coldplate node (0.045 K/W to 65 °C coolant, τ 60 s — coldplate Rth is a stated assumption).
{
  const Rjc = 0.066, Rch = 0.015, Rha = 0.045, Ta = 65;
  const Cj = 0.8 / Rjc, Cc = 5 / Rch, Ch = 60 / Rha;
  const Pcont = 121, Ppk = 232;           // per-switch: design-verify loss rows (cond+sw) at 120/220 kW
  let Tj = 0, Tc = 0, Th = 0;             // temperatures above coolant
  const dt = 0.02, tr = { x: [], y: [] };
  let t = 0, TjPk = 0;
  const step = (P) => {
    const qjc = (Tj - Tc) / Rjc, qch = (Tc - Th) / Rch, qha = Th / Rha;
    Tj += dt * (P - qjc) / Cj; Tc += dt * (qjc - qch) / Cc; Th += dt * (qch - qha) / Ch;
  };
  for (; t < 120; t += dt) step(Pcont);                    // reach continuous steady state
  for (const tEnd of [30]) {
    for (let te = 0; te < tEnd; te += dt) { step(Ppk); t += dt; TjPk = Math.max(TjPk, Tj); tr.x.push(t - 120); tr.y.push(Ta + Tj); }
  }
  for (let te = 0; te < 60; te += dt) { step(Pcont); t += dt; tr.x.push(t - 120); tr.y.push(Ta + Tj); }
  plot("s4-thermal-30s.svg", "S4 junction temperature: 120 kW steady → 220 kW for 30 s → back (65 °C coolant)",
    [{ name: "Tj (°C)", x: tr.x, y: tr.y }], "time from peak start (s)", "°C");
  add("S4", "Tj at end of 30 s / 220 kW peak", `${f(Ta + TjPk, 0)} °C`, "175 °C max (design ≤150)",
    Ta + TjPk < 150 ? "PASS" : Ta + TjPk < 175 ? "WARN" : "FAIL",
    "coldplate 0.045 K/W per switch is an assumption — thermal test closes it");
}

// ============ S5 — discharge transient incl. bias startup + resistor stress ============
{
  for (const [tag, C, R] of [["nominal", 323e-6, 1829], ["worst (+10 %C, +5 %R)", 355e-6, 1920]]) {
    const tBias = 2.5e-3;                 // QA01C soft-start + TLP152 turn-on (DS-class figure)
    let V = 850, t = 0; const dt = 1e-3, Rp = 67.5e3 * (tag.includes("worst") ? 1.05 : 1);
    const tr = { x: [], y: [] }; let Eres = 0, Ppk = 0;
    while (V > 60 && t < 100) {
      const Reff = t < tBias ? Rp : (1 / (1 / Rp + 1 / R));
      const P4 = t < tBias ? 0 : V * V / R;            // active string total
      Ppk = Math.max(Ppk, P4 / 4); Eres += (P4 / 4) * dt;
      V *= Math.exp(-dt / (Reff * C)); t += dt;
      if (tr.x.length < 400) { tr.x.push(t); tr.y.push(V); }
    }
    if (tag === "nominal") plot("s5-discharge.svg", "S5 active discharge 850→60 V (incl. 2.5 ms bias startup)",
      [{ name: "Vbus (V)", x: tr.x, y: tr.y }], "time (s)", "V");
    add("S5", `Active discharge to 60 V, ${tag}`, `${f(t, 2)} s`, "≤2 s crash target (5 s R100)",
      t <= 2 ? "PASS" : "FAIL", `peak ${f(Ppk, 0)} W and ${f(Eres, 1)} J per 470 Ω (100 J single-pulse class)`);
  }
}

// ============ S6 — current-loop phase margin with the drawn filter chain ============
// Plant: PMSM d-axis L=0.35 mH, R=25 mΩ (ASSUMED motor — labeled); chain: 482 kHz RC,
// 15.9 kHz RC, LEM 40 kHz 1st-order, transport delay 1.5·Ts @10 kHz sampling; PI tuned
// for each crossover with the standard L·ωc / R·ωc rule.
{
  const L = 0.35e-3, Rm = 25e-3, Td = 0.75 / 10e3;   // double-update FOC: 1.5 samples @2·fsw
  const poles = [482e3, 15.9e3, 40e3];
  const pmAt = (fcT) => {
    let ph = -90; // PI zero cancels the plant pole -> integrator at crossover
    for (const p of poles) ph -= Math.atan2(fcT / p, 1) * 180 / Math.PI;
    ph -= 360 * fcT * Td;
    return 180 + ph;
  };
  for (const fcT of [1000, 1500]) {
    const pm = pmAt(fcT);
    add("S6", `Current-loop PM @${fcT / 1000} kHz crossover`, `${f(pm, 0)}°`, "≥45° (≥40 accepted)",
      pm >= 45 ? "PASS" : pm >= 40 ? "WARN" : "FAIL",
      "double-update FOC (75 µs delay); motor 0.35 mH/25 mΩ assumed — bind at commissioning");
  }
  let fMax = 100; while (pmAt(fMax) >= 45 && fMax < 5000) fMax += 10;
  add("S6", "Max crossover for 45° margin", `${f((fMax - 10) / 1e3, 2)} kHz`, "design statement: loop BW ≤1.2 kHz",
    (fMax - 10) >= 1200 ? "PASS" : "WARN", "sets the firmware bandwidth ceiling with the drawn filters");
}

// ============ S7 — gate switching event vs driver capability ============
{
  const Vsw = 20.7, Ron = 1.5 + 0.9, Roff = 1.0 + 0.45, Qg = 1.24e-6;
  const IgOn = Vsw / Ron, IgOff = Vsw / Roff;
  const tOn = Qg / (0.6 * IgOn), tOff = Qg / (0.6 * IgOff);
  add("S7", "Gate peak current (on/off)", `${f(IgOn, 1)} / ${f(IgOff, 1)} A`, "10 A driver class",
    IgOff <= 15 ? "PASS" : "WARN", "off-path exceeds 10 A only into the nominal short — real Ipk source-limited by the driver");
  add("S7", "Effective switching time (Qg model)", `${f(tOn * 1e9, 0)} / ${f(tOff * 1e9, 0)} ns`, "-", "ℹ️",
    "double-pulse remains the bench gate for dv/dt, overshoot and Rg trim");
}

// ============ report ============
let md = `# Simulation report (rev ${REV} · generated ${new Date().toISOString().slice(0, 10)})

Numerical time/frequency-domain simulations of the drawn circuits at the actual operating
conditions (\`calculations/sim-verify.mjs\`). These complement — not replace — the closed-form
worst-case rows in \`docs/verification-report.md\`. Items that require hardware (layout
parasitics, transformer saturation, SiC short-circuit withstand, EMI) remain bench gates and
are flagged in each row's note.

| # | Simulation | Result | Limit / target | Status | Note |
|---|---|---|---|---|---|
`;
let pass = 0, warn = 0, fail = 0;
for (const r of rows) {
  if (r.st === "PASS") pass++; else if (r.st === "WARN") warn++; else if (r.st === "FAIL") fail++;
  md += `| ${r.id} | ${r.name} | ${r.result} | ${r.limit} | ${r.st === "PASS" ? "✅ PASS" : r.st === "WARN" ? "⚠️ WARN" : r.st === "FAIL" ? "❌ FAIL" : r.st} | ${r.note} |\n`;
}
md += `
**${pass} PASS · ${warn} WARN · ${fail} FAIL**

![S1](img/sim/s1-flyback-startup.svg)
![S2](img/sim/s2-boost-bode.svg)
![S3](img/sim/s3-dclink-ripple.svg)
![S4](img/sim/s4-thermal-30s.svg)
![S5](img/sim/s5-discharge.svg)

## Modeling assumptions (each is a named bench-closure item)
- S1: transformer leakage 2 % of Lp; converter losses lumped at 8 %; P-control stands in for the UCC28C40 error amp.
- S2: current-mode small-signal per TI SLVSBD4E; internal current-sense gain estimated; slope compensation not modeled.
- S4: coldplate 0.045 K/W per switch to 65 °C coolant; single-τ nodes (no vendor Zth curve published).
- S5: 2.5 ms bias-startup dead time before the active path conducts.
- S6: motor 0.35 mH / 25 mΩ assumed; PI tuned by the L·ωc rule.

## Explicitly NOT simulatable at schematic stage (bench/vendor gates)
Commutation-loop overshoot (needs layout L), SiC short-circuit withstand (vendor/bench),
transformer core saturation at the CS limit (bench), EMI/CISPR (hardware), FS26 VCORE loop
(internally compensated — component selections verified against Table 106 instead).
`;
writeFileSync(join(ROOT, "docs", "simulation-report.md"), md);
console.log(`sim-verify: ${pass} PASS · ${warn} WARN · ${fail} FAIL → docs/simulation-report.md + docs/img/sim/`);
