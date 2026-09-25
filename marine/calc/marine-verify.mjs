#!/usr/bin/env node
// marine-verify.mjs — MARINE SERIES sizing and verification. A separate product series from the
// Road GEN-1 platform: own cells, ratings, operating basis and report (marine/verification-report.md).
// Silicon data: marine/design-basis.md §4 (datasheet citations). Loss equations: the Road model's
// sinusoidal-PWM averages (calculations/loss-model.mjs igbtLoss — re-checked below, not edited),
// with two marine additions for continuous duty: switching energy ∝ V^1.3 instead of linear, and
// the transistor/diode heat sharing one coldplate footprint. Road-mirrored protection numbers (FW-05/06/16, ASC entry,
// DESAT corners, §6 release rule, barrier register) follow Road rev A.14 (marine/design-basis.md §9).
// Run: node marine/calc/marine-verify.mjs
import { writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { igbtLoss, MOD } from "../../calculations/loss-model.mjs";
const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const f = (x, d = 1) => Number(x.toFixed(d));

// ---------------- marine operating basis (design-basis §3) ----------------
// coolant 45 °C: IACS UR M40 sea water 32 °C + central-cooler approach; Tj 125 °C continuous
// (25 K under Tvjop for a 20-year life), 150 °C at overload; propeller duty 110 % / 60 s.
const OPM = { coolant: 45, rthCH: 0.015, rthPlate: 0.045, pf: 0.85, mres: 0.95, m: 1.1,
  tjCont: 125, tjOvl: 150, ovl: 1.1, kv: 1.3, capCont: 0.8, lifeSF: 2 };
// power-cycling missions (20 years): ferry = one dock↔cruise cycle per crossing (30/day, cruise 85 %);
// tug = 6 jobs × 20 full-power pulses per day, 330 days (research brief §8.3, EJ — owner profile replaces)
const DUTY = { ferry: { mission: 30 * 365 * 20, load: 0.85 }, tug: { mission: 6 * 20 * 330 * 20, load: 1.0 } };
const pAvail = (V, I) => Math.sqrt(1.5) * OPM.mres * V * I * OPM.pf / 1e3;   // kW, linear SVPWM, 5 % reserve

// ---------------- modules (per switch: v0/r knee+slope hot, energies at iRef/vRef at the drawn gate) ----
const MODS = {
  // Road-verified HCG600FH120D3E1EA constants (calculations/loss-model.mjs MOD.igbt), energies @600 V/600 A
  hcg600_12: { name: "HCG600FH120D3E1EA", vces: 1200, inom: 600, v0: MOD.igbt.v0, r: MOD.igbt.r, v0d: MOD.igbt.v0d, rd: MOD.igbt.rd,
    eon: 100e-3, eoff: 104e-3, erec: 32e-3, iRef: 600, vRef: 600, rthJC: MOD.igbt.rthJC, rthJCd: MOD.igbt.rthJCd,
    tvjop: 150, tsc: 6e-6, tscV: 800, cies: 106e-9, qg: MOD.igbt.qg, itrms: null, src: "Road loss-model (HCG600 DS)" },
  // HCG600FH170D3E1 / …E1A (1700 V/600 A, same D3 pin map): the WORSE of the two per term, so either
  // part fits. VCEsat/VF terminal @600 A/175 °C 2.36/1.95 V (E1A T4); Eon 197.6 (E1) ×1.47 for our
  // gate network ≈ 290 mJ (covers E1A Fig.5 at 1.9 Ω equivalent), Eoff 217.9, Erec 75.2 mJ (E1A T5/T6)
  // @900 V/600 A/175 °C; Rth 0.062/0.092 (E1A T3); SC 3200 A ≤6 µs @1000 V/175 °C; Qg 5.01 µC; Cies 79.2 nF
  hcg600_17: { name: "HCG600FH170D3E1/E1A", vces: 1700, inom: 600, v0: 0.9, r: (2.36 - 0.9) / 600, v0d: 0.9, rd: (1.95 - 0.9) / 600,
    eon: 290e-3, eoff: 217.9e-3, erec: 75.2e-3, iRef: 600, vRef: 900, rthJC: 0.062, rthJCd: 0.092,
    tvjop: 150, tsc: 6e-6, tscV: 1000, cies: 79.2e-9, qg: 5.01e-6, itrms: null, src: "HCG600FH170D3E1 + E1A DS" },
  // HCG900FH120D3E1A (1200 V/900 A): terminal 2.05/2.10 V @900 A/175 °C; T5 energies @0.51 Ω are
  // 120/130/48 mJ — Eon ×2.25 for our gate network (the Road 600 A part went ×2.5 from 0.51 to 1.9 Ω;
  // the DS Fig.4 curve itself reads ≈270 mJ); Rth 0.046/0.072; SC 3200 A ≤6 µs @800 V; Qg 10.8 µC
  hcg900_12: { name: "HCG900FH120D3E1A", vces: 1200, inom: 900, v0: 0.9, r: (2.05 - 0.9) / 900, v0d: 0.9, rd: (2.10 - 0.9) / 900,
    eon: 270e-3, eoff: 130e-3, erec: 48e-3, iRef: 900, vRef: 600, rthJC: 0.046, rthJCd: 0.072,
    tvjop: 150, tsc: 6e-6, tscV: 800, cies: 124e-9, qg: 10.8e-6, itrms: null, src: "HCG900FH120D3E1A DS" },
  // HCS800FH170D3C1 (1700 V/800 A SiC): R_DS(on) 6.3 mΩ chip @175 °C + 0.5 mΩ leads; Eon/Eoff/Err
  // 58.5/80.6/9.7 mJ @900 V/800 A/3.3 Ω/150 °C; V_SD 5.3 V; Rth(j-c) 0.038 + 0.015 (DS); SC NOT published
  // Road HCS600FH120D3C1 at the Road gate network (calculations/loss-model.mjs MOD.sic)
  hcs600_12: { name: "HCS600FH120D3C1 (SiC)", sil: "sic", vces: 1200, inom: 600, rds: MOD.sic.rdsHot, eon: MOD.sic.eswPerA600 * 600, eoff: 0,
    err: MOD.sic.errPerA600 * 600, vsd: MOD.sic.vsd, tdead: MOD.sic.tdead, iRef: 600, vRef: 600, rthJC: MOD.sic.rthJC, rthJCd: MOD.sic.rthJC, tvjop: 175,
    tsc: null, cies: null, qg: 1.09e-6, itrms: null, pcFactor: 0.33, src: "Road loss-model (HCS600 DS)" },
  hcs800_17: { name: "HCS800FH170D3C1 (SiC)", sil: "sic", vces: 1700, inom: 800, rds: 6.8e-3, eon: 58.5e-3, eoff: 80.6e-3, err: 9.7e-3,
    vsd: 5.3, tdead: 1e-6, iRef: 800, vRef: 900, rthJC: 0.038, rthJCd: 0.038, tvjop: 175, tsc: null, cies: 61e-9, qg: 2.048e-6, itrms: null, pcFactor: 0.33, src: "HCS800FH170D3C1 DS" },
};

// ---------------- marine cells ----------------
// can: per-can C / ripple rating / U_N at 85 °C; sensor: LEM range (A pk)
const CELLS = {
  m8: { name: "M8 IGBT", mod: "hcg600_12", vMin: 500, vNom: 720, vMax: 850, ovTrip: 880, fsw: 5e3,
    can: { n: 16, c: 20e-6, irms: 15.4, vr85: 1000, part: "Faratronic C3D1M206KFSA382 20 µF/1100 V (Road 8XX bank)" }, sensor: 900, tscDer: 5 / 6, nDiode: 2, launch: true,
    dis: { rpN: 6, rpPer: 22e3, raN: 4, raPer: 470, qdis: "1200 V / 42 A (Road part)" } },
  // new 1100 V power stage (design-basis §5): 1700 V IGBT, 1300 V-class cans, 3 DESAT diodes
  m10: { name: "M10 IGBT", mod: "hcg600_17", vMin: 650, vNom: 950, vMax: 1100, ovTrip: 1150, fsw: 3e3,
    can: { n: 20, c: 15e-6, irms: 15, vr85: 1300, part: "15 µF/1300 V class can (RFQ)" }, sensor: 900, tscDer: 0.8, nDiode: 3, launch: true,
    dis: { rpN: 8, rpPer: 22e3, raN: 5, raPer: 470, qdis: "≥ 1700 V class switch" } },
  // small craft on request: the Road 8XX SiC build, frozen like M8; short motor cables only (§6c)
  m8sic: { name: "M8-SiC (small craft)", mod: "hcs600_12", vMin: 500, vNom: 720, vMax: 850, ovTrip: 880, fsw: 10e3,
    can: { n: 16, c: 20e-6, irms: 15.4, vr85: 1000, part: "Faratronic C3D1M206KFSA382 20 µF/1100 V (Road 8XX bank)" }, sensor: 900, nDiode: 2, launch: true, sicBuild: true,
    dis: { rpN: 6, rpPer: 22e3, raN: 4, raPer: 470, qdis: "1200 V / 42 A (Road part)" } },
  // evaluation only — what the MW end could use; caps/sensor are sized to fit, the thermal limit is the answer
  m8hp: { name: "M8-HP (eval: 1200 V/900 A IGBT)", mod: "hcg900_12", vMin: 500, vNom: 720, vMax: 850, ovTrip: 880, fsw: 3e3, sensor: 1500, tscDer: 5 / 6, nDiode: 2 },
  m10sic: { name: "M10-SiC (eval: 1700 V/800 A SiC)", mod: "hcs800_17", vMin: 650, vNom: 950, vMax: 1100, ovTrip: 1150, fsw: 8e3, sensor: 1500, nDiode: 3 },
  m10p2: { name: "M10-2P (eval: 2 × 1700 V/600 A per switch)", mod: "hcg600_17", par: 2, imb: 1.1, vMin: 650, vNom: 950, vMax: 1100, ovTrip: 1150, fsw: 3e3, sensor: 1500, tscDer: 0.8, nDiode: 3 },
};

// ---------------- loss + thermal ----------------
function loss(m, Irms, V, fsw, mcos, kv = OPM.kv) {
  const Ipk = Irms * Math.SQRT2, k = (Ipk / Math.PI) / m.iRef * (V / m.vRef) ** kv * fsw;
  if (m.sil === "sic") {   // synchronous: ½·I²·R per switch (Road sicLoss), body diode = same die
    const condT = 0.5 * Irms * Irms * m.rds, swT = (m.eon + m.eoff) * k, rec = m.err * k + 2 * m.tdead * fsw * m.vsd * Ipk / Math.PI;
    return { condT, swT, condD: 0, rec, T: condT + swT + rec, D: 0 };
  }
  const condT = m.v0 * Ipk * (1 / (2 * Math.PI) + mcos / 8) + m.r * Ipk ** 2 * (1 / 8 + mcos / (3 * Math.PI));
  const condD = m.v0d * Ipk * (1 / (2 * Math.PI) - mcos / 8) + m.rd * Ipk ** 2 * (1 / 8 - mcos / (3 * Math.PI));
  const swT = (m.eon + m.eoff) * k, rec = m.erec * k;
  return { condT, swT, condD, rec, T: condT + swT, D: condD + rec };
}
// self-check: with linear V scaling and the Road reference this IS the Road igbtLoss
{ const a = loss(MODS.hcg600_12, 300, 700, 5e3, 0.85, 1), b = igbtLoss(300, 700, 5e3, 0.85);
  if (Math.abs(a.T - b.sw_die) > 1e-9 || Math.abs(a.D - b.d_die) > 1e-9) throw new Error("marine loss() drifted from the Road igbtLoss"); }
// T and D of one switch position share one coldplate footprint
const tj = (m, L) => { const plate = OPM.coolant + (L.T + L.D) * OPM.rthPlate;
  return { T: plate + L.T * (m.rthJC + OPM.rthCH), D: plate + L.D * (m.rthJCd + OPM.rthCH), plate }; };
const mc = OPM.m * OPM.pf;
const tjAt = (m, I, V, fsw) => ({ mot: tj(m, loss(m, I, V, fsw, mc)), reg: tj(m, loss(m, I, V, fsw, -mc)) });
const die = (s, I) => I * (s.imb ?? 1) / (s.par ?? 1);   // current per module for paralleled builds
const tjMax = (m, I, V, fsw) => { const t = tjAt(m, I, V, fsw); return Math.max(t.mot.T, t.reg.D); };
// LESIT (Held 1997): N_f of one duty cycle (idle ↔ d.load × I) at pack nominal
// SiC ×0.33 (Semikron AN 21-001 chip factor, research brief §8.3)
const lesit = (m, s, I, d = DUTY.ferry) => { const L = loss(m, die(s, I * d.load), s.vNom, s.fsw, OPM.m * OPM.pf), dT = tj(m, L).T - OPM.coolant;
  return { nf: (m.pcFactor ?? 1) * 302500 * dT ** -5.039 * Math.exp(9.89e-20 / (1.380649e-23 * (OPM.coolant + dT / 2 + 273.15))), dT, tj: dT + OPM.coolant, L }; };
const solve = (fn, lo, hi) => { for (let i = 0; i < 60; i++) { const mid = (lo + hi) / 2; fn(mid) ? (lo = mid) : (hi = mid); } return lo; };

// Kolar closed form (Road design-verify §2) and a switch-state voltage-ripple run (Road S3 method)
const kolar = (M, c) => Math.sqrt(2 * M * (Math.sqrt(3) / (4 * Math.PI) + c * c * (Math.sqrt(3) / Math.PI - 9 * M / 16)));
let kMax = 0; for (let M = 0.05; M <= 1.15; M += 0.005) for (let c = 0; c <= 1.0001; c += 0.01) kMax = Math.max(kMax, kolar(M, c));
function vRipple(Iph, M, pf, C, fs, spc = 128, f1 = 50) {   // worst in-period ΔV_pp; battery supplies the mean
  const phi = Math.acos(pf), per = Math.round(fs / f1), I0 = 0.75 * M * Math.SQRT2 * Iph * pf;
  let worst = 0;
  for (let p = 0; p < per; p++) {
    let q = 0, qmin = 0, qmax = 0;
    for (let n = 0; n < spc; n++) {
      const th = 2 * Math.PI * (p + n / spc) / per, tri = Math.abs((n / spc) * 2 - 1);
      const vr = [0, 1, 2].map((k) => M / 1.1547 * Math.sin(th - k * 2 * Math.PI / 3) * 1.1547);
      const z = -(Math.max(...vr) + Math.min(...vr)) / 2;
      let idc = 0;
      for (let k = 0; k < 3; k++) if (tri < 0.5 + 0.5 * (vr[k] + z)) idc += Math.SQRT2 * Iph * Math.sin(th - k * 2 * Math.PI / 3 - phi);
      q += (I0 - idc) / (fs * spc); qmin = Math.min(qmin, q); qmax = Math.max(qmax, q);
    }
    worst = Math.max(worst, (qmax - qmin) / C);
  }
  return worst;
}

// ---------------- check engine ----------------
const rows = [];
const add = (sec, name, value, limit, status, note = "") => rows.push({ sec, name, value, limit, status, note });
const judge = (sec, name, value, limit, ratio, warnAt, note = "") =>
  add(sec, name, value, limit, ratio <= warnAt ? "PASS" : ratio <= 1 ? "WARN" : "FAIL", `${(ratio * 100).toFixed(0)}% of limit${note ? " · " + note : ""}`);
const RATING = {};
const R_TUG = (id) => RATING[id].iTug;

// ============ 1. CELL RATINGS ============
const LAUNCH = Object.entries(CELLS).filter(([, s]) => s.launch);
const EVAL = [];
for (const [id, s] of Object.entries(CELLS)) {
  const m = MODS[s.mod], sic = m.sil === "sic", n = 6 * (s.par ?? 1);
  const sec = `Cell rating — ${s.name} (${s.par ? `${s.par} × ` : ""}${m.name}, ${s.vMin}–${s.vMax} V, ${s.fsw / 1e3} kHz)`;
  const lim = {
    thermal: solve((I) => tjMax(m, die(s, I), s.vMax, s.fsw) <= OPM.tjCont, 1, 3 * m.inom * (s.par ?? 1)),
    life: solve((I) => lesit(m, s, I).nf >= OPM.lifeSF * DUTY.ferry.mission / 0.8, 1, 3 * m.inom * (s.par ?? 1)),
    ...(s.launch ? { caps: OPM.capCont * s.can.n * s.can.irms / kMax, sensor: 0.9 * s.sensor / (1.25 * Math.SQRT2 * OPM.ovl) } : {}),
    ...(m.itrms ? { terminal: 0.8 * m.itrms * (s.par ?? 1) } : {}),
  };
  const [bind, iCont] = Object.entries(lim).reduce((x, y) => (y[1] < x[1] ? y : x));
  const I = Math.floor(iCont / 5) * 5;
  const iTug = Math.floor(Math.min(I, solve((x) => lesit(m, s, x, DUTY.tug).nf >= OPM.lifeSF * DUTY.tug.mission / 0.8, 1, 3 * m.inom * (s.par ?? 1))) / 5) * 5;
  RATING[id] = { I, p: pAvail(s.vNom, I), pMin: pAvail(s.vMin, I), pMax: pAvail(s.vMax, I), iTug, pTug: pAvail(s.vNom, iTug) };
  const Ic = I * DUTY.ferry.load, Lc = loss(m, die(s, Ic), s.vNom, s.fsw, mc), pOut = pAvail(s.vNom, Ic) * 1e3, pSemi = n * (Lc.T + Lc.D);
  const eff = 100 * pOut / (pOut + pSemi);
  if (!s.launch) {   // evaluation: the thermal limit is the answer; caps/sensor sized to fit
    const cans = Math.ceil(kMax * I / (OPM.capCont * 15.4));
    EVAL.push([s.name, I, RATING[id], eff, cans, s]);
    continue;
  }
  add(sec, "Continuous current (S1) and its binding limit", `**${I} A rms** (bound by ${bind})`,
    Object.entries(lim).map(([k, v]) => `${k} ${f(v, 0)} A`).join(" · "), "INFO",
    `thermal: Tj ≤ ${OPM.tjCont} °C at ${OPM.coolant} °C coolant, ${s.vMax} V, motoring and regeneration; life: LESIT ≥ ${OPM.lifeSF}× mission with 20 % margin; caps: Kolar worst ${f(kMax, 3)}·I at ${OPM.capCont * 100} % of the can rating; sensor: the FW-05 trip ±${f(1.25 * Math.SQRT2 * OPM.ovl * I, 0)} A (1.25 × √2 × the ${f(OPM.ovl * 100, 0)} % current, instantaneous amperes — Road round 12) inside 90 % of range`);
  add(sec, "Continuous power (S1)", `**${f(RATING[id].p, 0)} kW @${s.vNom} V** · ${f(RATING[id].pMin, 0)} kW @${s.vMin} V · ${f(RATING[id].pMax, 0)} kW @${s.vMax} V`, "-", "INFO",
    `PF ${OPM.pf}, 5 % modulation reserve; P ∝ V_dc below the motor's base-speed voltage`);
  const t = tjAt(m, die(s, I), s.vMax, s.fsw), L = loss(m, die(s, I), s.vMax, s.fsw, mc);
  judge(sec, `${sic ? "MOSFET" : "IGBT"} Tj, continuous motoring (${I} A, ${s.vMax} V)`, `${f(t.mot.T, 0)} °C (${f(L.T, 0)} W: cond ${f(L.condT, 0)} + sw ${f(L.swT, 0)})`,
    `${m.tvjop} °C ${sic ? "Tj max" : "Tvjop"}`, t.mot.T / m.tvjop, OPM.tjCont / m.tvjop + 1e-9, "switching ∝ V^1.3; T+D share the coldplate footprint");
  if (sic) add(sec, "Regeneration", "same die (synchronous rectification)", "-", "INFO", "reverse conduction is in the MOSFET row above; SiC loss is symmetric in motoring and braking");
  else judge(sec, "Diode Tj, continuous regeneration", `${f(t.reg.D, 0)} °C (${f(loss(m, die(s, I), s.vMax, s.fsw, -mc).D, 0)} W)`, `${m.tvjop} °C`, t.reg.D / m.tvjop, OPM.tjCont / m.tvjop + 1e-9,
    "crash stop / braking puts full current through the FWD");
  const Io = I * OPM.ovl, to = tjMax(m, die(s, Io), s.vMax, s.fsw);
  judge(sec, `Overload ${f(OPM.ovl * 100, 0)} % / 60 s (${f(Io, 0)} A), steady-state bound`, `${f(to, 0)} °C`, `${OPM.tjOvl} °C`, to / OPM.tjOvl, 0.97, "a 60 s step is inside the module+plate τ; the steady-state bound over-states it");
  const { nf, dT, tj: tc } = lesit(m, s, I);
  judge(sec, `Power cycling, ferry duty: dock↔cruise (${f(DUTY.ferry.load * 100, 0)} % load, ${s.vNom} V, ΔTj ${f(dT, 0)} K, Tj ${f(tc, 0)} °C)`,
    `LESIT N_f ≈ ${f(nf / 1e6, 2)} M cycles`, `${OPM.lifeSF} × ${f(DUTY.ferry.mission / 1e6, 2)} M (30 crossings/day × 20 years)`, OPM.lifeSF * DUTY.ferry.mission / nf, 0.8,
    `LESIT (Held 1997) — ≈2× more conservative than Semikron AN 21-001 at these mean temperatures${m.pcFactor ? `; SiC ×${m.pcFactor}` : ""}; hiitio power-cycling curves are the gate`);
  const lt = lesit(m, s, R_TUG(id), DUTY.tug);
  judge(sec, `**Tug-duty rating**: ${RATING[id].iTug} A → ${f(RATING[id].pTug, 0)} kW (full-power pulses, ΔTj ${f(lt.dT, 0)} K)`,
    `LESIT N_f ≈ ${f(lt.nf / 1e6, 2)} M pulses`, `${OPM.lifeSF} × ${f(DUTY.tug.mission / 1e6, 2)} M (6 jobs × 20 pulses/day × 330 days × 20 years)`,
    OPM.lifeSF * DUTY.tug.mission / lt.nf, 0.8, `${f(100 * RATING[id].iTug / I, 0)} % of the ferry rating — the pulse count is an estimate; an owner's measured profile moves this line, not the hardware`);
  add(sec, `Semiconductor efficiency at cruise (${f(pOut / 1e3, 0)} kW)`, `${f(eff, 2)} % (${f(pSemi, 0)} W)`, "-", "INFO", `${n} switches, hot VCEsat`);
}
{ // evaluation cells side by side (MW-end options)
  const sec = "Evaluation — per-cell options for the MW end (thermal + life limits; caps and sensor sized to fit)";
  for (const [name, I, r, eff, cans, s] of EVAL)
    add(sec, name, `**${I} A rms → ${f(r.p, 0)} kW @${s.vNom} V** (${f(r.pMin, 0)}–${f(r.pMax, 0)} kW) · tug ${r.iTug} A → ${f(r.pTug, 0)} kW · ${f(eff, 2)} % at cruise`,
      `Tj ≤ ${OPM.tjCont} °C @${OPM.coolant} °C and life, ${s.fsw / 1e3} kHz`, "INFO",
      `needs ≈${cans} × 15 A cans (vs 16 on the Road bank) and a ±${s.sensor} A-class sensor${s.par ? "; two modules per switch share one driver via a gate booster (new power PCB)" : ""}`);
}

// ============ 2. DC LINK ============
for (const [id, s] of LAUNCH) {
  const sec = `DC link — ${s.name} (${s.can.n} × ${s.can.part})`, I = RATING[id].I, C = s.can.n * s.can.c;
  judge(sec, `Ripple per can, continuous ${I} A (Kolar worst M/cosφ)`, `${f(kMax * I / s.can.n)} A`, `${s.can.irms} A`, kMax * I / s.can.n / s.can.irms, OPM.capCont + 1e-9);
  judge(sec, `Ripple per can, overload ${f(I * OPM.ovl, 0)} A`, `${f(kMax * I * OPM.ovl / s.can.n)} A`, `${s.can.irms} A`, kMax * I * OPM.ovl / s.can.n / s.can.irms, 0.95, "60 s is inside the can's thermal τ");
  judge(sec, `Can U_N(85 °C) vs OV trip ${s.ovTrip} V`, `${s.ovTrip} V`, `${s.can.vr85} V`, s.ovTrip / s.can.vr85, 0.9);
  let dv = 0, at = "";
  for (const M of [0.6, 0.8, 1.0, 1.1]) for (const pf of [1, 0.85]) { const v = vRipple(I, M, pf, C, s.fsw); if (v > dv) { dv = v; at = `M ${M}, cosφ ${pf}`; } }
  judge(sec, `V_max + ½ΔV_pp (in-period ripple at ${s.fsw / 1e3} kHz) vs OV trip`, `${s.vMax} + ${f(dv / 2, 0)} V (ΔV_pp ${f(dv, 0)} V at ${at})`, `${s.ovTrip} V trip`,
    (dv / 2) / (s.ovTrip - s.vMax), 0.8, `${f(C * 1e6, 0)} µF; switch-state run, battery supplies the mean${id === "m8" ? "; at 3 kHz it would reach the trip — why M8 switches at 5 kHz" : ""}`);
}

// ============ 3. GATE DRIVE AND SHORT CIRCUIT ============
{
  // NSI6611 DS 1.2 worst corners (Road P.drv); soft-off I_STO 400 mA typ / 100 mA DS min, from the 16.9 V VCC2 high corner (Road §5)
  const drv = { vth: 10, ichg: 350e-6, leb: 0.2e-6, deg: 0.32e-6, isto: [0.4, 0.1], vcc2Hi: 16.9 };
  const cap = (fosc) => 0.5 * (8e-6 / 3) * (0.9 / 0.33) ** 2 * fosc * 0.92;     // Road worst-part flyback capacity
  for (const [id, s] of LAUNCH) {
    const m = MODS[s.mod], sec = `Gate drive — ${s.name}`;
    if (m.sil === "sic") {   // the Road SiC chain, unchanged: 47 pF blank, RT default (253 kHz)
      const pB = 3 * (m.qg * 20.7 * s.fsw + 20.7 * 3.3e-3 + 20.7 ** 2 / 5.1e3) + 0.2;
      judge(sec, `Gate-power demand per bank @${s.fsw / 1e3} kHz`, `${f(pB, 2)} W`, `${f(cap(253e3), 2)} W worst-part capacity`, pB / cap(253e3), 0.8, "Road SiC numbers");
      add(sec, "Short-circuit withstand", "SiC t_SC not published by hiitio", "3.1 µs Road DESAT reaction", "WARN", "Road VERIFY ③ — hiitio letter + contained SC test before any marine SiC delivery");
      continue;
    }
    // Road round 7 (RR04) + round 12 (R1-F06/R2-F05): both soft-off corners; the 100 mA DS minimum does not close and the
    // rating is a test condition (6 µs at 800/1000 V, 15 V), not a corner guarantee — an open release gate, as on the Road
    const cBlank = 82e-12, tDet = cBlank * 1.05 * drv.vth / drv.ichg + drv.leb + drv.deg;
    const [tTyp, tLo] = drv.isto.map((i) => tDet + m.cies * (drv.vcc2Hi - 10) / i), tscDer = m.tsc * s.tscDer;
    add(sec, `DESAT worst detection + soft-off (${f(cBlank * 1e12, 0)} pF blank)`, `${f(tTyp * 1e6, 2)} µs @400 mA typ · ${f(tLo * 1e6, 2)} µs @100 mA DS min (detect ${f(tDet * 1e6, 2)} µs)`,
      `${f(m.tsc * 1e6, 0)} µs @${m.tscV} V → ${f(tscDer * 1e6, 1)} µs at ${s.vMax} V`, "WARN",
      `RELEASE GATE (Road RR04/③): typ is ${f(100 * tTyp / tscDer, 0)} % of the derated rating, the 100 mA corner does not close — NOVOSENSE I_STO distribution + hiitio's SC statement at ${s.vMax} V and the ${drv.vcc2Hi} V gate-rail corner + contained SC test (Cies ${f(m.cies * 1e9, 0)} nF from ${drv.vcc2Hi} to 10 V); firmware now holds MCU_GATE_EN for a CAL 60 µs minimum before a software caller can drop it (Road round 14/A.13), so the ISR can no longer race this soft-off — clears both corners above with margin`);
    // DESAT trip at the collector (Road method: V_th − n·0.6 V − I_chg·4.7 k) vs VCEsat at the overload peak, hot
    const tripMin = 8.5 - s.nDiode * 0.6 - 650e-6 * 4.7e3, tripMax = 10 - s.nDiode * 0.6 - 350e-6 * 4.7e3;
    const vce = m.v0 + m.r * die(s, RATING[id].I * OPM.ovl) * Math.SQRT2;
    judge(sec, `DESAT trip at the switch (${s.nDiode} × US1M, 4.7 k)`, `${f(tripMin, 2)}–${f(tripMax, 2)} V`, `> VCEsat ${f(vce, 2)} V at the ${f(OPM.ovl * 100, 0)} % peak, hot`,
      vce / tripMin, 0.8, "no nuisance trip at the worst-low corner; short circuit drives V_CE to the bus");
    const fosc = 308e3, pBank = 3 * (m.qg * 20.7 * s.fsw + 20.7 * 3.3e-3 + 20.7 ** 2 / 5.1e3) + 0.2;
    judge(sec, `Gate-power demand per bank @${s.fsw / 1e3} kHz`, `${f(pBank, 2)} W`, `${f(cap(fosc), 2)} W worst-part capacity (RT 8.2 k)`, pBank / cap(fosc), 0.8,
      `Qg ${f(m.qg * 1e6, 2)} µC full-swing, Road method`);
  }
}

// ============ 3b. DISCHARGE (Road rules; marine active target 5 s per IEC 61800-5-1, no crash case) ============
for (const [id, s] of LAUNCH) {
  const d = s.dis, sec = `Discharge — ${s.name} (bleeder 2 × ${d.rpN} × ${d.rpPer / 1e3} k · active ${d.raN} × ${d.raPer} Ω)`;
  const cN = s.can.n * s.can.c + 3e-6, cMax = s.can.n * s.can.c * 1.1 + 3.3e-6, ln = Math.log(s.vMax / 60);
  const rp = d.rpN * d.rpPer / 2, ra = d.raN * d.raPer;
  judge(sec, `Passive ${s.vMax}→60 V, worst (R+5 %, C+10 %)`, `${f(rp * 1.05 * cMax * ln)} s`, "120 s service rule", rp * 1.05 * cMax * ln / 120, 0.8, `nominal ${f(rp * cN * ln)} s`);
  const vRes = s.vMax * 1.05 / (1.05 + (d.rpN - 1) * 0.95);
  judge(sec, `Bleeder V on the worst-tolerance resistor @${s.vMax} V`, `${f(vRes)} V`, "200 V working (2512)", vRes / 200, 0.8);
  const pRes = (s.vMax / (d.rpN * d.rpPer * 0.95)) ** 2 * d.rpPer * 0.95;
  judge(sec, `Bleeder W/resistor @${s.vMax} V (R−5 %)`, `${f(pRes, 2)} W`, "2 W", pRes / 2, 0.65, `standing loss ${f(s.vMax ** 2 / rp, 0)} W`);
  const t60 = 1 / (1 / (ra * 1.05) + 1 / (rp * 1.05)) * cMax * ln + 2.5e-3;
  judge(sec, `Active + passive ${s.vMax}→60 V, worst corner`, `${f(t60, 2)} s`, "5 s (IEC 61800-5-1 accessible-part rule)", t60 / 5, 0.8);
  const eRes = 0.5 * cMax * s.vMax ** 2 / d.raN;
  judge(sec, "Energy per 10 W wirewound (C+10 %)", `${f(eRes)} J`, "100 J single-pulse", eRes / 100, 0.5, "firmware ≤ 3 discharges / 5 min");
  judge(sec, "V per wirewound", `${f(s.vMax / d.raN)} V`, "≥350 V axial class", (s.vMax / d.raN) / 350, 0.8);
  add(sec, "QDIS switch", `${f(s.vMax / ra, 2)} A pk at ${s.vMax} V`, d.qdis, "PASS",
    `fully enhanced, no linear region; gate through the kept 1.5 k/10 k divider, now 11.6–16.3 V from the UCC14141-Q1 (Road A.12; low end set by the VOW3120's guaranteed V_OH ≥ V_CC − 4 V)${id === "m10" ? " — same bias module, already rated ≥ 1150 V DC" : ""}`);
  add(sec, "QDIS stuck ON with the battery connected", `${f(s.vMax ** 2 / ra, 0)} W continuous`, "not survivable by 10 W parts", "WARN",
    `bounded as on the Road (F23): fire only with the DC breaker reported OPEN; now detected at the next contactor opening → latched no-re-energise DTC + contactor-open request (Road round 14/A.13, replaces the earlier precharge-only check); fail-open flameproof wirewounds — candidate TE SQP10 (700 V, ${f(s.vMax / d.raN, 0)} V here) through the stuck-ON test (Road gate ㉖)`);
  // Road FW-16 (round 9, A8-N03/R9X-07): the boot self-test runs only at ≤ 0.1 J — both channels read < 3 V (≤ 12 V true),
  // or QDIS for 2 τ from a < 60 V reading (≤ 69 V true); τ at R+5 % and C_max
  const tau = ra * 1.05 * cMax, eRead = 0.5 * cMax * 12 ** 2, eTop = 0.5 * cMax * (69 * Math.exp(-2)) ** 2;
  judge(sec, "FW-16 self-test residual energy (read < 3 V, or QDIS for 2 τ from < 60 V)", `≤ ${f(eRead * 1e3, 0)} mJ (read) · ≤ ${f(eTop * 1e3, 0)} mJ (QDIS 2 τ = ${f(2 * tau, 2)} s)`,
    "0.1 J design limit", Math.max(eRead, eTop) / 0.1, 0.8, "Road round 9 (A8-N03, R9X-07); the 2 τ top-up time is a Marine parameter-set value");
}

// ============ 4. BATTERY / DC-GRID WINDOWS (pack series count per cell class) ============
// Typical cell-datasheet limits — confirm against the chosen cell. n_max keeps V_max(cell) with 3 %
// regen/BMS headroom under the cell class V_max; n_min keeps the empty pack under 5 % load sag above V_min.
const CHEM = { LFP: [2.5, 3.2, 3.65], NMC: [3.0, 3.65, 4.2], LTO: [1.5, 2.3, 2.7] };
const WINDOWS = {};
const CLASSES = LAUNCH.filter(([, s]) => !s.sicBuild);   // M8 and M10 — M8-SiC shares the M8 window
for (const [id, s] of CLASSES) {
  const sec = `Battery windows — ${s.name} (${s.vMin}–${s.vMax} V)`;
  for (const [ch, [lo, nom, hi]] of Object.entries(CHEM)) {
    const nMax = Math.floor(0.97 * s.vMax / hi), nMin = Math.ceil(s.vMin / (0.95 * lo)), n = nMax;
    const vEmpty = n * lo * 0.95, ok = nMin <= nMax;
    (WINDOWS[id] ||= {})[ch] = { n, nMin, v: [n * lo, n * nom, n * hi] };
    add(sec, `${ch} ${lo}/${nom}/${hi} V per cell`, ok ? `**${nMin}–${nMax} s** → ${n}s: ${f(n * lo, 0)} / ${f(n * nom, 0)} / ${f(n * hi, 0)} V` : `${n}s: ${f(n * lo, 0)} / ${f(n * nom, 0)} / ${f(n * hi, 0)} V`,
      `${s.vMin}–${s.vMax} V`, ok ? "PASS" : "INFO",
      ok ? `whole SOC range inside the window; ${f(pAvail(n * nom, RATING[id].I), 0)} kW per cell at pack nominal`
        : `swing ${f(hi / lo, 2)}:1 exceeds the ${f(s.vMax / s.vMin, 2)}:1 window — below ${f(s.vMin, 0)} V (≈ the last few % SOC) the cell derates P ∝ V; empty-under-load ${f(vEmpty, 0)} V`);
  }
}
add("DC-grid sources", "Genset rectifier DC (1.35 × V_LL, +10 % high line)", "400 V → 540/594 V · 440 V → 594/653 V · 690 V → 932/1025 V", "M8 500–850 · M10 700–1150", "INFO",
  "400/440 V ship-service gensets sit in the M8 window; 690 V gensets need M10");

// ============ 5. VESSEL SIZING — cells per shaft, one cell per 3-phase winding set ============
// Propeller law P ∝ n³: a shaft that loses (1/N) of its power keeps ((N−1)/N)^(1/3) of its speed.
// Classes and per-shaft powers from the research brief §1 (sourced examples in design-basis §1)
const VESSELS = [
  ["Water taxi / small craft (Navalt 2 × 20 kW · Candela P-12 2 × 110–160 kW)", 2, [40, 160], "ferry"],
  ["Passenger ferry, inland/coastal (Ampere 2 × 450 kW)", 2, [250, 450], "ferry"],
  ["Ro-pax / car ferry (Ellen 2 × 750 kW · ForSea 4 × 1.5 MW)", 2, [750, 1500], "ferry"],
  ["Harbour tug 32–45 t BP (Zeetug30 2 × 925 kW)", 2, [900, 1300], "tug"],
  ["Harbour tug 60–70 t BP (GTTP 60 t · eWolf 2 × 2.1 MW)", 2, [1900, 2300], "tug"],
];
for (const [id, s] of CLASSES) {
  const sec = `Vessel sizing — ${s.name} cells (ferry ${f(RATING[id].p, 0)} kW · tug ${f(RATING[id].pTug, 0)} kW each at ${s.vNom} V)`;
  for (const [cls, shafts, [pLo, pHi], duty] of VESSELS) for (const P of [pLo, pHi]) {
    const rate = (c) => (duty === "tug" ? RATING[c].pTug : RATING[c].p);
    const pc = rate(id), n = Math.max(1, Math.ceil(P / pc));
    // N−1 on the propeller law (T ∝ n²): the shaft keeps torque r, speed √r, power r^1.5 — the other shaft is untouched
    const r = Math.min(1, (n - 1) * pc / P);
    const big = n > 6 ? ` · >6 sets: ${Math.ceil(P / rate("m10p2"))} × M10-2P, or tandem motors (§6)` : "";
    add(sec, `${cls}: ${shafts} × ${P} kW (${duty} rating)`, `${n} cell${n > 1 ? "s" : ""}/shaft → ${n * 3}-phase motor (${n} set${n > 1 ? "s" : ""})`, `${shafts * n} cells/vessel`, n <= 6 ? "INFO" : "WARN",
      n === 1 ? "one cell lost: that shaft stops — redundancy is the second shaft" : `one cell lost: that shaft keeps ${f(100 * r, 0)} % torque, ${f(100 * Math.sqrt(r), 0)} % speed${big}`);
  }
}

// ============ 5d. MULTI-SET MOTORS — one cell per isolated-neutral 3-phase set (multiphase research §3, §7) ============
{
  const sec = "Multi-set motor interface — N cells on one motor";
  const groups = { 2: "0/30°", 3: "0/20/40°", 4: "0/15/30/45°", 6: "3 groups 0/20/40° × 2", 8: "4 groups 0/15/30/45° × 2" };
  for (const N of [2, 3, 4, 6, 8]) {
    const load = (1 + N) * 64e-6 / 1e-3;   // CAN-FD, 16 data bytes at 1/5 Mbit/s ≈ 64 µs per frame; 1 command + N status per ms
    const r = (N - 1) / N;
    judge(sec, `N = ${N}: set angles ${groups[N]} · harmonic loops ${N === 2 ? "6ω" : "6ω + 12ω"}`, `shaft bus ${f(100 * load, 0)} % at 1 kHz`, "≤ 60 % CAN-FD load",
      load / 0.6, 0.8, `N−1 keeps ${f(100 * r, 0)} % torque / ${f(100 * Math.sqrt(r), 0)} % speed / ${f(100 * r ** 1.5, 0)} % power on that shaft (propeller law)${load > 0.48 ? " · drop to 500 Hz" : ""}`);
  }
  add(sec, "PWM carrier sync between cells", "CAN-FD time-stamped SYNC + period-trim PLL (firmware)", "≤ 1 µs (1.8° at 5 kHz)", "WARN",
    "separate winding sets need sync only for x–y ripple and, on a common link, capacitor ripple — free-running clocks at 100 ppm beat at 0.4 Hz; bench gate: if CAN sync misses 1 µs, route the Road card's spare JVEH SP1/SP2 to a PWM sync input");
  add(sec, "Paralleling two cells on ONE winding set", "not offered", "≈100 ns matching + coupling reactors + one controller for both", "INFO",
    "add winding sets instead (isolated neutrals block zero sequence; leakage is the coupling inductor); a paralleled-module cell (M10-2P) is the big-tug route");
}

// ============ 5a. COSMIC-RAY FAILURE RATE (1200 V IGBT, Semikron AN 17-003 via research brief §8.1) ============
{
  const sec = "Cosmic-ray failure rate (sea level)";
  for (const [id, s] of CLASSES.filter(([, c]) => MODS[c.mod].vces === 1200)) {
    add(sec, `${s.name}: 1200 V IGBT at ${s.vNom} V nominal / ${s.vMax} V full charge`, "≈1.3 / 7 FIT per switch → ≈8 / 42 FIT per cell",
      "≤ 800 V: 1.3 · 850 V: 7 · 900 V: 41 · 1000 V: 480 FIT/switch", "PASS",
      "most hours sit near nominal (LFP 225 s: 720 V), so ≈10–20 FIT per cell — the reason M8 stops at 850 V");
  }
  add(sec, "M10: 1700 V IGBT at 950–1100 V", "no FIT data found", "hiitio FIT curve", "WARN", "request before freezing V_max = 1100 V (Danfoss runs 1700 V parts to 1200 V)");
  add(sec, "SiC (M8-SiC, M10-SiC)", "no FIT data found", "hiitio FIT curve", "WARN", "request before committing SiC above 800 V");
}

// ============ 5b. LOW SPEED / HIGH TORQUE — what the inverter actually sees ============
// Torque is the motor's job (T = P/ω; more poles, more turns); the inverter supplies current at
// f₁ = p·n/60. Low-speed PM motors carry many pole pairs, so f₁ stays in the tens of Hz.
{
  const sec = "Low speed / high torque — inverter view (examples)";
  for (const [what, kW, rpm, pp] of [["Direct-drive propeller motor", 400, 300, 10], ["Direct-drive propeller motor", 1000, 180, 16],
    ["Geared azimuth-thruster motor", 1500, 1000, 4], ["Geared azimuth-thruster motor", 2000, 1200, 3]]) {
    const T = kW * 1e3 / (rpm * 2 * Math.PI / 60), f1 = pp * rpm / 60;
    add(sec, `${what}: ${kW} kW @ ${rpm} rpm, ${pp * 2} poles`, `${f(T / 1e3, 1)} kNm · f₁ ${f(f1, 0)} Hz`,
      `pulse ratio ≥ 20`, 5e3 / f1 >= 20 ? "PASS" : "WARN", `f_sw/f₁ = ${f(5e3 / f1, 0)} at 5 kHz (M8), ${f(2.5e3 / f1, 0)} at 2.5 kHz`);
  }
  add(sec, "Stall / near-zero speed", "a propeller's torque ∝ n²: full torque only near full speed", "-", "INFO",
    "the road case (full torque at 0 Hz, one switch carrying the peak) does not arise in propulsion; crash-stop passes 0 Hz in seconds — firmware derates below 2 Hz (MFW-09)");
}

// ============ 5c. STARTING, STOPPING, JOINING THE BUS — the inverter is the soft starter ============
// No DOL inrush exists: FOC applies controlled current from 0 Hz and a PM rotor needs no
// magnetising time. What remains at MW level is thermal at 0 Hz, bus joining, back-EMF and regen.
for (const [id, s] of LAUNCH) {
  const m = MODS[s.mod], sec = `Starting and stopping — ${s.name}`, fsLow = 1e3, R = RATING[id];   // MFW: 1 kHz below 2 Hz
  // 0 Hz: output voltage ≈ 0 → duty ≈ 50 %; worst angle puts one phase at √2·I as DC: its HS IGBT
  // conducts half the time, the LS diode the other half, both switching at every period
  // 0 Hz: output voltage ≈ 0 → duty ≈ 50 %; the worst angle puts one phase at √2·I as DC — its HS IGBT
  // conducts half the time, the LS diode the other half, both switching at the PEAK current every period
  const stall = (I) => {
    const Ipk = Math.SQRT2 * die(s, I), k = Ipk / m.iRef * (s.vMax / m.vRef) ** OPM.kv * fsLow;
    const sic = m.sil === "sic";   // SiC: synchronous — both positions carry ½·Ipk²·R_DS
    const pT = sic ? 0.5 * Ipk * Ipk * m.rds + (m.eon + m.eoff) * k : 0.5 * (m.v0 + m.r * Ipk) * Ipk + (m.eon + m.eoff) * k;
    const pD = sic ? 0.5 * Ipk * Ipk * m.rds + m.err * k + 2 * m.tdead * fsLow * m.vsd * Ipk : 0.5 * (m.v0d + m.rd * Ipk) * Ipk + m.erec * k;
    return Math.max(OPM.coolant + pT * (OPM.rthPlate + m.rthJC + OPM.rthCH), OPM.coolant + pD * (OPM.rthPlate + m.rthJCd + OPM.rthCH));
  };
  const iStall = solve((I) => stall(I) <= OPM.tjOvl, 1, 3 * m.inom), tjS = stall(R.I);
  judge(sec, `Breakaway / stall: 100 % rated current (${R.I} A) at 0 Hz, held indefinitely, f_sw ${fsLow / 1e3} kHz below 2 Hz`, `Tj ${f(tjS, 0)} °C`, `${OPM.tjOvl} °C`, tjS / OPM.tjOvl, 0.95,
    `steady-state bound (no coldplate-τ credit); limit ${f(iStall, 0)} A = ${f(100 * iStall / R.I, 0)} % of rated. At the running f_sw (${s.fsw / 1e3} kHz) the same point would exceed 150 °C — the 1 kHz standstill rate is what makes full torque from 0 rpm free. Propeller torque ∝ n² is ≈0 at 0 rpm; a fouled propeller or pushing tug is the case`);
  // joining a live DC bus: per-cell precharge, sized for ≤ 10 A peak
  const C = s.can.n * s.can.c, Rpre = Math.ceil(s.vMax / 10 / 10) * 10, zc = Math.sqrt(10e-6 / C);
  add(sec, "Hot-joining a live bus WITHOUT precharge", `≈${f(s.vMax / zc / 1e3, 1)} kA peak`, "fuse / can surge", "INFO",
    `LC ring of ${f(C * 1e6, 0)} µF with ≈10 µH of feeder loop — a repaired cell cannot rejoin a live bus at sea without precharge`);
  judge(sec, `Per-cell precharge ${Rpre} Ω (DC entry kit)`, `${f(s.vMax / Rpre, 1)} A pk · ${f(0.5 * C * s.vMax ** 2, 0)} J per charge · 5τ = ${f(5 * Rpre * C * 1e3, 0)} ms`,
    "≤ 10 A pk", s.vMax / Rpre / 10, 1.0, "resistor + small DC contactor, beside the cell's 2-pole DC fuses and disconnector");
  // back-EMF rule (motor specification item): with no stored current the rectified link stays under the trip at every speed
  const Cm = s.can.n * s.can.c * 0.9 + 2.7e-6, eLim = 0.95 * s.ovTrip, iO = R.I * OPM.ovl;   // C_min (−10 %), as the Road
  add(sec, "Motor spec: back-EMF at 115 % overspeed (propeller racing, towing)", `E_LL,pk ≤ ${f(eLim, 0)} V`, `0.95 × OV trip ${s.ovTrip} V`, "INFO",
    `the rectified link never reaches the trip in steady state; the stored winding energy is the next row (Road §6 rule (a), A.11; both matrix columns since round 13/A.12, F135) — a typical SPM at ${s.vNom} V lands near ${f(0.9 * 0.95 * s.vNom * 1.15, 0)} V`);
  // Road §6 rule (a) since A.11 (R1-F01/R2-F08; both matrix columns since round 13/A.12, F135): pulse-off with the DC path lost rectifies the stored winding energy
  // ¾·(L_d·î_d² + L_q·î_q²) = 1.5·L·I² into the isolated link, at any speed. Road screen V_pk ≤ E + √((V₀ − E)² + 2·W/C_min),
  // V₀ = OV trip, shown at E = 0 (back-EMF only raises it). Screening motor = the Marine motor spec itself: I_ch = ψ_f/L_d ≈ 1 pu
  // (§6), ψ_f from E at the limit above and 80 Hz at rated speed (top of the 40–80 Hz range). Commissioning inputs: L_d/L_q(i), ψ_f, n_max.
  const lScr = eLim / 1.15 / (Math.sqrt(3) * 2 * Math.PI * 80) / (Math.SQRT2 * R.I), wMag = 1.5 * lScr * iO ** 2;
  const head = 0.5 * Cm * (s.can.vr85 ** 2 - s.ovTrip ** 2);
  add(sec, `Motor spec: winding energy at pulse-off with the DC path lost (${f(iO, 0)} A, screening L_d ${f(lScr * 1e3, 2)} mH)`,
    `${f(wMag, 0)} J → ${f(Math.sqrt(s.ovTrip ** 2 + 2 * wMag / Cm), 0)} V at zero back-EMF`, `${f(head, 1)} J headroom, ${s.ovTrip} V trip → ${s.can.vr85} V U_N at C_min ${f(Cm * 1e6, 0)} µF`,
    wMag > head ? "WARN" : "PASS",
    `Road §6 rule (a): the cell's link alone covers this motor only to ${f(Math.sqrt(head / (1.5 * lScr)), 0)} A rms, so it is released under rule (b) — the DC grid stays connected through the cell's pulse-off intervals for every opening cause, the cell's own faults included (IEC 61660 selectivity study + FAT) — and FW-06 LS-ASC holds the energy in the winding if the DC path does open. L_d here = I_ch ≈ 1 pu at 80 Hz; the motor's L_d/L_q(i), ψ_f and n_max are commissioning inputs (motor partner, gate 5)`);
  add(sec, "Crash stop / hard deceleration", `regeneration up to ${f(R.p, 0)} kW per cell`, "BMS charge limit (CAN)", "INFO",
    "firmware caps regen at the BMS limit; a full battery or a genset-only bus needs the EMS headroom or a brake chopper (below)");
  // battery breaker opens during full regen (Road FW-06, round-7 chain, ASC timing re-derived A.12 for the UCC14141-Q1): regen
  // power charges the link, V(t)² = V0² + 2·P·t/C from the trip, for ≤ 15.6 µs to the ASC request (Road OVP.tReq); then all six
  // switches are off for the ASC entry (≤ 7.56 µs, UCC14141-Q1 17.4 V low end, Road ASC.tEntryMax; release ≤ 1.06 µs) and the
  // phase current rectifies in at Î/C. M10 uses the same bias module and opto, already rated ≥ 1150 V DC.
  const Pr = R.pMax * 1e3 * OPM.ovl, vp = (t) => Math.sqrt(s.ovTrip ** 2 + 2 * Pr * t / Cm), tReq = 15.6e-6, tAsc = 7.56e-6;
  const vPk = vp(tReq) + Math.SQRT2 * iO * tAsc / Cm;
  judge(sec, `Battery path lost at full regen (${f(Pr / 1e3, 0)} kW): link peak with the FW-06 chain (${f(tReq * 1e6, 1)} µs to the ASC request, then ${f(tAsc * 1e6, 2)} µs all-off at ${f(Math.SQRT2 * iO, 0)} A)`,
    `${f(vPk, 0)} V`, `${s.can.vr85} V can U_N at 85 °C`, vPk / s.can.vr85, 0.92,
    `${f(Pr / (Cm * s.vMax) / 1e6, 2)} V/µs; a 100 µs response would end at ${f(vp(100e-6), 0)} V, a 1-per-PWM-period sample (${f(1e6 / s.fsw, 0)} µs) at ${f(vp(1 / s.fsw), 0)} V — N9 applies to marine too (Road 8XX: 906 V)`);
  // brake-chopper mode (firmware option): each leg's LS IGBT switches R_leg from AC to DC+, HS diode freewheels.
  // Limit = the cell's DC entry current (sized for its own rating), not the silicon: I_leg = I_DC,cell / 3.
  if (m.sil !== "sic") {
    const iDC = R.pMax * 1e3 / s.vMax / 0.97, iLeg = iDC / 3, Rleg = s.vMax / iLeg;
    const pT = (m.v0 + m.r * iLeg) * iLeg + (m.eon + m.eoff) * iLeg / m.iRef * (s.vMax / m.vRef) ** OPM.kv * 1e3 * 0.5;   // worst: D≈1 conduction + ≤1 kHz hysteresis edges
    const tjC = OPM.coolant + pT * (OPM.rthPlate + m.rthJC + OPM.rthCH);
    judge(sec, `Brake-chopper mode (option): 3 legs × ${f(Rleg, 1)} Ω → ${f(3 * s.vMax ** 2 / Rleg / 1e3, 0)} kW at ${s.vMax} V`, `LS IGBT Tj ${f(tjC, 0)} °C at ${f(iLeg, 0)} A per leg`,
      `${OPM.tjCont} °C`, tjC / OPM.tjCont, 0.9, `bounded by the DC entry (${f(iDC, 0)} A = the cell's own rating), not by silicon; hysteresis on V_DC; only for genset-only buses or a full battery — resistors external, per project`);
  }
}

// ============ 6. INSULATION — barrier components (datasheet ratings) ============
// The Road barrier register (docs/design-basis.md §6a, A.12) with its gates. M8 runs the Road boards at 850 V, so its verdicts
// are the Road ones; M10 (1150 V) is a new power stage and each line below is a requirement on it — the same statements at a
// higher working voltage. PCB creepage = the insulation-coordination study.
{
  const sec = "Insulation — barrier components (Road register §6a; M8 verdict at 850 V; M10 requirement at 1150 V)";
  const B = [
    ["NSI6611A-Q1 gate driver", "reinforced IEC 60747-17, V_IOWM 2121 V DC, CPG 8.0 mm, CTI > 600", "reinforced ≥ V_max", "PASS", "keep (2121 V DC ≥ 1150 V)"],
    ["AMC1311B V_DC amplifier", "reinforced IEC 60747-17, V_IOWM 2120 V DC, CPG 8.5 mm, CTI ≥ 600", "reinforced ≥ V_max", "PASS", "keep"],
    ["UCC12050 V_DC-channel bias ×2 (Road A.11; the earlier \"MGJ2D150505SC\" code never existed; production AVL part UCC12051QDVERQ1, AEC-Q100, A.12)", "reinforced VDE 0884-11, V_IOWM 1200 Vrms / 1697 V DC, CPG > 8 mm, CTI > 600", "reinforced ≥ V_max", "PASS", "keep (1697 V DC ≥ 1150 V)"],
    ["VOW3120-X017T opto (UQD, UASC) — replaces TLP152 (Road gate ⑪, A.12)", "DIN EN 60747-5-5 (VDE 0884-5) opt.1 reinforced, V_IORM 1414 Vpk, V_ISO 5.3 kVrms, CPG/CLR ≥ 10 mm", "reinforced ≥ V_max", "PASS", "keep (1414 Vpk ≥ 1150 V; VDE/UL/CQC certificates listed \"planned\" in the DS — check at PO)"],
    ["UCC14141-Q1 isolated bias (PSASC, PSQD) — replaces QA01C-18 (Road gate ㉔, A.12)", "DIN EN IEC 60747-17 reinforced, V_IORM 1414 Vpk, V_IOWM 1000 Vrms / 1414 V DC", "reinforced ≥ V_max", "PASS", "keep (1414 V DC ≥ 1150 V; VDE certificate 40058888 issued and archived, round 14/A.13 — UL/CQC still listed \"planned\" in the DS, check at PO)"],
    ["VGT12EEM flyback transformer", "2.6 kVrms/1 min NP–NS, 1.3 kVrms coil–core, no working rating", "reinforced ≥ V_max", "WARN", "transformer certified for ≥ 1150 V DC working (Road gate ⑤)"],
    ["HC5FW 900-S/SP1 hall sensor", "reduced insulation (no sleeve): 2.5 kV/1 min, CPG 3.6 / CLR 2.7 mm", "insulation completed by the busbar", "WARN", "busbar sleeve rated for 1150 V + LEM sign-off (Road gate ⑩ / N7)"],
    ["Y-caps CY1/CY2 — Vishay VY1472M63Y5UQ6TV0 (Road gate ㉔, A.12)", "Y1 500 VAC / 1500 VDC (X1 760 VAC)", "DC working rating ≥ V_max (a first earth fault on the IT network leaves the whole link across one)", "PASS", "keep (1500 V DC ≥ 1150 V; the Y1 / 500 VAC class is sized for the 690 V AC grid case, not the DC bus)"],
    ["US1M DESAT diodes (series string)", "V_RRM 1000 V each; 2 in series on the Road card", "string ≥ V_max + turn-off overshoot", "PASS", "3 in series (≈1500 V peak, leakage sharing at 100 °C)"],
  ];
  for (const [part, rating, lim, m8, m10] of B) add(sec, part, rating, lim, m8, `M10: ${m10}`);
}

// ---------------- render ----------------
const counts = { PASS: 0, WARN: 0, FAIL: 0, INFO: 0 };
rows.forEach((r) => counts[r.status]++);
let md = `# Marine Series — Verification Report (${new Date().toISOString().slice(0, 10)})

Generated by \`marine/calc/marine-verify.mjs\`. **${counts.PASS} PASS · ${counts.WARN} WARN · ${counts.FAIL} FAIL** (+${counts.INFO} info).
Basis: ${OPM.coolant} °C coolant · Tj ≤ ${OPM.tjCont} °C continuous / ${OPM.tjOvl} °C overload · ${OPM.ovl * 100} % / 60 s overload ·
PF ${OPM.pf} at modulation ${OPM.m} · power cycling ≥ ${OPM.lifeSF}× a 20-year ferry mission. See [design-basis](design-basis.md).
`;
let last = "";
for (const r of rows) {
  if (r.sec !== last) { md += `\n### ${r.sec}\n\n| Check | Value | Limit | Verdict | Note |\n|---|---|---|---|---|\n`; last = r.sec; }
  const b = { PASS: "✅ PASS", WARN: "🟡 WARN", FAIL: "🔴 FAIL", INFO: "ℹ️" }[r.status];
  md += `| ${r.name} | ${r.value} | ${r.limit} | ${b} | ${r.note} |\n`;
}
writeFileSync(join(ROOT, "verification-report.md"), md);
console.log(`marine-verify: ${counts.PASS} PASS · ${counts.WARN} WARN · ${counts.FAIL} FAIL (+${counts.INFO} info)`);
for (const [id, r] of Object.entries(RATING)) console.log(`  ${id}: ${r.I} A rms → ${f(r.p, 0)} kW @nom (${f(r.pMin, 0)}–${f(r.pMax, 0)} kW)`);
if (counts.FAIL) process.exitCode = 1;
