#!/usr/bin/env node
// sim-verify.mjs — operating-point SIMULATION layer (S1–S10), on top of the closed-form
// design-verify. These are numerical time/frequency-domain simulations of the drawn
// circuits at the actual operating conditions — not measured waveforms. Items that
// physically require hardware (layout parasitics, core saturation, SC withstand, EMI)
// stay on the bench list and are marked so.
// Run: node calculations/sim-verify.mjs  → docs/simulation-report.md + docs/img/sim/*.svg
import { writeFileSync, mkdirSync } from "node:fs";
import { REV } from "./rev.mjs";
import { OP, SKU, lossOf, rthT, tjLimit, MOD } from "./loss-model.mjs";
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

// ============ S1 — gate-power flyback: regulation, drain stress, and STARTUP (review A.6 F17/F18) ============
// Regulation/throughput: in DCM with a peak-current limit the energy per cycle is ½·Lp·Ipk²,
// independent of Vin as long as t_on + t_reset < T (true down to ≈6 V) — so the steady rail is
// legitimately Vin-independent; Vin only moves the drain stress. What that model can NOT show
// is STARTUP, so a separate cycle-by-cycle model follows (ported from the independent Opus
// cross-check of review A.6): VDD trickle charge, UCC28C40 UVLO bursts (0.4 V hysteresis),
// VREF-cap recharge and COMP soft-start per burst, CCM/DCM primary current, energy to the
// winding with the lowest reflected voltage (secondaries ÷2.9, aux ÷1.6), secondary loads.
{
  const Lp = 10e-6 / 3, fsw = 253e3, Ns = 2.9, Ilim = 1.0 / 0.33, Vf = 0.8, Llk = 0.2e-6, Vtar = 21.4;
  for (const Vin of [9, 16]) {
    const vRing = Math.min(Math.sqrt(Llk / 50e-12) * Ilim * 0.15, 19.5);
    const vpk = Vin + (Vtar + Vf) / Ns + Math.min(vRing, 19.5 + 0.7);
    add("S1", `Flyback drain peak @${Vin} V in`, `${f(vpk, 1)} V`, "80 V BUK7Y14-80E", vpk < 64 ? "PASS" : "WARN",
      "leakage 2 % assumed — bench-confirm the ring; SMAJ13A clamp path bounds it");
  }
  const cap = 0.5 * Lp * (0.9 * Ilim) ** 2 * fsw * 0.92;
  add("S1", "Flyback DCM throughput per bank (regulation)", `${f(cap, 2)} W at 90 % of the CS limit`, "Vin-independent 6–16 V",
    "PASS", "t_on + t_reset < T above ≈6 V, so the rail regulates at any KL30 once running — startup is the separate question below");

  function flyStart(V12, Rst, Cvdd, o = {}, tr = null) {
    // round 7 (A6-R07): the 67 k divider sits on the separate FFS sense node, so VDD carries no divider
    // (Rfb = ∞) and aux takeover is VDD back above 10 V while running (regulation ≈10.9 V); the A.5
    // historical case keeps its 71 k on VDD and the old 11 V criterion
    const k = { Von: 7.0, Voff: 6.6, Ist: 100e-6, Iic: 3.0e-3, Qg: 25e-9, Lp: 10e-6 / 3, Vcs: 1.0, Iea: 1e-3, Icc2: 1.5e-3, Rfb: Infinity, vOk: 10.0, ...o };
    const T = 1 / 253e3, nf = 1.6, Rfb = k.Rfb, C1 = 9.5e-6, C2 = 10e-6, Ccomp = 22e-9, Cvref = 100e-9;
    let t = 0, vdd = 0, v1 = 0, v2 = 0, iL = 0, comp = 0, run = false, bursts = 0, pk = 0;
    const loads = (dt) => {
      const vt = v1 + v2, icc = (v1 < 11.2 ? k.Icc2 : 3e-3) * Math.min(1, vt / 4), ib = vt / 5.1e3;
      v1 = Math.max(0, v1 - (ib + icc) * dt / C1);
      v2 = Math.min(5.1, Math.max(-0.7, v2 - (ib + icc + Math.max(v2, 0) / 10e3) * dt / C2));
    };
    const offtime = (toff) => {
      const dt = toff / 12;
      for (let n = 0; n < 12 && iL > 0; n++) {
        const vrs = (v1 + v2 + 0.8) / Ns, vra = (vdd + 0.5) / nf, vr = Math.min(vrs, vra);
        const h = Math.min(dt, iL * k.Lp / vr), q = (iL - vr * h / k.Lp / 2) * h;
        if (vra < vrs) vdd += q / nf / Cvdd; else { const qs = q / Ns / 3; v1 += qs / C1; v2 = Math.min(5.1, v2 + qs / C2); }
        iL = Math.max(0, iL - vr * h / k.Lp);
      }
    };
    while (t < 3 && bursts <= 400) {
      if (!run) {
        const dt = 20e-6;
        vdd += ((V12 - vdd) / Rst - k.Ist - vdd / Rfb) * dt / Cvdd; loads(dt); t += dt;
        if (tr && Math.round(t / dt) % 50 === 0) { tr.t.push(t * 1e3); tr.vdd.push(vdd); tr.vs.push(v1 + v2); }
        if (vdd >= k.Von) { run = true; bursts++; vdd -= Cvref * 5 / Cvdd; comp = 0; }
      } else {
        let iload = k.Iic + vdd / Rfb - (V12 - vdd) / Rst;
        if (comp < 4.8) { comp = Math.min(4.8, comp + k.Iea * T / Ccomp); iload += k.Iea; }
        const ipk = Math.min(k.Vcs, Math.max(0, (comp - 1.15) / 3)) / 0.33;
        let ton = 0, qg = 0;
        if (ipk > 0) { ton = Math.min(Math.max(iL < ipk ? (ipk - iL) * k.Lp / V12 : 0, 100e-9), 0.94 * T); iL += V12 * ton / k.Lp; qg = k.Qg; }
        offtime(T - ton);
        vdd -= (iload * T + qg) / Cvdd; loads(T); t += T; pk = Math.max(pk, v1 + v2);
        if (tr && Math.round(t / T) % 64 === 0) { tr.t.push(t * 1e3); tr.vdd.push(vdd); tr.vs.push(v1 + v2); }
        if (vdd >= k.vOk) return { ok: true, t, bursts };
        if (vdd < k.Voff) { run = false; offtime(20e-6); iL = 0; }
      }
    }
    return { ok: false, t, bursts, pk };
  }
  const WORST = { Iic: 3.5e-3, Vcs: 0.9, Iea: 0.5e-3, Lp: 8e-6 / 3, Von: 7.5, Voff: 7.1, Icc2: 3e-3, Qg: 45e-9 };
  const trN = { t: [], vdd: [], vs: [] }, trO = { t: [], vdd: [], vs: [] };
  const A5 = { Rfb: 71e3, vOk: 11.0 };
  flyStart(8.15, 2.2e3, 37e-6, WORST, trN); flyStart(8.15, 4.7e3, 3.8e-6, A5, trO);
  plot("s1-flyback-startup.svg", "S1 gate-power start-up at KL30 9 V: rev A.7 (2.2 k / 47 µF, FB off VDD, worst parts) vs rev A.5 (4.7 k / 4.7 µF, typical)",
    [{ name: "A.7 V_sec (V)", x: trN.t, y: trN.vs }, { name: "A.7 VDD (V)", x: trN.t, y: trN.vdd },
     { name: "A.5 V_sec (V) — never starts", x: trO.t.filter((x) => x <= trN.t.at(-1)), y: trO.vs.slice(0, trO.t.filter((x) => x <= trN.t.at(-1)).length) }], "time (ms)", "V");
  const cell = (r) => r.ok ? `${f(r.t * 1e3, 0)} ms (${r.bursts} burst${r.bursts > 1 ? "s" : ""})` : `NO START (secondaries peak ${f(r.pk ?? 0, 1)} V)`;
  for (const [tag, Rst, Cv, base] of [["A.5 as drawn: 4.7 k / 4.7 µF", 4.7e3, 3.8e-6, A5], ["A.7: 2.2 k / 47 µF (≈37 µF eff), FB on the FFS node", 2.2e3, 37e-6, {}]]) {
    const typ9 = flyStart(8.15, Rst, Cv, base), w9 = flyStart(8.15, Rst, Cv, { ...WORST, ...base }), w14 = flyStart(13.15, Rst, Cv, { ...WORST, ...base });
    const ok = typ9.ok && w9.ok && w14.ok;
    add("S1", `Gate-power STARTUP, ${tag}`, `KL30 9 V: typ ${cell(typ9)} · worst ${cell(w9)} · KL30 14 V worst ${cell(w14)}`,
      "one-burst start at every corner, KL30 9–16 V", ok ? "PASS" : "FAIL",
      tag.startsWith("A.5") ? "historical — the rev A.5 values could never start at 9 V (review F18 was right, and it was worse than 'marginal')"
        : "worst = UVLO 7.5/7.1 V, 3.5 mA IC, 45 nC FET, CS 0.9 V, Lp −20 %, 3 mA driver current; 22 µF (18 µF eff) still fails this corner");
    if (tag.startsWith("A.5")) rows[rows.length - 1].st = "ℹ️";
  }
}

// S1b — bank load per silicon at its SKU switching frequency (Qg·ΔV·f + ICC2 + 5.1 k bleeder per domain)
{
  const cap = 0.5 * (10e-6 / 3) * (0.9 * 3.03) ** 2 * 253e3 * 0.92;
  for (const [tag, q, fs, fo] of [["SiC @10 kHz (1.09 µC)", 1.09e-6, 10e3, 253e3], ["IGBT @5 kHz (4.36 µC, unscaled, RT 8.2 k)", 4.36e-6, 5e3, 308e3]]) {
    const Pload = 3 * (q * 20.5 * fs + 3.3e-3 * 20.5 + 20.5 ** 2 / 5.1e3) + 0.2;   // span 15.4 + 5.1 V (round 7 rail)
    const capW = 0.5 * (8e-6 / 3) * (0.9 / 0.33) ** 2 * fo * 0.92;
    add("S1", `Flyback bank load, ${tag}`, `${f(Pload, 2)} W demand`, `${f(capW, 2)} W at worst parts (CS 0.9 V, Lp −20 %, 100 % limit)`, Pload < 0.8 * capW ? "PASS" : "WARN", "same rails, same transformer for every SKU");
  }
}

// ============ S2 — UB15 boost loop Bode WITH the A.4.3 compensation ============
// Current-mode CCM model (TI SLVSBD4E §8.2.1.2.11 form): power stage Gps with output pole,
// RHP zero; EA gm=360 µS, Ro=10 MΩ, Zc = Rc + 1/sCc (∥ Cp). Vin 12/9 V, load 0.33 A.
{
  // Cout 35 µF is the PM-worst corner: 2 × 22 µF/50 V 1210 keep ≈ 20 µF at 15.4 V, which gives 80°+ (RR10)
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

// ============ S3 — DC-link ripple: switch-state simulation, center-aligned SVPWM ============
// Sinusoidal phase currents imposed; duty from min-max zero-sequence injection (SVPWM); the
// cap current is i_dc − mean. Swept over modulation index and power factor: the worst point
// (M ≈ 0.6, cosφ = 1) is 0.65·I — Kolar's closed form agrees (review F21; the old run used
// one sawtooth-carrier point).
{
  const ripple = (Iph, M, pf, fs = 10e3, f1 = 50, spc = 64) => {
    const phi = Math.acos(pf), N = Math.round(fs / f1) * spc;
    let s1 = 0, s2 = 0;
    for (let n = 0; n < N; n++) {
      const t = n / (fs * spc), th = 2 * Math.PI * f1 * t, tri = Math.abs(((n % spc) / spc) * 2 - 1);
      const vr = [0, 1, 2].map((k) => M * Math.sin(th - k * 2 * Math.PI / 3));
      const z = -(Math.max(...vr) + Math.min(...vr)) / 2;
      let idc = 0;
      for (let k = 0; k < 3; k++) if (tri < 0.5 + 0.5 * (vr[k] + z)) idc += Math.SQRT2 * Iph * Math.sin(th - k * 2 * Math.PI / 3 - phi);
      s1 += idc; s2 += idc * idc;
    }
    return Math.sqrt(s2 / N - (s1 / N) ** 2);
  };
  let kmax = 0, at = "";
  for (const M of [0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1]) for (const pf of [1.0, 0.9, 0.8, 0.6]) {
    const k = ripple(100, M, pf) / 100; if (k > kmax) { kmax = k; at = `M ${M}, cosφ ${pf}`; }
  }
  for (const [tag, I, n, rating] of [["8XX bank, 340 A", 340, 16, 15.4], ["4XX bank, 400 A", 400, 16, 18]]) {
    const perCan = kmax * I / n;
    add("S3", `Cap ripple per can, worst point — ${tag}`, `${f(perCan, 1)} A rms (bank ${f(kmax * I, 0)} A, ${at})`, `${rating} A/can @10 kHz/70 °C`,
      perCan <= rating ? "PASS" : "FAIL", `rated point (M 1.0, cosφ 0.85) ${f(ripple(I, 1.0, 0.85) / n, 1)} A/can; equal sharing assumed — 15–20 % busbar imbalance is a thermal-test item`);
  }
  const w = { x: [], y: [] };
  plot("s3-dclink-ripple.svg", "S3 cap RMS current / phase RMS vs modulation index (SVPWM, cosφ = 1 and 0.85)",
    [{ name: "cosφ 1.0", x: [0.3, 0.45, 0.6, 0.75, 0.9, 1.05], y: [0.3, 0.45, 0.6, 0.75, 0.9, 1.05].map((M) => ripple(100, M, 1.0) / 100) },
     { name: "cosφ 0.85", x: [0.3, 0.45, 0.6, 0.75, 0.9, 1.05], y: [0.3, 0.45, 0.6, 0.75, 0.9, 1.05].map((M) => ripple(100, M, 0.85) / 100) }], "M", "I_C / I_ph");
}

// ============ S4 — junction thermal transient through the 30 s peak, every SKU ============
// Shared loss model (loss-model.mjs). Junction-case and case-coldplate are STATIC resistances: since
// any real Zth(t) rises monotonically to its Rth, Tj = T_plate + P·(Rjc + Rch) bounds the junction
// from above at every instant GIVEN the plate temperature (round 8, R7-07: the old single 0.8 s junction
// pole was called "conservative", but a slower pole rises LESS early — it is not a bound). Only the
// coldplate mass is dynamic (0.045 K/W to 65 °C coolant, τ 60 s — ASSUMED; the thermal test closes it).
// The same objection applies to that pole, so the static-plate figure (no plate mass at all) is reported
// as the unconditional bound (cross-check R8X-11). Initialized at the CONTINUOUS steady state.
{
  const series = [], stat = [];
  for (const id of ["sic8", "igbt8", "igbt4", "sic4"]) {
    const s = SKU[id];
    const rjc = s.sil === "sic" ? MOD.sic.rthJC : MOD[s.sil].rthJC, rch = 0.015, rha = OP.rthPlate;
    const Lc = lossOf(s, s.iCont, s.vNom), Lp = lossOf(s, s.iPk, s.vMax);
    const Pc = Lc.sw_die, Pp = Lp.sw_die, Dc = Lc.d_die, Dp = Lp.d_die;   // RR07: the diode heats the shared coldplate (IGBT SKUs; SiC d_die = 0)
    const Ch = 60 / rha, dt = 0.01;
    let Th = (Pc + Dc) * rha, Tj = Th + Pc * (rjc + rch), pk = 0;
    const tr = { x: [], y: [] };
    const step = (P, D) => { Th += dt * (P + D - Th / rha) / Ch; Tj = Th + P * (rjc + rch); };
    for (let t = 0; t < 30; t += dt) { step(Pp, Dp); pk = Math.max(pk, Tj); if (Math.round(t / dt) % 10 === 0) { tr.x.push(t); tr.y.push(OP.coolant + Tj); } }
    for (let t = 30; t < 90; t += dt) { step(Pc, Dc); if (Math.round(t / dt) % 10 === 0) { tr.x.push(t); tr.y.push(OP.coolant + Tj); } }
    series.push({ name: `Tj ${s.name}`, x: tr.x, y: tr.y });
    const tj = OP.coolant + pk, lim = tjLimit(s);
    stat.push({ s, lim, tj: OP.coolant + (Pp + Dp) * rha + Pp * (rjc + rch) });
    add("S4", `Tj end of 30 s peak — ${s.name} (${s.iPk} A, ${s.vMax} V, ${s.fsw / 1e3} kHz)`, `${f(tj, 0)} °C (from ${f(OP.coolant + (Pc + Dc) * rha + Pc * (rjc + rch), 0)} °C continuous)`,
      `${lim} °C ${s.sil === "sic" ? "Tj max" : "Tvjop"}`, tj < 0.9 * lim ? "PASS" : tj < lim ? "WARN" : "FAIL",
      `${f(Pp, 0)} W/switch peak, ${f(Pc, 0)} W continuous (hottest die)${Dp ? ` + diode ${f(Dp, 0)}/${f(Dc, 0)} W into the shared coldplate (RR07)` : ""}; coldplate 0.045 K/W assumed`);
  }
  const worst = stat.reduce((a, b) => (b.tj / b.lim > a.tj / a.lim ? b : a));
  add("S4", "Tj static-plate bound (30 s peak held to steady state, no plate mass) — all SKUs", stat.map((x) => `${f(x.tj, 0)}`).join(" / ") + " °C",
    stat.map((x) => `${x.lim}`).join(" / ") + " °C", worst.tj < 0.9 * worst.lim ? "PASS" : worst.tj < worst.lim ? "WARN" : "FAIL",
    `${stat.map((x) => x.s.name).join(" / ")}; every SKU stays under its limit for ANY plate time constant — the modelled rows above take τ 60 s (assumed). Worst: ${worst.s.name} at ${f(100 * worst.tj / worst.lim, 0)} % of ${worst.lim} °C — the thermal test (T7-10) measures the plate pole`);
  plot("s4-thermal-30s.svg", "S4 junction transient per SKU: continuous → 30 s peak (at V_max) → continuous, 65 °C coolant",
    series, "time from peak start (s)", "°C");
}

// ============ S5 — discharge transient, both paths applied ONCE (review F25) ============
{
  for (const [tag, V0, Cn, Ra, Rp] of [["8XX", 850, 323e-6, 1880, 66e3], ["4XX", 500, 803e-6, 880, 45e3]]) {
    for (const [corner, kC, kR] of [["nominal", 1, 1], ["worst (C +10 %, R +5 %)", 1.1, 1.05]]) {
      const C = Cn * kC, ra = Ra * kR, rp = Rp * kR, tBias = 2.5e-3;
      let V = V0, t = 0, E = 0; const dt = 1e-4, tr = { x: [], y: [] };
      while (V > 60 && t < 200) {
        const R = t < tBias ? rp : 1 / (1 / ra + 1 / rp);
        E += (t < tBias ? 0 : V * V / ra / 4) * dt;
        V *= Math.exp(-dt / (R * C)); t += dt;
        if (tr.x.length < 600 && Math.round(t / dt) % 5 === 0) { tr.x.push(t); tr.y.push(V); }
      }
      if (tag === "8XX" && corner === "nominal") plot("s5-discharge.svg", "S5 active + passive discharge 850 → 60 V (8XX values, 2.5 ms bias start)", [{ name: "Vbus (V)", x: tr.x, y: tr.y }], "time (s)", "V");
      add("S5", `Active discharge ${V0}→60 V, ${tag}, ${corner}`, `${f(t, 2)} s`, "≤2 s crash target (5 s R100)", t <= 2 ? "PASS" : "FAIL",
        `${f(E, 1)} J per ${Ra / 4} Ω wirewound (100 J single-pulse class); bleeder ${f(Rp / 1e3, 1)} k counted once`);
    }
  }
}

// ============ S6 — current-loop phase margin per switching frequency (review F32) ============
// Plant PMSM d-axis 0.35 mH / 25 mΩ (ASSUMED — bind at commissioning); chain 482 kHz + 15.9 kHz
// RC, LEM 40 kHz, transport delay 1.5 samples with double update (0.75/fsw). PI cancels the
// plant pole. The IGBT SKUs run 5 kHz: they get their own ceiling, not the SiC one.
{
  const poles = [482e3, 15.9e3, 40e3];
  const pmAt = (fc, fs) => { let ph = -90; for (const p of poles) ph -= Math.atan2(fc / p, 1) * 180 / Math.PI; return 180 + ph - 360 * fc * 0.75 / fs; };
  for (const [tag, fs] of [["SiC 10 kHz", 10e3], ["SiC 8 kHz", 8e3], ["IGBT 5 kHz", 5e3]]) {
    let fMax = 50; while (pmAt(fMax, fs) >= 45 && fMax < 5000) fMax += 10;
    add("S6", `Max current-loop crossover for 45° PM — ${tag}`, `${f((fMax - 10) / 1e3, 2)} kHz`, "firmware gain set per SKU", "ℹ️",
      `PM at 1 kHz crossover: ${f(pmAt(1000, fs), 0)}°; double-update FOC; motor constants assumed`);
  }
}

// ============ S7 — gate switching event vs driver capability ============
{
  // NSI6611 DS §9.6: I = min[(VCC2−VEE)/(R_G + R_OH|OL + R_Gint), 10 A]
  const ig = (rg, rd, ri) => Math.min(20.5 / (rg + rd + ri), 10);
  add("S7", "Gate peak current on/off, SiC (3.3/6.8 Ω)", `${f(ig(3.3, 2.2, 1.1), 1)} / ${f(ig(6.8, 0.3, 1.1), 1)} A`, "10 A driver", "PASS", "DS formula with R_OH 2.2 / R_OL 0.3 Ω");
  add("S7", "Gate peak current on/off, IGBT (1.0/1.0 Ω)", `${f(ig(1, 2.2, 0.5), 1)} / ${f(ig(1, 0.3, 0.5), 1)} A`, "10 A driver", "PASS", "sink at the driver's own limit");
}

// ============ S8 — turn-off overshoot budget (review F01: the old term was 1000× too small) ============
// Vpk = Vbus + L·di/dt. di/dt from the DATASHEET fall time (90→10 %) at the characterized
// 3.3 Ω, scaled by the total turn-off resistance for 6.8 Ω. No separate "ring" term (it double-
// counted the same commutation energy). Module Ls is unpublished: the sweep gives the BUDGET.
{
  const sw = [];
  for (const [tag, tf] of [["3.3 Ω cold (DS tf 13 ns)", 13e-9], ["6.8 Ω cold (tf ≈23 ns)", 13e-9 * 8.2 / 4.7], ["6.8 Ω hot (tf ≈38 ns)", 22e-9 * 8.2 / 4.7]]) {
    const didt = 0.8 * 481 / tf, x = [], y = [];
    for (let L = 5; L <= 30; L += 0.5) { x.push(L); y.push(850 + L * 1e-9 * didt); }
    sw.push({ name: tag, x, y });
    const lBudget = (1080 - 850) / didt;
    add("S8", `Loop-L budget @850 V/481 A for 1080 V — ${tag}`, `${f(lBudget * 1e9, 1)} nH (${f(didt / 1e9, 1)} kA/µs)`, "module + busbar ≥ 15 nH class",
      lBudget >= 15e-9 ? "PASS" : "WARN", "DPT GATE: measured Vds,pk at 850 V/481 A, −20 °C and hot, sets RG_OFF (3.3–10 Ω); IGBT tf 200–385 ns ⇒ < 40 V");
  }
  plot("s8-double-pulse.svg", "S8 SiC turn-off Vds peak vs loop inductance (850 V, 481 A; DS fall time)", sw, "L_loop (nH)", "V");
}

// ============ S9 — short-circuit reaction: worst-case DESAT detection + soft turn-off ============
// Detection = LEB + C·V_TH,max/I_CHG,min (C +5 %) + deglitch max; soft-off = gate charge above
// the SC-carrying level / I_STO (400 mA typ; the DS minimum is 100 mA — contained SC test gate).
{
  const tr = { x: [], y: [] };
  // IGBT soft-off charge from the round-7 high-corner rail (16.7 V) to the ~10 V SC plateau
  for (const [tag, C, qSto, lim] of [["SiC 47 pF", 47e-12, 0.46e-6, null], ["IGBT 82 pF", 82e-12, 106e-9 * 6.74, 6e-6]]) {
    const det = 0.2e-6 + C * 1.05 * 10 / 350e-6 + 0.32e-6, sto = qSto / 0.4, stoMin = qSto / 0.1;
    add("S9", `DESAT reaction, ${tag}`, `${f((det + sto) * 1e6, 2)} µs (detect ${f(det * 1e6, 2)} + soft-off ${f(sto * 1e6, 2)}; ${f((det + stoMin) * 1e6, 1)} µs at 100 mA)`,
      lim ? "6 µs @800 V/15 V/175 °C (DS)" : "tSC not published — vendor letter", "WARN",   // RR04: both corners must close — release gate
      lim ? "150 pF gave 4.5 µs detection alone (F03). Global DRV_EN drop is held 22–53 µs past this (RC delay into the USCH Schmitt buffer) so it cannot cut the soft-off short"
        : "timeline only — not a device SC validation (F05); contained SC test at 850 V/150 °C is the release gate");
  }
}

// ============ S10 — ASC hold-up through total LV loss (review F14) ============
// Two series reservoirs per LS domain (VCC2–Kelvin 2×4.7 µF + 0.1 µF, Kelvin–VEE 10 µF), DC-bias
// derated. Loads: driver ICC2 + 5.1 k bleeder through BOTH caps; 10 k gate pull-down + DESAT
// source on VCC2 only (gate held high in ASC). The old 15 ms put the VEE cap in parallel with
// VCC2 and ignored the bleeder and gate loads. The ASC COMMAND path collapses first: re-derived
// in round 17 for the A.12 parts (VOW3120 + UCC14141-Q1, VIN UVLO falling < 8 V) and the round-17
// LV bulk (CLVC1/2/3 ≈ 127 µF card + ≈ 45 µF power board at ≈ 1.05 A whole-inverter load):
// dV/dt ≈ 6 V/ms, so the 12 V rail reaches the 8 V UCC14141 floor in ≈ 0.7 ms and the FS26
// VPRE/V5A latch supply follows within ≈ 1 ms — the "≈ 1 ms" figure stands, now with its basis.
{
  // start at the round-7 rail: 15.4 V nominal, 13.54 V low corner (design-verify §5)
  const hold = (k15, kvee, icc, ides, uvlo, tolc = 1, v0 = 15.4) => {
    const C1n = 9.4e-6 * tolc, C2n = 10e-6 * tolc;
    const c1 = (v) => C1n * (1 - (1 - k15) * Math.min(v, v0) / v0) + 0.1e-6;
    const c2 = (v) => C2n * (1 - (1 - kvee) * Math.min(Math.max(v, 0), 5.1) / 5.1);
    let v1 = v0, v2 = 5.1, t = 0; const dt = 1e-6, tr = { x: [], y: [] };
    while (v1 > uvlo && t < 0.1) {
      const ia = icc + (v1 + v2) / 5.1e3, ib = v1 / 10e3 + v1 / 1e6 + ides;
      v1 -= (ia + ib) * dt / c1(v1); v2 = Math.max(-0.7, v2 - ia * dt / c2(v2)); t += dt;
      if (Math.round(t / dt) % 50 === 0) { tr.x.push(t * 1e3); tr.y.push(v1); }
    }
    return { t, tr };
  };
  const typ = hold(0.45, 0.75, 3.3e-3, 0.5e-3, 10.4), worst = hold(0.35, 0.65, 7e-3, 0.65e-3, 11.8, 0.9, 13.54);
  plot("s10-asc-holdup.svg", "S10 LS driver VCC2 decay after total LV loss (typ, DC-bias-derated MLCC)", [{ name: "VCC2 (V)", x: typ.tr.x, y: typ.tr.y }], "time (ms)", "V");
  add("S10", "ASC hold-up after TOTAL LV loss (gate reservoirs)", `${f(typ.t * 1e3, 1)} ms typ · ${f(worst.t * 1e3, 1)} ms worst`, "-", "WARN",
    "and the ASC command path collapses within ≈ 1 ms (round 17 re-derivation: ≈ 170 µF of LV bulk at ≈ 1.05 A gives 6 V/ms, the UCC14141-Q1 bias input reaches its 8 V UVLO in ≈ 0.7 ms and the V5A latch supply follows): sustained ASC REQUIRES KL30 (FS26 GPIO1 holds the flybacks) — the safety-concept assumption IR-05/IR-32 (LV loss = SPO; LV loss with the battery disconnected above n_x is a double event, IR-33). With LV dead the bridge is three-phase-open — energy-safe only if the motor's E_LL,pk at n_max (cold magnets) < the 1000 V cap rating; otherwise fit the HV-fed backup-bias option (motor-dependent, see firmware contract)");
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
![S8](img/sim/s8-double-pulse.svg)
![S10](img/sim/s10-asc-holdup.svg)

## Modeling assumptions (each is a named bench-closure item)
- S1: startup model ported from the review A.6 independent cross-check (cycle-by-cycle, CCM/DCM, COMP soft-start, VREF recharge, UVLO bursts); FET gate charge 25 nC typ / 45 nC worst at ~7 V drive; NSI6611 ICC2 in UVLO 1.5 mA typ / 3 mA worst (unpublished).
- S3: sinusoidal phase currents, equal sharing between cans (imbalance → thermal test).
- S4: coldplate 0.045 K/W per switch to 65 °C coolant with a 60 s time constant — ASSUMED; losses from loss-model.mjs at V_max.
- S5: 2.5 ms bias-startup dead time before the active path conducts.
- S6: motor 0.35 mH / 25 mΩ assumed; PI tuned by the L·ωc rule.
- S8: di/dt from the DS fall time, scaled with the total turn-off resistance for 6.8 Ω — an estimate until DPT.
- S9: soft-off charge = Cies·ΔV above the SC-carrying gate level at 400 mA; the DS minimum 100 mA is flagged, not assumed away.
- S10: MLCC DC-bias retention 45 % (typ) / 35 % (worst) at the 15.4 V rail (worst starts at the 13.54 V low corner).

## What simulation cannot close (bench/vendor gates — the numbers above bound them)
S8 gives the loop-inductance BUDGET; the real loop and waveform come from double-pulse (module Ls unpublished).
S9 gives the reaction TIMELINE; SiC withstand needs the vendor letter, the IGBT build a contained SC test.
S10 gives the hold WINDOW; whether three-phase-open is energy-safe with LV dead is a MOTOR property (E_LL at n_max).
Still hardware-only: transformer core saturation at the CS limit, EMI/CISPR, measured
regulator Bode/load steps, FS26 VCORE loop (internally compensated — parts per Table 106).
`;
writeFileSync(join(ROOT, "docs", "simulation-report.md"), md);
console.log(`sim-verify: ${pass} PASS · ${warn} WARN · ${fail} FAIL → docs/simulation-report.md + docs/img/sim/`);
