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
  boost: { vref: 1.229, ilim: 5.25, vinAbs: 40, vinRec: 38, src: "TPS55340-Q1 DS SLVSBV5C (the fitted Q1 part: 38 V recommended / 40 V absolute; the commercial part is 32/34 V — round 13, A11-R03)" },
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
// The 100 R FB-sense drop is the divider current over the diode's conduction fraction d (round 8;
// cross-check R8X-16): d = 1 on the low corner (least drop), 0.5 nominal, 0.2 on the high corner
// (peak detection on the flattest plateau — the fast-US1M corner; the leakage spike pulls the other way).
const vcc2Of = (vfb, r1, r2, vfFs, vfSec, vz, d = 0.5) => (vfb * (r1 + r2) / r2 + vfFs + (vfb / r2) / d * 100) * (P.xfmr.ns / P.xfmr.nf) - vfSec - vz;
const VCC2 = {
  nom: vcc2Of(2.5, 52.3e3, 15e3, 0.55, 0.85, 5.1),
  lo: vcc2Of(2.45, 52.3e3 * 0.99, 15e3 * 1.01, 0.45, 1.3, 5.4, 1) - 2e-6 * 11.7e3 * (67.3 / 15) * (P.xfmr.ns / P.xfmr.nf),
  hi: vcc2Of(2.55, 52.3e3 * 1.01, 15e3 * 0.99, 0.65, 0.7, 4.8, 0.2),
  old: vcc2Of(2.5, 56e3, 15e3, 0.8, 0.85, 5.1),   // A.6: 56 k on VDD, i.e. behind the US1M aux diode
};
// 74LVC3G17-Q100 (Nexperia DS Table 8, −40…125 °C, V_CC 4.5–5.5 V) as fractions of V_CC
const SCH = { tpLo: Math.min(1.90 / 4.5, 2.20 / 5.5), tpHi: Math.max(3.30 / 4.5, 3.80 / 5.5),
  tnLo: Math.min(1.00 / 4.5, 1.20 / 5.5), tnHi: Math.max(2.20 / 4.5, 2.50 / 5.5) };
// ASC entry (round 7, RR05). Power board: VOW3120 → RASCG 2.2 k / RASCPD 10 k (1.8 k Thevenin) → CASCD 12 nF
// → NSI6611 ASC (V_ASCH 2.7–3.2 V, tASC_r 0.39–1.1 µs). The bias is a UCC14141-Q1 since A.12 (barrier closure;
// the QA01C-18 it replaces had no published working voltage — its 16.9–20.9 V envelope was the round-9 model):
// B18 below is the regulated envelope. The high sides start turning off first: FS0B drops DRV_EN
// (FS1B path) or the eFlexPWM fault forces the high-side PWM off (MCU path) — EN is NOT used for ASC ordering,
// because the NSI6611 honours DESAT over ASC only with EN high (DS §8.12, round-7 cross-check).
// TI UCC14141-Q1 (SLUSF10B) single-output configuration: VDD−VEE = V_FBVDD_REF × (1 + R_TOP/R_BOT) with 62 k/10 k
// 1 %; V_FBVDD_REF 2.4675–2.5325 V; the hysteretic loop holds the FB pin within 9–12.3 mV of it (the output
// ripple band). Load and line do not enter (hysteretic regulation at the pin); our loads are a few mA of the 1 W.
const B18 = (() => { const k = (rt, rb) => 1 + rt / rb, hy = 12.3e-3 / 2.5;
  return { nom: 2.5 * k(62e3, 10e3), min: 2.4675 * k(62e3 * 0.99, 10e3 * 1.01) * (1 - hy), max: 2.5325 * k(62e3 * 1.01, 10e3 * 0.99) * (1 + hy) }; })();
const ASC = (() => {
  const rTh = 2.2e3 * 10e3 / 12.2e3, c = 12e-9, vTh = (v) => (v - 0.3) * 10 / 12.2;
  const tRc = (r, cc, vth, vt) => r * cc * Math.log(vth / (vth - vt));
  return {
    tLsMin: tRc(rTh * 0.99, c * 0.95, vTh(B18.max), 2.7) + 0.39e-6,
    tEntryMax: 5.5e-9 + 0.5e-6 + tRc(rTh * 1.01, c * 1.05, vTh(B18.min), 3.2) + 1.1e-6,   // UASCG (74LVC1G08-Q100 5.5 ns, 125 °C) + VOW3120 tpLH 0.5 µs max (10–16 mA) + RC + tASC_r
    tHsEn: 5.4e-9 + 2 * 4.4e-9 + 5e-9 + 60e-9 + 130e-9,   // FS1B path: USCH, two ANDs, harness, EN deglitch, EN→OUT (≈tpHL max)
  };
})();
// FW-06 over-voltage budget (round 7, RR06/A6-R08): link crossing the trip → ASC request
const OVP = { tDiv: (2.82e6 * 6.2e3 / (2.82e6 + 6.2e3)) * 1e-9, tAmc: 2.1e-6, tRx: 0.3e-6, tSample: 1 / 200e3, tConv: 1.0e-6, tAct: 1.0e-6 };
OVP.tReq = OVP.tDiv + OVP.tAmc + OVP.tRx + OVP.tSample + OVP.tConv + OVP.tAct;
// Round-8 interface data (EXTRACTED-PARAMS §26). LVC outputs (74LVC1G08/3G17-Q100 Table 7): V_OH ≥ 3.4 V
// at V_CC 4.5 V, I_O −32 mA, −40…125 °C (≥ 3.8 V over −40…85 °C); below that current the PMOS drop is
// bounded by the same resistance (triode), so V_OH(I) ≥ V_CC − I·R_out. VOW3120 (A.12; doc 82442) V_F: 1.0–1.6 V
// at 10 mA (guaranteed) and a −1.4 mV/°C tempco that Vishay gives as TYPICAL only — the same ±28 % band as the
// round-9 TLP152 treatment (−1.0…−1.8 mV/°C), each end where it hurts — cold V_F max 1.72 V, hot V_F max 1.53 V,
// hot V_F min 0.87 V. Guaranteed: I_FLH 8 mA max over −40…100 °C and 25 mA abs max; the 10–16 mA I_F(ON)
// window is recommended, not guaranteed. The LVC R_out is a MOS-triode bound (V_drop/I falls with I below
const LVC = { vOh32: 3.4, vOh32c: 3.8 }; LVC.rOut = (4.5 - LVC.vOh32) / 32e-3; LVC.rOutC = (4.5 - LVC.vOh32c) / 32e-3;
const LED = { vfMax: 1.60 + 65 * 1.8e-3, vfMaxHot: 1.60 - 75 * 1.0e-3, vfMin: 1.00 - 75 * 1.8e-3, iFlh: 8e-3, iRecMin: 10e-3, iRecMax: 16e-3, iAbs: 25e-3, tpLH: 0.5e-6, tpHL: 0.5e-6, tRel: 0.5e-6 + 0.08e-6 + 0.48e-6 };
const FLTL = { vol: 0.3, bat46: 0.45 };

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
      "not survivable by 10 W parts — must not flame: TT/Welwyn SQP 'will not burn or emit incandescent particles under any condition of applied temperature or overload' (SQP.pdf)", "PASS",
      "F23 / round 17: bounded, not survived, and now non-flaming by the manufacturer's statement for the bound RDIS part (SQP10-470RJB15); the Yageo SQP alternate states only a flameproof case, so it is an alternate only with the same statement or the overload test in docs/qualification-plan.md (VR-28 asks TT and Yageo for the qualification data). Firmware fires QDIS only with contactors reported OPEN + auto-timeout; a pre-existing FET short is caught at the next precharge (link plateaus ≈5 % low, abnormal τ) and at the next contactor opening (F157), latched as service-required");
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
      "Qg·ΔV·f + ICC2 + 5.1 k bleeder per domain; F27: IGBT uses the full ±15 V Qg (no scaling); IGBT SKUs fit RT 8.2 k (~308 kHz). The 20 kHz SiC option runs at 86 % of the worst-part flyback capacity — a margin statement, measured on the six-domain gate-supply bench of docs/qualification-plan.md (gate-power capacity at 20 kHz, KL30 9–16 V)");
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
  add("Gate drive", "Slow edges at LVC inputs (Δt/ΔV 5–10 ns/V)", "RC nodes and FS0B via USCH, FLT diode-OR and FS1B strap via USCH2, RDY_HS/LS via USCH3 (no limit) — none left", "every open-drain or RC edge on the safety chain", "PASS",
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
  const v12at9 = OP.kl30.min - 0.95;                     // two reverse Schottkys + the FLVC fuse + 2 chain polyfuses at ~1 A (round 17: FLVC is a fuse, ≤ 0.06 V at 2.5 A)
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
  // round 17 (F189): the V12 rails are no longer clamped by a 24 V TVS — they ride the 35 V test-B plateau (≈ 34.2 V after
  // DREVC, the polyfuses and DRx) and, for tens of µs, the pulse-2a peak CLVC3 holds on NRC (≤ 37.5 V, LV A.16)
  const vDrainLD = Math.max(35, 37.5) - 0.8 + 21.5 + 0.7;     // rail + SMAJ13A clamp at its V_C + blocking Vf
  judge("Flyback A.4", "Drain worst case (35 V test-B plateau / pulse-2a peak on the rail)", `${f(vDrainLD, 1)} V`, "72 V (BUK7Y14-80E: 80 V, V(BR)DSS 72 V at −55 °C)",
    vDrainLD / 72, 0.85, "F38 — replaces SMBJ85A (94.4 V min breakdown, forward path in OFF); round 17: the rail is let through, no longer clamped at 39 V");
  // F39 DESAT clamp direction is topological (erc-audit); F40 ASC latch levels:
  // round 7 (A6-R01): FS1B holds V_OL ≤ 0.4 V only up to 2 mA and may current-limit at 4 mA. Budget at
  // V5A 5.1 V, 1 % parts. Round 8 (A7-N04): FS1B has no own pull-up — the RFS1/RFS2 strap is its pull-up.
  const vOL = 0.4, v5 = 5.1;
  const iRenp = (v5 - vOL) / (5.1e3 * 0.99), iStrap = (v5 - vOL) / (11e3 * 0.99), iFout = (v5 - vOL - 0.3) / (9.9e3 + 0.99e3), iFs1b = iStrap + iFout;   // FAULT_OUT sinks through DFO (BAT46 ≈0.3 V)
  judge("Safety A.8", "FS1B load at its V_OL point (strap + FAULT_OUT)", `${f(iFs1b * 1e3, 2)} mA`, "2 mA (V_OL ≤ 0.4 V; current limit ≥ 4 mA)", iFs1b / 2e-3, 0.95,
    `A6-R01: the 1 k pulls took ${f((5 / 1e3 + 5 / 11e3) * 1e3, 2)} mA — a 4 mA-limit part sat at 1.33 V, ASC_SET_N at 1.67 V (> VIL); the old row compared with the 22 mA maximum and divided by 1000 again`);
  const vSetLow = vOL + (v5 - vOL) * 1.01e3 / (1.01e3 + 9.9e3), vTnMin = SCH.tnLo * 4.9;
  judge("Safety A.8", "ASC latch preset low level (FS1B at V_OL, 1k into 10k) vs USCH2 V_T−", `${f(vSetLow, 2)} V`, `${f(vTnMin, 2)} V V_T− minimum at V5A 4.9 V`,
    vSetLow / vTnMin, 0.85, "F40/A6-R01; round 8 buffers the preset (R7-02), so the Schmitt threshold, not the LVC VIL, is the limit");
  const vFltCmb = FLTL.vol + FLTL.bat46;
  judge("Safety A.8", "FLT diode-OR low level (V_OL + BAT46, cold) vs USCH2 V_T−", `${f(vFltCmb, 2)} V`, `${f(vTnMin, 2)} V V_T− minimum`,
    vFltCmb / vTnMin, 0.85, "round 8: DFLT1/2 Schottky — a 1N4148 (≈0.7 V cold) would put the node at the threshold");
  judge("Safety A.8", "FS0B load at its V_OL point (5.1 k into the USCH input)", `${f(iRenp * 1e3, 2)} mA`, "2 mA (V_OL ≤ 0.4 V)", iRenp / 2e-3, 0.6,
    "pin ≤ 0.4 V: under the SBC's own 0.7 V read-back threshold and the buffer's 1.0 V V_T− minimum");
  const vFoutLo = vOL + 0.3 + iFout * 1.01e3;
  judge("Safety A.8", "FAULT_OUT asserted level at the VCU (10 k to 5 V)", `${f(vFoutLo, 2)} V`, "1.5 V (5 V CMOS V_IL)", vFoutLo / 1.5, 0.85,
    "sink-only through DFO: the VCU must pull up (firmware-contract §9)");
  // KL30 short on FAULT_OUT with FS1B released: DFO + RFS4 + RFS1 (1.98 k low) into ZSET; RFS2 back to V5A.
  // ZSET BZT52-B5V6 (Nexperia): 5.71 V max at 5 mA, S_Z ≤ +2.5 mV/K (→ 125 °C), r_dif ≤ 40 Ω; the current out
  // through RFS2 is ignored (more zener current = higher clamp). 16 V KL30 max, 24 V jump start, 35 V suppressed
  // load dump — each with the wire already shorted (cross-check R8X-05: a C5V6 reached 6.62 V at 35 V).
  const ZS = { v5: 5.71 + 100 * 2.5e-3, rz: 40, r: 1.98e3 };
  const vzAt = (vk) => (ZS.v5 - 5e-3 * ZS.rz + ZS.rz * (vk - 0.3) / ZS.r) / (1 + ZS.rz / ZS.r);
  const [vz16, vz24, vz35] = [16, 24, 35].map(vzAt), iZ35 = (35 - 0.3 - vz35) / ZS.r;
  const iBackLive = (vz35 - 4.9) / 10e3, iBackDead = vz35 / 10e3;
  judge("Safety A.8", "FAULT_OUT shorted to KL30 (FS1B released): ASC_SET_N clamp vs USCH2 V_I abs max",
    `${f(vz16, 2)} / ${f(vz24, 2)} / ${f(vz35, 2)} V at 16 / 24 / 35 V (125 °C)`, "6.5 V abs max (74LVC3G17)", vz35 / 6.5, 0.99,
    `to ground: blocked by DFO. Into V5A ≤ ${f(iBackLive * 1e3, 2)} mA live / ${f(iBackDead * 1e3, 2)} mA with V5A off (disabled LDO2 discharges it through 20–60 Ω: ≤ 40 mV). RFS1/RFS4 ${f(iZ35 * 1e3, 1)} mA for the ≤ 0.4 s pulse, ${f((24 - 0.3 - vz24) / ZS.r * 1e3, 1)} mA at a jump start (short-time overload of the 0603s — accepted for a shorted wire). A7-N04: the round-7 BAT46 into V5A back-fed a sleeping rail with ≈8 mA. Vishay alt BZT52B5V6 (+6·10⁻⁴/K): ≤ 6.43 V`);
  judge("Safety A.8", "ASC break-before-make: HS off before LS on", `LS starts ≥ ${f(ASC.tLsMin * 1e6, 2)} µs after the latch sets`,
    `HS off by ${f((ASC.tHsEn + 2.5e-6) * 1e6, 2)} µs (${f(ASC.tHsEn * 1e6, 2)} µs to EN + 2.5 µs IGBT dead time)`, (ASC.tHsEn + 2.5e-6) / ASC.tLsMin, 0.9,
    "RR05, FS1B path shown (FS0B → USCH → ANDs → EN); the MCU path is faster (eFlexPWM fault on the high-side outputs → IN+ low, tpHL ≤ 0.13 µs), and its low sides come on by PWM after the dead time. EN stays high on the MCU path so LS DESAT keeps priority (DS §8.12). SiC dead time is 1.0 µs — more margin");
  add("Safety A.8", "ASC entry, latch set → LS gates on (worst)", `${f(ASC.tEntryMax * 1e6, 2)} µs`, "counted in the FW-06 budget (§2b)", "INFO",
    `release ≤ ${f(LED.tRel * 1e6, 2)} µs (VOW3120 tpHL 0.5 µs max + DASCR discharge + tASC_f 0.48 µs); exit is MCU-sequenced (FW-06a)`);
  // the mask must release the healthy low sides' ASC pins before the fault latch drops DRV_EN, or they sit in
  // EN-low ASC, where the LS DESAT is not documented (cross-check R8X-08: this row used to be a fixed PASS)
  const tMask = 5.4e-9 + 5.5e-9 + LED.tRel, tDropMin = 9.9e3 * 3.3e-9 * 0.95 * Math.log(1 / SCH.tnHi);
  judge("Safety A.8", "Latched driver FLT masks ASC on every path (UASCG) before DRV_EN drops",
    `ASC_CMD low ≤ ${f((tMask - LED.tRel) * 1e9, 0)} ns, LS ASC pins released ≤ ${f(tMask * 1e6, 2)} µs`, `DRV_EN drop ≥ ${f(tDropMin * 1e6, 0)} µs after FLT`, tMask / tDropMin, 0.5,
    "round 8 R7-01/A7-N01: the faulted NSI6611 holds its own gate off through IN-low and EN-low with ASC high (DS Fig. 8.11); the gate removes ASC from the HEALTHY low sides and the eFlexPWM fault forces IN low, so the bridge reaches SPO (FS1B-ASC included). The MCU re-enters ASC only through §4c after the FW-15 reset. Wiring locked in erc-audit");
  // both VOW3120 LEDs, from buffered 5 V logic: I = (V_CC − I_pd·R_out − V_F)/(R + R_out), R 261 R ±1 %, the
  // far-end 10 k pulldown (RPD8/RPD9) loading the same output; temperature-consistent corners (R8X-04)
  const rLed = 270;   // RASCL = RQDL (ERC-locked; 261 → 270 in A.12 for the lower VOW3120 V_F)
  const iLedAt = (rOut, vf) => (4.9 - 0.5e-3 * rOut - vf) / (rLed * 1.01 + rOut);
  const iLedCold = iLedAt(LVC.rOutC, LED.vfMax), iLedHot = iLedAt(LVC.rOut, LED.vfMaxHot);
  const iLed = Math.min(iLedCold, iLedHot), iLedMax = (5.1 - LED.vfMin) / (rLed * 0.99);
  const ledSt = iLed < 1.25 * LED.iFlh || iLedMax > LED.iAbs ? "FAIL" : iLed >= LED.iRecMin && iLedMax <= LED.iRecMax ? "PASS" : "WARN";
  for (const [tag, drv] of [["ASC opto (RASCL, from UASCG)", "74LVC1G08-Q100"], ["discharge opto (RQDL, from USCH2 ch3)", "74LVC3G17-Q100"]])
    add("Safety A.8", `VOW3120 LED current — ${tag}`, `${f(iLedCold * 1e3, 2)} mA cold · ${f(iLedHot * 1e3, 2)} mA hot · ${f(iLedMax * 1e3, 2)} mA max`,
      "guaranteed: ≥ 1.25 × I_FLH 8 mA, ≤ 25 mA abs · recommended 10–16 mA", ledSt,
      `${drv}: guaranteed margins ${f(iLed / LED.iFlh, 2)}× over I_FLH and ${f(LED.iAbs / iLedMax, 2)}× under the abs max; the 10–16 mA window closes only with the ±28 % V_F tempco band (typical-derived — T7-04 bench). Cold = −40 °C V_F ${f(LED.vfMax, 2)} V with R_out ≤ ${f(LVC.rOutC, 1)} Ω (V_OH ≥ 3.8 V at −32 mA, −40…85 °C); hot = 100 °C V_F ${f(LED.vfMaxHot, 2)} V with R_out ≤ ${f(LVC.rOut, 1)} Ω (125 °C); max at V5A 5.1 V, V_F ${f(LED.vfMin, 3)} V, R_out 0. R7-03/A7-N03: 470 R from the MCU pin gave 6.4–6.8 mA; cross-check R8X-04: 270 R mixed temperatures and dipped to 9.9 mA cold on the TLP152; A.12: the VOW3120 V_F (1.0–1.6 V) moves the window, 261 R would reach 16.4 mA`);
  // ---- round 10 (S9-01): RASCG carries the opto-to-clamp current for as long as ASC is held (minutes at speed):
  // (bias top − ZASC low end)² / R at −1 %, against the ESR03 rating derated to 85 °C
  {
    const pAscg = (B18.max - 4.8) ** 2 / (2.2e3 * 0.99), pRat = 0.33 * (155 - 85) / (155 - 70);
    judge("Safety A.9", "RASCG continuous dissipation while ASC is held (UCC14141-Q1 top into the ZASC clamp)", `${f(pAscg * 1e3, 0)} mW`,
      `${f(pRat * 1e3, 0)} mW at 85 °C (ESR03EZPF2201, 0.33 W at 70 °C)`, pAscg / pRat, 0.8,
      `S9-01: a generic 0603 rated 0.1 W at 70 °C allows only 82 mW at 85 °C. 2.2 k is kept: it sets the ASC break-before-make RC with CASCD`);
  }
  // ---- round 9 (A8-03): RFS4 with FAULT_OUT shorted to KL30 WHILE FS1B is asserted (it sinks at 0.4 V up to its
  // 4–22 mA limit; the 22 mA end is the stress case). Above the limit the node rises, and past the ZSET clamp RFS1
  // shares the current. The 16 V case is the continuous one: FS1B held with no release (MCU never boots, or DFS).
  // BACKUP_SAFETY_PATH_FS1B = 0 (FW-12) removes the RSTB repetition, so a pulse is ≤ 0.1 s (TDUR) or the ≤ 0.3 s
  // boot hold. Part: ROHM ESR03EZPF1001, 0.33 W at 70 °C → 0.27 W at 85 °C, overload 2 × U_R for 5 s ≈ 1.3 W.
  {
    const iLim = 22e-3, vzC = vzAt(16) - 0.3;   // clamp near 3 mA, a little under its 5 mA value
    const pAt = (vw) => { const i0 = (vw - 0.3 - 0.4) / 1e3; if (i0 <= iLim) return i0 ** 2 * 1e3;
      const vn1 = vw - 0.3 - iLim * 1e3, vn = vn1 > vzC ? (vn1 + vzC) / 2 : vn1; return ((vw - 0.3 - vn) / 1e3) ** 2 * 1e3; };
    const [p16, p24, p35] = [16, 24, 35].map(pAt), pCont = 0.33 * (155 - 85) / (155 - 70);
    judge("Safety A.9", "RFS4 with FAULT_OUT shorted to KL30 and FS1B asserted (22 mA limit end)", `${f(p16, 3)} / ${f(p24, 3)} / ${f(p35, 3)} W at 16 / 24 / 35 V`,
      `${f(pCont, 2)} W continuous at 85 °C (ESR03, 0.33 W at 70 °C); ≈1.3 W for 5 s overload`, p16 / pCont, 0.9,
      `A8-03: 0.23 W was only the 16 V case. Pulses ≤ ${f(p35, 2)} W last ≤ 0.1 s (FS1B_TDUR) or the ≤ 0.3 s boot hold — ≤ ${f(p35 * 0.3, 2)} J against the 5 s overload rating; the 16 V row is the continuous case (no release). BACKUP_SAFETY_PATH_FS1B = 0 stops RSTB loops. A 0.1 W 0603 is 0.08 W at 85 °C and fails the continuous case`);
  }
  // R9X-14: FS1B HELD for a whole key-on — a high-limit FS1B current-limits at 24 V (node ≈1.7 V reads high:
  // ABIST refuses the release). 18 V/60 min (ISO 16750-2, T_max − 20 °C ≈ 65 °C) and a 24 V/60 s jump start at 25 °C.
  {
    const p18 = ((18 - 0.7) / 1e3) ** 2 * 1e3, p24 = (22e-3) ** 2 * 1e3;
    const pR = 0.5, rth = (155 - 70) / pR, tEl24 = 25 + p24 * rth;   // ESR18 (1206) 0.5 W at 70 °C (ROHM ESR series Rev.012); element temperature from the rating's own slope
    add("Safety A.9", "RFS4 with FS1B held a whole key-on (18 V/60 min at 65 °C · 24 V/60 s at 25 °C) — 1206 anti-surge since round 17", `${f(p18, 2)} W · ${f(p24, 2)} W (element ≈${f(tEl24, 0)} °C)`,
      "0.5 W continuous at ≤ 70 °C (ESR18EZPF, AEC-Q200); 155 °C element", p18 > pR || tEl24 > 155 ? "FAIL" : p24 > pR ? "WARN" : "PASS",
      `R9X-14 / round 17: 18 V fits; the 24 V jump start runs the 1206 at ${f(p24 / pR, 2)}× its nameplate for 60 s — inside the rating (the 0603 ESR03 ran at 1.47×), in a triple condition (FAULT_OUT shorted to KL30, a high-limit FS1B part, a jump start). Drift or an open only disconnects FAULT_OUT from an already-shorted wire, and the FS1B preset keeps working through the strap. Accepted; bench item`);
  }
  // ---- round 9 (A8-N03 + cross-check R9X-07): FW-16 energy eligibility. The low-voltage V_DC reading is not
  // trusted: the chain is ≈ ±9 V uncalibrated at this level (MCU ADC alone ±3.3 V of bus). Either both channels
  // read < 3 V (≤ 12 V true), or QDIS runs for 2 τ from a < 60 V reading (≤ 69 V true → ≤ 9.3 V).
  for (const id of ["sic8", "sic4"]) {
    const sk = SKU[id], cMax = 16 * sk.can.c * 1.1 + 3.3e-6, ra = sk.vMax === 850 ? 1880 : 880, tau = ra * 1.05 * cMax;
    const eRead = 0.5 * cMax * 12 ** 2, eTop = 0.5 * cMax * (69 * Math.exp(-2)) ** 2, e = Math.max(eRead, eTop);
    judge("Safety A.9", `FW-16 self-test residual energy, ${sk.vMax === 850 ? "8XX" : "4XX"} bank (read < 3 V, or QDIS 2 τ from < 60 V)`,
      `≤ ${f(eRead * 1e3, 0)} mJ (read) · ≤ ${f(eTop * 1e3, 0)} mJ (QDIS 2 τ = ${f(2 * tau, 2)} s)`, "0.1 J design limit", e / 0.1, 0.8,
      `A8-N03: "< 60 V" allowed ${f(0.5 * cMax * 59 ** 2, 2)} J at C_max ${f(cMax * 1e6, 1)} µF. R9X-07: the first "< 12 V read" rule assumed a 1 V error — it can be 9 V. n_ss from E_LL,pk(n_ss) ≤ 12 V. Fixture-qualified, not a destructive test`);
  }
  // ---- round 9, self-found N17 + cross-check R9X-03/04/10: power-board LV feed switch QLVS (DMP6023LEQ: −60 V,
  // 28 mΩ at −10 V, 35 mΩ at −4.5 V, V_GS(th) −1…−3 V, ±20 V, I_DSS ≤ 1 µA; DS39935). Gate from NRC (KL30 −
  // ≈0.5 V: reverse Schottky + polyfuse): 4.7 k to the V5A-driven 2N7002, 10 k gate-source, BZT52-C15 clamp,
  // and 1 k + 100 nF drain-gate for a controlled turn-on slew. Sleep: V5A off → gate at source → off.
  {
    const nrc = (vk) => vk - 0.5, k = 10 / 14.7, rTh = 10e3 * 4.7e3 / 14.7e3;
    const vgs = (vk) => Math.min(nrc(vk) * k, 15.6), [g6, g9, g16, g42] = [6, 9, 16, 42].map(vgs);
    judge("LV A.9", "LV feed switch V_GS from NRC: KL30 6 / 9 / 16 / 42 V (pin-side clamp, round 17)", `−${f(g6, 1)} / −${f(g9, 1)} / −${f(g16, 1)} / −${f(g42, 1)} V`,
      "±20 V V_GS max; ≥ 4.5 V for the 35 mΩ point", Math.max(g42 / 20, 4.5 / g9), 0.8,
      `at the 9 V crank floor the gate sits ${f(g9, 1)} V below the source (≤ 35 mΩ; 0.6 A → ≤ 13 mW); even at 6 V (${f(g6, 1)} V) it stays past V_GS(th) max 3 V. R9X-10: taken from NRC, not KL30`);
    const iLeak = 50e-6;   // 2N7002 off-state drain leakage at 85 °C, V_DS ≈ 12 V (vendors give ≤ 1 µA at 25 °C, up to 500 µA at 125 °C)
    judge("LV A.9", "LV feed switch held off by a hot 2N7002 (leakage × 10 k gate-source)", `${f(iLeak * 10e3, 2)} V at ${iLeak * 1e6} µA`,
      "1.0 V V_GS(th) min", iLeak * 10e3 / 1.0, 0.6, "R9X-10: with the first 100 k the same leakage made 5 V — the switch could half-close in a hot parked car");
    const vPl = 3.0, cL = 45e-6, cM = 100e-9;   // Miller plateau (low end → largest gate current), power-board input capacitance
    const [i12, i16] = [12, 16].map((vk) => cL * ((nrc(vk) * k - vPl) / rTh) / cM);
    judge("LV A.9", "LV feed switch inrush into the power board at each wake (drain-gate 100 nF slew)", `${f(i12, 2)} A at 12 V · ${f(i16, 2)} A at 16 V`,
      "3 A (polyfuses, harness, VBATC dip)", i16 / 3, 0.6,
      `R9X-04: slew = gate current / 100 nF ≈ ${f(((nrc(12) * k - vPl) / rTh) / cM / 1e3, 0)} V/ms at 12 V; without it the gate-drain charge alone let 9–31 A through (2.5–4.4 mJ) and dipped VBATC ~2 V at every wake`);
    // whole-inverter sleep current with KL30 present (FS26 in LPOFF): FS26 30 µA typ (60 max at 85 °C), ULDOEX
    // inhibited ≤ 10 µA, QLVS I_DSS ≤ 1 µA, 2N7002 ≤ 1 µA, TVS ≤ 1 µA at 25 °C; the power board is unpowered.
    // Round 17: CLVC3 (100 µF hybrid polymer on NRC) — the ZC catalogue gives I ≤ 0.01·C·V at the RATED voltage (50 µA at
    // 50 V); applied at 16 V it is 16 µA (an estimate, no working-voltage figure is published), doubled hot. The pin-side
    // TVS pair conducts one leakage (the reverse-biased 33 V member), as the single TVS did.
    const iClvc3 = 0.01 * 100 * 16 * 1e-6;
    const iSleep25 = 30e-6 + 10e-6 + 1e-6 + 1e-6 + 1e-6 + iClvc3, iSleep85 = 60e-6 + 10e-6 + 1e-6 + iLeak + 5e-6 + 2 * iClvc3;
    const iQa = 2 * 16e-3 * 15 / 0.85 / 12;
    judge("LV A.9", "Parking drain, whole inverter (KL30 present, FS26 in LPOFF)", `≤ ${f(iSleep25 * 1e6, 0)} µA at 25 °C · ≤ ${f(iSleep85 * 1e6, 0)} µA at 85 °C`,
      "0.1 mA at 25 °C (OEM sleep budgets ≤ 0.1–1 mA per ECU)", iSleep25 / 0.1e-3, 0.8,
      `N17 + R9X-03: before, the power board's LV side drew ≥ ${f(iQa * 1e3, 0)} mA from the QA01C-18 no-load inputs alone (≈150 mA in all) and ULDOEX kept the exciter at ≈0.9 mA; both now follow V5A. S1 start-up already starts VDD at 0 V, so a switched feed costs no start time. Round 17 adds CLVC3's leakage, ${f(iClvc3 * 1e6, 0)} µA estimated (0.01·C·V at 16 V; IR-39 asks the OEM for the hot budget)`);
  }
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
  judge("LV A.4", "V15 behind ULDO15 @24 V jump start", "15.0 V", "18 V UCC14141-Q1 recommended max (16.5 V was the QA01C window)",
    15.0 / 16.5, 0.95, "F51 — boost pass-through clamped; LDO input 23.5 V << 40 V rating");
  judge("LV A.4", "ULDO15 input at the 35 V test-B plateau (let-through)", "≈33.8 V", "40 V NCV4276C operating max",
    33.8 / 40, 0.9, "F51; round 17 (F189): 35 V less DREVC, the fuse and polyfuses, DRL and DB15 (≈ 1.2 V) — no TVS clamps the 12 V node below 35 V any more");
  judge("LV A.7", "CB15O1/2 (V15B) at the 35 V test-B plateau", "≈33.8 V", "50 V MLCC rating",
    33.8 / 50, 0.8, "round 7 RR10 — V15B follows V12L−Vf in pass-through; the 25 V parts were overstressed");
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
  // CSB5 1 µF + CMA1 1 µF + CMA2 0.1 µF (round 12, R2-F11: 2.2 µF put the nominal rail AT the 3.3 µF upper limit)
  // CSB5 (0805) at −15 % bias, CMA1 (0603) at −30 %, CMA2 0.1; −10 % tolerance, −15 % at −40 °C, −5 % aging;
  // max: +10 % tolerance, +5 % after reflow, +15 % X7R over temperature, no bias credit
  const minVREF = (1.0 * 0.85 + 1.0 * 0.70 + 0.1) * 0.9 * 0.85 * 0.95, maxVREF = 2.1 * 1.1 * 1.05 * 1.15;
  add("LV A.4", "VREF5 rail effective (whole rail: 2.1 µF nom)", `${f(minVREF, 2)}–${f(maxVREF, 2)} µF`, "1.1–3.3 µF eff window (FS26 COUT_VREF)",
    minVREF >= 1.1 && maxVREF <= 3.3 ? "PASS" : "FAIL", "round 12 (R2-F11): 2.2 µF put the nominal rail AT the 3.3 µF limit and the A.4 row never looked at the +tolerance corner; CMA1 stays 1 µF as the reservoir at the MCU VREFH pins (AN5032)");
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
  // QDIS gate (round 9, A8-N02; A.12 parts): PSQD UCC14141-Q1 → VOW3120 (V_OH ≤ V_CC; guaranteed ≥ V_CC − 4 V at
  // −100 mA, the drop at the ≈1.5 mA static load is far smaller — the 4 V bound is used) → RQDG 1.5 k / RQDPD 10 k.
  const vgsMax = B18.max * 10.1e3 / (10.1e3 + 1.485e3), vgsMin = (B18.min - 4) * 9.9e3 / (9.9e3 + 1.515e3);   // 1 % divider corners
  judge("Discharge", "QDIS gate V_GS (UCC14141-Q1 envelope through the 1.5 k/10 k divider) vs HCM75S12T4K3 +22 V abs",
    `${f(vgsMin, 1)}–${f(vgsMax, 1)} V (rail ${f(B18.min, 1)}–${f(B18.max, 1)} V)`, "+22 V abs max (+18 V recommended)", vgsMax / 22, 0.85,
    `round 9 A8-N02: 47 Ω passed the QA01C-18 rail straight to the gate (up to 20.9 V at this load); the divider was kept when the regulated UCC14141-Q1 replaced it (A.12) — the top sits ${f(22 - vgsMax, 1)} V under the abs max and the low end (VOW3120 V_OH bound, UVLO 11–13.5 V rising first) still fully enhances a 0.45 A discharge. BENCH: V18Q, QDVO and V_GS at start-up, no load and ON`);

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
  judge("LV", "V15 boost setpoint (110k/9.53k)", `${f(vb15, 2)} V`, "13.5–16.5 V design band (the UCC14141-Q1 accepts 8–18 V; the bias LDOs 5–40 V)", Math.abs(vb15 - 15) / 1.5, 0.7, "kept at the tighter QA01C-era band");
  const iload15 = 2 * (1 / 0.75) / 15 + 2 * (1 / 0.75) / 15;      // 2×UCC14141-Q1 + 2×UCC12050 taken at a 1 W class each (conservative: the biases carry a few mA)
  const iin15 = vb15 * iload15 / (OP.kl30.min * 0.85);
  judge("LV", "Boost switch current @9 V", `${f(iin15, 2)} A avg`, `${P.boost.ilim} A limit`, iin15 / P.boost.ilim, 0.5);
  const i5 = 6 * 0.005 + 0.02;
  judge("LV", "NCV4276 5 V load (6 driver VCC1 + optos)", `${f(i5 * 1e3)} mA`, "400 mA", i5 / 0.4, 0.5);
  judge("LV", "FS26 VMONEXT divider (52.3k/10k @5 V)", "0.794 V", "0.8 V fixed reference ±window", Math.abs(0.794 - 0.8) / 0.8 / 0.12, 0.5, "F34 — old 10k/18.7k fed 3.26 V = permanent OV; OTP window set around 100 %");
  add("LV", "S32K39 core topology", "FS26 VCORE→V15S 1.5 V → QBAL ballast → V11 1.14 V", "-", "PASS", "F36 — per DS Table 11; direct VCORE→V11 is not a supported topology");
  const pLdo = (12 - 5) * i5;
  judge("LV", "NCV4276 dissipation @12 V", `${f(pLdo, 2)} W`, "~1.5 W DPAK on copper", pLdo / 1.5, 0.6);
  const iChain = pOutChainEstimate();
  judge("LV", "Chain polyfuse hold (FVBL/FL1, worst chain @9 V, 85 °C)", `${f(iChain, 2)} A`, "1.50 A hold at 85 °C (MF-LSMF300/24X; 3.0 A is the 23 °C figure)", iChain / 1.5, 0.85,
    "round 17 (F187): the row compared one chain with the 23 °C hold — FLVC, which carries the whole inverter, is judged in LV A.16");
  add("LV", "Load-dump path", "let-through (round 17): 33 V/18 V stand-off TVS pair at the pin, 33 V TVS on the power board, 100 µF hybrid bulk on NRC", "-", "PASS",
    "F25 — all 12 V-node MLCCs raised to 50 V rating; F189 — nothing clamps below the 35 V plateau and every downstream part is rated for it (TPS55340-Q1 VIN 38 V rec / 40 V abs — the earlier 45 V abs text was wrong)");
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
  add("Sensing", "Hall ratiometric ref vs ADC ref", "V5S(V5A) vs VREF5 — ±1–2 % gain drift after the EOL gain calibration (FW-20)", "±5 % torque-accuracy allocation (IR-26, docs/interface-requirements.md)", "PASS", "round 17: two 5 V sources; the static ratio is removed at EOL and the residual temperature drift is bounded inside the torque allocation; the safety checks (KCL stuck-channel, hardware OC) do not depend on it. GEN3 ships the same topology; moving VREFH_SAR_0123 to V5A would make the precision channels ratiometric too — not taken");
  // HVIL signatures
  add("Sensing", "HVIL signatures (drive hi/lo/open)", "3.0 / 2.0 / 2.5 V", "-", "PASS", "distinct at ±5 % R tolerance (worst separation 0.38 V)");
  const vmp = 4.0 * 12.1 / (12.1 + 4.99), vmn = 4.0 * 24 / (24 + 4.99);
  judge("Sensing", "Resolver monitor dividers @4 V pk", `${f(vmp, 2)} / ${f(vmn, 2)} V`, "5 V SDADC input", Math.max(vmp, vmn) / 5, 0.85);
  {
    const vexd9 = 9 - 0.45 - 0.25, vPk = 1.91, vNeed = 2.5 + vPk + 0.2;   // STPS5L60S ≈ 0.45 V, NCV4276C dropout ≈ 0.25 V at ~100 mA; ALM2402 swing limit 0.2 V at 200 mA (35 mA here)
    judge("Sensing", "Resolver drive @9 V KL30 (round 17 recheck of the A.4.3 row)", `outputs centred on VMID_REX 2.5 V (V5A/2): ${f(2.5 - vPk, 2)}–${f(2.5 + vPk, 2)} V at the 7.64 V pp setpoint; VEXD ≈ ${f(vexd9, 1)} V at 9 V KL30`,
      `VEXD ≥ ${f(vNeed, 1)} V (upper swing + 0.2 V rail margin)`, vNeed / vexd9, 0.85,
      "the A.4.3 row assumed a single-ended drive centred on VEXD/2 with an 8 V pp target; since round 12 the two ALM2402 outputs swing ±1.91 V pk around the 2.5 V AFE mid-rail, so the rail only has to clear 4.6 V — any KL30 ≥ 5.3 V keeps the full amplitude. Cold crank is not an amplitude gap; the slew ceiling (2.07 V pk at −40 °C) is the binding limit and is independent of VEXD");
  }
}

// ============ 7b. ROUND 12 — system review of 4544715 (docs/review-A11-disposition.md) ============
{
  // R1-F01/R2-F08: SPO (all switches off) with the battery path gone — the module diodes rectify the stored
  // winding energy 1.5·L·I² into the isolated link before the current is gone. Rule (a) of §6 now needs
  // the link headroom ½·C_min·(U_N² − V_max²) to cover it. Screening motor from S6 (0.35 mH); the real
  // L_d/L_q(i), ψ_f and n_max are a commissioning input (the back-EMF work during the decay adds to this).
  const Lscr = 0.35e-3;
  for (const s of [SKU.sic8, SKU.igbt4]) {
    const cMin = 16 * s.can.c * 0.9 + 3e-6, v0 = s.ovTrip, head = 0.5 * cMin * (s.can.vr85 ** 2 - v0 ** 2);
    const wMag = 1.5 * Lscr * s.iPk ** 2, iOk = Math.sqrt(head / (1.5 * Lscr));
    const vEnd = Math.sqrt(v0 ** 2 + 2 * wMag / cMin);   // the E = 0 case of the §6 bound V_pk ≤ E + √((V₀−E)² + 2·W/C)
    add("System A.11", `SPO freewheel energy at ${s.iPk} A rms into the isolated link (${s.name}, screening motor 0.35 mH, zero back-EMF)`,
      `${f(wMag, 0)} J → ${f(vEnd, 0)} V from the ${v0} V trip`, `${f(head, 1)} J headroom to ${s.can.vr85} V U_N at C_min ${f(cMin * 1e6, 0)} µF`,
      wMag > head ? "WARN" : "PASS",
      `round 12: rule (a) of §6 covers this motor only up to ${f(iOk, 0)} A rms at zero back-EMF; with back-EMF the decay is slower and the generated work adds 19–76 J (Opus check: 1098–1301 V at 340 A for E_LL,pk 300–800 V), and at E ≥ V₀ the current does not decay at all — a diode-bridge simulation with the real L_d(i)/L_q(i), ψ_f, R_s decides; otherwise rule (b). The reviewers' 300 µH/20 mΩ ODE from 850 V ends at 1038 V (8XX) / 667 V (4XX)`);
  }
  // R2-F04: resolver exciter — textbook MFB (R1 10k in, R2 28k feedback since A.15 (was 24k), R3 10k, C1 1.5 nF to ground, C2 220 pF feedback).
  // Round 18 (A16 rechecks, Opus cross-check xcheck18, F195): |H| includes the 100 nF coupling capacitor (a 0.2 % term, not a short);
  // the band is computed over the BOUND tolerances — R ±1 %, CEXA1/CEXA2 ±5 % C0G (parts-db), CEXA3 ±20 % — every corner; the ±10 %
  // build band is printed as the QP-RX-01 acceptance for such a build; the H-bridge REXB ±1 % corner and the IR-13 60 Ω interface
  // minimum enter the winding stack; the PTC resistance is the sheet's 0.35–5.0 Ω window, "1.3 Ω cold" was never a sheet value.
  {
    const R1 = 13e3, R2 = 28e3, R3 = 10e3, C1 = 1.5e-9, C2 = 220e-12, C3 = 100e-9, w = 2 * Math.PI * 10e3;   // R1 = REXA1 + REXA2, in series with the CEXA3 coupling capacitor
    const H = (r1, r2, r3, c1, c2, c3) => {   // ideal-op-amp MFB low-pass with a complex input branch Zin = R1 + 1/(jωC3): |H| = |R2/Zin| / |1 − ω²C1C2R2R3 + jωC2(R2 + R3 + R2R3/Zin)|
      const zr = r1, zi = -1 / (w * c3), zm = zr * zr + zi * zi, ir = zr / zm, ii = -zi / zm;   // 1/Zin
      const tr = r2 + r3 + r2 * r3 * ir, ti = r2 * r3 * ii, a = 1 - w * w * c1 * c2 * r2 * r3;
      return Math.hypot(r2 * ir, r2 * ii) / Math.hypot(a - w * c2 * ti, w * c2 * tr);
    };
    const band = (dr, dc, dc3) => {   // every corner (2^6) of R ±dr, C1/C2 ±dc, C3 ±dc3
      const v = [];
      for (const a of [-1, 1]) for (const b of [-1, 1]) for (const c of [-1, 1]) for (const d of [-1, 1]) for (const e of [-1, 1]) for (const g of [-1, 1])
        v.push(H(R1 * (1 + a * dr), R2 * (1 + b * dr), R3 * (1 + c * dr), C1 * (1 + d * dc), C2 * (1 + e * dc), C3 * (1 + g * dc3)));
      return [Math.min(...v), Math.max(...v)];
    };
    const h = H(R1, R2, R3, C1, C2, C3), hB = band(0.01, 0.05, 0.2), h10 = band(0.01, 0.10, 0.2);
    const f0 = 1 / (2 * Math.PI * Math.sqrt(C1 * C2 * R2 * R3)), Q = Math.sqrt(C1 * C2 * R2 * R3) / (C2 * (R2 + R3 + R2 * R3 / R1));
    const swg = [1.884, 2.093, 2.302];   // S32K39 Table 40 MAXAPP min/typ/max, V pk-pk (a part-to-part MAXIMUM: the trim can only go down from it)
    const kBr = [(0.99 / 1.01) * (1 + 0.99 / 1.01), 2, (1.01 / 0.99) * (1 + 1.01 / 0.99)];   // H-bridge (R2/R1)(1 + R4/R3) with REXB 24 k ±1 %: 1.941–2.061
    const slewPk = 0.13e6 / (2 * Math.PI * 10e3);   // ALM2402 −40 °C slew ceiling per output, V pk
    const kMon = (rp, rptc) => (rp + 2 * rptc) / (rp + 2 * rptc + 4.4), kWind = (rp, rptc) => rp / (rp + 2 * rptc + 4.4), kMW = (rp, rptc) => rp / (rp + 2 * rptc);   // planes: amplifier → monitor (after RSX) → winding (after the PTCs)
    const vAmp = swg.map((v) => 2 * v * h), target = 7.2, vAmpT = target / kMon(70, 1.3), swgNeed = vAmpT / (2 * h), swgNeedLo = target / kMon(70, 1.3) / (kBr[0] * hB[0]);
    const vWind70 = swg[0] * 2 * h * kWind(70, 1.3), vWindStack = swg[0] * kBr[0] * hB[0] * kWind(60, 1.3), vWindStack70 = swg[0] * kBr[0] * hB[0] * kWind(70, 1.3);
    add("Sensing A.11", "Resolver excitation at 10 kHz (SWG → MFB → ALM2402 H-bridge)", `|H| ${f(h, 3)} nominal (coupling capacitor included) · ${f(hB[0], 2)}–${f(hB[1], 2)} over the BOUND tolerances (R ±1 %, C0G ±5 %) · ${f(h10[0], 2)}–${f(h10[1], 2)} for a ±10 % build → ${f(vAmp[1], 1)} V pp at the amplifier (${f(vAmp[0], 1)}–${f(vAmp[2], 1)} over the SWG range, untrimmed)`,
      "the trim (FW-10) holds the MONITOR plane at 7.2 V pp; the resolver floor 6.5 V pp is at the WINDING", "INFO",
      `f0 ${f(f0 / 1e3, 1)} kHz, Q ${f(Q, 2)}, passband gain −${f(R2 / R1, 2)} (round 16: 28 k, was 24 k / 1.85 — the SWG low corner gave 6.34 V pp at the winding through the RSX + PTC losses). Round 18 (F195): the QP-RX-01 acceptance band is the bound-tolerance band ${f(hB[0], 2)}–${f(hB[1], 2)} (a ±5 % C0G board reading 1.94–1.98 is OUT of tolerance; a ±10 % build spans ${f(h10[0], 2)}–${f(h10[1], 2)}). Round 12 (R2-F04): the A.10 network had the MFB feedback pair swapped (|H| 0.18)`);
    judge("Sensing A.15", "Excitation at the WINDING with the SWG at its LOW corner (1.884 V pp, trim saturated) — 70 Ω screening resolver, nominal gain", `${f(vWind70, 2)} V pp (amplifier ${f(vAmp[0], 2)} × ${f(kWind(70, 1.3), 3)}: RSX 2.2 Ω + PTC 1.3 Ω typical per line into 70 Ω)`, "≥ 6.5 V pp (gate ㉕)", 6.5 / vWind70, 0.97,
      "A14-R04 (review 3): the corner the round-15 row did not carry — with the 24 k it was 6.34 V pp. The 70 Ω primary is the screening assumption; the selected resolver's impedance decides (gate ㉕). The full tolerance stack at the IR-13 minimum is the next row");
    add("Sensing A.17", "Winding amplitude, full tolerance stack: SWG low corner × |H| low corner (bound ±5 % C0G, R ±1 %) × REXB low corner, at the IR-13 60 Ω interface minimum", `${f(vWindStack, 2)} V pp at 60 Ω (${f(vWindStack70, 2)} V pp at the 70 Ω screening resolver) — SWG needed for the 7.2 V pp setpoint at that gain corner ${f(swgNeedLo, 3)} V pp vs 1.884 V pp MAXAPP`, "≥ 6.5 V pp at the winding; a trim that cannot reach the setpoint sets DTC_RSLV_SWG_SAT and the unit does not arm (FW-30)", "INFO",
      "round 18 (xcheck18 §4, F195): at the coincidence of five corners the winding is under the floor and the trim saturates at ≈ 0.93–0.98 of the setpoint — a defined outcome (no arm), caught at EOL by QP-RX-01 (which also screens the MCU's SWG maximum), not a field failure. No hardware change: the screening resolver clears it; REXA4 30.1 k (+7 % gain, free) is the margin lever if EOL yield ever shows saturation, with cal_swg_code_init and the FW-30 headroom constants re-derived");
    judge("Sensing A.15", "FW-10 trim setpoint 7.2 V pp at the monitor: SWG amplitude needed vs its low-corner maximum", `${f(swgNeed, 3)} V pp nominal (amplifier ${f(vAmpT, 2)} V pp = ${f(vAmpT / 2, 2)} V pp per output) · ${f(swgNeedLo, 3)} V pp at the low gain corner`, `≤ 1.884 V pp (MAXAPP min) · ≤ ${f(2 * slewPk, 2)} V pp per output (−40 °C slew)`, Math.max(swgNeed / 1.884, (vAmpT / 2) / (2 * slewPk)), 0.985,
      `headroom ${f((1.884 / swgNeed - 1) * 100, 1)} % at nominal gain (the low gain corner needs ${f((swgNeedLo / 1.884 - 1) * 100, 1)} % more than the part's minimum maximum: the trim then saturates at ${f(1.884 / swgNeedLo, 3)} of the setpoint, inside the ±5 % trim band — no DTC). Winding ${f(target * kMW(70, 1.3), 2)} V pp at 70 Ω (× ${f(kMW(70, 1.3), 3)}), ${f(target * kMW(60, 0.35), 2)} / ${f(target * kMW(60, 1.3), 2)} / ${f(target * kMW(60, 5), 2)} V pp at 60 Ω with the PTCs at R_min / 1.3 Ω / R_1max (the sheet bounds R at 0.35–5.0 Ω: EOL sets the FW-10 window from the measured monitor-to-terminal transfer, QP-RX-01; the post-trip state is flagged, as intended). The SWG ramps up from ≈ 1.5 V pp under the trim: the untrimmed max corner (${f(vAmp[2], 1)} V pp) would slew-limit`);
  }
  // R1-F03: hall open-wire signature — 100 k to AGND at the card input
  {
    const tau = 100e3 * 3.3e-9, t = tau * Math.log(2.5 / 0.2);
    add("Sensing A.11", "Hall signal open wire (R⟨ph⟩B0 100 k)", `reads 0 V; 2.5 → 0.2 V (the window edge) in ${f(t * 1e3, 2)} ms`, "outside the HC5FW 0.2–4.8 V window (FW-05); RL ≥ 10 k", "PASS",
      "round 12 (R1-F03): the README had claimed the pull-down since A.4; an unpowered sensor reads the same 0 V. The Σi = 0 check alone could not see a stale 2.5 V (zero-current) reading at standstill");
  }
  // R2-F02/F03 → round 14 (A12-R07): the PRODUCTION UCC12051-Q1 draws 52 mA typ / 80 mA MAX at no load (SNVSBY2A §6.9,
  // 5 V select) + the AMC1311 (8 mA at ≈ 50 % efficiency) = 96 mA — the round-13 76 mA (50 mA typ × 1.2) did not cover it.
  // Fix: R5L 47 Ω ballast ahead of each NCV4276C splits the 15 → 5 V drop. NCV4276C DPAK RθJA 58.5 K/W on the 1.14 in² pad.
  {
    const v15 = [14.55, 15.45], iIn = [52e-3 + 8e-3 / 0.5, 80e-3 + 8e-3 / 0.5], r = [47 * 0.99, 47 * 1.01 * 1.04];
    const pLdo = Math.max(...v15.flatMap((v) => iIn.flatMap((i) => r.map((rr) => (v - i * rr - 5) * i))));
    const pRes = iIn[1] ** 2 * r[1], vInMin = v15[0] - iIn[1] * r[1], pLdoNoBallast = (15 - 5) * iIn[1];
    const tj = 85 + pLdo * 58.5, tj100 = 100 + pLdo * 58.5;
    judge("Sensing A.13", "VDC bias LDO (NCV4276C DPAK) behind the 47 Ω ballast, production UCC12051-Q1 at 96 mA", `${f(pLdo, 2)} W → Tj ≈ ${f(tj, 0)} °C at 85 °C (${f(tj100, 0)} °C at 100 °C) on the 58.5 K/W pad`, "150 °C Tj max", tj / 150, 0.9,
      `A12-R07: without the ballast the same current is ${f(pLdoNoBallast, 2)} W → ${f(85 + pLdoNoBallast * 58.5, 0)} °C. R5L carries ${f(pRes, 2)} W (2512 1 W, ≈ 0.82 W derated at 85 °C); LDO input ≥ ${f(vInMin, 1)} V at the 96 mA / 14.55 V corner (dropout needs 5.5 V). Layout rule dfm §4 stays; the hot first article measures both input currents`);
    const pPassMax = (15.45 - 4.9) ** 2 / (4 * 47 * 0.99), iAtMax = (15.45 - 4.9) / (2 * 47 * 0.99);
    add("Sensing A.13", "LDO pass dissipation, analytic maximum over load (P = (Vs−Vo)·I − R·I²)", `${f(pPassMax, 2)} W at ${f(iAtMax * 1e3, 0)} mA (15.45 V in, 4.9 V out, 46.5 Ω)`, `Tj ≈ ${f(85 + pPassMax * 58.5, 0)} °C at 85 °C, ${f(100 + pPassMax * 58.5, 0)} °C at 100 °C`, "INFO",
      "round 15 check: the worst case sits above the 96 mA budget (the resistor makes the LDO dissipation a concave function of load), so the 96 mA screen is not the ceiling — the hot first article measures the loaded input current");
    judge("Sensing A.13", "R5L ballast dissipation (47 Ω 2512, 1 W)", `${f(pRes * 1e3, 0)} mW at 96 mA, +5 % R`, "≈ 820 mW at 85 °C (1 W at 70 °C, linear to 155 °C)", pRes / 0.82, 0.8, "the resistor sits on the 2 oz copper next to the LDO; ERC locks the 47 Ω and the V15 → V15Lx → LDO chain");
    judge("Sensing A.11", "VDC bias barrier working voltage (UCC12050 V_IOWM)", "1697 VDC / 1200 Vrms reinforced (VDE 0884-11)", "850 VDC link (same class as the AMC1311, 1.2 kVrms)", 850 / 1697, 0.9,
      "round 12 (R1-F12, R2-F02/F03): the A.4.4 bind \"MGJ2D150505SC\" does not exist and the MGJ2 family is reinforced to 150 Vrms only — every module family checked (MGJ1/2, NXE/NXJ, RECOM RxxP/R1SX, Mornsun QA, ADuM6028) fails the working-voltage criterion");
  }
  // ---- round 14 (F03/F04/F05, F07, F09, F18, A12-R05): excitation monitor, exciter terminal fault, motor-temp clamp,
  // discharge stuck-on, SC extinction budget, firmware DESAT hold vs the hardware RC ----
  {
    const rM = 18e3 * 0.99, iMon = [35, 50].map((v) => v / rM), pRexm = 35 ** 2 / 18e3;
    judge("Sensing A.13", "Excitation-monitor pad injection (REXM 18 k, MCU unpowered, 0 V pad)", `${f(iMon[0] * 1e3, 2)} mA at 35 V · ${f(iMon[1] * 1e3, 2)} mA at 50 V`, "3 mA per pad (S32K39 operating and abs max)", iMon[1] / 3e-3, 0.97,
      `F03: the round-12 5.1 k / 12 k / 24 k network let 5.45–5.66 mA in (powered-clamp assumption); 18 k / 42.2 k / 84.5 k keeps the 0.70 / 0.82 ratios. REXM1/3 dissipate ${f(pRexm * 1e3, 0)} mW during a 35 V pulse (0603, 100 mW at 70 °C; 75 V rated) and 32 mW at a 24 V/60 s jump start`);
    const rTh = 18e3 * 42.2e3 / 60.2e3 + 18e3 * 84.5e3 / 102.5e3, fcM = 1 / (2 * Math.PI * rTh * 220e-12), phM = Math.atan(10e3 / fcM) * 180 / Math.PI;
    const fcS = 1 / (2 * Math.PI * 2 * 12.12e3 * (220e-12 + 47e-12 + 22e-12)), phS = Math.atan(10e3 / fcS) * 180 / Math.PI;
    add("Sensing A.13", "Excitation-monitor anti-alias filter (220 pF C_AAF across SDADC1, Thevenin 12.6 k / 14.8 k per leg)", `corner ${f(fcM / 1e3, 1)} kHz, −${f(phM, 1)}° at 10 kHz (SIN/COS −${f(phS, 1)}°)`, "R_AAF ≤ 20 kΩ per leg, C_AAF 180–220 pF (Table 38)", "PASS",
      `F05: the pair had no external capacitor. The ${f(phS - phM, 1)}° static offset between the demodulation reference and the signal channels is a fixed number, absorbed by the FW-20 phase calibration; the 220 pF ±5 % C0G stays ≥ 180 pF`);
    // F04 / round 15 / round 18: exciter output forced to battery through the harness — SMDJ8.5A-HRA on the PROTECTED node, MF-MSMF020/33X per line.
    // Round 18 (A16 rechecks + Opus cross-check xcheck18, F191/F193/F194): every figure at its worst corner — V_BR,min (and the 8/20 µs r_dyn,
    // 9.04 V at −40 °C) for currents, V_BR,max for clamp voltages; the SMDJ 10 ms point converted from the exponential test pulse to the PTC's
    // near-rectangular one (×0.65) and derated to T_J(init) 85 °C (×0.808, Fig. 3) → 5.3 J; the sheet's ONLY maximum trip time (20 ms at 8 A)
    // applied only where the current is ≥ 8 A; the /33X TYPICAL time-to-trip curve (sheet p.8, curve C — p.7 is the unsuffixed 30 V part)
    // where no maximum exists; the ECU-asleep case (amplifier off) added; RSX judged against its overload rating.
    const vbr = [9.44, 10.4], vbrCold = 9.04, rdyn = (14.4 - 10.4) / 208.3, rdyn8 = (18.6 - 10.4) / 1041.5;   // SMDJ8.5A-HRA: 14.4 V at 208.3 A (10/1000 µs) → 19.2 mΩ; the 8/20 µs row 18.6 V at 1041.5 A → 7.9 mΩ; α_T 0.066 %/K
    const rPtc = 0.35, rHarn = 0.5, pdPtc = 0.8;   // MF-MSMF020/33X: R_min 0.35 Ω (23 °C), R_1max 5.0 Ω (1 h after a trip), P_d 0.8 W tripped (23 °C, still air); 0.5 Ω harness for the clamp rows
    const iF = (v, rExt = rHarn, vb = vbr[1], rd = rdyn) => (v - vb) / (rExt + rPtc + rd), vCl = (v, rExt = rHarn) => vbr[1] + iF(v, rExt) * rdyn;
    const pTrickle = (v, vb = vbr[1]) => vb * pdPtc / (v - vb);   // after the trip the PTC passes P_d/(V_S − V_BR): the TVS then dissipates this for as long as the fault lasts
    judge("Sensing A.13", "Exciter terminal fault — clamp at the protected node during the PTC trip window (PTC in the path, A13-R01)", `${f(vCl(24), 1)} V at 24 V (${f(iF(24), 1)} A) · ${f(vCl(35), 1)} V at 35 V (${f(iF(35), 1)} A ≤ 40 A PTC I_max) at 0.5 Ω harness`, "≤ 12.8 V while VEXD is up (12.1 V rail + a diode) · 18 V output abs max", vCl(35) / 12.8, 0.95,
      `F04: the amplifier's regulated 12.1 V rail does not protect an output pin forced by the harness (ALM2402 output abs max 18 V; its output diodes conduct reverse current only as pulses, DS §8.3.6 — the clamp below the rail avoids that entirely). SMDJ8.5A-HRA (round 17; SMCJ8.5A in round 16, SMCJ8.5CA in round 15) V_BR ${vbr[0]}–${vbr[1]} V clears the excitation swing (0.4–4.6 V); ≈ 12.5 V at T_J 150 °C. The MF-MSMF020/33X (0.2 A hold, 33 V, I_max 40 A, AEC-Q200) trips in ≤ 0.02 s at 8 A and faster at these 20–70× overloads. After the trip the PTC's ≈ ${pdPtc} W (23 °C) leaves V_BR·P_d/(V_S − V_BR) in the TVS: ${f(pTrickle(24), 2)} W at 24 V, ${f(pTrickle(35), 2)} W at 35 V — but ${f(pTrickle(16), 2)} W at 16 V, ${f(pTrickle(14.4), 2)} W at 14.4 V and ${f(pTrickle(12.6), 2)} W at 12.6 V (the ECU-asleep row below; the awake amplifier sinks the trickle instead). Line-to-ground shorts are the ALM2402's own current limit (≈ 550–750 mA). BENCH (gate ㉘): both lines, MCU on/off, 24 V/60 s and 35 V/400 ms`);
    // --- bounded region: the sheet's only maximum trip time (≤ 20 ms at ≥ 8 A) ---
    const eCap25 = 0.65 * 1000 * 0.010, eCap85 = eCap25 * 0.808;   // SMDJ Fig. 2: 1.0 kW at the 10 ms end of an EXPONENTIAL test pulse (td = time to half); a rectangular pulse of the same peak T_J carries ≈ 0.65× → 6.5 J at 25 °C; Fig. 3 derates I_PP to 80.8 % at T_J(init) 85 °C → 5.3 J
    const rExt8 = (v) => (v - vbr[1]) / 8 - rPtc - rdyn;   // the current is ≥ 8 A (bound applies) while the external resistance is ≤ this
    const e8 = 8 * (vbr[1] + 8 * rdyn) * 0.020;   // worst energy in the bounded region: at 8 A for the full 20 ms; above 8 A the trip time falls at least as fast as I⁻² (adiabatic I²t; the typical curve is steeper), so E ∝ 1/I falls with the current
    judge("Sensing A.15", "Exciter TVS energy — single fault (line shorted to KL30, ≤ 26 V), the sheet-bounded region: fault current ≥ 8 A ⇒ PTC clears within 20 ms", `${f(e8, 2)} J worst (8 A × ${f(vbr[1] + 8 * rdyn, 1)} V × 20 ms; 24 V/0 Ω: ${f(iF(24, 0), 0)} A trips in ≈ 0.3 ms typ, 0.13 J) — the region is external ≤ ${f(rExt8(24), 2)} Ω at 24 V, ≤ ${f(rExt8(16), 2)} Ω at 16 V, ≤ ${f(rExt8(26), 2)} Ω at 26 V: the ISO 16750-2 §4.10 direct short is inside it`, `${f(eCap85, 1)} J — the SMDJ8.5A-HRA 10 ms point converted to a rectangular pulse at T_J(init) 85 °C (${f(eCap25, 1)} J at 25 °C); a longer, lower-energy pulse is inside the curve`, e8 / eCap85, 0.9,
      "round 18 (F193): the round-17 row judged 3.35 J against 9 J 'at any source impedance' — the 9 J was the exponential-pulse rating read as rectangular, and the 20 ms bound exists only at ≥ 8 A. Below 8 A the PTC's trip time is a TYPICAL curve (next rows)");
    // --- unbounded region: the typical /33X time-to-trip curve (sheet p.8 curve C, digitised: xcheck18 §3) ---
    const ttt = [[0.5, 6.16], [0.6, 3.03], [0.8, 1.01], [1, 0.431], [1.5, 0.122], [2, 0.065], [3, 0.030], [4, 0.0177], [5, 0.0119], [8, 0.0053]];
    const tTrip = (i) => {   // log-log interpolation; I⁻² beyond the last point; ∞ below I_trip 0.4 A
      if (i < 0.4) return Infinity;
      if (i <= ttt[0][0]) return ttt[0][1] * Math.pow(ttt[0][0] / i, 2.5);
      for (let k = 1; k < ttt.length; k++) if (i <= ttt[k][0]) { const [i0, t0] = ttt[k - 1], [i1, t1] = ttt[k]; return Math.exp(Math.log(t0) + (Math.log(t1) - Math.log(t0)) * (Math.log(i) - Math.log(i0)) / (Math.log(i1) - Math.log(i0))); }
      return ttt[ttt.length - 1][1] * Math.pow(ttt[ttt.length - 1][0] / i, 2);
    };
    const rthJA = 75, tjMax = 150;   // SMDJ-HRA: typical, 8 × 8 mm pads; T_J max 150 °C (not 175)
    const pSs = (ta, rth) => (tjMax - ta) / rth;   // steady-state capability
    const sweep = (v, rs) => rs.map((r) => { const i = iF(v, r, vbr[1], rdyn), p = (vbr[1] + i * rdyn) * i, t = tTrip(i); return { r, i, p, t, e: p * t }; });
    const win = (v) => { let lo = null, hi = null; for (let r = 0.5; r <= 200; r += 0.1) { const x = sweep(v, [r])[0]; const bad = x.e > eCap85 || x.p > pSs(23, rthJA); if (bad && lo === null) lo = r; if (bad) hi = r; } return [lo, hi]; };
    const w24 = win(24), w16 = win(16), w26 = win(26);
    const tab = (v, rs) => sweep(v, rs).map((x) => `${x.r} Ω: ${f(x.i, 2)} A · ${f(x.p, 1)} W · t_typ ${x.t === Infinity ? "never (< I_trip)" : x.t >= 1 ? f(x.t, 1) + " s" : f(x.t * 1e3, 0) + " ms"} · ${x.e === Infinity ? "∞" : f(x.e, 2) + " J"}`).join(" | ");
    add("Sensing A.17", "Exciter TVS/PTC coordination BELOW the 8 A bound — a sustained short through external resistance (typical /33X trip curve, ECU asleep so the amplifier sinks nothing)", `24 V: ${tab(24, [2, 5, 10, 20, 35, 70])} · 16 V: ${tab(16, [1, 2, 5, 10, 20])}`, `TVS energy ≤ ${f(eCap85, 1)} J where the PTC trips; where it never trips (I < 0.4 A) or trips slowly, the TVS power must stay under its steady state: ${f(pSs(23, rthJA), 2)} W at 23 °C / ${f(pSs(85, rthJA), 2)} W at 85 °C on the sheet's 8 × 8 mm pads (75 K/W), ≈ ${f(pSs(85, 40), 1)} W hot on a ≥ 3 cm² island with thermal vias (≈ 40 K/W)`, "WARN",
      `round 18 (A16-R03 md / R04 html, xcheck18 §3, F193): UNPROTECTED ON PAPER for external ≈ ${f(w24[0], 0)}–${f(w24[1], 0)} Ω at 24 V, ≈ ${f(w16[0], 0)}–${f(w16[1], 0)} Ω at 16 V, ≈ ${f(w26[0], 0)}–${f(w26[1], 0)} Ω at 26 V (typical curve; on guaranteed data everything below 8 A is unbounded). The 'any source impedance' PASS of round 17 was wrong. No cheap part closes it: a lower-V_BR TVS moves the trickle window to the direct short (45 A > I_max), a higher one lets a normal-voltage short charge VEXD above the ALM2402's 18 V through DEX. Closure: (1) LAYOUT RULE (dfm.md): TVSEP/TVSEN on a ≥ 3 cm² copper island with thermal vias, FEXP/FEXN thermally coupled on the same island — the PTC's hold current extrapolates to ≈ 0 at 94–106 °C, so a TVS at 2–4 W trips it (the PolyZen principle); (2) QP-RX-04 sweep, ECU asleep, 12.6/14.4/16/24 V × external 0–100 Ω, TVS lead temperature; (3) VR-17 (pulse data beyond 10 ms — needed again); (4) IR-42 (the OEM's short-circuit test condition and resolver-harness routing). End state if the window is hit for long: the TVS fails (short, the usual mode) → the PTC trips on the follow-on current (V_S/(R_ext + 0.35) ≥ 0.4 A for external ≤ 60 Ω at 24 V, ≤ 40 Ω at 16 V) and holds ≤ 63 mA → the line is dead, FW-10 reports it at key-on, no arm — fail-safe, a repair`);
    add("Sensing A.17", "Sustained exciter short to the NORMAL battery (12.6–16 V) with the ECU asleep — post-trip trickle and the never-trip region", `TVS after a trip: ${f(pTrickle(12.6), 2)} W at 12.6 V · ${f(pTrickle(14.4), 2)} W at 14.4 V · ${f(pTrickle(16), 2)} W at 16 V (V_BR,max; ${f(pTrickle(12.6, vbr[0]), 2)} W at V_BR,min) · never-trip region (external > ${f((12.6 - vbr[1]) / 0.4 - rPtc, 0)} Ω at 12.6 V, > ${f((16 - vbr[1]) / 0.4 - rPtc, 0)} Ω at 16 V): up to ${f(vbr[1] * 0.4, 1)} W at 23 °C, ${f(vbr[1] * 0.14, 1)} W at 85 °C (I_trip ≈ 2 × hold)`, `steady state ${f(pSs(23, rthJA), 2)} W at 23 °C / ${f(pSs(85, rthJA), 2)} W at 85 °C (75 K/W) — ≈ ${f(pSs(23, 40), 1)} / ${f(pSs(85, 40), 1)} W on the 40 K/W island`, "WARN",
      `round 18 (xcheck18 §3, NEW — no row covered it; F193): the largest gap is at the normal battery voltage, not at 24 V: with the amplifier off (V5A = 0 pulls the ALM2402 SDN low; LPOFF; or after its OTF) nothing sinks the trickle and the TVS junction reaches ${f(23 + pTrickle(12.6) * rthJA, 0)} °C at 12.6 V on the minimum pads (${f(23 + pTrickle(12.6) * 40, 0)} °C on the island). The awake amplifier sinks ≤ 0.36–0.70 A through RSX, which keeps the TVS dark for external ≳ 5 Ω. Same closure as the row above (layout coupling + QP-RX-04 sweep + IR-42 + VR-17); the 60 s ISO 16750-2 short at 16 V passes on the island (${f(23 + pTrickle(16) * 40, 0)} °C from 23 °C) and is marginal on the minimum pads at 85 °C ambient`);
    // --- PTC current against I_max 40 A (worst corner: V_BR,min, the 8/20 µs r_dyn, cold) ---
    const i24 = (rExt, vb, rd) => (24 - vb) / (rExt + rPtc + rd);
    const rSrcMin35 = (35 - vbrCold) / 40 - rPtc - rdyn8, rSrcMin24 = (24 - vbrCold) / 40 - rPtc - rdyn8;
    judge("Sensing A.15", "Exciter PTC current vs I_max 40 A — single fault at 24 V with the IR-16 minimum harness (0.05 Ω)", `${f(i24(0.05, vbrCold, rdyn8), 1)} A cold (V_BR 9.04 V, r_dyn 7.9 mΩ) · ${f(i24(0.05, vbr[0], rdyn8), 1)} A at 25 °C · at a true 0 Ω: ${f(i24(0, vbr[0], rdyn8), 1)}–${f(i24(0, vbrCold, rdyn8), 1)} A (${f(i24(0, vbr[1], rdyn), 1)} A at the V_BR,max corner round 17 used)`, "40 A (MF-MSMF020/33X I_max; trip-cycle life tested at V_max/I_max, no arcing or burning)", i24(0.05, vbrCold, rdyn8) / 40, 0.95,
      `round 18 (xcheck18 §1c/§6, F193): the round-17 'any source impedance' figure (37 A) was the V_BR,max corner; at V_BR,min the direct short exceeds I_max by up to 5 % — external ≥ ${f(rSrcMin24, 3)} Ω restores it, and IR-16 now states the harness minimum 0.05 Ω at ≤ 26 V (any harness has it). The rated diversion (DEX through RSX) adds only 0.1–0.25 A here: it takes its current from the TVS`);
    add("Sensing A.15", "Exciter PTC current vs I_max 40 A — load-dump-coincident fault (35 V)", `at 0.1 Ω external: ${f((35 - vbrCold) / (0.1 + rPtc + rdyn8), 0)} A > 40 A · needs ≥ ${f(rSrcMin35, 2)} Ω source + harness at the cold V_BR,min corner (round 17 said 0.27 Ω at V_BR,max); V_max 33 V is also exceeded by 6 %`, "double event (IR-16 ≥ 0.29 Ω / IR-33)", "INFO",
      "the PTC still trips (faster than at 8 A); above I_max its survival is not warranted — it may fail open, which the FW-10 amplitude window reports as a resolver fault (fail-safe, not fail-dangerous). Accepted as a double event; the OEM allocation is IR-16 (raised to 0.29 Ω in round 18) and the PTC transient question VR-16");
    const e35b = 40 * (vbr[1] + 40 * rdyn) * 0.020, t35 = tTrip(40);
    add("Sensing A.15", "Exciter TVS energy — load-dump-coincident fault (35 V for ≤ 400 ms, ISO 16750-2 test B, AND a line short at the same time, IR-16 allocation)", `${f(e35b, 1)} J if the PTC took the full 20 ms at 40 A · ${f(40 * (vbr[1] + 40 * rdyn) * t35, 2)} J on the typical curve (${f(t35 * 1e3, 1)} ms)`, `${f(eCap85, 1)} J (rectangular, 85 °C) — the 20 ms bound is NOT met at 40 A on paper; the typical curve is`, "INFO",
      "round 18 (F193): round 17 closed this on the 9 J reading; with the corrected 5.3 J the double event is inside the curve only on the TYPICAL trip time — it stays what it was for the PTC side: an accepted, characterised double event (IR-16 / IR-33, QP-RX-04 step 3), VR-17 pulse data beyond 10 ms would close it on paper");
    // --- back-drive with VEXD absent: the rated diversion, now THROUGH RSX (round 18 moved the DEX anode to the amplifier node, F191) ---
    const cRail = 22e-6 + 4.7e-6, vfDex = 0.45, rDex = 0.03;
    const solve0 = (vs, rExt) => {   // t = 0+: (V_S − V)/R_s = (V − V_BR,max)/r_dyn + (V − V_F)/(2.2 + r_DEX), rail at 0 V
      const rs = rExt + rPtc, g1 = 1 / rs, g2 = 1 / rdyn, g3 = 1 / (2.2 + rDex);
      const v = (vs * g1 + vbr[1] * g2 + vfDex * g3) / (g1 + g2 + g3);
      return { v, iTvs: (v - vbr[1]) * g2, iDiv: (v - vfDex) * g3, iPtc: (vs - v) * g1 };
    };
    const s24 = solve0(24, 0), s35 = solve0(35, 0.29);
    const vTh = (vs, rExt) => { const rs = rExt + rPtc; return (vs / rs + vbr[1] / rdyn) / (1 / rs + 1 / rdyn); };   // Thevenin the diversion loop sees while the TVS conducts
    const vFin = vTh(35, 0.29) - vfDex, tau = (2.2 + rDex + 0.018) * cRail, qRev = cRail * vFin, i2t = s35.iDiv ** 2 * tau / 2, eDiode = vfDex * qRev + rDex * i2t, eRsx = 2.2 * i2t;
    const i2tFsm = 70 ** 2 * 8.3e-3 / 2;   // PMEG4050EP-Q I_FSM 70 A, 8.3 ms half-sine, T_j(init) 25 °C — the sheet's only pulse point, no derating vs T_j or width
    judge("Sensing A.15", "Exciter back-drive with VEXD absent — rated diversion THROUGH RSX (DEXP/DEXN on the amplifier node, round 18)", `${f(s24.iDiv, 2)} A (24 V/0 Ω) – ${f(s35.iDiv, 2)} A (35 V/0.29 Ω) peak through RSX + DEX while the TVS holds the node at ${f(s24.v, 2)}–${f(s35.v, 2)} V (TVS ${f(s24.iTvs, 0)} / ${f(s35.iTvs, 0)} A, PTC total ${f(s24.iPtc, 1)} / ${f(s35.iPtc, 1)} A); τ = ${f(tau * 1e6, 0)} µs, ${f(qRev * 1e3, 2)} mC, ${f(eDiode * 1e3, 2)} mJ in the Schottky, ${f(eRsx * 1e3, 1)} mJ in RSX; rail ≈ ${f(vFin, 1)} V, ≤ 11.5 V`,
      `I²t ${f(i2t * 1e6, 0)} µA²s vs the I_FSM point ${f(i2tFsm, 1)} A²s (8.3 ms half-sine, T_j(init) 25 °C only) · ALM2402 18 V abs · NCV4276C V_Q −1…40 V covers the NODE voltage only`, i2t / i2tFsm, 0.5,
      `round 18 (A16-R02 all three rechecks, xcheck18 §1/§2, F191/F192): round 17 had drawn the anode on the PROTECTED node — the harness then charged the empty rail through PTC → DEX with no RSX: 27–67 A at 24 V and 39–99 A at 35 V (45–58 A with 0.1–1 µH), τ 9–24 µs, TVS dark, the Schottky dropping 1–3 V so the ALM2402's own diode carried 0.2–1.1 A — while the row divided by 2.2 Ω. The anode now sits on the amplifier node (ERC graph cut), so this model IS the circuit; the diode is judged by I²t, not peak against a 140× wider pulse (the internal diode sees ≈ 10 mA at the Schottky's 0.49 V — ALM2402 Fig. 7 ends at 0.4 A). The 40 V output rating says nothing about reverse current from OUT toward IN or GND with IN at 0 V, at 6 V (cranking) or inhibited: QP-RX-05 is a RELEASE GATE again (reverse rail current into ULDOEX, 0.1 Ω shunt) and VR-33 asks onsemi; if such a path conducts, the 60 µs pulse becomes ≈ 1.3–1.6 A DC through RSX (3–5 W) until the PTC trips — the gate decides whether a blocking element is needed`);
    // --- negative terminal fault (where the OEM allocates it, IR-41) ---
    const iNegTvs = (24 - 1.2) / (rHarn + rPtc), iNegPtc0 = (24 - 1.2) / rPtc, rNegMin = (24 - 1.2) / 40 - rPtc, iNegAmpOff = (1.2 - 0.7) / 2.2, iSrcLim = [0.60, 0.93];
    judge("Sensing A.15", "Exciter negative terminal fault — UNIDIRECTIONAL SMDJ8.5A-HRA forward-conducts (node at −0.7…−1.2 V)", `TVS forward ≈ ${f(iNegTvs, 0)} A at −24 V through 0.5 Ω until the PTC trips (${f(iNegPtc0, 0)} A at a true 0 Ω — above the PTC's 40 A: needs external ≥ ${f(rNegMin, 2)} Ω) · amplifier OFF: lower diode ≈ ${f(iNegAmpOff, 2)} A through RSX · amplifier ON: it sources its limit ${iSrcLim[0]}–${iSrcLim[1]} A into the clamped node (RSX ${f(iSrcLim[1] ** 2 * 2.2, 1)} W, amplifier 7–11 W → OTF in ≈ 0.1–1 s)`, "I_FSM 300 A (8.3 ms half sine — SMDJ-HRA sheet) · ALM2402 −0.3 V abs bounded by the diode · PTC I_max 40 A (IR-16 where negative faults are allocated)", iNegTvs / 300, 0.8,
      "round 16 (A14-R02): with the round-15 BIdirectional part a negative fault put 4.4 A (43 W) through the lower output diode and RSX for the PTC trip time — 0.9 J in a 1206; the excitation never goes below ground (4–8 V around VMID), so the unidirectional part is free. Round 18 (xcheck18 §6, F193/F194): the judge now divides by the bound part's 300 A (round 17 still divided by the SMCJ's 200 A), the PTC current at 0 Ω was never checked (65 A > 40 A: IR-16 0.22 Ω where allocated) and the powered amplifier's source limit through RSX is the RSX row below. Negative faults themselves are an OEM allocation (IR-41, gate ㉘)");
    // --- RSX (2.2 Ω 1206) power during faults — no row had checked it (round 18, F194) ---
    const pRsxPos = 0.70 ** 2 * 2.2, pRsxNeg = iSrcLim[1] ** 2 * 2.2, pRsxOvl = 4 * 0.5;   // ROHM ESR18: short-time overload = rated voltage × 2.0 for 5 s ⇒ 4 × 0.5 W = 2.0 W (1 Ω ≤ R < 1 kΩ, ESR series Rev.012 characteristics table); Panasonic ERJ-8ENF (0.25 W): 2.5 × V for 5 s ⇒ 1.56 W
    judge("Sensing A.17", "RSX power during a terminal fault — positive with the rail present (amplifier sinking its limit) and negative with the amplifier sourcing its limit, until the ALM2402's OTF (≈ 0.1–1 s)", `${f(pRsxPos, 2)} W (0.70 A sink limit at −40 °C) · ${f(pRsxNeg, 2)} W (0.93 A source limit at −40 °C) · 8 mW in service (60 mA excitation)`, `${f(pRsxOvl, 1)} W for 5 s — ROHM ESR18EZPF2R20 (0.5 W at 70 °C, anti-surge, AEC-Q200, same 1206 pad) short-time overload; the Panasonic ERJ-8ENF2R20V it replaces: 0.25 W, 1.56 W overload`, pRsxNeg / pRsxOvl, 0.97,
      "round 18 (xcheck18 §6 item 11, F194): the negative fault with the amplifier powered put 122 % of the 0.25 W part's overload rating through it for the OTF time; the anti-surge 0.5 W part on the same pad (the RFS4 precedent, round 17) covers both OTF-bounded cases. The LDO-reverse DC case (3–5 W, if VR-33 says the NCV4276C conducts from OUT) is not covered by any 1206 — that is what QP-RX-05 decides");
    // --- hold current at the interface minimum resolver impedance (IR-13) ---
    const iEx = (vpp, rp) => vpp / (2 * Math.SQRT2) / (rp + 2 * rPtc), iExSet = iEx(7.2, 60), iExHi = iEx(7.2 * 1.05, 60), iExCal = iEx(8.3, 60);   // PTCs at R_min: the worst (highest) current
    judge("Sensing A.15", "Exciter PTC hold current (MF-MSMF020/33X, 0.07 A at 85 °C) vs the excitation current at the interface minimum resolver impedance", `${f(iExSet * 1e3, 1)} mA rms at the 7.2 V pp setpoint into the 60 Ω interface minimum (IR-13) · ${f(iExHi * 1e3, 1)} mA at the top of the ±5 % trim band · ${f(iExCal * 1e3, 1)} mA at the 8.3 V pp CAL ceiling · ≈ ${f(iEx(7.2, 70) * 1e3, 1)} mA with the 70 Ω screening resolver`, "70 mA at 85 °C (Bourns derating table — the /33X row; the unsuffixed 020 is 90 mA; the table ends at 85 °C, the part's operating limit)", iExCal / 0.07, 0.8,
      "round 17: the round-16 '60 mA assumed maximum' had no source; the resolver interface states Z_primary ≥ 60 Ω at 10 kHz (IR-13). Round 18 (xcheck18 §4/§6, F195): the round-17 expression added 2.6 Ω of PTC that is not on the sheet (R_min 0.35 Ω) and judged the setpoint only — the CAL ceiling is the bound: 69 % of the hot hold. The selected resolver is measured against it at qualification (docs/qualification-plan.md); a nuisance trip shows as the FW-10 amplitude window, not as a silent fault");
    // amplitude planes (round 16, A14-N01): the monitor is on the protected node BEFORE the PTC — the winding sees less
    const kW = (rp, rptc) => rp / (rp + 2 * rptc);
    add("Sensing A.15", "Amplitude planes: monitor (protected node) vs winding — 70 Ω screening resolver and the 60 Ω interface minimum, PTCs at the sheet's 0.35 / 5.0 Ω bounds (1.3 Ω typical)", `monitor 7.2 V pp (trim setpoint) → winding at 70 Ω: ${f(7.2 * kW(70, 0.35), 2)} / ${f(7.2 * kW(70, 1.3), 2)} / ${f(7.2 * kW(70, 5), 2)} V pp (R_min / typ / R_1max) · at 60 Ω: ${f(7.2 * kW(60, 0.35), 2)} / ${f(7.2 * kW(60, 1.3), 2)} / ${f(7.2 * kW(60, 5), 2)} V pp`, "≥ 6.5 V pp at the WINDING (gate ㉕); the post-trip state (R_1max, an hour after a trip — or a part at R_1max from new) is flagged by FW-10", "INFO",
      "the round-15 wording 'the monitor sees what the resolver gets' was too strong: the monitor does not observe the PTC or harness drop. Round 18 (F195): '1.3 Ω cold' is not a sheet value — the sheet bounds R at 0.35–5.0 Ω. EOL characterises monitor-to-terminal transfer with the real harness and sets the FW-10 window with that allowance (QP-RX-01)");
    // F18: motor-temperature line shorted to KL30 — fuse + SMAJ5.0A + 1 k into the buffer
    const iMt = (24 - 8) / 1.0, iBuf = (9.2 - 5.3) / 1e3;
    judge("Sensing A.13", "Motor-temp line KL30 short: SMAJ5.0A carries the fault until FMT opens", `≈ ${f(iMt, 0)} A through ≈ 1 Ω (fuse + harness) · V_C ≤ 9.2 V`, "I_PP 43.5 A (400 W, 10/1000 µs)", iMt / 43.5, 0.8,
      `F18: the PESD5V0L1BA was an ESD-class part. The 1 k RMTxS limits the OPA333 input to ${f(iBuf * 1e3, 1)} mA (10 mA abs); the 10 k pull-up injects < 0.5 mA into VREF5. FMT = Littelfuse 0438.375WRA (0603, 63 V DC, I²t 0.0041 A²s → ≈ ${f(0.0041 / iMt ** 2 * 1e6, 0)} µs melting at ${f(iMt, 0)} A, AEC-Q200)`);
    // F09: QDIS shorted with the battery connected — no software can interrupt it
    for (const [sku, v, r] of [["8XX", 850, 470], ["4XX", 500, 220]]) {
      const i = v / (4 * r), p = i * i * r;
      add("Discharge", `Stuck-ON QDIS with the battery connected, ${sku}`, `${f(i, 2)} A · ${f(p, 0)} W per resistor (10 W rated)`, "resistor must not flame (gate ㉖): TT/Welwyn SQP statement — no burning or incandescent particles under any overload (SQP.pdf)", "PASS",
        "F09 / round 17: the software timeout is bypassed by a shorted switch; the firmware detects the unexpected discharge at the next contactor opening (latched DTC, no re-energisation); the no-flame property is the manufacturer's statement for the bound part, the fail-open time is a characterisation in docs/qualification-plan.md (VR-28; the Yageo alternate needs the same statement)");
    }
    // F07: short-circuit current-extinction budget = detection (blank + threshold + deglitch) + soft-off + current fall/tail
    const d = P.drv;
    for (const [tag, c, tFall, lim] of [["SiC", 47e-12, 0.3e-6, null], ["IGBT", 82e-12, 1.8e-6, P.igbt.tsc]]) {
      const tDet = c * 1.05 * d.vth[2] / d.ichg[0] + d.leb + d.deg[2];
      const soft = tag === "SiC" ? 0.46e-6 / 0.4 : 106e-9 * (VCC2.hi - 10) / 0.4;
      const tTyp = tDet + soft + tFall, tMin = tDet + soft * 4 + tFall;
      add("Gate drive", `SC current-extinction budget, ${tag} (F07)`, `${f(tTyp * 1e6, 2)} µs at the 400 mA I_STO typ · ${f(tMin * 1e6, 2)} µs at the 100 mA DS minimum (fall/tail ${f(tFall * 1e6, 1)} µs assumed)`,
        lim ? `≤ ${f(lim * 1e6, 0)} µs withstand at 800 V (hiitio Table 5; 850 V not documented)` : "SiC withstand not published", "WARN",
        "the detection and soft-off terms are the DESAT rows above; the current fall/tail term is an ASSUMPTION until the module's SC waveform is in hand — release gates ③ (vendor SC data, contained SC test). Values are tuned only within the false-trip and overshoot limits; a stronger driver only if this budget cannot close");
    }
    // A12-R05: the firmware may not lower MCU_GATE_EN before the hardware fault path would
    const tauDhi = 10.1e3 * 3.3e-9 * 1.05, dHi = tauDhi * Math.log(1 / SCH.tnLo) + 0.8e-6;
    judge("Safety A.13", "Firmware DESAT hold before MCU_GATE_EN drops (TI_CAL_DESAT_EN_HOLD_US default 60 µs)", "60 µs", `≥ ${f(dHi * 1e6, 1)} µs (10 k / 3.3 nF RC upper corner + FLT)`, dHi / 60e-6, 0.95,
      "A12-R05: the DESAT ISR used to call br_spo(true) at once, bypassing the deliberately delayed FLT → DRV_EN path through the undelayed MCU_GATE_EN AND input; the hold is enforced inside the bridge module for every caller (firmware tests: early/late ISR entry, apply_decision and recovery during the hold)");
    add("Sensing", "V_DC divider for an extended 920 V variant", "6.2 k bottom = 911 V full scale (893 V worst corner); 5.1 k would give 1108 V", "separate release: scaling, OV thresholds, calibration, can hot-voltage and switching margins together", "INFO",
      "F10: the fitted divider is right for the 500–850 V contract only; the 470 k tops are 1 % thin-film, the bottoms 0.1 % (BOM)");
  }
  // R2-F13 / A11-R02: a resolver wire shorted to KL30 (same vehicle connector) — pin injection through RSINF + RSINR,
  // bounded with the MCU rail at 0 V (clamp 0 V worst) and −1 % resistors; both legs pull VMID through the winding
  {
    const rS = 0.99 * (12e3 + 120), iPin = [16, 24, 35].map((v) => v / rS), iBuf = 2 * (35 - 2.5) / 12e3, iOld = (16 - 5.7) / 450;
    const cDiff = 220e-12 + 47e-12 + 22e-12, fc = 1 / (2 * Math.PI * 2 * 12.12e3 * cDiff), ph = Math.atan(10e3 / fc) * 180 / Math.PI;
    const gMin = 215 / (215 + 24.24), gMax = 380 / (380 + 24.24), r = gMax / gMin, dTheta = Math.asin((r - 1) / (r + 1)) * 180 / Math.PI;
    judge("Sensing A.11", "Resolver wire shorted to KL30: SDADC pin injection (RSINF 12 k + RSINR 120, MCU rail at 0 V, −1 % R)", `${f(iPin[0] * 1e3, 2)} / ${f(iPin[1] * 1e3, 2)} / ${f(iPin[2] * 1e3, 2)} mA at 16 / 24 / 35 V into a 0 V node`,
      "3 mA per pin — the S32K39 operating AND absolute-maximum limit, in every power state", iPin[2] / 3e-3, 0.98,
      `round 12/13: the GEN3 330 Ω + 120 Ω let ${f(iOld * 1e3, 0)} mA in; the round-12 10 k held only the powered case (5.7 V clamp) — unpowered it was 3.42 mA. The VMID buffer sinks ${f(iBuf * 1e3, 1)} mA through the two 12 k bias resistors at 35 V (OPA348 ≈ 7 mA at 125 °C; it saturates toward V5A, an invalid-resolver state for FW-10). 220 pF C_AAF at the pins (S32K39 Table 38: 180 pF min) + 47 pF + 22 pF common-mode both legs → corner ${f(fc / 1e3, 0)} kHz, −${f(ph, 0)}° at 10 kHz on both channels; source 24 k vs Z_DIFF 215–380 k: gain ${f(gMin, 3)}–${f(gMax, 3)}, cancels only as far as the channels match — worst independent corners ${f(dTheta, 2)}° electrical, an EOL calibration item (FW-20), not "ratio-cancelled"`);
  }
  // Round 17 (F185–F189): LV entry by LOAD-DUMP LET-THROUGH. ISO 16750-2 test B is the test-A source (Us 79–101 V behind
  // Ri 0.5–4 Ω, td 40–400 ms, tr 10 ms) clamped to Us* = 35 V by the central suppressor (Nexperia IAN50007, Diotec AN,
  // TI TIDUC41). A local TVS that clamps BELOW 35 V competes with that clamp and takes the unsuppressed current, so every
  // TVS on a KL30-derived net is dark at 35 V (ERC-locked) and everything downstream is rated for the plateau. Data:
  // Littelfuse TPSMC-VR DS (V_BR, V_C at I_PP, αT), Vishay SM8S DS + EDN load-dump table (SM8S24A 50 A for 10 × 400 ms),
  // FS26 Rev.3 Fig. 8 / Table 5 (36 V High Voltage Extended Operation, 40 V abs, WAKE −5 mA reverse DC), TPS55340-Q1
  // SLVSBV5C (38/40 V), NCV4276C DS (Fig. 32/33 single-pulse R(t)), STPS5L60 DS2741 (P_ARM), Bel 0680L (I²t, derating),
  // Panasonic ZC catalog (CLVC3).
  {
    const LN10 = Math.log(10);
    const TV = { vbr: [36.7, 40.6], vc: 53.3, ipp: 28.2, at: 0.097e-2 };     // TPSMC33A-VR (DTVSC) / TPSMC33CA-VR (DTVH/DTVL)
    const TN = { vbr: [20.0, 22.1], vc: 29.2, ipp: 51.4, at: 0.088e-2 };     // TPSMC18A-VR (DTVSC2, negative leg)
    const rdN = (TN.vc - TN.vbr[1]) / TN.ipp;
    const vbrMin = (t, T) => t.vbr[0] * (1 + t.at * (T - 25));
    const VF_TVS = 0.5;                                                     // forward drop of the series leg near the knee (≈ 1 mA)
    const kneeCard = vbrMin(TV, 18) + VF_TVS, kneePwr = vbrMin(TV, 18), kneeCold = vbrMin(TV, -40);
    // whole-inverter LV load: flybacks 2 × 4.23 W and boost 6.45 W at full gate load (the LV-budget rows), card 5.0 W (S32K396 V11
    // 1.47 A max at 125 °C, DS Table 14, through the FS26 bucks, + the V5A/V3B/VREF loads), ULDOEX 0.15 A + UGDL 0.05 A linear;
    // once V12L passes the boost setpoint the V15 load (0.356 A) is linear too
    const P_CARD = 5.0, P_FLY = 2 * (3 * 1.0 + 0.3) / 0.78, VB15 = P.boost.vref * (1 + 110 / 9.53), I15 = 4 * (1 / 0.75) / 15, P_B15 = VB15 * I15 / 0.85;
    const iLV = (v) => (P_CARD + P_FLY) / v + 0.15 + 0.05 + (v < VB15 + 0.8 ? P_B15 / v : I15);
    const DROP_CARD = 0.35 + 0.05, DROP_V12 = DROP_CARD + 0.03 + 0.4, DROP_V15B = DROP_V12 + 0.4;   // DREVC + FLVC · + FVBL/FL1 + DRL · + DB15
    const vNrcLD = 35 - DROP_CARD, vV12LD = 35 - DROP_V12, vV15BLD = 35 - DROP_V15B;
    const ldOpen = (t, us, td = 0.4) => 14 + (us - 14) * (t < 0.01 ? t / 0.01 : Math.exp(-(t - 0.01) / (td / LN10)));
    const tPlat = 0.01 + (0.4 / LN10) * Math.log((101 - 14) / (35 - 14));
    // (1) the absorb alternative, kept as the reason for the change (INFO)
    const absorb = (vbr, rd, ri, model, us) => { let ipk = 0, e = 0;
      for (let t = 0; t < 1.2; t += 1e-4) { const vo = ldOpen(t, us), vs = model === "T" ? Math.min(vo, 35) : vo, i = Math.max(0, (vs - vbr) / (ri + rd)); ipk = Math.max(ipk, i); e += i * (vbr + rd * i) * 1e-4; }
      return [ipk, e]; };
    const tab = (vbr, rd) => [0.5, 2, 4].map((ri) => { const [iT, eT] = absorb(vbr, rd, ri, "T", 101), [iP79] = absorb(vbr, rd, ri, "P", 79), [iP101, eP] = absorb(vbr, rd, ri, "P", 101);
      return `Ri ${ri} Ω: ${f(iT, 1)} A / ${f(eT, 0)} J (35 V behind Ri) · ${f(iP79, 0)}–${f(iP101, 0)} A / ≤ ${f(eP, 0)} J (clamp in parallel)`; }).join(" · ");
    add("LV A.16", "Test B by ABSORPTION — why the entry changed (24 V stand-off TVS, Us 79/101 V, td 400 ms)",
      `TPSMC24CA-VR (SMC): ${tab(26.7, (38.9 - 29.5) / 38.6)} ‖ SM8S24A (DO-218AB): ${tab(26.7, (38.9 - 29.5) / 170)}`,
      "SMC: no rating beyond 1 ms (1.5 kW at 1 ms extrapolates to ≈ 112 W at 400 ms); SM8S24A 50 A for 10 × 400 ms (Vishay load-dump table)", "INFO",
      `round 17 (F189): the result hinges on how the generator makes Us* — a 35 V source behind Ri, or the unsuppressed 79–101 V source with the 35 V clamp in parallel (the alternator physics, and the parameter set ISO 16750-2 gives for test B). In the second model any local clamp below 35 V takes the full unsuppressed current: the SM8S24A holds only for Ri ≳ 1.5 Ω, the SMC part at no Ri. Let-through removes the dependence; the rows below carry the ${f(tPlat * 1e3, 0)} ms plateau of Us 101 V / td 400 ms`);
    // (2) the knee and what the plateau does to the load and the parts on it
    judge("LV A.16", "Test B: KL30 pin-side TVS knee (DTVSC V_BR,min at 18 °C + DTVSC2 forward) vs Us* + 1 V", `${f(kneeCard, 2)} V`, "≥ 36 V (Us* 35 V + generator tolerance; ISO 16750-2 test B at RT)", 36 / kneeCard, 0.99,
      `the TVS draws 0 A at every Ri 0.5–4 Ω in both generator models, so no Ri is required of the OEM (IR-03). At −40 °C the knee is ${f(kneeCold + VF_TVS, 1)} V (a cold central clamp also sits lower); the old TPSMC24CA-VR knee was 26.7 V`);
    const iPl = iLV(vNrcLD), iL = P_FLY / 2 / vV12LD + I15 + 0.05, iH = P_FLY / 2 / vV12LD, fuseRer = 5 * 0.98 * 0.8;
    judge("LV A.16", "Test B: inverter LV current at the 35 V plateau vs FLVC and the chain polyfuses", `${f(iPl, 2)} A (L chain ${f(iL, 2)} A · H chain ${f(iH, 2)} A)`,
      `FLVC ${f(fuseRer, 2)} A rerated (5 A × 0.98 at 85 °C × 0.8); FVBx/FHx 1.50 A hold at 85 °C`, Math.max(iPl / fuseRer, iL / 1.5), 0.8,
      "nothing trips during the pulse, so no fuse or polyfuse V_max applies (the 24 V polyfuse V_max mattered only because the old TVS drew the dump through it)");
    judge("LV A.16", "Test B: FS26 VSUP / VSUP_PWR at the 35 V plateau", `${f(vNrcLD, 2)} V`, "36 V — FS26 High Voltage Extended Operation, full function for a limited time (DS Rev.3 Fig. 8); 40 V abs", vNrcLD / 36, 0.95,
      "the datasheet names load dump as the use case but gives no duration for its limited period; the design needs 5 × ≤ 0.4 s — VR-29 (NXP). VSUPOV_I latches at 19.3–20.7 V: an interrupt, not a fault reaction");
    judge("LV A.16", "Test B: TPS55340-Q1 VIN / EN at the 35 V plateau (V12L)", `${f(vV12LD, 2)} V`, `${P.boost.vinRec} V recommended / ${P.boost.vinAbs} V abs (SLVSBV5C §6.3/§6.1)`, vV12LD / P.boost.vinRec, 0.95,
      "in pass-through (V12L above the 15.4 V setpoint) SW ≈ VIN; the DS pin table still reads VIN 2.9–32 V against the 38 V table — VR-30 (TI)");
    judge("LV A.16", "Test B: DB15 (SS34) reverse when the boost restarts at the end of the dump", `${f(vV15BLD, 2)} V`, "40 V V_RRM (SS34)", vV15BLD / 40, 0.9,
      "V15B holds the plateau value on CB15O while V12L falls back; the switch closing puts it across the diode");
    // (3) linear-regulator junctions through the plateau: superposition of the onsemi single-pulse R(t) (NCV4276C DS Fig. 32
    // DPAK-5 / Fig. 33 D2PAK-5, 736/788 mm² curves read at the decades) over min(test-A profile, 35 V) at Us 101 V, td 400 ms
    const zt = (tz, t) => { if (t <= tz[0][0]) return tz[0][1] * t / tz[0][0];
      for (let j = 1; j < tz.length; j++) if (t <= tz[j][0]) { const [t0, z0] = tz[j - 1], [t1, z1] = tz[j], a = Math.log(t / t0) / Math.log(t1 / t0); return Math.exp(Math.log(z0) + a * Math.log(z1 / z0)); }
      return tz[tz.length - 1][1]; };
    const Z_DPAK = [[1e-3, 3.2], [1e-2, 6.5], [0.1, 8.5], [0.3, 9.5], [1, 11], [3, 13.5], [10, 17], [100, 45], [1000, 58.5]];
    const Z_D2PAK = [[1e-3, 3.0], [1e-2, 5.8], [0.1, 6.8], [0.3, 7.5], [1, 8.5], [10, 13], [100, 35], [1000, 43.3]];
    const dTj = (pAt, tz) => { const dt = 2e-3, n = 750, ps = Array.from({ length: n }, (_, j) => pAt(j * dt)); let best = 0;
      for (let k = 0; k < n; k++) { let s = 0, prev = 0; for (let j = 0; j <= k; j++) { const dp = ps[j] - prev; prev = ps[j]; if (dp) s += dp * zt(tz, (k - j + 1) * dt); } best = Math.max(best, s); }
      return best; };
    const rail = (t, drop) => Math.min(ldOpen(t, 101), 35) - drop;
    const p15 = (t) => Math.max(0, rail(t, DROP_V15B) - 15.0) * I15, p0 = (VB15 - 15.0) * I15;
    const dD2 = dTj(p15, Z_D2PAK), dDP = dTj(p15, Z_DPAK), tj = 85 + p0 * 43.3 + dD2, tjDpak = 85 + p0 * 58.5 + dDP;
    judge("LV A.16", "Test B: ULDO15 junction, D2PAK-5 (85 °C ambient, V15 0.356 A, Us 101 V / td 400 ms)", `${f(tj, 0)} °C (+${f(dD2, 0)} K from ${f(p15(0.05), 1)} W)`, "150 °C (NCV4276C Tj max; TSD 150–210 °C)", tj / 150, 0.95,
      `F189: in DPAK-5 the same pulse reaches ${f(tjDpak, 0)} °C — over the 150 °C maximum, so ULDO15 moves to NCV4276CDSADJR4G. The single-pulse curves are read from the DS figures and the V15 load is the conservative 4 × 1 W class figure; the thermal first article measures it at 35 V`);
    const pEx = (t) => Math.max(0, rail(t, DROP_CARD) - 12.1) * 0.06, pGd = (t) => Math.max(0, rail(t, DROP_V12) - 5.0) * 0.05;
    const tjEx = 85 + (13.5 - DROP_CARD - 12.1) * 0.06 * 58.5 + dTj(pEx, Z_DPAK), tjGd = 85 + (13.5 - DROP_V12 - 5.0) * 0.05 * 58.5 + dTj(pGd, Z_DPAK);
    judge("LV A.16", "Test B: ULDOEX / UGDL junction (DPAK-5, 85 °C ambient)", `${f(tjEx, 0)} °C (${f(pEx(0.05), 1)} W) / ${f(tjGd, 0)} °C (${f(pGd(0.05), 1)} W)`, "150 °C", Math.max(tjEx, tjGd) / 150, 0.9,
      "ULDOEX at the IR-13 bound (≤ 42 mA rms excitation + ALM2402 quiescent ≈ 60 mA); UGDL at the 50 mA V5GD budget; steady part on the 58.5 K/W pad");
    // (4) fast pulses on the let-through entry
    const p2a = (us, c, tdTo) => { const a = (tdTo === 10 ? LN10 : Math.LN2) / 50e-6, b = 1 / (2 * c), ts = Math.log(b / a) / (b - a);
      return 13.5 + us * b / (b - a) * (Math.exp(-a * ts) - Math.exp(-b * ts)); };
    const cEff = 100e-6 * 0.8 + 12e-6, cMl = 12e-6;                  // CLVC3 at −20 % + card MLCCs at bias (QLVS off: power-board caps not counted)
    const [pk50, pk10, pkNo] = [p2a(112, cEff, 2), p2a(112, cEff, 10), p2a(112, cMl, 2)];
    judge("LV A.16", "Pulse 2a (+112 V / 2 Ω, 50 µs) on NRC — closed form ΔV = Us·b/(b−a)·(e^(−a·t*) − e^(−b·t*)), t* = ln(b/a)/(b−a)",
      `${f(pk50, 1)} V (td read to 50 %) · ${f(pk10, 1)} V (td to 10 %); C_eff ${f(cEff * 1e6, 0)} µF`, `40 V (FS26 VSUP, TPS55340-Q1 VIN abs); TVS knee ${f(kneeCard, 2)} V`, pk50 / 40, 0.95,
      `a = ln10 or ln2 over td, b = 1/(Ri·C_eff); C_eff = CLVC3 at −20 % + the card MLCCs at bias, QLVS off (the power board's ≈ 14 µF adds when it is on). Without CLVC3 the same pulse reaches ${f(pkNo, 0)} V — the bead and MLCCs do not attenuate a 50 µs pulse. Us = +112 V is the 2011 upper level quoted from secondary sources (2004 levels +37/+50 V, TIDUC41) — IR-38`);
    const i1 = (150 - TN.vbr[1] - 1.0) / (10 + rdN), vNeg1 = TN.vbr[1] + rdN * i1 + 1.0;
    let e1 = 0; for (let t = 0; t < 4e-3; t += 1e-6) { const i = Math.max(0, (150 * Math.exp(-t / (2e-3 / LN10)) - TN.vbr[1] - 1.0) / (10 + rdN)); e1 += i * (TN.vbr[1] + rdN * i + 1.0) * 1e-6; }
    const i3 = (220 - TN.vbr[1] - 1.0) / (50 + rdN), vNeg3 = TN.vbr[1] + rdN * i3 + 1.0;
    const pOld = (13.5 + 150 - 70) / 10 * 70;
    judge("LV A.16", "Pulse 1 (−150 V / 10 Ω, 2 ms) — reverse voltage on DREVC behind the pin-side pair", `${f(13.5 + vNeg1, 1)} V (FCO at −${f(vNeg1, 1)} V, ${f(i1, 1)} A)`, "60 V V_RRM (STPS5L60S)", (13.5 + vNeg1) / 60, 0.8,
      `F185: V_BR,max(DTVSC2) + R_d·I + V_F(DTVSC) with NRC still at 13.5 V; ${f(e1, 2)} J in DTVSC2 (${f(vNeg1 * i1, 0)} W peak vs 1.5 kW at 10/1000 µs). Before, the TVS sat behind DREVC: the Schottky avalanched at ≈ ${f(pOld, 0)} W against P_ARM 144 W (10 µs, 125 °C; ≈ 5.8 W at 1 ms, DS Fig. 3). A single 33 V bidirectional part at the pin would leave 58.8 V (0.98)`);
    judge("LV A.16", "Pulse 3a (−220 V / 50 Ω) — reverse voltage on DREVC", `${f(13.5 + vNeg3, 1)} V (${f(i3, 1)} A)`, "60 V V_RRM", (13.5 + vNeg3) / 60, 0.8, "the 150 ns pulses of 3a/3b move the NRC bulk by millivolts");
    judge("LV A.16", "Reverse battery −14 V / 60 s vs the negative-leg knee (DTVSC2 at −40 °C)", `14 V vs ${f(vbrMin(TN, -40), 2)} V`, "dark (no conduction)", 14 / vbrMin(TN, -40), 0.9,
      "DREVC blocks the rest; KL15 is blocked by DIGN (US1M)");
    judge("LV A.16", "Test B: DTVH/DTVL (TPSMC33CA-VR) dark at the V12 plateau", `${f(vV12LD, 2)} V vs ${f(kneePwr, 2)} V knee (18 °C)`, "below the knee", vV12LD / kneePwr, 0.97,
      "the power-board TVSs sit behind DREVC, the chain polyfuses and DRH/DRL; a 24 V part here would take the dump through FVBx/FHx");
    add("LV A.16", "Jump start 26 V / 60 s (ISO 16750-2:2023, RT and T_min) vs the lowest KL30 TVS knee", `26 V vs ${f(kneeCold, 1)} V at −40 °C (${f(26 / kneeCold * 100, 0)} % of the knee)`, "IR-02 stays 24 V (2012 edition)", "INFO",
      `the 2023 edition raises the jump start to 26 V and adds T_min; the old TPSMC24CA-VR knee was ${f(26.7 * (1 - 0.00092 * 65), 1)} V at −40 °C (it would conduct for the whole minute). Rows sized at 24 V that change at 26 V: flyback start resistor ${f((26 - 10.9) ** 2 / 2.2e3, 3)} W (0.25 W 1206), ULDO15 ${f((26 - 0.5 - 15) * 0.33, 1)} W (TSD survival case), RFS4 with FS1B held (Safety A.9), the exciter single-fault TVS/PTC rows (Sensing A.15)`);
    // (5) FLVC — the fuse that replaces the undersized polyfuse (Bel 0680L5000-05: 5 A slow blow, 36 A²s < 10 ms, ≈ 98 % at 85 °C)
    const i9 = iLV(9 - 0.5), i135 = iLV(13.5 - 0.5);
    judge("LV A.16", "FLVC continuous current at 85 °C: 9 V crank / 13.5 V (whole inverter)", `${f(i9, 2)} A / ${f(i135, 2)} A`, `${f(fuseRer, 2)} A (5 A × 0.98 at 85 °C × 0.80 continuous)`, i9 / fuseRer, 0.8,
      `F187: the MF-LSMF300/24X it replaces holds 1.50 A at 85 °C (${f(i9 / 1.5, 2)}× at 9 V, ${f(i135 / 1.5, 2)}× at 13.5 V) — the old row compared one chain with the 23 °C hold. Load model: flybacks 2 × 4.23 W, boost 6.45 W, card 5.0 W constant-power + 0.2 A linear`);
    const cIn = (4.7 + 22 + 100) * 1.1e-6, i2tPlug = 16 ** 2 * cIn / (2 * 0.03), i2t2a = ((125.5 - 20) / 2) ** 2 * (50e-6 / LN10) / 2, i2t1 = i1 ** 2 * (2e-3 / LN10) / 2;
    judge("LV A.16", "FLVC I²t: KL30 hot-plug into CLVC1–3 (16 V, 30 mΩ loop) / pulse 2a / pulse 1", `${f(i2tPlug, 2)} / ${f(i2t2a, 3)} / ${f(i2t1, 3)} A²s`, "36 A²s melting (< 10 ms, Bel 0680L5000)", i2tPlug / 36, 0.2,
      "each ≤ 2 % of melting (pulse 2a repeats 500×: 0.1 % per hit); a hard short behind the fuse (≥ 90 A at 13.5 V through 0.15 Ω) is > 18 × I_n: open in 0.01–0.1 s, inside the 500 A / 75 V DC interrupting rating");
    // (6) KL15: the WAKE1 path behind the blocking diode
    judge("LV A.16", "KL15 pulse 1 (−150 V / 10 Ω): DIGN reverse and the WAKE1 pin", `${f(150 + 13.5, 0)} V on DIGN (US1M) · WAKE1 ≥ 0 V`, "1000 V (US1M); WAKE1 −0.3 V abs min, −5 mA reverse DC (FS26 Table 5)", (150 + 13.5) / 1000, 0.8,
      `F188: RIGN1 now hangs behind DIGN, so neither pulse 1 nor a reversed KL15 reaches WAKE1 — before, 5.1 k straight from KL15 took ≈ ${f(150 / 5.1e3 * 1e3, 0)} mA at pulse 1 (${f(150 / 5.1e3 / 5e-3, 1)}× the −5 mA rating) and ${f((14 - 0.7) / 5.1e3 * 1e3, 1)} mA at a reversed battery. The added diode drop leaves WAKE1 at ${f((6 - 0.6) * 10 / 15.1, 2)} V for a 6 V KL15 (V_IH 3.5 V at the high-threshold OTP, 2.0 V at the low)`);
  }
  // R1-F06/R2-F05: the IGBT SC rating is a test condition, not a corner guarantee
  add("Power stage — all SKUs", "IGBT short-circuit rating condition vs the design corner", "6 µs at 800 V / 15 V / 175 °C (hiitio DS)", "design corner 850 V / VCC2 up to 16.7 V", "WARN",
    "round 12 (R1-F06, R2-F05): the RR04 release gate (contained SC test) now also asks hiitio for the SC statement at 850 V and the 16.7 V gate-rail corner, or the gate rail's upper corner is tightened; the stale \"inside 6 µs\" BOM text was removed");
}

// ============ 8. SAFETY CHAIN TIMING/LOGIC ============
{
  add("Safety", "FS0B → driver EN path", "2 gate delays (~20 ns) + driver td", "-", "PASS", "no software; erc-verified topology");
  add("Safety", "ASC latch power", "V5A + RASCP default-low", "-", "PASS", "survives MCU reset; FS1B can SET via RFS1");
  add("Safety", "ASC drive path", "VOW3120 + UCC14141-Q1 + 5.1 V clamp, DCN-referenced", "-", "WARN", "DS 1.2 confirms ASC forces OUTH high at a GND2-referenced 0-5 V pin (F28 level fix applied); behaviour DURING VCC2-UVLO is unspecified — NOVOSENSE statement requested (VR-09, docs/vendor-requests.md) and the ASC-hold measurement with SBC-held gate power is a procedure in docs/qualification-plan.md");
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

## Findings log (F1–F36 rev A.3 campaign · F37–F46 rev A.4 · F47–F51 rev A.4.1 · F52–F57 rev A.4.2 · F58–F59 rev A.4.3 · F60–F62 rev A.5 docs audit · F63–F76 rev A.6 external review round 6 · F77–F89 rev A.7 review round 7 · F90–F97 rev A.8 review round 8 · F98–F105 its cross-check · F106–F113 rev A.9 review round 9 · F114–F119 its cross-check · F120–F122 rev A.10 schematic rechecks · F123–F134 rev A.11 system review · F135–F142 rev A.12 rechecks, pin freeze and production closure · F143–F158 rev A.13 round 14 · F159–F166 rev A.14 round 15 · F167–F173 rev A.15 round 16 (three rechecks of 32214be) · F174–F184 rev A.16 round 17 (gap closure) · F185–F189 rev A.16 round 17 (LV entry: test-B let-through, pulses 1/2a, FLVC, KL15) · F190–F198 rev A.17 round 18 (rechecks of 4425af9: diode pin numbers, the diversion through RSX, TVS/PTC coordination, RSX, acquisition-time firmware) — all fixed; review cross-reference in [\`review-A6-disposition.md\`](review-A6-disposition.md), [\`review-A7-disposition.md\`](review-A7-disposition.md) and [\`review-A8-disposition.md\`](review-A8-disposition.md))

| # | Severity | Finding | Fix |
|---|---|---|---|
| F90 | **HIGH** | A DESAT during latched ASC did not reach SPO: the faulted NSI6611 holds its own gate off (DS Fig. 8.11), but the healthy low sides stayed on through ASC, and a reset edge with the latch still set re-energised the faulted switch (R7-01/A7-N01) | UASCG: ASC_CMD = latch AND no-FLT on every path (FS1B-ASC included); FW-15 clears the ASC latch before the recovery; §4c transition table |
| F91 | MED | The FLT diode-OR reached the fault-latch preset at 111–135 ns/V (R7-02); buffering it exposed a 1N4148 low level at the Schmitt threshold (N12) | USCH2 74LVC3G17-Q100 on both latch presets; DFLT1/2 BAT46 |
| F92 | MED | Discharge TLP152 driven 470 Ω from the MCU pin: 6.4–6.8 mA vs 7.5 mA I_FLH max; the ASC opto proof used an assumed output drop (R7-03/A7-N03) | QDIS_CMD from USCH2 ch3, RQDL 270 Ω; both LEDs 9.6–14.2 mA from the guaranteed LVC V_OH point and V_F cold |
| F93 | MED | FW-16 boot test masked by RDY with gate power off; single-polarity (R7-04/A7-N02) | sensitized test with gate power up, before precharge, both polarities; coverage stated |
| F94 | MED | FAULT_OUT short to KL30 back-fed V5A through DSET/RENP2 (≈8 mA with V5A off) (A7-N04) | ZSET to ground, RENP2 removed: ≤ 0.6 mA into V5A |
| F95 | LOW | bom-gen wrote per-board CSVs before the semantic checks, so a failed run overwrote good outputs (A7-N05) | validate first, then publish atomically (temp + rename); mutation-tested |
| F96 | LOW | S4 called a 0.8 s junction pole conservative, and its case node delayed plate heating (R7-07, N14) | junction-case and case-plate static (an upper bound at every instant); +3 °C |
| F97 | LOW | Contract said the NSI6611 reset needs EN low ≥ t_FLT_MUTE; the DS (Fig. 8.8) has a mute time from the fault plus a ≥ t_RST_FIL pulse after it (N13) | §7 corrected; FW-15 unchanged |
| F98 | MED | Contract (cross-check R8X-01/02/03): FW-15 could leave the ASC latch set, so ASC returned at the reset edge with PWM held low (LS on with IN+ low — DESAT undocumented); FW-16 relied on sequence position, not measured V_DC/speed, for its HV-free step a; FW-16 d/e could not drop RDY while FS_GPIO1 held the flyback OR, and the FS_GPIO1 input was never tested | FW-15 always clears ASC (re-entry only through §4c); FW-16 measured preconditions, VCU precharge handshake, stored-pass arming; d/e drive FS_GPIO1 low, new g tests the OR; new h injects FLT (latch + mask covered in the field); FS1B_TDELAY = 0 required |
| F99 | MED | Dead-state gaps (R8X-06 + self-found): a Hi-Z USCH2 floated the fault-latch preset and the ASC mask — FW-16 passed with both gone; behind UASCG a Hi-Z ULAT floated the gate input (round 8 had moved the ASC pull-down off the latch) | RFCB 100 k on FLT_CMB_B (dead USCH2 = latched FLT: SPO, ASC masked, FW-16 b fails); RASCP moved to ASC_Q (dead ULAT = no ASC) |
| F100 | MED | QDIS_CMD sat next to V15 on the 4-way discharge header: a pin short put 15 V into USCH2 — which also carries both latch presets — and back into V5A (R8X-07) | header V15-GND-CMD-GND on both boards, locked by pin number |
| F101 | LOW | TLP152 LEDs 9.9 mA at the consistent cold corner, under the 10 mA recommended I_F; the model paired the 125 °C output resistance with the −40 °C V_F and omitted the far-end pulldown (R8X-04) | RASCL = RQDL = 261 Ω 1 %: 10.3–14.8 mA, judged against 10–15 mA |
| F102 | LOW | ZSET C5V6 "≤ 6.0 V" held only at 25 °C/5 mA: 6.62 V at a 35 V load dump on a KL30-shorted FAULT_OUT at 125 °C, over the USCH2 6.5 V abs max (R8X-05) | BZT52-B5V6 (±2 %): ≤ 6.34 V. C5V1 rejected — its soft knee through RFS2 would sag the released level toward V_T+ |
| F103 | LOW | FLT_LS_N had no glitch filter (CFLTF only on FLT_HS_N); a glitch now drops ASC and latches SPO with no automatic retry (R8X-12) | CFLTF2 100 pF |
| F104 | LOW | Lock-ins missed the FLT diode-OR, the FS1B back-feed by net and all of parts-db — a moved DFLT2 anode and an added FS1B pull-up both passed 854/0; two A.8 rows were fixed PASS (R8X-08) | ERC by net, pin number and first-match MPN per SKU; both rows computed; every new lock-in mutation-tested |
| F105 | LOW | S4 "true upper bound" held only for the assumed 60 s plate pole; VCC2 used 50 % FB-sense conduction on every corner; UASCG delay 4.4 ns (R8X-11/16) | static-plate bound reported (8XX IGBT 142 °C, WARN); per-corner conduction 1/0.5/0.2; 5.5 ns |
| F106 | MED | PSASC/PSQD are QA01C-18 (+18/−3 V), but F61 had bound the base QA01C sheet (+20/−4 V): the ASC timing assumed 18–24 V and the discharge gate ≈19.5 V (A8-N01) | QA01C-18 envelope 16.9–20.9 V modelled (Mornsun Fig. 1, line, tempco): ASC entry 7.07 → 7.52 µs, FW-06 end-point 905 → 906 V; MPN locked |
| F107 | MED | QDIS gate followed the bias rail through 47 Ω: up to 20.9 V against the +18 V recommended / +22 V abs, with the < 10 % load region extrapolated (A8-N02) | RQDG 1.5 k with RQDPD 10 k: V_GS 13.3–18.2 V with 1 % resistors (a divider, not a clamp — F122) |
| F108 | MED | FLT/RDY pulled up to V5A while the drivers' VCC1 is V5GD: 0.2 V over the FLT/RDY abs max (VCC1, no +0.3 V) at rail corners, and an undefined V5GD-loss state (A8-01; P-03, R8X-10) | pull-ups and the diode-OR pull-up on V5GD via harness pin 39 (DGND/HW_ID neighbours), RV5GP for an open pin: a lost V5GD is SPO, deterministically |
| F109 | MED | FW-16 allowed its ASC-actuating steps below 60 V: 0.62 J (8XX) / 1.54 J (4XX) at C_max into a latent high-side short (A8-N03) | eligibility < 12 V read (≤ 30/75 mJ), QDIS top-up from ≤ 60 V, n_ss from E_LL,pk |
| F110 | MED | Self-found (N17): the power board's LV side — V15 boost and four bias modules, V5GD, flyback controllers — ran on unswitched KL30, ≈150 mA while the vehicle sleeps | card-side P-FET switch (DMP6023LEQ) on V5A: ≤ 1 µA asleep |
| F111 | LOW | RFS4's 0.23 W case was only 16 V; 24/35 V reach 0.48/0.65 W and 0.23 W continues if FS1B is never released; BACKUP_SAFETY_PATH_FS1B = 1 repeated it through RSTB (A8-03) | RFS4 = ROHM ESR03EZPF1001 (0.27 W at 85 °C, ≈1.3 W/5 s); BACKUP_SAFETY_PATH_FS1B = 0 |
| F112 | LOW | The 10–15 mA LED window used Toshiba's typical V_F tempco as if guaranteed (A8-02) | ±28 % tempco band at every corner; the row judges the guaranteed I_FLH and abs-max margins first |
| F113 | LOW | "Battery opens in the SPO window = double fault" assumed independence (A8-G02) | §6 motor/vehicle release rule: energy-safe SPO, proven keep-connected, or the total-KL30-loss backup |
| F114 | MED | Cross-check R9X-01: a dead V5GD unpowers the AMC1311 LV sides, so the receivers sit at their +0.5 V offset — a valid-looking "0 V bus" (850 V could be reported as discharged); the N18 discriminator relied on it | V5GD read on PTB5 (RV5GP/RV5GS 2 × 47 k); V_DC invalid and SPO forced outside 4.75–5.25 V; also catches a back-powered, hovering V5GD (R9X-06) |
| F115 | MED | R9X-02: the drawn 2×20 harness numbers row by row, so VBAT faced ASC_CMD/QDIS_CMD/FLT_LS_N/RDY_HS and V5GD faced VDC1_P; "MICROFIT3-40" does not exist | HARNESS40 re-laid so VBAT and V5GD touch only ground or their own rail in both row-by-row and odd/even numbering (ERC checks both); class MPN until the layout picks the family |
| F116 | MED | R9X-03: ULDOEX (resolver exciter LDO) had INH on VBATC — ≈0.9 mA in FS26 LPOFF; the parking row summed the power board only | INH on V5A; the row now sums every unswitched load (≤ 43 µA at 25 °C) |
| F117 | LOW | R9X-04/10: QLVS turned on through its gate-drain charge (9–31 A into ≈45 µF at each wake), and a hot 2N7002's leakage × 100 k could half-close it while parked | 10 k/4.7 k gate network + 1 k/100 nF drain-gate: ≤ 1.1 A inrush, ≤ 0.5 V leakage drive |
| F118 | LOW | R9X-07/08: FW-16 trusted a 1 V low-voltage V_DC error (≈ ±9 V real); V5GD now follows V5A, so an FS26 restart erased a latched DESAT | read < 3 V or QDIS for 2 τ; FW-15 writes the DESAT to NVM and §9 gates arming on it |
| F119 | LOW | R9X-05/09/11/12/14: QLVS/QLVN G/S label swaps and a V5A–V5GD bridge passed the ERC; release rule (c) claimed the backup could cover the V5GD row; 25 V caps on the 20.9 V bias rails; RFS4 with FS1B held through a jump start unlisted; stale texts | pin-number and no-bridge locks; (c) limited to total KL30 loss; CASC/CQD 50 V; RFS4 held-case row (WARN, accepted); texts fixed |
| F120 | MED | Schematic recheck A9-S01: RDY_HS/RDY_LS (open-drain, 5.1 k to V5GD) drove 74LVC1G11-Q100 AND inputs directly at ≈20–100 ns/V against the 10 ns/V recommended limit; the round-7/8 "state-benign" rejection left the shutdown chain outside the datasheet | USCH3 74LVC3G17-Q100 on both RDY lines (RRDB dead-state pull-down); ERC by pin number |
| F121 | LOW | S9-01: RASCG (2.2 k) carries up to 0.12 W for as long as ASC is held, but was a generic 0603 (0.1 W at 70 °C → 82 mW at 85 °C) | ROHM ESR03EZPF2201 (0.33 W at 70 °C → 0.27 W at 85 °C); 2.2 k kept for the ASC timing |
| F122 | LOW | S9-02 and notes: discharge sheet heading "67.5k bleeder (58 s)"; "V_GS 13.4–18.1 V" read as a hard ceiling; the A9 disposition still quoted the interim pin-39 V5GD pin | 66 k (56 s nom / 65 s worst); 13.3–18.2 V with 1 % resistors ("a divider, not a clamp"); the final harness map quoted |
| F123 | **HIGH** | §6 release rule (a) accepted SPO on back-EMF alone and omitted the stored winding energy the diodes rectify into an isolated link: 1.5·L·I² = 61 J (8XX) / 84 J (4XX) with the 0.35 mH screening motor against 40 J of link headroom (R1-F01, R2-F08) | rule (a) now requires ½·C_min·(U_N² − V_max²) ≥ W_mag + W_emf from the motor's dq model; screening row (WARN): 8XX covers that motor only to 277 A rms, above it rule (b) |
| F124 | **HIGH** | Resolver exciter drawn with the MFB feedback pair swapped (4.7 nF output→summing node, 24 k output→inverting input): first-order 2 kHz roll-off, \|H(10 kHz)\| 0.18, so the S32K39 SWG (≤ 2.30 V pp) would have driven ≈ 0.7 V pp instead of 8 V pp (R2-F04) | textbook MFB 10k/24k/10k with 1.5 nF to ground and 220 pF feedback: f0 17.9 kHz, Q 0.70, \|H\| 1.85 → 7.7 V pp; SWG amplitude is the firmware knob; ERC by topology and value |
| F125 | **HIGH** | VDC-channel bias "MGJ2D150505SC" is not an existing order code, and the MGJ2 family is reinforced only to 150 Vrms (R1-F12, R2-F02/F03) | TI UCC12050 per channel (V_IOWM 1200 Vrms / 1697 VDC, VDE 0884-11 reinforced, the AMC1311's own class) behind its own NCV4276C from V15; 10 µF X7R both sides; ERC by pin |
| F126 | MED | The Hall open-wire pull-down claimed in README/design-basis since A.4 was never drawn (R1-F03) | R⟨ph⟩B0 100 k at each card input: open wire or dead sensor reads 0 V, outside the 0.2–4.8 V validity window now in FW-05 |
| F127 | MED | LEM HC5FW drawn as a 3-pin VCC/OUT/GND symbol; the device has 1 V_ref, 2 V_out, 3 Gnd, 4 U_C and E1–E4 mass pins and no connector (R1-F04) | 8-terminal symbol by DS number (V_ref open, E1–E4 to Gnd); the off-board carrier drawing derives from it; ERC by terminal number |
| F128 | MED | A resolver wire shorted to KL30 (shares the vehicle connector) injected ≈ 23 mA into an SDADC pin through 330 + 120 Ω against the S32K39's 3 mA limit (operating and absolute maximum), and both legs pulled 20 mA into the VMID buffer through the 680 Ω bias (R2-F13, R1-F20) | RSINF/RCOSF and RSIN/RCOS → 10 k: 1.0 / 1.8 / 2.9 mA at 16 / 24 / 35 V, buffer 2.7 mA; caps rescaled (47 p + 100 p differential, 22 p common-mode on both legs — the P-only caps converted CM to DM): corner 47 kHz, −12° on both channels; SWG given its 47 pF load |
| F129 | LOW | VREF5 sat at 3.3 µF nominal, the FS26 upper limit, before tolerance (R2-F11) | CSB5 2.2 µF → 1 µF (0805 X7R 16 V): 2.1 µF nominal, 1.4–2.3 µF effective over tolerance, bias, temperature and aging |
| F130 | **HIGH** | LV-entry coordination (R1-F14, R2-F10, found by the Opus check): the notes described the classic TPSMC24CA (V_BR 22.8–25.2 V), which conducts at the 24 V/60 s jump start and, in an ISO 16750-2 test B at Ri ≤ 2 Ω, takes more than the (extrapolated) 400 ms capability; at 0.5 Ω the polyfuse then trips inside the pulse, sees ≈ 30 V against its 24 V V_max and latches until a KL30 cycle; the archived datasheet was the -VR series | TPSMC24CA-VR bound explicitly (AEC-Q101, 24 V stand-off, V_BR 26.7–29.5 V, same pad and price): dark at 24 V, clamps ≤ 33 V under the boost's 34 V abs max; test B covered on paper at Ri ≥ 4 Ω, the OEM's Ri is gate ㉗ (≤ 2 Ω: TVS pulse test or 35 V-rated rails). Polyfuse kept (the 33 V part holds 1.17 A hot against the 1.19 A worst chain) |
| F131 | LOW | UEXD (ALM2402QPWPRQ1) bought as "HTSSOP16"; the PWP package is 14-pin (R1-F05, R2-F26) | footprint HTSSOP14-PWP; ERC asserts the 14-pin package against the symbol |
| F132 | LOW | Board NTCs drawn as 2-pin headers, bought as 0603 NTCs (R2-F27) | on-board RTAMB/RTHS 0603 (symbol = BOM) |
| F133 | LOW | kicad5-verify exited 0 on an empty page set (R2-F29) | fails unless 4 sheets and ≥ 1500 pins were compared |
| F134 | LOW | Contract and document defects: FW-05 threshold in A rms; §6 merged "contactor open" with "BMS limit 0"; FW-15 "NVM first" ahead of the safe action; FW-02 τ bands overlap; HW_ID "pin 40"; "LQFP-176"; stale "inside 6 µs" IGBT text; ALT field offered M7 rectifiers and a 3-lead TO-247; CAN termination fixed; FW-18 silent on invalid witnesses; S6 double-update unstated (R1-F02/F11/F18/F23/F24/F25/F27, R2-F18/F19/F24/F33/F34/F35) | all corrected: instantaneous ±601/±707 A; rows split; retained-RAM latch, queued NVM; plausibility wording + EOL measurement; pin 2; 289-MAPBGA; RR04 wording; ALT = qualify-before-use; endpoint DNP option; "unknown, never safe"; delay stated |
| F135 | **HIGH** | Round 13 (A11-R01 ×2): rule (a) was fixed but the §6 matrix stayed speed-split — standstill freewheel of the 0.35 mH screening motor at 340 A rms takes an isolated 8XX link to 1089 V from the trip; the KL30 row still cited the back-EMF test and rule (b) applied only at n ≥ n_x | column note: the energy condition applies in both columns; battery-path-lost row at n < n_x uses FW-06 LS-ASC as the energy sink; KL30 row cites rule (a); rule (b) at every point where (a) fails, barred where the premise is the lost battery |
| F136 | MED | Round 13 (A11-R02/R03): the 10 k injection bound assumed a powered 5.7 V clamp — unpowered, 35 V gives 3.42 mA against the 3 mA absolute limit; and the SDADC anti-alias capacitor (C_AAF 180 pF min, Table 38) had been cut to 100 pF | RSINF/RCOSF and RSIN/RCOS 12 k (2.92 mA at 35 V into 0 V, −1 % R; VMID buffer 5.4 mA); 220 pF C0G at the pins; corner 23 kHz, −24° both channels; the channel-matching bound is stated (1.3° from the Z_DIFF corners by the verifier's amplitude-ratio form; the recheck's 1.09° used a phase form — the larger is carried) instead of "ratio-cancelled" |
| F137 | LOW | Round 13 (A11-R05/N01): the verifier's guard counted JSON pages, not distinct assemblies | exact set {power, capbank, disch, card}, no duplicates, ≥ 1500 pins |
| F138 | LOW | Round 13 (A11-R06 / R03): the LV-entry argument used the commercial TPS55340's 34 V absolute maximum (and an older row 45 V) — the fitted -Q1 part is 38 V recommended / 40 V absolute, so the "≥ 42 V boost" premise was false | model, BOM and gate ㉗ corrected; the commercial part marked PROTO ONLY |
| F139 | LOW | Round 13 (R04): the V_DC bias LDO thermal row assumed 40 K/W; the NCV4276C DPAK reference pad is 58.5 K/W and the UCC12050's 50 mA is typical | 0.76 W worst → Tj ≈ 129 °C at 85 °C on the reference pad (WARN); ≥ 1.2 in² 2 oz copper per LDO in dfm §4; measured at the hot first article |
| F140 | **HIGH** | Pin freeze (rev A.12): the A.4 "GEN3-exact" MCU port list was symbolic and partly wrong — PTG10 has no PWM output, PTB0/PTB4/PTB5/PTF5 no ADC, PTB2/PTB3 are external-mux ADDRESS outputs, PTA10 is JTAG_TDO, PTA11–13 do not exist on the 289-MAPBGA | all 289 balls bound from SPF-91122 rev C (anchored on the DS supply balls): PWM_1 A/B pairs on PTC31/PTA6, PTC30/PTA7, PTC29/PTC8 with FAULT0/FAULT2 on PTC26/PTC25; 17 analog inputs on ADC-capable balls with the currents on three instances; SDADC pairs as GEN3; FS26 48 pins verified; ERC by ball |
| F141 | **HIGH** | Barrier parts without a published working voltage (gates ⑪/㉔): the TLP152 optos and the QA01C-18 bias modules on the 850 V link carried UL1577 test voltages only, no V_IORM/V_IOWM | Vishay VOW3120-X017T (V_IORM 1414 Vpk, DIN EN 60747-5-5, CPG ≥ 10 mm) and TI UCC14141-Q1 (reinforced, V_IORM 1414 Vpk / V_IOWM 1000 Vrms, DS §7.5 — certificates listed "planned", gate kept for the status) drawn on both boards in the single-output configuration; LED resistors 270 Ω for the lower V_F; Y-caps bound to Vishay VY1 (Y1 500 VAC / 1500 VDC) |
| F142 | MED | Orderability: the harness connector, the 4XX capacitor can and the discharge resistors were class placeholders ("HARNESS-2x20-CLASS", generic can, generic 10 W) | Samtec IPL1-120-01-L-D-K / IPD1-20-D-K / CC79L crimps (−55…125 °C, 3.8 A/pin, positive latch; no 2.54 mm family states AEC-Q200 — A-Series MPN to confirm, pin numbering to confirm against the print), Faratronic C3D1U506KFAA382, TT SQP10-470RJB15 / -220RJB15 |
| F143 | **CRITICAL** | Round 14 (A12-R01/R02): two MCU supply balls were wrong in the A.12 freeze — H5 (V15, the 1.5 V core input, 2.75 V abs max) carried the 5 V VREF5; J7 (V25, the 2.5 V flash-regulator output) was grounded | H5 → V15S; J7 → V25 + CV25 220 nF; the whole map re-derived against NXP's GEN3 net report (U513, 244 balls): every supply ball and 164/165 port names agree; the four signals on GEN3-open balls moved to netlist-confirmed balls (B5/T15/D5/U4) |
| F144 | **HIGH** | Round 14 (A12-R03): the exported MCU symbol numbered its pins 1…130 with the ball only in the name — not a physical package binding | pages.mjs emits the ball ID as the KiCad pin NUMBER for '<ball>_<signal>' labels; kicad5-verify fails unless every MCU X record's number equals the manifest ball for its label |
| F145 | LOW | Round 14 (A12-R04): the NMOS ballast network lacked NXP's 1 nF gate-stability capacitor (DS Table 13) | CBAL 1 nF C0G, NMOS_CTRL to VSS |
| F146 | **CRITICAL** | Round 14 (A12-R05): the DESAT ISR called br_spo(true), dropping the undelayed MCU_GATE_EN and bypassing the 22–53 µs hardware FLT → DRV_EN delay that protects the driver's soft turn-off | bridge module enforces a DESAT hold (CAL 60 µs ≥ the RC upper corner) for every caller; PWM inhibit stays immediate; regression tests |
| F147 | **HIGH** | Round 14 (A12-R06): now_ms() = us/1000 of a wrapping 32-bit counter — the ms value jumped 4 294 967 → 0 after 71.6 min, so unsigned ages became ≈ 4.29·10⁹ ms (false VCU/BMS staleness) | 64-bit monotonic µs clock in the HAL, ms derived from it (wraps at 2³² ms consistently); every /1000 timestamp producer audited; wrap-straddling tests |
| F148 | **HIGH** | Round 14 (A12-R08): keep_hv was speed-only while §6 credits battery retention at every speed — a low-speed high-current SPO relied on the battery without requesting it (0 rpm, 340 A: rule (a) false, rule (b) true, keep_hv false) | keep_hv derived from actual reliance on rule (b), held until rule (a) holds or ASC is active; propagated to the CAN status; lost-battery rows cannot borrow (b) |
| F149 | MED | Round 14 (A12-R07): the bias-LDO model used 76 mA (50 mA typ × 1.2) — the production UCC12051-Q1 draws 80 mA MAX at no load (96 mA with the AMC1311 → 0.96 W, Tj 141 °C) | R5L 47 Ω ballast per LDO (LDO ≈ 0.5 W, Tj ≈ 116 °C; resistor 0.45 W in a 2512); UCC12051QDVERQ1 promoted to the primary MPN, UCC12050 the proto fit |
| F150 | **CRITICAL** | Round 14 (F01): the target fault routing (IMCR indices / SSS for PTC26/PTC25 → PWM_1 FAULT0/2) was placeholder 0u | placeholders are a build error; the host build reports the route unbound and arming is refused (fail closed) until the RM-derived values are filled and the route validated |
| F151 | **CRITICAL** | Round 14 (F02): hal_pwm_config_locked() returned true from a register readback while REG_PROT/XRDC protection was a TODO — "configured" was presented as "write-protected" | config_matches() and protection_locked() separated; the lock witness reads the lock bits or returns false; arming-evidence record (route bound, config, lock, fault/OVP routes validated by an NVM-stored EOL record) gates gate-enable |
| F152 | **HIGH** | Round 14 (F03/F05): the excitation-monitor dividers (5.1 k) let 5.5–5.7 mA into the SDADC pads at a 35 V wire fault (3 mA limit) and the pair had no C_AAF | 18 k / 42.2 k / 84.5 k (1.96 mA at 35 V, 2.81 mA at 50 V, same ratios) + CEXM 220 pF; 3.0° fixed reference offset absorbed by FW-20 |
| F153 | **HIGH** | Round 14 (F04): the ALM2402 outputs reached the vehicle connector unprotected — a 24/35 V harness fault exceeds its 18 V output rating whatever the supply does | SMCJ8.5CA at each connector node (clamps at 10.6–11.4 V, below the 12.1 V rail + a diode: no reverse current into the ALM2402) + an MF-MSMF020 PTC (0.2 A, 30 V, AEC-Q200) per line; fault currents, TVS energy and amplitude loss in the register; bench gate ㉘ |
| F154 | MED | Round 14 (F18): the motor-temperature lines relied on an ESD-class PESD5V0L1BA and an unbound "350 mA class" fuse against a KL30 short | SMAJ5.0A (400 W) carries the ≈ 16 A for the ≈ 16 µs the Littelfuse 0438.375WRA (0603, 63 V, I²t 0.0041 A²s, AEC-Q200) takes to open; the 1 k series resistor limits the buffer input to 3.9 mA |
| F155 | **HIGH** | Round 14 (F23): torque_to_current() clamped id/iq after the field-weakening voltage calculation without a final feasibility witness — a finite but physically infeasible current request could be returned | final voltage-magnitude check after the clamps; iq (then id) reduced until feasible; an explicit infeasible status → zero torque + speed-limit request + DTC; motor-map sweep test |
| F156 | LOW | Round 14 (F24): the "any single-channel fault is detectable by KCL" claim fails for a stuck-at-zero channel at zero current and for equal gain errors | activity check per channel above a current threshold, coverage table per operating state (firmware docs); the window/range/stale checks kept; no fourth sensor |
| F157 | MED | Round 14 (F09): a shorted QDIS with the battery connected bypasses the software timeout (4 × 96 W in the resistors) and nothing detected it | firmware detects an unexpected discharge at the next contactor opening → latched DTC, no re-energisation, contactor-open request; the resistor's benign failure at 96 W stays gate ㉖ |
| F158 | LOW | Round 14 (N03, F17, F21, §3): stale DTVH/DTVL text (34 V boost, "Ri ≥ 4 Ω covered"), CAN termination population per vehicle, "the paperwork is the cost" wording, an "800 V inverter" claim against a 500–850 V contract, the 4XX-as-220 kW reading | BOM texts corrected; termination marked an end-node population; safety-work wording corrected; README states 500–850 V; variants.md states the 4XX AC-power envelope and the 750 V RFQ question |
| F159 | **CRITICAL** | Round 15 (A13-R01 ×3): the round-14 exciter TVS sat on the CONNECTOR node — an external battery fault fed it 25/46 A (287/555 W) without passing through the PTC; the verifier had modelled the PTC in the path | TVSEP/TVSEN moved to the protected (amplifier-side) node; RSX 2.2 Ω between the amplifier and that node; the monitor taps the protected node; ERC graph-cut check (the connector node carries only JVEH and the PTC) |
| F160 | MED | Round 15 (A13-R02): the "no back-drive" argument assumed VEXD ≈ 12.1 V; with the rail absent/cranking the ALM2402 reverse diode conducts (pulse-rated only), and a negative fault reaches the −0.3 V output limit | rows for the VEXD-absent pulse (RSX-bounded, rail pumped to ≈ 10.8 V ≤ 18 V) and the negative fault; gate ㉘ extended to VEXD off/low/on and both polarities |
| F161 | MED | Round 15 (A13-R03/R06): MF-MSMF020 was bound to a 1206 package (the family is 1812) and the hot hold current was quoted for 60 °C (0.13 A) instead of 85 °C (0.09 A); the unsuffixed part is flagged for new designs | MF-MSMF020/33X (33 V, 40 A, 0.02 s at 8 A) in 1812; 0.09 A at 85 °C against the 40–60 mA excitation |
| F162 | **HIGH** | Round 15 (A13-R04): NTC_A moved to ADC5_S11 but the target ADC map still used the precision-channel class ('P' → PCDR[11] instead of ICDR[11]) | the board-map generator emits the instance/subtype/channel triple and MAP[] uses it for every input; regression test |
| F163 | **CRITICAL** | Round 15 (A13-R05): the shipped KiCad files are the EasyEDA-import variant (library pre-mirrored because that importer ignores the orientation matrix) while native KiCad 5 applies the matrix — read natively, H5 landed on FLT_CLR_M and J7 on ASC_CLR_M; the verifier checked only the import convention | separate 'traction-native/' variant (un-mirrored library, same sheets) and zip; kicad5-verify applies the matrix for the native variant and checks both; a flipped matrix is a detected mutation |
| F164 | **HIGH** | Round 15 (review-3 A13-R02): the contactor-loss detector required n ≥ n_x, so a known low-speed OPEN/INVALID contactor report did not raise the battery-path-lost row, and st_run() kept arm/torque_enable for the same invocation | loss detected at any speed while armed (OPEN/INVALID/stale explicit); ordinary torque permission removed in the same invocation; the §6 policy selects the response |
| F165 | **HIGH** | Round 15 (self-found while deriving the ADC schedule from the ball map): ADC1's injected chain was never started, so MT2_SIG, INTRLOK_N and TMOD_W were never converted on the target; the pre-fix driver also read out of bounds for the standard-class channel | the 1 ms list starts ADC0/3/4/5 normally and ADC1's injected conversions; hal_adc_init() reads the chain masks back and refuses a configuration that does not match the ball map; the FW-06 sample wait includes the injected conversions (measured on the target) |
| F166 | MED | Round 15 (self-found): HW_ID was classified in app_init before any slow conversion had run — on the target a false "HW_ID short", and the board never arms | init_identity() starts the slow list before each sample; TI_FW_ID bumped (0x0A0D000F) — the image needs a new EOL/HIL validation record before it arms |
| F167 | **HIGH** | Round 16 (A14-R04 review 3 / A14-N01): the SWG's low corner (MAXAPP 1.884 V pp) through the round-15 series losses gave 6.34 V pp at the winding — under the 6.5 V pp resolver floor — and the monitor plane (protected node, before the PTC) had been called "what the resolver gets" | exciter MFB 24 k → 28 k (|H| ≈ 2.09): low corner ≈ 7.2 V pp at the winding; FW-10 trim setpoint 7.2 V pp at the monitor plane with the SWG-headroom and −40 °C slew checks; planes documented (winding = monitor × 0.964 cold, × 0.875 post-trip) |
| F168 | MED | Round 16 (A14-R02 ×3): the round-15 back-drive row read one RC time constant (48 µs) as the end of the pulse, ignored CEXD (26.7 µF total) and graded a reverse-diode pulse on the rail voltage; the negative-fault case put 4.4 A through the amplifier's lower diode and 0.9 J into a generic 1206 | exponential model (4.9 A peak, τ 59 µs, 0.19 mJ) marked OPEN (WARN) pending the measured diode envelope; **TVS changed to the unidirectional SMCJ8.5A** — a negative fault is carried by its forward diode (I_FSM 200 A) and the amplifier diode sees < 0.3 A; RSX bound to ERJ-8ENF2R20V with its pulse stress stated |
| F169 | MED | Round 16 (A14-R03 review 1 / R05 review 3): the TVS-energy PASS used a 20 ms allowance (400 W) extrapolated beyond the SMCJ curve (ends at 10 ms) and treated the Bourns 8 A / 20 ms point as a universal clearing bound; the 0.5 Ω source impedance is unallocated (0.1 Ω → 50 A > the PTC's 40 A) | rows conditional on the measured clearing time (5.5 J at 10 ms supported), a source-impedance allocation row (≥ 0.27 Ω at 35 V) — WARN, gate ㉘ |
| F170 | LOW | Round 16 (A14-R01/R03 ×2): the /33X PTC's hot hold current is 0.07 A at 85 °C, not the unsuffixed part's 0.09 A the A.14 text carried | corrected (BOM, EXTRACTED §31); judged against 35 mA nominal / 60 mA assumed (WARN: measured with the selected resolver) |
| F171 | **HIGH** | Round 16 (A14-R04 review 1 / R03 review 2 / R01 review 3): hal_adc_read_phase() writes the timestamp only on a complete triplet while sense_fast() ignored the return and passed an uninitialised timestamp with zero-filled channels into isns_update() | deterministic HAL contract (explicit invalid result, outputs untouched); the caller consumes only complete triplets and otherwise marks the current invalid through the sensor-failure path while V_DC and resolver acquisition keep running; tests for every missing-channel combination |
| F172 | **CRITICAL** | Round 16 (A14-R01 review 2): resolver validity did not expire when new blocks stopped — the gap check ran only when a later block was consumed (valid after 1 s of silence) | per-tick age check of the last accepted coherent frame with a bounded hold (CAL from the angle-error envelope), then angle invalidated and the resolver-invalid safe state dispatched; tests with delivery stopped at standstill and rotating |
| F173 | **CRITICAL** | Round 16 (A14-R02 review 2): one SIN-DMA heartbeat tagged all three SDADC channels fresh — a frozen EXC/COS buffer read as new and a completion between the reads let a mixed-generation tuple through | per-channel completion handshake, one coherent frame (EXC + SIN + COS + epoch) copied atomically with the generation checked across the copy, partial/overrun detection; incoherent frames feed the age policy, never a fresh stamp; host DMA model per channel |
| F174 | MED | Round 17 (gap closure): the exciter back-drive with VEXD absent stayed OPEN because the charge into the 26.7 µF rail ran through the ALM2402 reverse diodes (pulsed use only, no envelope) | DEXP/DEXN Nexperia PMEG4050EP-Q (AEC-Q101, I_FSM 70 A, V_F 0.49 V at 5 A) from each protected node to VEXD carry the 4.9 A / 59 µs / 0.29 mC exponential; NCV4276C output abs max 40 V covers the back-fed rail — row PASS with a rated path |
| F175 | MED | Round 17: the exciter TVS energy row was conditional and the source-impedance row asked the OEM for ≥ 0.27 Ω because a single fault (≤ 24 V) and a load-dump-coincident 35 V case were judged together | rows split: single fault PASS at any source impedance (3.4 J at the 20 ms bound, 37 A ≤ 40 A); TVSEP/TVSEN upgraded to SMDJ8.5A-HRA (3 kW, AEC-Q101, same SMC pad, ≈ 9 J at 10 ms) so the 35 V energy is inside the curve too; the 35 V PTC current is the documented double event IR-16/IR-33 |
| F176 | LOW | Round 17: the PTC hold row judged 70 mA against a "60 mA assumed maximum" that had no source | resolver interface requirement IR-13 (Z_primary ≥ 60 Ω at 10 kHz) bounds the excitation at 41 mA rms — 58 % of the hot hold, PASS |
| F177 | LOW | Round 17: the "resolver drive at 9 V KL30 ≈ 6.5 V pp" WARN was a stale A.4.3 model — the outputs have swung ±1.91 V pk around the 2.5 V AFE mid-rail since round 12 | recomputed: the rail only has to clear 4.6 V (VEXD ≈ 8.3 V at 9 V KL30) — PASS; the slew ceiling is the binding limit |
| F178 | MED | Round 17: gate ㉖ asked the bench to prove the discharge resistors do not flame when QDIS sticks ON, although the bound TT/Welwyn SQP sheet states it ("will not burn or emit incandescent particles under any condition of applied temperature or overload") | both stuck-ON rows PASS on the statement; the Yageo alternate (flameproof case only) is constrained in the BOM; VR-28 asks for the qualification data; the fail-open time becomes a QP characterisation |
| F179 | LOW | Round 17: RFS4 (0603 ESR03, 0.33 W) ran at 1.47× its nameplate for the 24 V / 60 s jump start with FS1B held | RFS4 → ESR18EZPF1001 (1206, 0.5 W at 70 °C, AEC-Q200; ROHM ESR series Rev.012 archived) — 0.96× for 60 s, element ≈ 107 °C, PASS |
| F180 | LOW | Round 17: the hall ratiometric-reference drift row named "calibrate at EOL or move VREFH" without a bound | EOL gain calibration (FW-20) plus the ±5 % torque-accuracy allocation IR-26 bound it — PASS; VREFH stays on VREF5 (GEN3 parity) |
| F181 | **HIGH** | Round 17: the KiCad hand-off was verified only by our placement rule. The installed KiCad 10.0.6 loads the KiCad 5 legacy sheets but resolves no symbols (sym-lib-table, cache library and project file tried), so KiCad 9/10 cannot netlist the legacy set at all | sym-lib-table + cache library added to both legacy folders/zips (KiCad 5–8 open them without the remap dialog); a KiCad 9/10 format set (kicad/traction, embedded symbols + project library) converted from the native sheets and proved by kicad-cli netlist export against the built netlist on every board, with mutation tests (mirrored MCU, ball swap, harness numbering, duplicate reference) |
| F182 | LOW | Round 17 datasheet round: the JIC/JICC note asked to confirm interleaved pin numbering; the BOM had TDK "AT000" beads as automotive alternates and the Murata BLM31 sheet was missing; the plain SMDJ has no AEC-Q101 statement | Samtec drawing: numbering is sequential per row (row A 1–20, row B 21–40); TDK's automotive MPZ2012 catalog has no 120 Ω part (220 Ω alternate confirmed, 120 Ω has none); BLM31PG121SH1L sheet archived (automotive application code, no literal AEC-Q200 line — VR-14); the -HRA suffix is the AEC-Q101 SMDJ; DMP6023LEQ still "Advance Information" (VR-20) |
| F183 | MED | Round 17 (found by the KiCad-10 proof): the legacy KiCad 5 sets numbered the JIC/JICC harness pins P1–P40 — the MCU ball rule in pages.mjs matched the harness labels P1_…P40_ — against pads 1–40 (netlist unaffected, symbol pin numbers wrong) | the ball rule applies to UMCU only; all sets and PDFs regenerated; the modern set takes pin numbers from the built netlist and the verifier detects the old numbering as a mutation (62 failures) |
| F184 | LOW | Round 17 (found by the KiCad-10 proof): the legacy generator classed diodes by MPN prefix (SMAJ/SMBJ/SMCJ…) — after the SMDJ8.5A-HRA upgrade TVSEP/TVSEN were drawn as resistors, and the PMEG Schottky needed the same rule | SMDJ, 5.0SMDJ and PMEG classed as diodes in kicad5-gen; regenerated |
| F185 | **HIGH** | Round 17 (LV-entry study): ISO 7637-2 pulse 1 (−75…−150 V, 10 Ω) reached the reverse Schottky DREVC unclamped — the card's TVS sat behind it on NRC, so DREVC avalanched at ≈ 654 W (−150 V) against P_ARM 144 W (10 µs, 125 °C; ≈ 5.8 W at 1 ms); pulse 3a (−220 V / 50 Ω) exceeded P_ARM too. GEN3 places its TVS ahead of the Schottky | pin-side anti-series pair on FCO: DTVSC TPSMC33A-VR (cathode FCO) + DTVSC2 TPSMC18A-VR (cathode DGND) — DREVC sees ≤ 38.3 V at −150 V and 37.1 V at 3a; the −14 V reverse battery leaves DTVSC2 dark (18.9 V knee at −40 °C) |
| F186 | MED | Round 17: ISO 16750-2:2023 raised the jump start to 26 V / 60 s at RT and T_min; the TPSMC24CA-VR knee (26.7 V at 25 °C) is 25.1 V at −40 °C — it would conduct for the whole minute | every TVS on a KL30-derived net is a 33 V stand-off part (knee 34.4 V at −40 °C); IR-02 stays 24 V until the OEM names the 2023 edition; an INFO row lists the 24 V-sized rows that change at 26 V |
| F187 | MED | Round 17: FLVC (MF-LSMF300/24X, 3 A hold at 23 °C) carries the WHOLE inverter — 2.54 A at 9 V, 1.73 A at 13.5 V — against a 1.50 A hold at 85 °C; the verifier compared one chain (1.19 A) with the 23 °C hold | FLVC → Bel 0680L5000-05, 5 A slow-blow 2410 (125 V DC / 100 A and 75 V DC / 500 A interrupting; 36 A²s): 0.65 of its 85 °C / 80 % rerating at 9 V; hot-plug, pulse-1 and pulse-2a I²t each ≤ 2 % of melting; AEC-Q200 report requested (VR-32); the chain-polyfuse row now judges at 85 °C |
| F188 | MED | Round 17: KL15 pulse 1 drove the FS26 WAKE1 pin through RIGN1 (5.1 k straight from KL15): ≈ 29 mA reverse against the −5 mA WAKE rating (−0.3 V abs min), 2.6 mA at a reversed battery; the 1N4148WS steering diode (75 V) would avalanche at −150 V | RIGN1 moved behind DIGN, DIGN → US1M (1000 V, already in the BOM): WAKE1 and IGN_SNS both behind one blocking diode; WAKE1 still reads 3.6 V at a 6 V KL15 |
| F189 | **HIGH** | Round 17 (gate ㉗): ISO 16750-2 test B (35 V / 400 ms, Ri 0.5–4 Ω) could not be shown on paper with the 24 V stand-off TVS at any Ri — no SMC sheet rates a pulse beyond 1 ms — and if the generator is the unsuppressed 79–101 V source with the 35 V clamp in parallel (the parameter set ISO 16750-2 gives for test B), a local clamp below 35 V takes 70–100 A at Ri 0.5 Ω; ULDO15 (DPAK-5) also reached ≈ 150 °C at the old ≈ 33 V clamp | let-through: no TVS on a KL30-derived net below 36.7 V (ERC-locked), CLVC3 100 µF hybrid polymer holds pulse 2a on NRC ≤ 37.5 V, DTVH/DTVL → TPSMC33CA-VR, ULDO15 → NCV4276CDSADJR4G (D2PAK-5, ≈ 140 °C); the FS26 (36 V HV extended operation), TPS55340-Q1 (38 V rec) and NCV4276C (40 V) carry the 35 V plateau; no Ri is required of the OEM (IR-03). Open: the FS26 limited period (VR-29), the TPS55340-Q1 pin text (VR-30), CLVC3 pulse life (VR-31) |
| F190 | MED | Round 18 (A16-R01 md, rechecks of 4425af9): the generic diode cell numbered the ANODE as pin 1 on all 35 two-pin diodes — Nexperia's SOD128/SOD123 pinning tables (PMEG4050EP-Q, PMEG4010EH, BZT52: 1 = K, 2 = A) and KiCad's Device:D / Diode_SMD convention (pad 1 = K) are the opposite, so a standard footprint assigned at layout reversed every diode | shared Diode cell: pin 1 = cathode, pin 2 = anode, port names unchanged (ERC/BOM/glyph by name); ERC lock over every 2-pin diode on every board (mutation-tested); the 15 bound MPNs audited against their sheets (Sonnet, docs/datasheets/, two sheets archived): no part contradicts it |
| F191 | **HIGH** | Round 18 (A16-R02 html/md/csv): the round-17 diversion Schottky DEXP/DEXN sat on the PROTECTED node, so a harness fault with VEXD absent charged the empty rail through PTC → DEX with no RSX in the loop — 27–67 A at 24 V, 39–99 A at 35 V, TVS dark, the ALM2402 diode carrying 0.2–1.1 A — while the row divided by 2.2 Ω (4.9 A / 59 µs) | anode moved to the AMPLIFIER node (in parallel with the ALM2402 upper diode; zero parts): PTC → RSX → DEX is the loop, 4.7–5.0 A / 60 µs / 0.28 mC, 0.15 mJ in the Schottky, TVS clamping meanwhile (PTC 37 A at 24 V/0 Ω); ERC graph cut (no DEX on the protected node); the Schottky judged by I²t (7·10⁻⁴ vs 20 A²s), not peak-vs-peak; comments, QP-RX-05, README ㉘ aligned |
| F192 | MED | Round 18 (A16-R03 html / R04 md): the round-17 closure credited the NCV4276C's V_Q −1…40 V maximum rating as proof that VEXD may be driven above the regulator input — it is a pin-to-ground stress rating next to the operating condition V_I ≥ V_Q + 0.5 V and says nothing about reverse current from OUT toward IN or GND with IN at 0 V, in dropout or inhibited | claim withdrawn from the row, the card and the plan; QP-RX-05 is a RELEASE GATE again (reverse rail current into ULDOEX in the three input states, 0.1 Ω shunt, SMU sweep, 100 pulses + parametrics); VR-33 asks onsemi; the consequence if it conducts (a 1.3–1.6 A DC path through RSX) is stated, the blocking element prescribed only on a failed gate |
| F193 | **HIGH** | Round 18 (A16-R04 html / R03 md, Opus cross-check): the single-fault TVS/PTC rows claimed PASS "at any source impedance" on the 8 A / 20 ms sheet point, which bounds the trip only while the fault current is ≥ 8 A (external ≤ 1.33 Ω at 24 V); the 9 J TVS allowance was the exponential-pulse rating read as rectangular (≈ 5.3 J at 85 °C); the PTC current at 0 Ω used V_BR,max (37 A) where V_BR,min gives 40.7–41.8 A; the negative-fault row divided by the old part's 200 A and never checked the PTC's 40 A; and NO row covered the ECU-asleep sustained short to the normal 12.6–16 V battery, where the tripped PTC's trickle leaves 1.5–3.8 W in the TVS indefinitely | rows recomputed at their worst corners: bounded region PASS (1.7 J at the 8 A point vs 5.3 J; ISO 16750-2 direct short inside), the sub-8 A window and the ECU-asleep trickle as computed WARN rows (unprotected on paper for ≈ 14–83 Ω at 24 V, 6–34 Ω at 16 V; 3.8 W at 12.6 V) with their closure — dfm.md LAYOUT RULE (TVS copper island ≥ 3 cm² with thermal vias, PTC thermally coupled on it), QP-RX-04 sweep with the ECU asleep, IR-42 (short-circuit test condition and harness routing), VR-17 needed again — and the fail-safe end state; IR-16 → 0.05 Ω at ≤ 26 V, 0.29 Ω at 35 V, 0.22 Ω negative; 35 V TVS energy back to the double event; no part change |
| F194 | MED | Round 18 (cross-check): RSX (Panasonic ERJ-8ENF2R20V, 0.25 W) carried the ALM2402's source limit (0.93 A, 1.9 W) during a negative fault with the amplifier powered until its OTF — 122 % of the part's 5 s overload rating — and no row checked RSX power at all | RSXP/RSXN → ROHM ESR18EZPF2R20 (0.5 W, anti-surge, AEC-Q200, same 1206 pad, 1 %; overload 2.0 W for 5 s → 95 %); row added; ERC MPN lock |
| F195 | LOW | Round 18 (cross-check, html amplitude remarks): the gain band "1.95–2.21 over ±10 % caps" swept C1/C2 only (bound parts are ±5 % C0G; QP-RX-01's 1.94–2.2 would reject good ±10 % boards and accept out-of-tolerance ±5 % ones); the hold-current row used 2.6 Ω of PTC that is not on the sheet and judged the setpoint instead of the CAL ceiling; "1.3 Ω cold" PTC and "7.2 V pp at the winding" had no sheet basis | \|H\| 2.076 nominal with the coupling capacitor; bound-tolerance band 1.98–2.18 (±10 % build 1.92–2.24) in the row and QP-RX-01; hold current 41.9 mA at the setpoint / 48.3 mA at the 8.3 V pp CAL ceiling (69 % of the hot hold); planes at the sheet's 0.35 / 5.0 Ω; the full five-corner stack at 60 Ω (6.5 V pp, trim saturates, no arm — EOL catches it) recorded as INFO, REXA4 30.1 k named as the free margin lever |
| F196 | **HIGH** | Round 18 (A16-R01 html, firmware): the current-loop ISR captured now_us at entry, then the TARGET HAL stamped the phase triplet and every V_DC channel with a later hal_time_us(); isns_update()/vdc_update() compared with unsigned subtraction, so a stamp one tick newer than the ISR entry read as 4.29·10⁹ µs old — control lost on fresh data. The host simulation stamped with the frozen sim clock and could not see it | FW-34: the freshness check-time is read after the acquisition reads (ISR-entry time kept for WCET/liveness), a signed-safe freshness helper for sensor stamps that may postdate the check time, a host knob that advances the sim clock inside each HAL read so the suite reproduces the target ordering; tests across the 32-bit wrap; firmware/docs/traceability.md "Round 18" |
| F197 | **HIGH** | Round 18 (A16-R02 csv, firmware): the resolver ring stamped each block from the completion-INTERRUPT's execution time (a callback delayed by up to one carrier period stamped old data up to 100 µs too new — 24° at 10 000 rpm) and inferred blocks-since-last-count modulo the 4-slot ring, so a delay of exactly one lap was indistinguishable from a repeated interrupt | FW-35: cadence-locked stamps (block k at t_origin + (k − k0)·period — the SDADC data rate and the STM timer share one PLL), a servicing-deadline bound cal_sd_irq_lat_max_us before publish, automatic re-acquisition with an information DTC instead of a dead ring; tests: held-off interrupts (accepted/rejected), one lap, one channel lapping, all stalled, reader preempted across a lap, wrap |
| F198 | **HIGH** | Round 18 (A16-R03 csv, firmware): rslv_theta_e_at() SUBTRACTED cal_rslv_latency_us, documented as a positive chain delay (SDADC group delay + filter envelope) — a delayed sample represents an EARLIER instant and needs more extrapolation, not less: −12°/−24° at 10 000 rpm for 25/50 µs; the existing test encoded the wrong sign | FW-36: sign corrected, header/params/timing.md/T-37 wording made unambiguous, the test replaced by an independent physical-time oracle (both directions, two speeds, two delays) |
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

Since round 17 every remaining WARN resolves to a numbered item in one of three documents: the questions to
vendors with acceptance criteria in [vendor-requests.md](vendor-requests.md) (HIITIO module Ls, SiC
short-circuit envelope at 850 V and FIT; NOVOSENSE I_STO distribution, RST/EN during soft-off, ASC during
VCC2 UVLO; TDK VGT12EEM saturation/working insulation; Faratronic 4XX can; Murata BLM31PG121SH1L specification (the MGJ2 line of earlier rounds is stale — superseded by the UCC12051-Q1 since A.11); Bourns,
Littelfuse, Diodes, Samtec, NXP, LEM, the coldplate supplier), the assumptions about the vehicle, motor,
resolver and harness in [interface-requirements.md](interface-requirements.md) (motor data for the
safe-state decision, resolver impedance, the exciter fault double event, central load-dump suppression at Us* ≤ 36 V (no R_i needed since round 17), LV-loss
behaviour) and the executable procedures with pass criteria in [qualification-plan.md](qualification-plan.md)
(double pulse, contained short circuit, thermal, VCC2 six-domain bench, terminal-fault bench ㉘, discharge
㉖, HIL/EOL). Datasheet values used are in docs/datasheets/EXTRACTED-PARAMS.md.

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
