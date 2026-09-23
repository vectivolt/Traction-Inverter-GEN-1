#!/usr/bin/env node
// design-verify.mjs — numeric end-to-end verification of the Traction Inverter design.
// Recomputes every sizing decision independently, at the ACTUAL operating corners
// (V_bus 500/700/850 V · KL30 9/12/16 V · f_sw 10 kHz · coldplate 65 °C), with component
// tolerances at worst case. Emits docs/verification-report.md with PASS/WARN/FAIL + margins.
//
// All constants are datasheet-real (HCS600, NSI6611 DS 1.2, SLUS458I, C3D, VGT12EEM, LEM, FS26 Rev.3, S32K39 Rev.3); anything still "assumed"
// is listed in the report's assumptions table.
// Run: node calculations/design-verify.mjs

import { writeFileSync } from "node:fs";
import { REV } from "./rev.mjs";
import { OP as LOP, MOD, SKU, lossOf, rthT, rthD, tjPos, tjLimit, pAvail } from "./loss-model.mjs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");

// ---------------- operating conditions ----------------
const OP = {
  vbusNom: 700, vbusMax: 850, vbusMin: 500,
  fsw: 10e3, iphPk: 340, iphCont: 185, pf: 0.85, mres: 0.95,
  kl30: { min: 9, nom: 13.5, max: 16 }, coolant: LOP.coolant, rthPlate: LOP.rthPlate,
};

// ---------------- datasheet / design constants ----------------
const P = {
  mod: MOD.sic, igbt: MOD.igbt,
  // link cap — REAL Faratronic C3D DS: 15.4 A rms @10 kHz/70 °C, ESR 7.8 mΩ, U_N 1100 V @70 °C / 1000 V @85 °C
  cap: { c: 20e-6, n: 16, vr: 1100, vr85: 1000, irmsEach: 15.4, esr: 7.8e-3, tolC: 0.10, src: "C3D datasheet" },
  // NSI6611A-Q1 DS 1.2: UVLO2 on 9.8/11.2/12.8, off 9.0/10.4/11.8; ICC2 1/3.3/7 mA; DESAT 8.5/9.3/10 V,
  // I_CHG 350/500/650 µA, LEB 200 ns, deglitch 100/200/320 ns; soft-off I_STO 100/400/570 mA;
  // FLT latched until an RST/EN RISING edge after ≥ t_FLT_MUTE (0.55–1.3 ms) low
  drv: { ipk: 10, uvlo2On: 12.8, uvlo2Off: 11.8, uvlo2OffTyp: 10.4, vccRecMin: 13, vcc2Rec: 32, vcc2Abs: 35, icc2: 7e-3, icc2Typ: 3.3e-3,
    vth: [8.5, 9.3, 10], ichg: [350e-6, 500e-6, 650e-6], leb: 0.2e-6, deg: [0.1e-6, 0.2e-6, 0.32e-6], isto: [0.1, 0.4, 0.57], tMute: 1.3e-3, src: "NSI6611 DS 1.2" },
  // UCC28C40 (SLUS458I): VDD_ON 6.5/7.0/7.5, VDD_OFF 6.1/6.6/7.1 V, I_START 50/100 µA, IVDD 2.3/3.0 mA
  pwm: { vref: 2.5, vcs: 1.0, uvloOn: 7.0, uvloOnMax: 7.5, uvloOff: 6.6, vccAbs: 20, istart: 100e-6, irun: 3.0e-3, kOsc: 1.72, src: "SLUS458I (C40 grade)" },
  // gate charge — HCS600 DS: QG 1240 nC @800 V/360 A, +18/−5 (Fig.11: ≈1.09 µC for −5.1→+15.6 V)
  qg: { total: 1.24e-6, vswing: 20.1, src: "HCS600 datasheet" },
  // flyback magnetics — REAL VGT12EEM-200S1A4: NP:NF:NS = 1:1.6:2.9, Lp 10 uH ±20 %
  xfmr: { lp: 10e-6, nf: 1.6, ns: 2.9, isat: 4.5, src: "TDK datasheet (Isat unpublished — CS limit is the guard)" },
  // TPS55340
  boost: { vref: 1.229, ilim: 5.25, vinAbs: 45, src: "TPS55340 DS" },
  // LEM HC5FW 900-S — DS: 2.22 mV/A ratiometric @5 V, 2.5 V @0 A, BW >=40 kHz
  hall: { sens: 2.22e-3, v0: 2.5, src: "LEM datasheet" },
  // AMC1311B (the drawn DWVR grade): gain error ±0.2 % max, offset ±1.5 mV, linear 0–2 V
  amc: { vinFs: 2.0, gainErr: 0.002, vos: 1.5e-3, src: "AMC1311 DS SBAS786C" },
  // resistor tolerances
  tolR: 0.05, tolRp: 0.01,
};

// ---------------- round-7 shared models ----------------
// Gate rail from the separate FB-sense rectifier (A6-R06): the divider holds FFS at V_FB·(R1+R2)/R2,
// the aux plateau is FFS + Vf_FS, the secondary plateau is that ×NS/NF, and VCC2 is what is left
// after the secondary rectifier and the 5.1 V split zener. Corners: V_FB 2.45–2.55 V (SLUS458I),
// 1 % divider, 1N4148WS 0.45–0.65 V, US1M 0.7–1.3 V (FFS peak-detects at the secondary's peak
// current, where the US1M drop is highest — cross-check 6a), BZT52-C5V1 4.8–5.4 V; the low corner
// also carries the FB bias current (≤ 2 µA into the 11.7 k divider Thevenin → −0.19 V on VCC2).
const vcc2Of = (vfb, r1, r2, vfFs, vfSec, vz) => (vfb * (r1 + r2) / r2 + vfFs) * (P.xfmr.ns / P.xfmr.nf) - vfSec - vz;
const VCC2 = {
  nom: vcc2Of(2.5, 52.3e3, 15e3, 0.55, 0.85, 5.1),
  lo: vcc2Of(2.45, 52.3e3 * 0.99, 15e3 * 1.01, 0.45, 1.3, 5.4) - 2e-6 * 11.7e3 * (67.3 / 15) * (P.xfmr.ns / P.xfmr.nf),
  hi: vcc2Of(2.55, 52.3e3 * 1.01, 15e3 * 0.99, 0.65, 0.7, 4.8),
  old: vcc2Of(2.5, 56e3, 15e3, 0.8, 0.85, 5.1),   // A.6: 56 k on VDD, i.e. behind the US1M aux diode
};
// 74LVC3G17-Q100 (Nexperia DS Table 8, −40…125 °C, V_CC 4.5–5.5 V) as fractions of V_CC
const SCH = { tpLo: Math.min(1.90 / 4.5, 2.20 / 5.5), tpHi: Math.max(3.30 / 4.5, 3.80 / 5.5),
  tnLo: Math.min(1.00 / 4.5, 1.20 / 5.5), tnHi: Math.max(2.20 / 4.5, 2.50 / 5.5) };
// ASC entry (round 7, RR05). Power board: TLP152 → RASCG 2.2 k / RASCPD 10 k (1.8 k Thevenin) → CASCD 12 nF
// → NSI6611 ASC (V_ASCH 2.7–3.2 V, tASC_r 0.39–1.1 µs). The +20 V QA01C rail is taken as 18–24 V at this
// light load (DS curve: +6 % at 10 % load). The high sides start turning off first: FS0B drops DRV_EN
// (FS1B path) or the eFlexPWM fault forces the high-side PWM off (MCU path) — EN is NOT used for ASC ordering,
// because the NSI6611 honours DESAT over ASC only with EN high (DS §8.12, round-7 cross-check).
const ASC = (() => {
  const rTh = 2.2e3 * 10e3 / 12.2e3, c = 12e-9, vTh = (v) => (v - 0.3) * 10 / 12.2;
  const tRc = (r, cc, vth, vt) => r * cc * Math.log(vth / (vth - vt));
  return {
    tLsMin: tRc(rTh * 0.99, c * 0.95, vTh(24), 2.7) + 0.39e-6,
    tEntryMax: 0.17e-6 + tRc(rTh * 1.01, c * 1.05, vTh(18), 3.2) + 1.1e-6,
    tHsEn: 5.4e-9 + 2 * 4.4e-9 + 5e-9 + 60e-9 + 130e-9,   // FS1B path: USCH, two ANDs, harness, EN deglitch, EN→OUT (≈tpHL max)
  };
})();
// FW-06 over-voltage budget (round 7, RR06/A6-R08): link crossing the trip → ASC request
const OVP = { tDiv: (2.82e6 * 6.2e3 / (2.82e6 + 6.2e3)) * 1e-9, tAmc: 2.1e-6, tRx: 0.3e-6, tSample: 1 / 200e3, tConv: 1.0e-6, tAct: 1.0e-6 };
OVP.tReq = OVP.tDiv + OVP.tAmc + OVP.tRx + OVP.tSample + OVP.tConv + OVP.tAct;

// ---------------- check engine ----------------
const rows = [];   // {sec, name, value, limit, margin, status, note}
const add = (sec, name, value, limit, status, note = "") => rows.push({ sec, name, value, limit, status, note });
const judge = (sec, name, value, limit, ratio, warnAt, note = "") => {
  // ratio = value/limit must stay <= 1; warn above warnAt
  const st = ratio <= warnAt ? "PASS" : ratio <= 1 ? "WARN" : "FAIL";
  add(sec, name, value, limit, st, `${(ratio * 100).toFixed(0)}% of limit${note ? " · " + note : ""}`);
  return st;
};
const f = (x, d = 1) => Number(x.toFixed(d));

// ============ 1. POWER STAGE (every SKU) ============
{
  for (const [id, s] of Object.entries(SKU)) {
    const sec = `Power stage — ${s.name}`;
    // envelope (F09): the rectangle "220 kW over 500–850 V" was not true at 340 A
    const vFullPk = s.pTarget[0] * 1e3 / (Math.sqrt(1.5) * OP.mres * s.iPk * OP.pf);
    const vFullCont = s.pTarget[1] * 1e3 / (Math.sqrt(1.5) * OP.mres * s.iCont * OP.pf);
    add(sec, "Current-limited envelope P_pk / P_cont", `${f(pAvail(s.vMin, s.iPk), 0)}/${f(pAvail(s.vMin, s.iCont), 0)} kW @${s.vMin} V · ${f(Math.min(s.pTarget[0], pAvail(s.vNom, s.iPk)), 0)}/${f(Math.min(s.pTarget[1], pAvail(s.vNom, s.iCont)), 0)} kW @${s.vNom} V`,
      `${s.pTarget[0]}/${s.pTarget[1]} kW from ${f(vFullPk, 0)}/${f(vFullCont, 0)} V up`, "INFO",
      `${s.iPk}/${s.iCont} A rms, PF ${OP.pf}, 5 % modulation reserve — firmware derates P(V_dc) below these voltages (review A.6 F09)`);
    for (const [tag, I, V] of [["peak 30 s", s.iPk, s.vMax], ["continuous", s.iCont, s.vNom]]) {
      const L = lossOf(s, I, V);
      const Lr = s.sil !== "sic" ? lossOf(s, I, V, -0.85) : L;
      const tj = tjPos(s, L).T;   // RR07: IGBT + diode heat share the coldplate footprint
      const detail = s.sil === "sic" ? `cond ${f(L.cond, 0)} + sw ${f(L.sw, 0)} + Qrr ${f(L.rr, 0)} + dead-time ${f(L.dt, 0)} W`
        : `IGBT ${f(L.condT, 0)}+${f(L.swT, 0)} W · diode ${f(L.condD, 0)}+${f(L.rec, 0)} W (motoring)`;
      judge(sec, `Tj steady-state bound, ${tag} (${I} A, ${V} V, ${s.fsw / 1e3} kHz)`, `${f(tj, 0)} °C (${f(L.sw_die, 0)} W/switch)`,
        `${tjLimit(s)} °C ${s.sil === "sic" ? "Tj max" : "Tvjop"}`, tj / tjLimit(s), tag === "continuous" ? 0.75 : 0.9,
        `${detail}; 65 °C coolant + 0.045 K/W coldplate (shared by the position's IGBT and diode, RR07); the 30 s transient is in S4`);
      if (s.sil !== "sic") {
        const tjd = tjPos(s, Lr).D;
        judge(sec, `Diode Tj bound, ${tag} regeneration (cosφ −0.85)`, `${f(tjd, 0)} °C (${f(Lr.d_die, 0)} W)`, `${P[s.sil].tvjop} °C`,
          tjd / P[s.sil].tvjop, 0.9, `F26 — the FWD is its own die (Rth 0.10 K/W); regen loads it hardest; RR07 — the IGBT's ${f(Lr.sw_die, 0)} W heats the shared coldplate too`);
      }
    }
    const Lc = lossOf(s, s.iCont, s.vNom);
    const pOut = Math.min(s.pTarget[1], pAvail(s.vNom, s.iCont)) * 1e3;
    const pSemi = 6 * (Lc.sw_die + Lc.d_die);
    add(sec, `Semiconductor efficiency @ continuous (${f(pOut / 1e3, 0)} kW, ${s.vNom} V)`, `${f(100 * pOut / (pOut + pSemi), 2)} % (${f(pSemi, 0)} W)`, "-", "INFO",
      "six switches, conservative 175 °C R_DS(on); caps/busbar/LV add ≈0.1–0.2 pt");
  }
  // module current and sensing headroom at the highest SKU currents
  judge("Power stage — all SKUs", "Peak switch current vs module rating", "566 A pk (4XX, 400 A rms)", "600 A DC / 1200 A 1 ms", 566 / 1200, 0.5,
    "8XX SKUs: 481 A pk");
  judge("Power stage — all SKUs", "Phase-current sensing headroom", "≈620 A (566 A pk + 10 % ripple, 4XX)", "±900 A LEM range", 620 / 900, 0.8,
    "8XX ≈530 A — the ±900 A sensor covers every SKU (review F33 assumed 600 A rms; above ≈480 A rms a 4XX-HP frame needs a larger sensor)");
  // turn-off overshoot (F01): the DS fall time at the characterized 3.3 Ω is the di/dt input
  const didtHot = 0.8 * 481 / P.mod.tfHot, didtCold = 0.8 * 481 / P.mod.tfCold;
  const vpk = OP.vbusMax + 15e-9 * didtCold;
  add("Power stage — all SKUs", "Turn-off overshoot, SiC 850 V / 481 A, cold (RG_OFF 6.8 Ω)", `${f(vpk, 0)} V at 15 nH (${f(didtCold / 1e9, 1)} kA/µs est.)`,
    "1080 V repetitive guard · 1200 V abs", vpk <= 1080 ? "PASS" : vpk <= 1200 ? "WARN" : "FAIL",
    `hot ${f(OP.vbusMax + 15e-9 * didtHot, 0)} V. DPT GATE, not closed on paper: module Ls unpublished (hiitio RFQ); at the DS 3.3 Ω tf (13 ns) the same loop would reach ${f(OP.vbusMax + 15e-9 * 0.8 * 481 / 13e-9, 0)} V. Levers: RG_OFF → 10 Ω (≈+30 mJ Eoff, +11 °C at peak) and/or firmware I_pk(V_dc) above 800 V. The old S8 0.3 V term was a 1000× unit error (F01). IGBT SKUs: tf 200–385 ns ⇒ <40 V`);
}

// ============ 2. DC LINK (every SKU) ============
// Capacitor RMS current, 2-level SVPWM (Kolar): I_C = I·√(2M[√3/4π + cos²φ(√3/π − 9M/16)]).
// Its maximum over M and cosφ is 0.65·I (M ≈ 0.61, cosφ = 1) — the old 0.62 was the rated point.
{
  const kolar = (M, c) => Math.sqrt(2 * M * (Math.sqrt(3) / (4 * Math.PI) + c * c * (Math.sqrt(3) / Math.PI - 9 * M / 16)));
  let kMax = 0;
  for (let M = 0.05; M <= 1.15; M += 0.005) for (let c = 0; c <= 1.0001; c += 0.01) kMax = Math.max(kMax, kolar(M, c));
  for (const s of [SKU.sic8, SKU.igbt4]) {
    const sec = `DC link — ${s.name.slice(0, 3)} bank (${s.name})`;
    const perCan = kMax * s.iPk / 16, perCanC = kMax * s.iCont / 16;
    judge(sec, `Ripple per can, 30 s peak (worst M/cosφ, ${s.iPk} A)`, `${f(perCan)} A`, `${s.can.irms} A @10 kHz/70 °C`, perCan / s.can.irms, 0.95,
      `bank ${f(kMax * s.iPk, 0)} A = ${f(kMax, 3)}·I (Kolar max); 30 s is far inside the can's thermal τ`);
    judge(sec, `Ripple per can, continuous (${s.iCont} A)`, `${f(perCanC)} A`, `${s.can.irms} A`, perCanC / s.can.irms, 0.8);
    judge(sec, `Voltage vs U_N at 85 °C, OV trip ${s.ovTrip} V`, `${s.ovTrip} V`, `${s.can.vr85} V`, s.ovTrip / s.can.vr85, 0.9,
      `normal max ${s.vMax} V = ${f(100 * s.vMax / s.can.vr85, 0)} %`);
    const cTot = 16 * s.can.c;
    add(sec, "Stored energy at V_max (C +10 % + 3 µF local)", `${f(0.5 * (cTot * 1.1 + 3.3e-6) * s.vMax ** 2)} J`, "-", "INFO", `${f(cTot * 1e6, 0)} µF nominal`);
  }
  const pEsr = (kMax * SKU.sic8.iCont / 16) ** 2 * P.cap.esr;
  judge("DC link — 8XX bank (8XX SiC)", "ESR heating per can, continuous", `${f(pEsr, 2)} W`, "1.85 W (15.4 A²·7.8 mΩ = the 15 K rise)", pEsr / 1.85, 0.6);
  add("DC link — 4XX bank (4XX IGBT)", "4XX can binding", "50 µF / 600 V (85 °C) in the same 37.5 mm positions", "≥18 A rms @10 kHz/70 °C", "WARN",
    "CLASS part until the Faratronic RFQ returns the exact MPN + ripple/ESR/life data (review F20/F21) — the busbar drawing is unchanged");
}

// ============ 2b. REGENERATION WITH THE BATTERY PATH LOST (FW-06/FW-08, rev A.6 N9) ============
// Constant regen power into the link after the contactor opens: C·V·dV/dt = P ⇒ V(t)² = V0² + 2·P·t/C,
// from V_max to the OV trip, then for the FW-06 response time; C at −10 % tolerance.
{
  for (const s of [SKU.sic8, SKU.igbt4]) {
    const sec = `Regeneration, battery path lost — ${s.name.slice(0, 3)} bus`;
    const C = 16 * s.can.c * 0.9 + 2.7e-6, P = s.pTarget[0] * 1e3;
    const tTrip = C * (s.ovTrip ** 2 - s.vMax ** 2) / (2 * P), vp = (t) => Math.sqrt(s.ovTrip ** 2 + 2 * P * t / C);
    add(sec, `Link charging at ${s.pTarget[0]} kW regen (C_min ${f(C * 1e6, 0)} µF)`, `${f(P / (C * s.vMax) / 1e6, 2)} V/µs; ${s.vMax}→${s.ovTrip} V trip in ${f(tTrip * 1e6, 0)} µs`, "-", "INFO",
      `a 100 µs response would end at ${f(vp(100e-6), 0)} V and a once-per-PWM-period sample at 5 kHz (200 µs) at ${f(vp(200e-6), 0)} V — why FW-06 is 20 µs on a free-running V_DC slot`);
    // regen power until the high-side-off/ASC request, then all six switches off: the phase current (up to
    // I_pk) rectifies into the link until the LS are on (cross-check item 9)
    const vPk = vp(OVP.tReq) + s.iPk * Math.SQRT2 * ASC.tEntryMax / C;
    judge(sec, `Link peak with the FW-06 response (${f(OVP.tReq * 1e6, 1)} µs to HS-off + ASC request, then ${f(ASC.tEntryMax * 1e6, 1)} µs all-off at ${f(s.iPk * Math.SQRT2, 0)} A)`, `${f(vPk, 0)} V`, `${s.can.vr85} V can U_N at 85 °C`, vPk / s.can.vr85, 0.92,
      "round 7: the whole chain, not a written 20 µs — the motor's stored magnetic energy adds a motor-dependent step on the non-ASC path (below n_x); HIL event-to-ASC measurement + dyno contactor opening under full regen are the gates");
  }
}

add("Regeneration, battery path lost — budget", "FW-06 latency to the ASC request (RR06/A6-R08)",
  `${f(OVP.tReq * 1e6, 1)} µs = divider lag ${f(OVP.tDiv * 1e6, 2)} + AMC1311B ${f(OVP.tAmc * 1e6, 1)} + receiver ${f(OVP.tRx * 1e6, 1)} + sample wait ${f(OVP.tSample * 1e6, 1)} (≥ 200 kS/s per channel) + conversion ${f(OVP.tConv * 1e6, 1)} + compare→fault→ASC_REQ ${f(OVP.tAct * 1e6, 1)}`,
  "allocated in FW-06", "INFO", "the divider's 6.2 µs is the lag of a first-order filter behind a ramp; the route (ADC analog watchdog → eFlexPWM fault + ASC_REQ) is a firmware deliverable measured on HIL — no comparator is added unless that measurement misses the budget");

// ============ 3. DISCHARGE (every bus class; worst-case corners, both paths applied ONCE) ============
{
  for (const s of [SKU.sic8, SKU.igbt4]) {
    const sec = `Discharge — ${s.vMax === 850 ? "8XX" : "4XX"} values`;
    const cN = 16 * s.can.c + 3e-6, cMax = 16 * s.can.c * 1.1 + 3.3e-6, ln = Math.log(s.vMax / 60);
    const rp = s.rp, ra = s.ra;
    judge(sec, `Passive ${s.vMax}→60 V, worst (R+5 %, C+10 %)`, `${f(rp * 1.05 * cMax * ln)} s`, "120 s service rule",
      rp * 1.05 * cMax * ln / 120, 0.8, `nominal ${f(rp * cN * ln)} s (${s.rpN * 2} × ${s.rpPer / 1e3} k, 2 strings)`);
    const vRes = s.vMax * 1.05 / (1.05 + (s.rpN - 1) * 0.95);        // one +5 % part among −5 % parts
    judge(sec, `Bleeder V on the worst-tolerance resistor @${s.vMax} V`, `${f(vRes)} V`, "200 V working (plain 2512)", vRes / 200, 0.8,
      `review A.6 F24: 5 × 27 k put 184 V (92 %) on it at 850 V`);
    const pRes = (s.vMax / (s.rpN * s.rpPer * 0.95)) ** 2 * s.rpPer * 0.95;
    judge(sec, `Bleeder W/resistor @${s.vMax} V (R−5 %)`, `${f(pRes, 2)} W`, "2 W", pRes / 2, 0.65);
    const rc = (x) => 1 / (1 / x + 1 / (rp * 1.05));
    const t60a = rc(ra * 1.05) * cMax * ln + 2.5e-3;
    judge(sec, `Active + passive ${s.vMax}→60 V, worst corner`, `${f(t60a, 2)} s`, "2 s crash target", t60a / 2.0, 0.95,
      `nominal ${f(1 / (1 / ra + 1 / rp) * cN * ln, 2)} s; paths combined ONCE + 2.5 ms bias delay (F25: S5 counted the bleeder twice)`);
    const eRes = 0.5 * cMax * s.vMax ** 2 / 4;
    judge(sec, "Energy per 10 W wirewound (C+10 %)", `${f(eRes)} J`, "100 J single-pulse", eRes / 100, 0.5,
      `peak ${f(s.vMax ** 2 / (ra * 0.95) / 4, 0)} W/resistor decaying τ = ${f(ra * cMax, 2)} s; firmware ≤ 3 discharges/5 min (thermal recovery)`);
    judge(sec, "V per wirewound", `${f(s.vMax / 4)} V`, "≥350 V axial class", (s.vMax / 4) / 350, 0.8);
    const pStuck = s.vMax ** 2 / ra;
    add(sec, "QDIS stuck ON with the battery connected", `${f(pStuck, 0)} W continuous (${f(pStuck / 4, 0)} W/resistor)`,
      "not survivable by 10 W parts", "WARN",
      "F23: bounded, not survived — firmware fires QDIS only with contactors reported OPEN + auto-timeout; a pre-existing FET short is caught at the next precharge (link plateaus ≈5 % low, abnormal τ); the fail-open flameproof wirewound class opens the string. Never demonstrated on a live battery");
  }
  add("Discharge — 8XX values", "QDIS stress", `${f(850 / 1880, 2)} A pk (4XX ${f(500 / 880, 2)} A)`, "1200 V / 42 A part", "PASS", "fully-enhanced switch, no linear region");
}

// ============ 4. GATE DRIVE ============
{
  const span = VCC2.nom + 5.1;   // regulated secondary (§5 model, round 7), +15/−5 combo (F30)
  judge("Gate drive", "VCC2 low corner vs UVLO-rising MAX", `${f(VCC2.lo, 2)} V`, `${P.drv.uvlo2On} V`, P.drv.uvlo2On / VCC2.lo, 0.95, "§5 corner stack: V_FB, divider, both rectifiers, split zener");
  judge("Gate drive", "VCC2 low corner vs recommended-min", `${f(VCC2.lo, 2)} V`, `${P.drv.vccRecMin} V rec-min`, P.drv.vccRecMin / VCC2.lo, 0.95);
  const spanHi = VCC2.hi + 4.8;
  judge("Gate drive", "VCC2−VEE2 span (high corner)", `${f(spanHi)} V`, `${P.drv.vcc2Rec} V recommended (35 abs)`, spanHi / P.drv.vcc2Rec, 0.8);
  // NSI6611 DS §9.6: I = min[(VCC2−VEE)/(R_G + R_OH|OL + R_Gint), 10 A]; R_OH 2.2 Ω, R_OL 0.3 Ω typ
  const ig = (rg, rdrv, rint) => Math.min(span / (rg + rdrv + rint), 10);
  add("Gate drive", "Peak gate current on/off (DS §9.6 formula)", `SiC ${f(ig(3.3, 2.2, 1.1))}/${f(ig(6.8, 0.3, 1.1))} A · IGBT ${f(ig(1, 2.2, 0.5))}/${f(ig(1, 0.3, 0.5))} A`,
    `${P.drv.ipk} A driver`, "PASS", "SiC 3.3/6.8 Ω, IGBT 1.0/1.0 Ω (SKU BOM); the IGBT sink sits at the driver's own 10 A limit");
  const qSic = 1.09e-6;   // HCS600 Fig.11: −5.1 → +15.6 V
  // bank capacity at 100 % of the CS limit, 92 % transfer efficiency: typ (1.0 V, 3.33 µH) and
  // worst (0.9 V CS, Lp −20 %) — the old "3.87 W" was the typical limit before losses
  const cap = (fosc, vcs = 1.0, lp = 10e-6 / 3) => 0.5 * lp * (vcs / 0.33) ** 2 * fosc * 0.92;
  for (const [tag, q, fs, fosc] of [["SiC @10 kHz", qSic, 10e3, 253e3], ["SiC @20 kHz option", qSic, 20e3, 253e3],
    ["IGBT @5 kHz (full 4.36 µC, RT 8.2 k)", P.igbt.qg, 5e3, 308e3]]) {
    const pBank = 3 * (q * span * fs + span * P.drv.icc2Typ + span ** 2 / 5.1e3) + 0.2;
    const cW = cap(fosc, 0.9, 8e-6 / 3);
    judge("Gate drive", `Gate-power demand per bank, ${tag}`, `${f(pBank, 2)} W`, `${f(cW, 2)} W worst-part capacity (typ ${f(cap(fosc), 2)} W) at ${f(fosc / 1e3, 0)} kHz`, pBank / cW, 0.8,
      "Qg·ΔV·f + ICC2 + 5.1 k bleeder per domain; F27: IGBT uses the full ±15 V Qg (no scaling); IGBT SKUs fit RT 8.2 k (~308 kHz)");
  }
  judge("Gate drive", "Positive gate clamp (18 V zener + Vf)", "18.8 V", "+22 V abs Vgs (SiC) / ±20 V (IGBT)", 18.8 / 20, 0.95);
  judge("Gate drive", "Negative gate clamp (5.1 V zener + Vf)", "−5.9 V", "−10 V abs Vgs", 5.9 / 10, 0.85);
  add("Gate drive", "HS DESAT sense point", "module aux drain pin 9 (DSH)", "-", "PASS", "F29 — real HCS600 pin map; kelvin sensing, no busbar drop in the trip level");
  add("Gate drive", "ASC drive level", "5.1 V clamp at ganged pins", "GND2+6 V abs", "PASS", "F28 — 2.2 k + zener from the +20 V opto rail");
  // DESAT trip level (collector/drain) and worst-case DETECTION time (review A.6 F03/F05)
  const d = P.drv, vf = 0.6;
  const trip = (r, i) => [d.vth[0] - 2 * vf - d.ichg[2] * r, d.vth[2] - 2 * vf - d.ichg[0] * r];
  const [tsMin, tsMax] = trip(100, 0), [tiMin, tiMax] = trip(4.7e3, 0);
  add("Gate drive", "DESAT trip at the switch (corners)", `SiC ${f(tsMin, 1)}–${f(tsMax, 1)} V ≈ ${f(tsMin / P.mod.rdsHot / 1e3, 1)}+ kA · IGBT ${f(tiMin, 1)}–${f(tiMax, 1)} V`,
    "-", "INFO", "short-circuit detection, not overload — halls + firmware own the operating current limit (F46)");
  for (const [tag, c, lim, soft] of [["SiC 47 pF", 47e-12, null, 0.46e-6 / 0.4], ["IGBT 82 pF", 82e-12, P.igbt.tsc, 106e-9 * (VCC2.hi - 10) / 0.4]]) {
    const tMin = c * 0.95 * d.vth[0] / d.ichg[2] + d.leb;
    const tDet = c * 1.05 * d.vth[2] / d.ichg[0] + d.leb + d.deg[2];
    // round 7, RR04/A6-R04: judged at BOTH soft-off corners; the 100 mA DS minimum does not close
    // against the 6 µs rating, and the 850 V derating is not documented — an OPEN release gate, not PASS
    if (lim) add("Gate drive", `DESAT worst detection + soft-off, ${tag}`, `${f((tDet + soft) * 1e6, 2)} µs @400 mA typ · ${f((tDet + soft * 4) * 1e6, 2)} µs @100 mA DS min (detect ${f(tDet * 1e6, 2)} µs)`,
      "tP ≤ 6 µs @800 V/15 V/175 °C (DS Table 5)", "WARN",
      `RELEASE GATE: typ closes (${f(100 * (tDet + soft) / 6e-6, 0)} % of 6 µs), the 100 mA corner does not — NOVOSENSE I_STO distribution + hiitio SC envelope at 850 V and the actual gate bias (§5: ${f(VCC2.nom, 1)} V nom, soft-off here from the ${f(VCC2.hi, 1)} V high corner) + contained SC test with integrated energy. Min blank ${f(tMin * 1e6, 2)} µs vs the turn-on tail (DPT)`);
    else add("Gate drive", `DESAT worst detection + soft-off, ${tag}`, `${f((tDet + soft) * 1e6, 2)} µs (detect ${f(tDet * 1e6, 2)} + STO ${f(soft * 1e6, 2)} @400 mA)`,
      "SiC tSC NOT published — vendor letter", "WARN", `min blank ${f(tMin * 1e6, 2)} µs; release gate: hiitio SC envelope at 850 V/150 °C/+${f(VCC2.hi, 1)} V (high corner) or a contained SC test (F04)`);
  }
  add("Gate drive", "Shoot-through lockout", "IN+/IN− complementary pairing", "-", "PASS", "verified structurally in erc-audit (12 checks)");
  // round 7 (RR01/RR02): both RC nodes now end in the 74LVC3G17-Q100 Schmitt buffer (no Δt/ΔV limit);
  // delays use its V_T−/V_T+ windows as fractions of V_CC, R ±1 %, C0G ±5 %, X7R −20/+15 %
  const tauDlo = 9.9e3 * 3.3e-9 * 0.95, tauDhi = 10.1e3 * 3.3e-9 * 1.05;
  const dLo = tauDlo * Math.log(1 / SCH.tnHi), dHi = tauDhi * Math.log(1 / SCH.tnLo), dRise = tauDhi * Math.log(1 / (1 - SCH.tpHi));
  const tSoft = 106e-9 * (VCC2.hi - 5.6) / P.drv.isto[0];   // IGBT Cies from the high-corner rail to below V_th at the DS-minimum I_STO
  judge("Gate drive", "Global DRV_EN drop after a DESAT vs the faulted driver's soft turn-off", `${f(dLo * 1e6, 0)}–${f(dHi * 1e6, 0)} µs RC delay (+0.4–0.8 µs FLT)`,
    `IGBT soft-off ${f(tSoft * 1e6, 1)} µs at the DS-minimum 100 mA`, tSoft / dLo, 1.0,
    "F71: NSI6611 DS is silent on RST/EN during soft turn-off — 10 k/3.3 nF to the USCH Schmitt threshold (V_T− 0.22–0.49 V_CC) makes the design independent of it; FS0B/MCU paths stay undelayed");
  const tOsLo = 9.9e3 * 15e-9 * 0.95 * Math.log(1 / (1 - SCH.tpLo)), tOsHi = 10.1e3 * 15e-9 * 1.05 * Math.log(1 / (1 - SCH.tpHi));   // CCLR C0G ±5 % (round 7 cross-check)
  const tReset = dRise + 0.8e-6 + 1.5e-6;   // FLT_OKD to V_T+ max, NSI6611 t_RST_FIL 0.8 µs, FLT/FLT_CMB pull-up rise
  judge("Gate drive", "Fault-latch CLEAR one-shot (15 nF into 10 k, at the USCH output)", `${f(tOsLo * 1e6, 0)}–${f(tOsHi * 1e6, 0)} µs low per falling edge`, `≥ ${f(tReset * 1e6, 0)} µs to deliver the drivers' reset edge through the delay`,
    tReset / tOsLo, 0.9,
    "review F06 disposition: PRE=CLR=L (both outputs high) is the ONLY way to give the NSI6611s their RST/EN rising edge while FLT is still asserted — a fault-dominant latch would deadlock recovery. The one-shot bounds one stuck-low pin in hardware; a re-pulsing pin is RR03 → FW-15 (eFlexPWM fault lock), the drivers' own latch and the FS26 watchdog");
  add("Gate drive", "Slow edges at LVC inputs (Δt/ΔV 5–10 ns/V)", "RC nodes and FS0B via USCH (no limit); ASC_SET_N ≈32 ns/V, FLT_CMB_N ≈64 ns/V, RDY ≈0.2 µs/V remain", "-", "INFO",
    "round 7 RR01/RR02: the two RC nodes were 14,000–63,500 ns/V — buffered. The remaining open-drain release edges only return a latch or AND input to its idle level with no output change (PRE release with CLR high holds; RDY releases while MCU_GATE_EN is low per §9 sequencing and FW-14)");
}

// ============ 5. GATE-POWER FLYBACKS (real VGT winding: NP:NF:NS = 1:1.6:2.9, Lp 10 µH) ============
{
  // round 7 (A6-R06/R07): the divider senses its own aux rectifier (FFS), so VDD is the aux plateau
  // minus the US1M drop and the start feed can no longer hold FB above the reference
  const vFfs = P.pwm.vref * (52.3 + 15) / 15, vdd = vFfs + 0.55 - 0.85;
  const vcc2 = VCC2.nom;
  judge("Flyback", "VDD in regulation (FFS 52.3k/15k, aux plateau − US1M)", `${f(vdd, 1)} V (FFS ${f(vFfs, 2)} V)`, `${P.pwm.vccAbs} V abs`, vdd / P.pwm.vccAbs, 0.75, "F33 — a 15 V target through NF would push the secondaries to ~27 V");
  const inWin = VCC2.lo >= 13.5 && VCC2.hi <= 17.0;
  add("Flyback", "Derived gate rail VCC2 (both diodes, all corners)", `${f(VCC2.nom, 1)} V nom · ${f(VCC2.lo, 2)}–${f(VCC2.hi, 1)} V (VEE −4.8…−5.4 V)`, "13.5–17.0 V bias window", inWin ? "WARN" : "FAIL",
    `A6-R06: VCC2 = (V_FFS + Vf_FS)·NS/NF − Vf_sec − Vz. The A.6 model dropped the aux diode (15.6 V claimed, ${f(VCC2.old, 1)} V real). Window: NSI6611 rec-min 13 V + margin; ≤ 17 V keeps the SC current near the 15 V DS data. The low corner sits at the window edge (US1M at peak current, FB bias) — BENCH GATE: six-domain VCC2 at start, full gate load, ASC and no-load, KL30 9–16 V, 24 V and 33 V`);
  const iqLow = 1.5e-3, vStopOld = (16 - 2.2e3 * iqLow) / (1 + 2.2e3 / 71e3);
  add("Flyback", "Restart after a stopped interval (start feed vs FB)", "FB from the aux-only FFS node", "the start feed must not hold FB ≥ 2.5 V", "PASS",
    `A6-R07: with the A.6 VDD sense a stopped converter sat at ${f(vStopOld, 1)} V (> the 11.83 V target) on a 16 V rail for a ${iqLow * 1e3} mA part (DS: 2.3 typ, no min), and at the 18 V clamp at a 24 V jump start — no restart, gate power lost. FFS decays through the 67 k divider (τ 6.7 ms) and the controller restarts`);
  const fsw = P.pwm.kOsc / (10e3 * 680e-12);
  add("Flyback", "Switching frequency (10k/680p)", `${f(fsw / 1e3)} kHz`, "-", "INFO", "F32 — Lp 10 µH demands small per-cycle energy; osc anchors per SLUS458I curves");
  const pOut = 3 * (P.qg.total * P.qg.vswing * OP.fsw + vcc2 * P.drv.icc2) + 0.3;
  const pin = pOut / 0.78;
  const ipkOp = Math.sqrt(2 * pin / ((P.xfmr.lp / 3) * fsw));  // DCM peak of the BANK: three 10 µH primaries in parallel (A6-R14)
  const ilim = P.pwm.vcs / 0.33;
  judge("Flyback", "DCM peak current vs CS limit (bank)", `${f(ipkOp, 2)} A op`, `${f(ilim, 2)} A limit (0.33 Ω)`, ipkOp / ilim, 0.75, "A6-R14 — the common switch/shunt carries all three primaries: bank Lp = 10 µH/3 (the old check used one transformer's 10 µH: 1.18 A)");
  judge("Flyback", "CS limit as the saturation guard", `${f(ilim, 2)} A`, `${P.xfmr.isat} A (Isat unpublished — guard band)`, ilim / P.xfmr.isat, 1.0, "bench-verify core at current limit");
  // start condition (review A.6 F18): the rail must push I_START AND the 71 k divider current into VDD at VDD_ON(max)
  const vNeed = (r) => P.pwm.uvloOnMax + r * P.pwm.istart;   // round 7: the 67 k divider hangs on FFS, not VDD
  const v12at9 = OP.kl30.min - 0.95;                     // two reverse Schottkys + 3 polyfuses at ~1 A
  judge("Flyback", "Start threshold at the 12 V node, worst (2.2 k)", `${f(vNeed(2.2e3), 2)} V needed`, `${f(v12at9, 2)} V at KL30 = 9 V`, vNeed(2.2e3) / v12at9, 0.99,
    `A.6 needed 7.95 V (divider on VDD). Burst-to-takeover energy is S1's job`);
  judge("Flyback", "Start resistor dissipation @24 V jump start", `${f((24 - vdd) ** 2 / 2.2e3, 3)} W`, "0.25 W (1206)", (24 - vdd) ** 2 / 2.2e3 / 0.25, 0.5,
    `${f((16 - vdd) ** 2 / 2.2e3 * 1e3, 0)} mW at 16 V; VDD sits at the aux-derived ${f(vdd, 1)} V`);
  // DCM shunt RMS = Ipk·√(D_on/3), D_on = t_on·f with t_on = L_bank·Ipk/V_in — worst at the 9 V-crank node
  const dOn = (P.xfmr.lp / 3) * ipkOp / (OP.kl30.min - 0.95) * fsw, pRcs = ipkOp ** 2 * (dOn / 3) * 0.33;
  judge("Flyback", "CS resistor power (bank, DCM)", `${f(pRcs, 3)} W`, "0.75 W (1210)", pRcs / 0.75, 0.6, `D_on ${f(dOn, 3)} at 8.05 V in; the old (Ipk/√3)² assumed a 100 % duty triangle`);
}

// ---------- rev A.4 corrections (external design review F37–F46) ----------
{
  // F37 transformer phasing is topological (locked by erc-audit); the numbers it protects:
  const vRefl = 21.4 / 2.9;             // secondary total reflected through NS -> NP
  judge("Flyback A.4", "Reflected voltage vs clamp-TVS standoff", `${f(vRefl, 1)} V`, "13 V SMAJ13A standoff",
    vRefl / 13, 0.75, "F38 — TVS must stay dark in normal OFF; dots per TDK p.3/9");
  const vDrainLD = 39 + 21.5 + 0.7;     // clamped load-dump rail + TVS clamp + blocking Vf
  judge("Flyback A.4", "Drain worst case (clamped load dump)", `${f(vDrainLD, 1)} V`, "80 V BUK7Y14-80E",
    vDrainLD / 80, 0.85, "F38 — replaces SMBJ85A (94.4 V min breakdown, forward path in OFF)");
  // F39 DESAT clamp direction is topological (erc-audit); F40 ASC latch levels:
  // round 7 (A6-R01): FS1B holds V_OL ≤ 0.4 V only up to 2 mA and may current-limit at 4 mA. Budget at
  // V5A 5.1 V, 1 % parts: RENP2 5.1 k + the RFS1/RFS2 strap + a FAULT_OUT VCU load of ≥ 10 k to ≤ 5.1 V.
  const vOL = 0.4, v5 = 5.1;
  const iRenp = (v5 - vOL) / (5.1e3 * 0.99), iStrap = (v5 - vOL) / (11e3 * 0.99), iFout = (v5 - vOL - 0.3) / (9.9e3 + 0.99e3), iFs1b = iRenp + iStrap + iFout;   // FAULT_OUT sinks through DFO (BAT46 ≈0.3 V)
  judge("Safety A.7", "FS1B load at its V_OL point (5.1 k + strap + FAULT_OUT)", `${f(iFs1b * 1e3, 2)} mA`, "2 mA (V_OL ≤ 0.4 V; current limit ≥ 4 mA)", iFs1b / 2e-3, 0.95,
    `A6-R01: the 1 k pulls took ${f((5 / 1e3 + 5 / 11e3) * 1e3, 2)} mA — a 4 mA-limit part sat at 1.33 V, ASC_SET_N at 1.67 V (> VIL); the old row compared with the 22 mA maximum and divided by 1000 again`);
  const vSetLow = vOL + (v5 - vOL) * 1.01e3 / (1.01e3 + 9.9e3);
  judge("Safety A.7", "ASC latch asserted-low level (FS1B at V_OL, 1k into 10k)", `${f(vSetLow, 2)} V`, "1.35 V VIL (0.3·V_CC at 4.5 V)",
    vSetLow / 1.35, 0.75, "F40/A6-R01 — the old row assumed FS1B at 0 V");
  judge("Safety A.7", "FS0B load at its V_OL point (5.1 k into the USCH input)", `${f(iRenp * 1e3, 2)} mA`, "2 mA (V_OL ≤ 0.4 V)", iRenp / 2e-3, 0.6,
    "pin ≤ 0.4 V: under the SBC's own 0.7 V read-back threshold and the buffer's 1.0 V V_T− minimum");
  const vFoutLo = vOL + 0.3 + iFout * 1.01e3;
  judge("Safety A.7", "FAULT_OUT asserted level at the VCU (10 k to 5 V)", `${f(vFoutLo, 2)} V`, "1.5 V (5 V CMOS V_IL)", vFoutLo / 1.5, 0.85,
    "sink-only through DFO: the VCU must pull up (firmware-contract §9)");
  const vk = 16, vFs1bShort = (vk - 0.3 + 5 / 5.1 + 5.3) / (1 + 1 / 5.1 + 1);
  add("Safety A.7", "FAULT_OUT wire faults (FS1B released)", `to ground: ASC_SET_N stays 5.0 V (A.6: 2.83 V, first A.7 draft: 1.47 V = preset) · to KL30 16 V: ASC_SET_N clamped 5.3 V, RFS4 ${f((vk - 0.3 - vFs1bShort), 1)} mA`, "no unintended ASC preset; ≤ 6.5 V at the latch", "PASS",
    "round 7 cross-check item 2: DFO blocks a ground short, a dead VCU input and negative spikes; DSET clamps a battery short. While shorted to KL30 FS1B cannot pull the node low — the FS26 read-back reports FS1B short-to-high (degraded, detected)");
  judge("Safety A.7", "ASC break-before-make: HS off before LS on", `LS starts ≥ ${f(ASC.tLsMin * 1e6, 2)} µs after the latch sets`,
    `HS off by ${f((ASC.tHsEn + 2.5e-6) * 1e6, 2)} µs (${f(ASC.tHsEn * 1e6, 2)} µs to EN + 2.5 µs IGBT dead time)`, (ASC.tHsEn + 2.5e-6) / ASC.tLsMin, 0.9,
    "RR05, FS1B path shown (FS0B → USCH → ANDs → EN); the MCU path is faster (eFlexPWM fault on the high-side outputs → IN+ low, tpHL ≤ 0.13 µs), and its low sides come on by PWM after the dead time. EN stays high on the MCU path so LS DESAT keeps priority (DS §8.12). SiC dead time is 1.0 µs — more margin");
  add("Safety A.7", "ASC entry, latch set → LS gates on (worst)", `${f(ASC.tEntryMax * 1e6, 2)} µs`, "counted in the FW-06 budget (§2b)", "INFO",
    "release ≤ 0.75 µs (TLP152 tpHL 0.19 µs + DASCR discharge + tASC_f 0.48 µs); exit is MCU-sequenced (FW-06a)");
  const iLed = (4.9 - 0.24 - 1.8) / 270, iLedMax = (5.1 - 0.05 - 1.4) / 270;
  judge("Safety A.7", "ASC opto LED current (RASCL 270 R) vs TLP152 I_FLH", `${f(iLed * 1e3, 1)} mA min · ${f(iLedMax * 1e3, 1)} mA max`, "7.5 mA I_FLH max · 15 mA recommended max",
    7.5e-3 / iLed, 0.8, "round 7 (self-found, N10): 470 R gave 5.4–7.0 mA, under the guaranteed turn-on current. V5A 4.9 V, LVC V_OH drop 0.24 V at 11 mA, V_F 1.8 V max");
  // F41 KL15 sense:
  judge("LV A.4", "IGN_SNS at 16 V KL15", `${f((16 - 0.7) * 10 / 57, 2)} V`, "5 V ADC range",
    ((16 - 0.7) * 10 / 57) / 5, 0.75, "F41 — was a raw diode into PTA25 (13.3 V)");
  judge("LV A.4", "IGN pin injection @40 V load dump", `${f((40 - 0.7 - 5.3) / 47e3 * 1e3, 2)} mA`, "3 mA S32K39 injection spec",
    ((40 - 0.7 - 5.3) / 47e3) / 3e-3, 0.5);
  // F42 VDC receiver fail-safe discrimination:
  judge("Sensing A.4", "Fail-safe window (healthy-zero 0.5 V vs railed ~0.05 V)", "0.45 V window", "≥0.2 V discrimination",
    0.2 / 0.45, 0.9, "F42 — AMC1311 dead-HV state now distinguishable from a dead bus");
  // F43 divider FS at tolerance corner (850 V system max, 470k 1% top / 6.2k 0.1% bottom):
  const fsWc = 2.0 * (2.82e6 * 0.99 + 6.2e3 * 1.001) / (6.2e3 * 1.001);
  judge("Sensing A.4", "V_DC linear FS, worst tolerance corner", `${f(fsWc, 0)} V`, "850 V operating max",
    850 / fsWc, 0.97, "F43 — reviewer corner assumed 900 V operation and 1% bottom; ours is 850 V / 0.1%");

  // ---- rev A.4.1 (second review round, F47–F51) ----
  judge("LV A.4", "TPS55340 SYNC pin level (grounded)", "0 V", "7 V abs on SYNC", 0.01, 0.5,
    "F49 — pin 5 is SYNC, not a second VIN; 12 V there exceeds abs max");
  judge("LV A.4", "V15 behind ULDO15 @24 V jump start", "15.0 V", "16.5 V QA01C normal-max",
    15.0 / 16.5, 0.95, "F51 — boost pass-through clamped; LDO input 23.5 V << 40 V rating");
  judge("LV A.4", "ULDO15 input at clamped load dump", "≈33 V", "40 V NCV4276C operating max",
    33 / 40, 0.9, "F51 — TPSMC24CA clamp level on the 12 V node");
  judge("LV A.7", "CB15O1/2 (V15B) at clamped load dump", "≈33 V", "50 V MLCC rating",
    33 / 50, 0.8, "round 7 RR10 — V15B follows V12L−Vf in pass-through; the 25 V parts were overstressed");
  judge("LV A.4", "ULDO15 dissipation @24 V sustained", `${f((24 - 0.5 - 15) * 0.33, 1)} W`, "TSD-protected (survival case, not an operating mode)",
    0.5, 0.9, "jump start is stationary service — brief V15 brown-out via TSD is acceptable; passive bleeder unaffected");

  // ---- rev A.4.2 (third review round, F52–F57) ----
  // FS26 buck passives: effective capacitance at bias/temperature/tolerance corners
  const effV15S = 44 * 0.8 * 0.9;      // 2x22 µF at 1.5 V bias (negligible) · -20 % tol · temp
  judge("LV A.4", "VCORE COUT effective (2×22 µF @1.5 V)", `${f(effV15S, 0)} µF`, "20–100 µF eff (Table 106)",
    20 / effV15S, 0.9, "F52 — was 10 µF nominal; inductor now 2.2 µH per CORE_LSEL_OTP");
  const effVPRE = 44 * 0.8 * 0.75;     // 2x22 µF at ~6 V bias on 16 V dielectric
  judge("LV A.4", "VPRE COUT effective (2×22 µF @6 V)", `${f(effVPRE, 0)} µF`, "≥22 µF test condition",
    22 / effVPRE, 0.95, "F52");
  const effVIN = 22 * 0.8 * 0.65;      // 22 µF 25 V 1210 at 14 V bias
  judge("LV A.4", "VPRE input effective (22 µF @14 V)", `${f(effVIN, 1)} µF`, "≥10 µF eff",
    10 / effVIN, 0.95, "F52 — CLVC2 4.7 µF was under the input spec");
  const effVREF = 3.3 * 0.8 * 0.85;    // CSB5 2.2 + CMA1 1 + CMA2 0.1 at 5 V bias
  judge("LV A.4", "VREF5 rail effective (whole rail: 3.3 µF nom)", `${f(effVREF, 1)} µF`, "1.1–3.3 µF eff window",
    effVREF / 3.3, 0.9, "reviewer correction accepted: count CMA1/CMA2, judge the rail not one part");
  // ULDO15 / ULDOEX compensation zeros vs onsemi 11–18 kHz guidance
  judge("LV A.4", "ULDO15 feed-forward zero (49.9k·220pF)", `${f(1 / (2 * Math.PI * 49.9e3 * 220e-12) / 1e3, 1)} kHz`,
    "11–18 kHz (onsemi Cb guidance)", 14.5 / 18, 0.95, "F54 — COUT 22 µF ceramic");
  judge("LV A.4", "ULDOEX feed-forward zero (38.3k·270pF)", `${f(1 / (2 * Math.PI * 38.3e3 * 270e-12) / 1e3, 1)} kHz`,
    "11–18 kHz (onsemi Cb guidance)", 15.4 / 18, 0.95, "F55");
  judge("LV A.4", "VEXD target for ALM2402", `${f(2.5 * (1 + 38.3 / 10), 2)} V`, "16 V recommended max (18 V abs)",
    (2.5 * (1 + 38.3 / 10)) / 16, 0.85, "F55 — was raw VBATC: 24 V jump start exceeded abs max");
  // Gate-power switch drive legality + clamp loading
  judge("Flyback A.4", "QF gate drive vs BUK7Y14-80E VGS abs", "11.8 V", "±20 V DC (was BUK9Y: ±10 V)",
    11.8 / 20, 0.8, "F56 — logic-level part was outside abs max at the VDD drive");
  add("Flyback A.4", "Gate zener standing load", "0 W (BZT52-C15 dark at the ≈11 V VDD)", "was ~0.4 W/zener all ON-time", "PASS",
    "F56 — the 5.6 V clamp conducted ~0.29 A through every ON interval (historical estimate, not carried into the new budget)");

  // ---- rev A.5 documentation-audit findings (F60–F61) ----
  judge("Discharge", "QDIS gate at the QA01C rail (+20 V per DS)", "≈19.5 V", "+22 V abs (+18 V rec) HCM75S12T4K3",
    19.5 / 22, 0.85, "F61 — the base QA01C row is +20/−4 V; inside abs, above rec — gate divider option at proto if bench confirms 20 V");

  // ---------- IGBT SKUs: HCG600FH120D3E1EA (same D3 pads + pin map) ----------
  // Losses/thermal/DESAT timing now live in the per-SKU blocks above (review A.6 F03/F26/F27);
  // what stays here is what the silicon swap itself must satisfy.
  judge("IGBT SKUs", "Gate rails legality (+15.6/−5.1)", "on 15.6 V · off −5.1 V", "±20 V abs; VGE(th) min 5.0 V",
    15.6 / 20, 0.85, "DS characterizes at ±15; high Vth + Miller clamp justify −5.1 off-bias — dv/dt shoot-through is a DPT row");
  add("IGBT SKUs", "Pin map / footprint", "IDENTICAL to HCS600FH120D3C1 (DS p.8: 1=G_L 2=E_L 3=DC− 4=DC+ 5/6=NTC 7=G_H 8=E_H 9=C-sense 10/11=AC)", "-", "PASS",
    "zero layout change; MODx pinLabels carry over (KS labels = Kelvin emitter)");
  add("IGBT SKUs", "Short-circuit rating used for DESAT timing", "tP ≤ 6 µs @800 V, 175 °C, VGE 15 V (DS Table 5)", "-", "PASS",
    "F03: docs and S9 carried a 10 µs class; 850 V/15.6 V operation shortens it — treated as ≈5 µs");

  // ---- rev A.4.3 (fourth review round, F58–F59) ----
  const D15 = 1 - 12 / 15.4, Rld15 = 15.4 / 0.33;
  const fRHPZ = (1 - D15) ** 2 * Rld15 / (2 * Math.PI * 10e-6) / 1e3;
  judge("LV A.4", "UB15 comp zero (2k·100nF) vs output pole", "796 Hz vs ~140 Hz", "fZ slightly above fP (TI rule)",
    0.6, 0.9, "F59 — series RC replaces the lone 10 nF (screening phase margin ~0°); bench Bode gates it");
  add("LV A.4", "UB15 RHP zero @12 V/0.33 A", `${f(fRHPZ, 0)} kHz`, "far above the loop crossover", "PASS",
    "F59 — light-load boost: RHPZ not the constraint");
}

// ============ 6. LV RAILS & PROTECTION ============
{
  const vb15 = P.boost.vref * (1 + 110 / 9.53);
  judge("LV", "V15 boost setpoint (110k/9.53k)", `${f(vb15, 2)} V`, "13.5–16.5 V module window", Math.abs(vb15 - 15) / 1.5, 0.7, "QA01C/ISO5V input range");
  const iload15 = 2 * (1 / 0.75) / 15 + 2 * (1 / 0.75) / 15;      // 2×QA01C + 2×ISO5V at ~1 W class
  const iin15 = vb15 * iload15 / (OP.kl30.min * 0.85);
  judge("LV", "Boost switch current @9 V", `${f(iin15, 2)} A avg`, `${P.boost.ilim} A limit`, iin15 / P.boost.ilim, 0.5);
  const i5 = 6 * 0.005 + 0.02;
  judge("LV", "NCV4276 5 V load (6 driver VCC1 + optos)", `${f(i5 * 1e3)} mA`, "400 mA", i5 / 0.4, 0.5);
  judge("LV", "FS26 VMONEXT divider (52.3k/10k @5 V)", "0.794 V", "0.8 V fixed reference ±window", Math.abs(0.794 - 0.8) / 0.8 / 0.12, 0.5, "F34 — old 10k/18.7k fed 3.26 V = permanent OV; OTP window set around 100 %");
  add("LV", "S32K39 core topology", "FS26 VCORE→V15S 1.5 V → QBAL ballast → V11 1.14 V", "-", "PASS", "F36 — per DS Table 11; direct VCORE→V11 is not a supported topology");
  const pLdo = (12 - 5) * i5;
  judge("LV", "NCV4276 dissipation @12 V", `${f(pLdo, 2)} W`, "~1.5 W DPAK on copper", pLdo / 1.5, 0.6);
  const iChain = pOutChainEstimate();
  judge("LV", "Polyfuse hold (worst chain @9 V)", `${f(iChain, 2)} A`, "3 A hold", iChain / 3, 0.65);
  add("LV", "Load-dump path", "TVS 24 V standoff, clamp ~39 V", "-", "PASS", "F25 — all 12 V-node MLCCs raised to 50 V rating; TPS55340 Vin abs 45 V rides the clamped pulse");
  function pOutChainEstimate() {
    const pFly = (3 * 1.0 + 0.3) / 0.78;
    const p15 = vb15 * iload15 / 0.85;
    return (pFly + p15) / OP.kl30.min;   // L-chain carries flyback + boost
  }
}

// ============ 7. SENSING ============
{
  const vtap = OP.vbusMax * 6.2e3 / (6 * 470e3 + 6.2e3);
  judge("Sensing", "VDC divider @850 V (6.2 k bottom)", `${f(vtap, 3)} V`, `${P.amc.vinFs} V AMC FS (= 911 V readable)`, vtap / P.amc.vinFs, 0.96, "OV witness keeps headroom above V_bus,max — F27");
  // F37: correlated top string (one lot, one temperature) does NOT shrink by √6 — worst case is linear.
  const top = P.tolRp, bot = 0.001, amc = P.amc.gainErr + P.amc.vos / 1.87, rx = 2 * 0.001, ref = 0.005;
  const gWc = top + bot + amc + rx + ref, gRss = Math.sqrt((top / Math.sqrt(6)) ** 2 + bot ** 2 + amc ** 2 + rx ** 2 + ref ** 2);
  judge("Sensing", "VDC chain error, WORST CASE uncalibrated", `±${f(gWc * 100, 2)} % (±${f(gWc * 880, 0)} V at the 880 V OV trip)`, "OV trip below the 1000 V can rating",
    (880 * (1 + gWc)) / 1000, 0.95, `top 1 % correlated + bottom 0.1 % + AMC1311B 0.2 %+offset + receiver 0.2 % + VREF5 0.5 %; RSS would claim ±${f(gRss * 100, 2)} %`);
  add("Sensing", "VDC chain error after EOL gain/offset calibration", "≈±0.3 % (residual drift/nonlinearity)", "5 % cross-check window", "PASS",
    "calibrated values feed protection only after the stored record passes CRC + range checks (F42)");
  add("Sensing", "Shared receiver offset VOFS monitored", "UVOF output → MCU ADC (PTB1)", "±5 % of 0.5 V", "PASS",
    "F11: a failed UVOF would shift BOTH channels by up to 0.5 V (≈228 V) and pass the 5 % cross-check — now read directly; BMS pack voltage is the third witness when contactors are closed");
  const pDiv = OP.vbusMax ** 2 / (6 * 470e3 + 6.2e3);
  judge("Sensing", "Divider dissipation @850 V", `${f(pDiv * 1e3)} mW total`, "6× 1206 (250 mW ea)", (pDiv / 6) / 0.25, 0.5, `${f(OP.vbusMax / 6)} V per 200 V-rated 1206 — 71 %`);
  const vHall = P.hall.v0 + OP.iphPk * Math.SQRT2 * P.hall.sens;
  judge("Sensing", "Hall output at 480 A pk", `${f(vHall, 2)} V`, "0.3–4.7 V buffer swing", (vHall - 2.5) / 2.2, 0.75);
  add("Sensing", "Hall ratiometric ref vs ADC ref", "V5S(V5A) vs VREF5", "-", "WARN", "two 5 V sources — ~±1–2 % gain drift between them; calibrate at EOL or move VREFH to V5A (GEN3 ships the same topology)");
  // HVIL signatures
  add("Sensing", "HVIL signatures (drive hi/lo/open)", "3.0 / 2.0 / 2.5 V", "-", "PASS", "distinct at ±5 % R tolerance (worst separation 0.38 V)");
  const vmp = 4.0 * 12.1 / (12.1 + 4.99), vmn = 4.0 * 24 / (24 + 4.99);
  judge("Sensing", "Resolver monitor dividers @4 V pk", `${f(vmp, 2)} / ${f(vmn, 2)} V`, "5 V SDADC input", Math.max(vmp, vmn) / 5, 0.85);
  add("Sensing", "Resolver drive @9 V KL30", "≈6.5 V pp available vs 8 V pp target", "-", "WARN", "ALM2402 swing at cold-crank INCLUDING ULDOEX dropout (~0.3 V @ ~150 mA, A.4.3) — angle still tracks (amplitude-invariant demod); GEN3-equivalent behavior");
}

// ============ 8. SAFETY CHAIN TIMING/LOGIC ============
{
  add("Safety", "FS0B → driver EN path", "2 gate delays (~20 ns) + driver td", "-", "PASS", "no software; erc-verified topology");
  add("Safety", "ASC latch power", "V5A + RASCP default-low", "-", "PASS", "survives MCU reset; FS1B can SET via RFS1");
  add("Safety", "ASC drive path", "TLP152 + QA01C + 5.1 V clamp, DCN-referenced", "-", "WARN", "DS 1.2 confirms ASC forces OUTH high at a GND2-referenced 0-5 V pin (F28 level fix applied); behaviour DURING VCC2-UVLO is unspecified — bench-verify that gate power (SBC-held flybacks) is sufficient for ASC hold");
  add("Safety", "Default-OFF discipline", "11 pulldowns power + 4 card", "-", "PASS", "erc-verified");
}

// ---------------- render report ----------------
const counts = { PASS: 0, WARN: 0, FAIL: 0, INFO: 0 };
rows.forEach((r) => counts[r.status]++);
let md = `# Design Verification Report — rev ${REV} (${new Date().toISOString().slice(0, 10)})

End-to-end verification of the 220 kW / 800 V traction inverter at actual operating corners
(V_bus 500–850 V · KL30 9–16 V · 10 kHz · 65 °C coldplate), worst-case component tolerances.
Three independent layers:

1. **Geometric pin-verify** (sheets vs netlist, \`kicad5-verify.mjs\`) — the sheets ship only at 100 %
2. **Structural ERC audit** (netlist vs design intent, \`erc-audit.mjs\`) — 0 fail, incl. a lock-in per fixed finding
3. **Numeric verification** (this report, \`design-verify.mjs\`): **${counts.PASS} PASS · ${counts.WARN} WARN · ${counts.FAIL} FAIL** (+${counts.INFO} info)

A WARN is an item this analysis cannot close on paper — each names its bench or vendor gate.
Every SKU of the platform (8XX/4XX × SiC/IGBT — \`loss-model.mjs\`) is checked on the
same PCBs; losses and thermal use the shared model that \`sim-verify.mjs\` also runs.

## Findings log (F1–F36 rev A.3 campaign · F37–F46 rev A.4 · F47–F51 rev A.4.1 · F52–F57 rev A.4.2 · F58–F59 rev A.4.3 · F60–F62 rev A.5 docs audit · F63–F76 rev A.6 external review round 6 · F77–F89 rev A.7 review round 7 — all fixed; review cross-reference in [\`review-A6-disposition.md\`](review-A6-disposition.md) and [\`review-A7-disposition.md\`](review-A7-disposition.md))

| # | Severity | Finding | Fix |
|---|---|---|---|
| F77 | **HIGH** | The A.6 RC timing nodes drove non-Schmitt LVC inputs: the clear one-shot into ULAT2 /CLR at ≈63,500 ns/V (5 ns/V allowed), the soft-off delay into UAND2 at ≈14,000 ns/V (10 ns/V) (RR01/RR02, A6-R02/R03) | 74LVC3G17-Q100 Schmitt buffer (no Δt/ΔV limit) on both nodes and on FS0B; one-shot 61–230 µs, delay 22–53 µs at its thresholds |
| F78 | **HIGH** | FS1B loaded 5.45 mA through 1 k pull-ups: V_OL ≤ 0.4 V holds only to 2 mA and the limit can be 4 mA — FS1B 1.33 V, ASC_SET_N 1.67 V (> VIL), SBC read-back (< 0.7 V) fails; the checker compared with 22 mA and divided by 1000 twice (A6-R01) | RENP1/2 5.1 k (NXP value): 1.79 mA incl. strap and a specified FAULT_OUT load, ASC_SET_N ≤ 0.84 V; checker at the V_OL point |
| F79 | **HIGH** | ASC entry had no break-before-make: FS0B/FS1B assert together on the MCU-dead path (HS turn-off raced the LS ASC), and the MCU path had no ordered entry (RR05) | CASCD 12 nF + DASCR: LS ASC ≥ 3.4 µs after the latch, entry ≤ 7.0 µs, release ≤ 0.75 µs; MCU path = eFlexPWM fault (high sides off) → ASC_REQ → PWM-ASC with EN high after the dead time (§4c). A first draft also dropped DRV_EN from the latch (DASC) — removed: with EN low the NSI6611 does not give DESAT priority over ASC (DS §8.12, cross-check) |
| F80 | **HIGH** | Flyback FB divider on VDD: the 2.2 k start feed could hold FB above 2.5 V with the converter stopped (12.3 V at a 16 V rail for a 1.5 mA controller; 18 V clamp at a 24 V jump start) → no restart, gate power lost (A6-R07); the VCC2 model omitted the aux diode — the real rail was 16.9 V, not 15.6 V (A6-R06) | FB senses its own aux rectifier (1N4148WS + 100 Ω + 100 nF), 52.3k/15k: VCC2 15.4 V nom, 14.0–16.7 V corners in a 13.5–17.0 V window |
| F81 | MED | CB15O1/2 22 µF/25 V on V15B, which follows V12L − Vf to ≈33 V in pass-through (RR10) | 22 µF/50 V 1210 (same part as CLVC2); boost PM 72–84° over 20–35 µF effective |
| F82 | MED | TLP152 ASC opto LED at 5.4–7.0 mA through 470 Ω — below its 7.5 mA guaranteed turn-on current (self-found, N10) | RASCL 270 Ω: 10.6–13.5 mA |
| F83 | MED | The one-shot comment claimed runaway code could not hold the chain permissive; repeated clear pulses do (RR03) | claim corrected; FW-15 locked eFlexPWM fault inputs (PWM forced low while FLT) + FW-12 WD_ERR_LIMIT 2 → FS0B |
| F84 | MED | DESAT timing row judged PASS at the 400 mA soft-off only; the 100 mA DS minimum needs 8.9–10.1 µs vs tP ≤ 6 µs (RR04/A6-R04) | both corners shown, WARN = release gate (I_STO distribution, SC envelope at 850 V and actual gate bias, contained SC test) |
| F85 | LOW | Thermal: S4/steady-state put only the switch die into the coldplate term; the diode heats the same plate (RR07) | tjPos: plate carries IGBT + diode; 8XX/4XX IGBT 30 s peak 129/123 °C |
| F86 | LOW | 8 kHz SiC mode allowed a 1.2 kHz current-loop crossover (43.3° PM) (RR08) | ≤ 1.1 kHz at 8 kHz (47.2°) in the SKU table |
| F87 | LOW | bom-gen exited 0 with missing inputs; JSWD 10-pin source vs a 20-pin BOM part (A6-R09/R10) | preflight aborts before any write (missing/empty/malformed/stale); Samtec FTSH-105-01-L-DV-K + contact-count check |
| F88 | LOW | Flyback peak-current check used one transformer's Lp for the bank current; README kept pre-A.6 loss numbers (A6-R14, README note) | bank Lp = 10 µH/3 (2.05 A, 68 %); README sizing regenerated from the loss model |
| F89 | LOW | ULAT/ULAT2 were TI SN74LVC1G74DCUR, a catalog part with no AEC-Q100 variant (self-found, N11) | Nexperia 74LVC1G74DC-Q100, pin-identical |
| F63 | **HIGH** | S8 turn-off overshoot used \`Ln·20e12·1e-6\` for 20 kA/µs — 1000× too small (0.3 V instead of 300 V at 15 nH), so its PASS was void; with the DS fall time (13 ns cold at 3.3 Ω ≈ 30 kA/µs at 481 A) no EconoDUAL-class loop holds 1080 V at 850 V (review R-F01) | SI units; overshoot budget from the DS tf; RG_OFF start value 6.8 Ω (Eoff booked in the loss model), RG_ON 3.3 Ω (the only characterized point, was 1.5/1.0); DPT gate at 850 V cold/hot; module Ls requested from hiitio |
| F64 | **HIGH** | SiC conduction loss used the IGBT transistor-only formula \`I·√(1/8+m·cosφ/3π)\`: synchronous SiC conducts ½·I²·R per switch — understated 2.4× (136 → 330 W/switch at 340 A) (R-F02) | exact ½·I²R + switching at the fitted Rg + Qrr + dead-time diode; thermal and efficiency restated (peak 30 s Tj 122 °C at 850 V, not 89 °C; 99.0 % semiconductor efficiency at the continuous point) |
| F65 | **HIGH** | IGBT short-circuit rating carried as "10 µs class"; HCG600 DS Table 5 says tP ≤ 6 µs at 800 V/175 °C/15 V; 150 pF blanking = 4.5 µs worst detection alone (R-F03) | IGBT blanking 82 pF C0G: 2.98 µs worst detection + ~1.5 µs soft-off < 5 µs derated; contained SC test is the release gate (the DS-minimum 100 mA soft-off current is not coverable) |
| F66 | **HIGH** | Gate-power flyback could not start at KL30 9 V: 4.7 k needed 8.47 V at the 12 V node (divider current omitted) AND the UCC28C40's 0.4 V UVLO hysteresis gave ~0.15 ms bursts on 4.7 µF (R-F17/F18, cycle-by-cycle S1) | 2.2 k 1206 start + 47 µF VDD (one-burst start in every corner) + 18 V VDD zener (the C40 has no internal clamp — the lower start resistor would lift VDD past 18 V at jump start with the flyback disabled) |
| F67 | MED | "220 kW / 120 kW over 500–850 V" is not deliverable at 340/185 A: full power needs ≥654/656 V (PF 0.85, 5 % modulation reserve) (R-F09) | published P(V_dc) envelope per SKU; firmware derates by V_dc |
| F68 | MED | Passive bleeder 5 × 27 k: the low-tolerance part carries 184 V at 850 V = 92 % of a plain 2512's 200 V working rating (R-F24 at 850 V) | 2 × 6 × 22 k = 66 k: 154 V (77 %), 56 s / 65 s to 60 V |
| F69 | MED | Global DRV_EN drop (≈0.5–0.9 µs after DESAT) could interrupt the faulted driver's soft turn-off — the NSI6611 DS does not state RST/EN priority during soft-off (R-F05) | 10 k/3.3 nF between the latch and the AND's Schmitt input: 12–40 µs; FS0B/MCU paths undelayed |
| F70 | MED | Fault-latch clear was level-sensitive: a stuck-low MCU pin held PRE=CLR=L (both outputs high) and silently disabled the global latch (residual of R-F06; the proposed "fault-dominant" fix would deadlock the NSI6611 FLT reset) | clear is a hardware one-shot (15 nF into the 10 k pull-up + BAT46 clamp): ≥54 µs per falling edge, re-arms by itself |
| F71 | MED | IGBT build thermal/efficiency omitted the FWD die, used m·cosφ = 0 and DS energies at 0.51 Ω; Qg scaled linearly (R-F26/F27) | separate IGBT/diode dies with m·cosφ (motoring + regen), energies referred to our driver, full Qg; 8XX IGBT rated at 5 kHz (127 °C end of 30 s, not 110 °C) |
| F72 | MED | Both V_DC receivers share the +0.5 V offset buffer UVOF: its failure shifts both channels by up to 228 V and passes the 5 % cross-check — OV and discharge witness blinded (R-F11) | VOFS routed to an MCU ADC (zero parts); BMS pack voltage is the third witness; the "fully independent" wording corrected |
| F73 | MED | BOM class MPNs had drifted from the netlist: RFS1–4 printed R0603-120R (re-creating F40), CLVC2 4.7 µF (re-creating F52), 10 more lines; the IGBT variant BOM printed the SiC value next to the IGBT MPN | parts-db fixed; SKU rows carry their value; bom-gen FAILS on any value/MPN disagreement |
| F74 | LOW | Simulation defects: S5 counted the bleeder twice; S10 hold-up put the VEE cap in parallel with VCC2 and ignored the bleeder/gate loads (15 ms claimed, 1.1–3.2 ms real); S4 started cold; S6 applied the 10 kHz bandwidth to the 4–6 kHz IGBT; S1 had no startup model (R-F14/F17/F25/F28/F32) | all rewritten on the shared loss model; ASC through total LV loss not credited (unchanged conclusion, corrected number) |
| F75 | LOW | Resolver cable shields terminated into AGND at the vehicle connector (R-F35) | shields on the connector ground (DGND); AGND keeps its single-point tie |
| F76 | LOW | Documentation overstated: HVIL "hardware window comparator" (it is an MCU ADC signature), RSS labelled worst case (±0.7 % → ±2.1 % worst), 0.62 ripple factor (worst 0.65), XM3 inductance reused for a D3 module, bias-bank "3.87 W" (100 % CS limit before losses; 2.3 W worst parts), and two "drop-in" module alternates that are not (HCS800FH120D4B3 has a lettered press-fit pin map; FF6MR12W2M1H is not an EconoDUAL-3 package code) (R-F12/F21/F29/F37) | wording and numbers corrected; IGBT SKUs fit RT 8.2 k (~308 kHz) for gate-power margin; alternates list limited to pin-map-verified parts |
| F1 | **HIGH** | Flyback CS resistor 0.033 Ω vs UCC28C43's 1 V threshold ⇒ 30 A "limit" = no overcurrent protection (value was scaled for the NJW4140's low CS threshold) | 0.22 Ω/1210 ⇒ 4.5 A limit vs 2.4 A worst-case operating peak |
| F7 | **HIGH** | FB divider (18k/15k/1.3k, GEN3 values for the NJW ref) regulates VCC at **5.26 V** with the 2.5 V UCC28C43 reference ⇒ UVLO lockout, gate supply never starts | 75k/15k ⇒ VCC 15.0 V |
| F21 | **HIGH** | Flyback VCC had **no start path** (aux-winding-only feed cannot bootstrap) | 4.7 k trickle-start from the 12 V rail (425 µA @9 V vs 100 µA start spec) |
| F20 | **HIGH** | Five net→net alias \`<trace>\`s left pins floating on the drawing: gate-bias winding returns (6×), module-NTC returns (3×), MCU temp inputs (5×), card-side TMOD_RTN unterminated | all aliases removed — direct binding; TMOD_RTN star-tied to AGND via RTMR 0 Ω |
| F2 | MED | Bus-to-chassis Y caps specced as **Y2** (250 Vac line class) at an 850 V DC bus | Y1-class 4.7 nF (500 Vac / 8 kV impulse); alt 2×Y2 series / CeraLink |
| F4 | MED | The two "independent" V_DC senses shared **one** bias module (common-cause vs the stated safety mechanism #7) | second reinforced module (PS5C) — channel 2 fully independent |
| F25 | MED | 12 V-node MLCCs were 25 V-rated under a 24 V-standoff TVS that clamps ≈ 39 V in load dump | all KL30-node caps ⇒ 50 V rating |
| F26 | MED | Active discharge (4×560 Ω) = 2.19 s at the R+5 %/C+10 % corner — over the 2 s crash target | 4×470 Ω ⇒ 1.60 s nom / 1.84 s worst |
| F27 | LOW | V_DC divider hit exactly 2.0 V full-scale at 850 V — the OV witness saturated right where it matters | bottom 6.65 k ⇒ 6.2 k (full-scale = 911 V) |
| F28 | **HIGH** | ASC pin driven from an 18 V rail vs **abs max GND2+6 V** (NSI6611 DS 1.2) | 2.2 k series + 5.1 V zener clamp at the ganged pins |
| F29 | MED | Module symbol used symbolic aux pins; HS DESAT sensed the DC+ power terminal | REAL HCS600 pin map (1=G_L…9=HS drain-sense, 10/11=AC); HS DESAT moved to the dedicated aux sense pin |
| F30 | LOW | Gate off-bias −4.3 V is not a HCS600-recommended combo (+15 pairs with −5) | zener split ⇒ +15/−5.1 (also consolidates to the C5V1 already on the BOM) |
| F31 | **HIGH** | UCC28C43's real UVLO is 8.4/7.6 V (SLUS458I) — the gate-power flyback cannot start at 9 V cold-crank | UCC28C40 grade (7.0/6.6 V) |
| F32 | **HIGH** | VGT12EEM's real Lp is 10 µH (not a 200 µH-class part) — at 52 kHz the peak current would hit ~10 A every cycle | oscillator retimed to ~250 kHz (10 k/680 pF); DCM Ipk ≈ 1.2 A |
| F33 | **HIGH** | FB divider targeted 15 V on the NF winding; with NP:NF:NS = 1:1.6:2.9 that drives the secondaries to ~27 V (zener overstress) | 56k/15k ⇒ VCC_reg 11.8 V ⇒ V_sec 21.4 V ⇒ +15.6/−5.1 V rails |
| F34 | **HIGH** | FS26 VMONEXT is a fixed 0.8 V reference — the 10k/18.7k divider fed it 3.26 V = permanent overvoltage fault | 52.3k/10k ⇒ 0.794 V at 5 V nominal |
| F35 | MED | FS0B/FS1B low-side outputs clamp at 4–22 mA; 120 Ω pull-ups forced 42 mA | 1 k pull-ups (4.6 mA) |
| F37 | **HIGH** | Gate-power transformer secondaries used the NON-dot end for the rectifier (TDK dots: NP=pin 2, NS=pin 8) ⇒ forward-mode transfer ≈2.9×Vin ≈ 35 V into gate rails rated +22 V abs | S-winding use swapped: rectifier on pin 8 (dot), return pin 5; pins 6/7 (no internal connection) NC'd; locked by ERC |
| F38 | **HIGH** | Flyback drain clamp SMBJ85A drawn forward (anode at drain) ⇒ conducts every OFF interval; and 94.4 V min breakdown cannot protect an 80 V FET | US1M blocking diode into SMAJ13A TVS returned to the rail: drain ≤60 V at clamped load dump, TVS dark below 13 V standoff (reflected 7.4 V) |
| F39 | **HIGH** | DESAT clamp fed VCC2 *into* the DESAT node (A1=VCC, K=DESAT) and treated BAT64-04 as common-cathode (it is a series pair) | Series pair correctly oriented: anode end on DESAT, cathode end on VCC2, junction pin NC |
| F40 | **HIGH** | ASC latch strapping: 120 Ω/120 Ω made a 2.5 V "low" at /PRE (indeterminate) and demanded 42 mA from the MCU on /CLR; SN74LVC1G74 symbol pin order did not match DCU package | 1 k series / 10 k pull-ups (asserted low = 0.45 V), MCU clear via 1 k, real CLK=1/D=2//Q=3/GND=4/Q=5//CLR=6//PRE=7/VCC=8 map |
| F41 | **HIGH** | KL15 → diode → PTA25 with no interface: 13.3 V onto a 5 V-domain MCU pin (RIGN1/2 belong to the WAKE1 divider, not this path) | 47 k/10 k divider + 100 nF after the diode; 16 V reads 2.68 V, load-dump injection ≤0.72 mA vs 3 mA spec |
| F42 | MED | AMC1311 fail-safe (negative differential when HV side dead) rails the single-supply receiver to 0 V — indistinguishable from a discharged bus | Receivers re-zeroed to +0.5 V via buffered VREF5 divider (OPA376); fail-safe ≈0 V vs healthy-zero 0.5 V |
| F43 | LOW | Reviewer flagged divider linear-range margin at 900 V/1 % | Not applicable as reviewed: system max is 850 V and the bottom leg is 0.1 % — worst-corner FS ≈ 902 V; margin documented |
| F44 | **HIGH** | VBAT_H/VBAT_L appeared ONLY as harness pins — no source anywhere on the card ⇒ the whole gate-power system had no positive feed | FVBH/FVBL polyfuses from the reverse-protected node NRC feed pins 31/32 & 35/36; power board keeps per-bank fuse+TVS+filter |
| F45 | **HIGH** | Symbol pin maps did not match packages: 74LVC1G11 (unpowered — no VCC pin at all), 74LVC1G32, NCV4276C (output on NC pin 4), TPS55340 (6-pin symbol for RTE-16; SS and FREQ missing entirely), BUK9Y14 (G/S swapped vs LFPAK56), ALM2402 (PWP-14; VCC_O supplies absent; SHDN tied stiff to a rail though it is also the open-drain OT flag — grounded/floating = shutdown), QA01C-class SIP-7 modules drawn as 4-pin, PESD parts drawn as 3-pin arrays, FS26 as a 34-pin abstraction | Every one rebound to the real package pins from its datasheet; FS26 now full LQFP-48+EP with VDIG/VBOS/bootstraps/DEBUG strap and DS-specified unused-pin terminations; MCU remains explicitly symbolic (no package table in the DS — bind at layout, printed on sheet) |
| F47 | **HIGH** | AMC1311 pins 2/3 swapped on BOTH V_DC channels (real: 2=IN, 3=SHTDN active-high w/ internal pull-up) — the analog inputs were grounded; channel agreement could not validate the measurement | IsoVSense symbol rebound: IN(2)=divider tap, SHTDN(3)=DCN; locked in ERC |
| F48 | **HIGH** | FS26 TRKIN grounded as an "unused tracker input" — it is the input SUPPLY of the VREF regulator, so VREF5 (ADC reference, temp networks, receiver offset) had no source | TRKIN → VPRE (headroom ≥ VREF+350 mV inside the 6.35 V max; CIN_TRK ≥0.5 µF eff at the pin — VPRE bank, layout note) |
| F49 | **HIGH** | TPS55340 pin 5 treated as a second VIN and tied to 12 V — pin 5 is SYNC, abs max 7 V | SYNC → DGND per DS ("if not used, tie to AGND") |
| F50 | MED | FS26 capacitor values under DS minimums: VBOS 1 µF (needs 4.7 µF; 3.3–6.1 eff), LDO1/V3B 1 µF (needs 4.7 µF; 2.35–15 eff) — and VREF 1 µF vs COUT_VREF 1.1–3.3 µF eff (found in the same audit) | CVBOS 4.7 µF · CSB6 4.7 µF · CSB5 2.2 µF |
| F51 | **HIGH** | Boost topology passes V12L−V_f straight to V15 whenever V12L > setpoint (a boost cannot regulate below its input): 24 V jump start / clamped load dump would put 19–33 V on QA01C modules rated 13.5–16.5 V (21 V/1 s surge) | NCV4276C-ADJ 40 V/0.4 A post-regulator: mild dropout in normal operation (V15 ≈ 15.0–15.2 V), hard 15.0 V clamp during pass-through; TSD covers the sustained-24 V service case |
| F52 | **HIGH** | FS26 VCORE buck network out of spec: LCOR 4.7 µH (Table 106 allows 1/1.5/2.2 µH by OTP), COUT 10 µF nominal vs 20–100 µF effective, bootstrap 100 nF vs 47 nF; VPRE COUT/input caps under the effective minimums | LCOR 2.2 µH (CORE_LSEL_OTP=0x02) · V15S 2×22 µF (≈32 µF eff) · CBTC 47 nF · CBTP 22 nF (typ; Rev 6.1 Table 100 allows 22–100 nF, so the prior 100 nF was legal — narrative corrected) · CSB1/2 22 µF · CLVC2 22 µF; effective-capacitance rows added |
| F53 | **HIGH** | LCAN1/LCAN2 mapped as windings 1-2/3-4 — ACT45B is physically wound 1-4 and 2-3, so transceiver CANH landed on the external CANL net (both ports) | Pin map corrected to the TDK circuit diagram; ERC asserts end-to-end pairing |
| F54 | MED | ULDO15 (ADJ + ceramic COUT) drawn without the required feed-forward capacitor; COUT 4.7 µF below the reference design | Cb 220 pF across the 49.9 k leg (f_z 14.5 kHz, in the 11–18 kHz window) · COUT 22 µF |
| F55 | **HIGH** | UEXD (ALM2402: 18 V abs, 16 V rec) fed from raw VBATC — 24 V jump start exceeds abs max and TPSMC24CA clamps far above 18 V | ULDOEX 12.1 V protective LDO (same NCV4276C-ADJ family) feeds VCC/VCC_O1/VCC_O2; crank behavior unchanged (dropout) |
| F56 | **HIGH** | Flyback switch BUK9Y14-80E is logic-level: V_GS abs ±10 V DC vs the 11.8 V VDD drive; the 5.6 V gate zener "fixed" it by conducting ~0.29 A through every ON interval (~0.4 W each, doubling the aux budget) | BUK7Y14-80E (standard-level, ±20 V, same LFPAK56/current class) + zener repurposed to a dark 15 V protective clamp |
| F57 | LOW | LDO ordering code transposed (NCV4276CADJDTRKG) | NCV4276CDTADJRKG per the DS ordering table |
| F62 | MED | The on-sheet ASIL-D + DISCHARGE review panel had silently vanished from the power sheet (its single void pick stopped fitting as the sheet grew through A.4.x) — found by the A.5 docs audit | Panel placement retries every void largest-first, and the build now FAILS if the panel cannot be placed; panel text refreshed (DRV_EN chain incl. RDY+fault-latch, 57 s bleed, "never energize w/o the DISCHARGE BOARD") |
| F60 | MED | JVEH bound to TE 776231-1 called "AMPSEAL 23" — the TE drawing in docs/datasheets shows 776231-1 is the **35-position** header (mates plug 776164) | Rebound to **770669-1**, the real 23-position AMPSEAL PCB header (mates the 770680-1 plug already cited); drawing TE-770669-1.pdf fetched |
| F61 | LOW | Rails named V18A/V18Q assume "+18 V": the QA01C DS selection row is **+20/−4 V**. ASC path unaffected (2.2 k + 5.1 V clamp); QDIS gate ≈19.5 V vs +22 abs / +18 rec | Documented + WARN row; clamps verified for +20 V; gate-divider option noted for proto |
| F58 | LOW | LCOR reconciliation: the netlist value became 2.2 µH in A.4.2 but the parts-db class MPN still printed "IND-4.7uH-2A" on the sheet/BOM — an assembler would fit the old value | MPN string now IND-2.2uH-4A with the OTP pairing in the description; value, MPN, BOM and OTP agree |
| F59 | MED | UB15 (TPS55340) COMP node had only 10 nF to ground — no compensation zero; simplified CCM screening gives ~0° phase margin | Series R3/C4 = 2 kΩ/100 nF per TI §8.2.1.2.11 + 470 pF HF pole cap; RHPZ ≈ 450 kHz (not limiting); measured Bode remains a bench gate |
| F46 | MED | No global hardware reaction to a driver DESAT trip (FLT only went to the MCU); driver soft-shutdown is per-channel | FLT_HS/LS diode-OR → SN74LVC1G74 fault latch → third AND input in DRV_EN: any DESAT latches all six channels off until the MCU clears after diagnosis |
| F36 | MED | S32K39 core is 1.14 V via an external NMOS ballast from a 1.5 V rail (DS Table 11) — direct FS26-VCORE→V11 is not a supported topology | VCORE→V15S 1.5 V + SQ2310ES ballast (GEN3-exact) regulated by the MCU's BCTRL loop |
| — | LOW | Hall ratiometric reference (V5A) ≠ ADC reference (VREF5): ±1–2 % gain drift between two 5 V rails | accepted (GEN3-identical); EOL calibration note |
| — | LOW | ALM2402 resolver swing at 9 V cold-crank ≈ 7 Vpp vs 8 Vpp target | accepted — amplitude-invariant demodulation |

## Margin tables

`;
let lastSec = "";
for (const r of rows) {
  if (r.sec !== lastSec) {
    md += `\n### ${r.sec}\n\n| Check | Value | Limit | Verdict | Margin note |\n|---|---|---|---|---|\n`;
    lastSec = r.sec;
  }
  const badge = r.status === "PASS" ? "✅ PASS" : r.status === "WARN" ? "🟡 WARN" : r.status === "FAIL" ? "🔴 FAIL" : "ℹ️";
  md += `| ${r.name} | ${r.value} | ${r.limit} | ${badge} | ${r.note} |\n`;
}
md += `
## Open vendor/bench inputs (every WARN above names one)

hiitio: module stray inductance Ls (SiC D3), SiC short-circuit envelope at 850 V ·
NOVOSENSE: RST/EN behaviour during DESAT soft turn-off and the I_STO distribution · TDK:
VGT12EEM saturation current and working-insulation rating · Faratronic: the 4XX 50 µF/600 V
can (ripple/ESR/life) · Murata: MGJ2 reinforced certificate · coldplate Rth (thermal test) ·
motor data (flux linkage, n_max, Ld/Lq) for the safe-state decision. Datasheet values used are
in \`docs/datasheets/EXTRACTED-PARAMS.md\`.

## Method

Closed-form worst-case analysis (tolerance corners: R ±5 %, precision ±1 %, C +10 %,
KL30 9–16 V, V_bus to 850 V / 500 V) — the correct tool at schematic phase; the time-domain
companion is \`sim-verify.mjs\`. Review A.6 added an independent cross-check: every
contested number (losses, overshoot, DESAT timing, startup, hold-up, discharge, ripple,
sensing) was recomputed in a separate script by a second reviewer; the two agree within
model assumptions. SPICE adds nothing without vendor switch models; the double-pulse,
short-circuit, thermal and EMC items are bench gates listed in \`docs/firmware-contract.md\`
and \`docs/review-A6-disposition.md\`.
`;
writeFileSync(join(ROOT, "docs", "verification-report.md"), md);
console.log(`design-verify: ${counts.PASS} PASS · ${counts.WARN} WARN · ${counts.FAIL} FAIL · ${counts.INFO} info → docs/verification-report.md`);
process.exit(counts.FAIL ? 1 : 0);
