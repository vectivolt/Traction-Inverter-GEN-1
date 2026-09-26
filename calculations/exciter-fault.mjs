// exciter-fault.mjs — the resolver-exciter terminal-fault model shared by the Road verifier (design-verify.mjs
// "Sensing A.13/A.15/A.17") and the Marine port (marine/calc/marine-verify.mjs), round 19 (rev A.18).
// Per excitation line: amplifier node (DEXP/DEXN to VEXD) → RSX 2.2 Ω → protected node (TVS SMDJ7.0A-HRA since round 19 — the
// 8.5A of rounds 17–18 left the parked-trickle case unclosable (xcheck19 §1) — to AGND,
// monitor tap) → PTC MF-MSMF020/33X → connector. One source of truth for the datasheet constants below; every
// number is the worst corner named next to it. Datasheets: docs/datasheets/SMDJ-HRA-series.pdf, MF-MSMF.pdf.
export const TVS = "SMDJ7.0A-HRA";        // round 19 (F203): was SMDJ8.5A-HRA (rounds 17–18)
export const V_RWM = 7.0;
export const VBR = [7.78, 8.60];            // V_BR min/max at 25 °C, I_T 10 mA (SMDJ-HRA sheet p.2)
export const ALPHA_T = 0.058e-2;            // per K (plain SMDJ sheet column)
export const VBR_COLD = VBR[0] * (1 - ALPHA_T * 65);   // 7.49 V: V_BR,min at −40 °C
export const vbrAt = (tj) => VBR[1] * (1 + ALPHA_T * (tj - 25));   // V_BR,max at a junction temperature: 8.90 V at 85 °C, 9.22 V at 150 °C
export const RDYN = (12.0 - 8.60) / 250.0;  // 13.6 mΩ: V_C 12.0 V at I_PP 250 A (10/1000 µs)
export const RDYN8 = 5.5e-3;                // the 8/20 µs row (plain SMDJ sheet) — the current-maximising corner
export const I_R_UA = 200;                  // µA at V_RWM, 25 °C (the 8.5A: 20 µA); ≤ 1.4 % peak dip at 125 °C on the ×10/55 K bound (xcheck19 §1e)
// the 8.5A for comparison rows
export const VBR85 = [9.44, 10.4], VBR85_COLD = 9.04, RDYN85 = (14.4 - 10.4) / 208.3, RDYN85_8 = (18.6 - 10.4) / 1041.5;
export const R_PTC = 0.35;                  // MF-MSMF020/33X R_min (23 °C); R_1max 5.0 Ω an hour after a trip
export const R_PTC_TRIP = 5.0;
export const PD_PTC = 0.8;                  // W the tripped PTC dissipates (23 °C, still air)
export const I_MAX_PTC = 40;                // A, the sheet's I_max
export const I_TRIP = 0.4, I_HOLD = 0.2;    // A at 23 °C (0.07 A hold at 85 °C)
export const T_TRIP_8A = 0.020;             // s — the sheet's ONLY maximum time to trip: at 8 A, 23 °C
export const ECAP25 = 0.65 * 1000 * 0.010;  // J: SMDJ Fig. 2 1.0 kW at the 10 ms end of an EXPONENTIAL pulse → ×0.65 rectangular-equivalent
export const ECAP85 = ECAP25 * 0.808;       // J at T_J(init) 85 °C (Fig. 3)
export const RTH_JA = 75, RTH_JL = 15, TJ_MAX = 150;  // K/W typical on the 8 × 8 mm pads; T_J max
export const R_HARN = 0.5;                  // Ω, the harness assumed in the clamp-voltage rows
export const MARGIN = 0.95;                 // the verifier's PASS threshold on the PTC I_max (5 % margin) — IR-16 minima are computed at it

// fault current through the PTC into the conducting TVS (linear TVS: V_BR + r_dyn·I); the diversion adds 0.1–0.25 A, taken from the TVS
export const iF = (v, rExt = R_HARN, vb = VBR[1], rd = RDYN) => (v - vb) / (rExt + R_PTC + rd);
export const vCl = (v, rExt = R_HARN) => VBR[1] + iF(v, rExt) * RDYN;            // clamp voltage at the protected node
export const iCold = (v, rExt) => iF(v, rExt, VBR_COLD, RDYN8);                   // the current-maximising corner (cold V_BR,min, 8/20 µs r_dyn)
export const rExtMin = (v, iLimit = I_MAX_PTC * MARGIN) => (v - VBR_COLD) / iLimit - R_PTC - RDYN8;   // external resistance that keeps the cold-corner current at iLimit
export const rExtMinNeg = (v, iLimit = I_MAX_PTC * MARGIN) => (v - 1.2) / iLimit - R_PTC;           // negative fault: the TVS forward diode (−1.2 V), no r_dyn term
export const pTrickle = (v, vb = VBR[1]) => vb * Math.min(PD_PTC / (v - vb), I_TRIP);   // TVS power after the PTC has tripped and the source persists — capped at I_trip (a tripped PPTC cannot pass more than its trip current at steady state; xcheck19 §1a)
// the coupled island (dfm.md): the tripped PTC is a thermostat at T_t; the TVS heat it receives through the island replaces its own I·V_P.
// Assumed parameters (xcheck19 §1h): TVS→PTC and PTC→TVS transfer k 40 K/W, TVS junction-to-ambient 55 K/W (R_thJL 15 + island 40), PTC 90 K/W.
export const coupledTj = (vs, ta, tt, vb = VBR[1], k = 40, thTT = 55, thPP = 90) => { const vp = vs - vb; return ta + (tt - ta) * (thTT * vb + k * vp) / (k * vb + thPP * vp); };
export const rExt8 = (v) => (v - VBR[1]) / 8 - R_PTC - RDYN;                     // the fault current is ≥ 8 A (the 20 ms bound applies) while the external resistance is ≤ this
export const pSs = (ta, rth) => (TJ_MAX - ta) / rth;                             // steady-state TVS capability

// the /33X TYPICAL time-to-trip curve (sheet p.8 curve C, digitised: round-18 cross-check); log-log interpolation
export const TTT = [[0.5, 6.16], [0.6, 3.03], [0.8, 1.01], [1, 0.431], [1.5, 0.122], [2, 0.065], [3, 0.030], [4, 0.0177], [5, 0.0119], [8, 0.0053]];
export const tTrip = (i) => {
  if (i < I_TRIP) return Infinity;
  if (i <= TTT[0][0]) return TTT[0][1] * Math.pow(TTT[0][0] / i, 2.5);
  for (let k = 1; k < TTT.length; k++) if (i <= TTT[k][0]) { const [i0, t0] = TTT[k - 1], [i1, t1] = TTT[k]; return Math.exp(Math.log(t0) + (Math.log(t1) - Math.log(t0)) * (Math.log(i) - Math.log(i0)) / (Math.log(i1) - Math.log(i0))); }
  return TTT[TTT.length - 1][1] * Math.pow(TTT[TTT.length - 1][0] / i, 2);   // ASSUMED beyond the sheet's last point (screening only — F200)
};
export const MAX_TYP_8A = T_TRIP_8A / tTrip(8);   // ≈ 3.8: the sheet's maximum over the typical curve at the one point it gives both

// one point of a sustained-short sweep: current, TVS power, typical trip time, energy
export const sweepPoint = (v, r) => { const i = iF(v, r), p = (VBR[1] + i * RDYN) * i, t = tTrip(i); return { r, i, p, t, e: p * t }; };
export const sweep = (v, rs) => rs.map((r) => sweepPoint(v, r));
// the external-resistance window in which the TVS is unprotected on paper (energy over the rectangular 85 °C allowance, or power over the 23 °C steady state)
export const win = (v) => { let lo = null, hi = null; for (let r = 0.5; r <= 200; r += 0.1) { const x = sweepPoint(v, r); const bad = (x.t === Infinity) ? (x.p > pSs(23, RTH_JA)) : (x.e > ECAP85);   /* round 19 (self-found, F202): energy where the PTC trips, steady-state power where it never does — round 18 OR-ed the two and printed 1–200 Ω */ if (bad && lo === null) lo = r; if (bad) hi = r; } return [lo, hi]; };
export const fmtT = (t, f) => (t === Infinity ? "never (< I_trip)" : t >= 1 ? f(t, 1) + " s" : f(t * 1e3, 0) + " ms");
export const tab = (v, rs, f) => sweep(v, rs).map((x) => `${x.r} Ω: ${f(x.i, 2)} A · ${f(x.p, 1)} W · t_typ ${fmtT(x.t, f)} · ${x.e === Infinity ? "∞" : f(x.e, 2) + " J"}`).join(" | ");
