# Hardware → firmware contract (rev A.18)

The hardware protects what software cannot react to in time; firmware owns every operating
limit. This file is the contract between the two for **every SKU of the platform**. Each
review finding dispositioned as *Firmware Handled* (round 6:
[`review-A6-disposition.md`](review-A6-disposition.md), round 7:
[`review-A7-disposition.md`](review-A7-disposition.md), round 8:
[`review-A8-disposition.md`](review-A8-disposition.md), round 9:
[`review-A9-disposition.md`](review-A9-disposition.md), round 10:
[`review-A10-disposition.md`](review-A10-disposition.md), rounds 12–13:
[`review-A11-disposition.md`](review-A11-disposition.md), [`review-A12-disposition.md`](review-A12-disposition.md), round 14:
[`review-A13-disposition.md`](review-A13-disposition.md)) points at a numbered requirement here
(`FW-xx`). The traction application itself is a separate deliverable (review R-F08); nothing in
this repository claims it exists. Verification of each requirement is HIL first, then bench.

## 1. Division of labour

| Fault class | Fast enough only in hardware | Owned by firmware |
|---|---|---|
| Short circuit / shoot-through | NSI6611 DESAT + soft turn-off per switch; global fault latch → DRV_EN (delayed 22–53 µs so it cannot cut a soft turn-off short) | diagnosis, retry limits, FLT_HS vs FLT_LS → safe-state choice (§6); FLT on the eFlexPWM fault inputs (FW-15) |
| Watchdog / MCU hang / reset | FS26 Q&A watchdog → FS0B → DRV_EN low (three-phase open); FS1B → LS ASC latch, entered break-before-make in hardware (§4c); every harness enable/PWM line default-OFF | FS26 configuration and the FS1B policy (FW-12) |
| Gate-power loss | NSI6611 VCC2 UVLO parks the gate low; RDY → DRV_EN | — |
| Phase overcurrent (operating) | — (DESAT sits at ≈2.8× the rated peak) | ADC hardware compare → PWM fault input, ≤ 2 PWM periods (FW-05) |
| DC-link overvoltage (regen / contactor open) | ASC entry once requested: ≤ 7.56 µs, break-before-make (§4c) | ADC hardware compare on both V_DC channels → ASC request ≤ 15.6 µs after the trip crossing (FW-06, round 7) |
| Everything else in §5 | — | firmware |

## 2. SKU identity and parameter sets

- **FW-01** Read `HW_ID` (harness pin 2; card 10 k pull-up to VREF5 against the power board's
  `RHWID`) before arming. Windows at VREF5 = 5.0 V (1 % parts, ±4 % window):

  | SKU | RHWID | V_ID nominal |
  |---|---|---|
  | 4XX IGBT | 2.2 k | 0.90 V |
  | 8XX IGBT | 4.7 k | 1.60 V |
  | 8XX SiC | 10 k | 2.50 V |
  | 4XX SiC | 22 k | 3.44 V |
  | open / short | — | > 4.6 V / < 0.2 V → **fault, no DRV_EN** |

- **FW-02** The loaded parameter set carries its SKU; a mismatch with `HW_ID` refuses
  `MCU_GATE_EN` (an 8XX OV trip on a 600 V 4XX cap bank destroys it). Cross-check at every
  key-off: the active-discharge time constant τ = R·C gives the fitted link capacitance
  (8XX ≈ 0.60 s with 1.88 kΩ, 4XX ≈ 0.71 s with 0.88 kΩ); > 20 % off ⇒ DTC. This τ check catches a
  wrong bank behind the right resistor string (0.60 s against 1.5 s) but **not** a wrong bank fitted
  together with its own discharge board (0.71 s sits inside the 8XX ±20 % band): it is a
  plausibility check, not the SKU proof. The voltage class of the cap-bank/discharge kit is proven
  by assembly traceability and the EOL capacitance/resistance measurement (`dfm.md` §4; round 12,
  R1-F24/R2-F28).

| Parameter set | 8XX SiC | 8XX IGBT | 4XX IGBT | 4XX SiC |
|---|---|---|---|---|
| V_DC normal range | 500–850 V | 500–850 V | 250–500 V | 250–500 V |
| OV trip (both channels) | 880 V | 880 V | 530 V | 530 V |
| Phase current pk / cont (A rms) | 340 / 185 | 340 / 185 | 400 / 250 | 400 / 250 |
| f_sw | 8–10 kHz | 5 kHz | 5 kHz | 8–10 kHz |
| Dead time (commissioning) | 1.0 µs | 2.5 µs | 2.5 µs | 1.0 µs |
| Current-loop crossover ceiling (S6, 45° PM) | ≤ 1.2 kHz at 10 kHz · **≤ 1.1 kHz at 8 kHz** | ≤ 0.7 kHz | ≤ 0.7 kHz | ≤ 1.2 kHz at 10 kHz · **≤ 1.1 kHz at 8 kHz** |
| Module NTC | B25/50 3375 (both modules — same curve) | | | |

The crossover ceiling is set **per switching mode**, not per silicon (round 7, RR08): the S6 model
gives 51.4° at 10 kHz / 1.2 kHz but 43.3° at 8 kHz / 1.2 kHz, and 47.2° at 8 kHz / 1.1 kHz. The
parameter set carries one gain set per f_sw; motor L/R and sampled-data timing re-close it. The S6
screen assumes double-update PWM (sample-to-actuation delay 0.75 T_sw, screening motor 25 mΩ /
0.35 mH); a single-update implementation (1.5 T_sw) drops the 10 kHz / 1.2 kHz case to 19°, so the
ceiling is re-derived from the measured delay and the real L_d/L_q (round 12, R2-F24).
**The current loop runs at 2·f_sw** (round 17, decided): the BCTU fires on both PWM half-cycles (double
update), 20 kHz at SiC 10 kHz, 16 kHz at SiC 8 kHz, 10 kHz for the IGBT SKUs' 5 kHz. A "10 kHz current
loop" is therefore the IGBT case only; a single-update 10 kHz loop at f_sw = 10 kHz would leave ≈ 19° at the
ceiling above.

## 3. Power envelope (review R-F09)

- **FW-03** Torque/power limits follow V_DC: P_max(V) = min(P_rated, √(3/2)·0.95·V·I_limit·0.85),
  with the current limits of §2. Full ratings exist only above these voltages:

| SKU | Peak 30 s | Continuous | At the low end of the range |
|---|---|---|---|
| 8XX (SiC, IGBT) | 220 kW ≥ 654 V | 120 kW ≥ 656 V | 168 / 91 kW at 500 V |
| 4XX (IGBT, SiC) | 150 kW ≥ 379 V | 90 kW ≥ 364 V | 99 / 62 kW at 250 V |

  Round 14 (FW-25): when no voltage-feasible current exists inside the demagnetisation and current limits
  (round 17: the dynamic voltage reserve is `cal_vdyn_reserve_frac`, 5 % of the FOC voltage limit
  `cal_mod_index_max`·V_DC/√3 by default, range 0–20 %, kept free for the current loop), the request is
  refused explicitly — zero torque, a speed-limit request on CAN, a DTC — never a finite but unattainable
  (i_d, i_q).
- **FW-04** Thermal derating from the module NTCs and coolant temperature; the rated 30 s peak
  assumes 65 °C coolant and the (to-be-measured) 0.045 K/W coldplate. Peak is re-allowed only
  after the continuous state has recovered (S4 time constants). Firmware (round 17, decided): the 30 s peak
  budget drains while the current is above the continuous rating and refills at 30 s per
  `cal_peak_recovery_s` (180 s, range 60–600: three times the assumed 60 s plate pole); once exhausted,
  peak returns only with the budget full again. An invalid module NTC derates to the continuous rating; an
  unknown coolant temperature allows no peak.

## 4. Fast protection paths

- **FW-05** Phase overcurrent: ADC channel thresholds with hardware compare (round 17, decided: the analog
  watchdog of each phase channel → TRGMUX/LCU (OR) → **eFlexPWM_1 FAULT1**, which DISMAP maps to the **high
  sides only**; the "eTPU fault input" of earlier revisions is superseded. That this route exists on the
  S32K39 and meets the budget is a silicon/HIL item of `firmware/docs/target-bringup.md`; until the EOL/HIL
  record proves it, FW-24 refuses to arm) at **1.25 × √2 × the SKU peak rms current, in instantaneous
  amperes, both polarities**: ±601 A (8XX, 340 A rms) / ±707 A (4XX, 400 A rms), converted to ADC
  codes with each channel's calibrated gain, offset and sign (round 12, R1-F25/R2-F18: the §2 table
  is in A rms, and a literal 1.25 × 340 = 425 A would sit below the 481 A crest of rated
  operation). Response ≤ 2 PWM periods, independent of the FOC ISR. Σ(Ia+Ib+Ic) plausibility every
  sample. **Per-channel validity window 0.2–4.8 V** (the HC5FW output range): outside it the channel
  is invalid — an open signal wire or an unpowered sensor reads 0 V through the card's 100 k
  pull-down (R⟨ph⟩B0, round 12, R1-F03). **Safe state after an overcurrent trip (A.12, from the
  firmware implementation):** the high sides are forced off by the PWM fault input and the event is
  treated as *control lost* — the §6 "control lost" row, a latched fault; as implemented (round 17) it is
  cleared only by a VCU fault reset below n_x, which also clears the watchdog flags and FAULT1's FFLAG; the low
  sides follow the §6 matrix (SPO, or PWM-ASC where the matrix requires it), never a torque-producing PWM.
  Round 23 (§10k item 5): inside `cal_asc_oc_window_ms` of an LS-ASC entry an overcurrent is the short-circuit
  transient of that entry — information (`DTC_ASC_OC_TRANSIENT`), no row, the compare re-armed after the window; the
  hardware action is the same (high sides off, which the ASC holds off anyway; the low sides stay on).
  Round 14 (FW-05 addendum, §10a): a latent stuck-at-zero channel is caught by the per-channel activity
  check once |i*| exceeds its threshold; an equal gain error on all three channels cannot be seen with three
  sensors and stays an EOL calibration item.
- **FW-06** DC overvoltage: both V_DC channels in hardware compare at the SKU OV trip; action =
  high sides forced off + zero torque request + ASC request, then PWM-ASC (§4c; if the §6 matrix
  allows). Round 7 (RR06/A6-R08) replaces the written "20 µs" with a budget that must be
  **measured on HIL**, link crossing the trip → `ASC_REQ` edge:

  | Stage | Allocation |
  |---|---|
  | Divider/filter lag (2.82 MΩ ∥ 6.2 kΩ, 1 nF: a first-order filter trails a ramp by τ) | 6.2 µs |
  | AMC1311B signal delay 50–50 % (max) | 2.1 µs |
  | Card receiver | 0.3 µs |
  | Sample wait — each V_DC channel **free-running at ≥ 200 kS/s** (not the PWM-synchronised list) | ≤ 5.0 µs |
  | Conversion | 1.0 µs |
  | ADC analog watchdog → eFlexPWM fault input (high sides off in hardware) → `ASC_REQ` | 1.0 µs |
  | **To the ASC request** | **≤ 15.6 µs** |
  | Hardware ASC entry, break-before-make (§4c; design-verify, Safety A.8 "ASC entry, latch set → LS gates on", UCC14141-Q1 bias since A.12) | ≤ 7.56 µs |

  Sample wait, as scheduled (round 17, decided): V_DC ch2 shares ADC1 with three slow inputs (MT2_SIG,
  INTRLOK_N, TMOD_W) converted as injected conversions of the 1 ms list, so ch2 waits at most (1 + 3)
  conversions = 4 µs at the allocated 1 µs, inside the 5.0 µs row. Measuring it on the target is a checklist
  item (`firmware/docs/target-bringup.md`); if it does not fit, MT2_SIG moves off ADC1.
  Route: ADC analog-watchdog (hardware threshold per channel) → TRGMUX → eFlexPWM FAULT and the
  `ASC_REQ` edge (a timer/trigger output or the first instruction of the fault ISR). A separate
  comparator is added only if this route misses the budget on HIL. Rationale (N9): with the
  battery path lost at the SKU's full regen power the link charges at ≈0.9 V/µs (8XX: 220 kW
  into 291 µF). After the PWM-off, all six switches are off until the low sides are in ASC. The
  phase current (up to 481 A) then rectifies into the link at ≈1.65 V/µs. The whole chain ends
  at 906 V (91 % of the cans' 1000 V U_N at 85 °C; 4XX: 542 V of 600 V). A 100 µs response would end at 962 V and a once-per-PWM-period sample
  at 5 kHz (200 µs) at 1038 V.
  Round 14 (FW-24): the HIL-measured chain time is stored in the EOL/HIL validation record; without it the
  OVP route counts as unvalidated and the firmware does not arm.
- **FW-07** V_DC plausibility: |VDC1 − VDC2| > 5 % ⇒ fault — as implemented (round 17), above a floor:
  max(5 % of the larger reading, `cal_vdc_disagree_floor_v` = 18 V, range 5–40 V: twice the ±9 V uncalibrated
  low-level error, R9X-07), so two healthy channels near 0 V never disagree; `VOFS` (the receivers' shared
  +0.5 V offset, now on an ADC pin) outside 0.475–0.525 V ⇒ **both channels invalid** (a failed
  offset buffer shifts both by up to 228 V and passes the 5 % check); with contactors closed,
  |V_DC − V_pack(BMS)| > 3 % ⇒ fault. A channel reading < 0.25 V is the AMC1311 fail-safe
  state, not a dead bus. **Both channels are also invalid whenever V5GD (V5GD_SNS = V5GD/2 on PTD27, ADC4_P6, ball A15, since the A.12
  ball map; PTB5 has no ADC on the 289-MAPBGA and is RDY_LS) is outside
  4.75–5.25 V** (round 9, R9X-01). The AMC1311 LV sides run on V5GD; unpowered, they leave the
  receivers at their +0.5 V offset, a false "0 V bus" that the plausibility checks cannot see.
- **FW-08** Regeneration with the battery path lost (contactor opens; a BMS charge limit that
  drops to 0 with the contactors closed is the connected-battery row of §6, not this case):
  below n_x apply **zero current** — i_d = i_q = 0, field weakening included — at the current-loop rate and
  rely on FW-06 for the fast part; request the VCU/BMS protocol "zero torque before opening" for every
  non-emergency opening.
  Round 15 (F164): while armed, the battery path counts as LOST at every speed unless the contactors are
  reported CLOSED in a fresh frame — OPEN, PRECHARGE, INVALID and a stale report are all lost, as is V_DC
  leaving the pack. Below n_x: zero current under current control while winding current remains (FW-06 LS-ASC
  as the backstop), then SPO once it is below `cal_spo_release_a`; at or above n_x, or at unknown speed:
  LS-ASC. Torque permission is withdrawn in the same cycle. A zero-torque opening below n_x with no winding
  current is a normal disarm, not a fault. When the row is evaluated (round 17, decided): every 1 ms task in
  ARMED_ZERO_TORQUE, RUN and DERATE; it is held while its response still energises the bridge (zero-current
  control or PWM-ASC) and is a FAULT only for that time, then clears — unarmed, open contactors are the
  precharge sequence.
  **DC-link trim (round 17, decided).** The battery-lost response is zero current, never a DC-link voltage
  controller: INV_STATUS reports the torque applied (0 Nm, with the zero-torque bit), never a controller
  output. The DC-link trim acts only in normal RUN/DERATE with the battery path proven (the state machine
  grants torque only with the contactors reported CLOSED and no battery-lost row): while V_DC is above the
  SKU's normal-range maximum (850 V / 500 V — its reference, never a higher value) it takes regenerative torque
  back, at most `cal_dcl_tmax_nm`, and never adds motoring torque; outside RUN it is reset, so it never engages
  with the battery path lost.
- **FW-08b** Report "fault recovery, keep HV connected" (`keep_hv`, INV_STATUS b1.5) to the VCU/BMS whenever
  the chosen safe state holds an SPO that relies on the battery — rule (a) fails for the present current and
  speed and the battery is present — at ANY speed and for every row that ends in SPO except the rows whose
  premise is a lost battery; hold it until rule (a) holds or ASC is active, re-evaluated every 1 ms. With the
  battery absent there is nothing to keep: report "no safe state proven" (b14.0) and set the energy DTC
  instead. (Round 8, R7-06: "after any DESAT at n ≥ n_x until ASC or n < n_x" — now the special case; worded
  as implemented in round 17, see §6 keep_hv.) During the mandatory ≥ 1.5 ms driver
  reset the bridge is three-phase open, and the battery absorbs the rectified current.
  After an **FLT_LS** the bridge stays in SPO until n < n_x (§6), so the rectified charge flows for
  seconds, not milliseconds. The VCU brings n below n_x with the friction brakes, and the pack must
  take that charge. Whether it can (full or cold pack, BMS short-term charge limit) is a vehicle-
  integration check from the motor data (I and E at n_max). A BMS that opens earlier is covered by
  the §6 motor/vehicle release rule, not assumed away as an independent second fault (round 9,
  A8-G02; cross-check R8X-15).

## 4b. Regenerative braking — who owns what (vehicle level)

The inverter is the only device that can turn wheel torque into DC power, so it **executes**
regeneration and **protects itself** during it. It does not decide how much regen the vehicle
wants, and it has no dump resistor.

| Function | Owner | The inverter's part |
|---|---|---|
| When to regenerate and how much (lift-off coast, brake pedal) | VCU | executes the negative torque request; reports the regen torque available within its limits (FW-03) |
| Blending with the friction brakes; ABS/ESC cutting regen on wheel slip | brake system, via the VCU | torque follows within the current-loop time; a stale command ramps to zero (FW-11) |
| How much charge the battery can accept | BMS (charge current / voltage limits) | caps regen at the limit relayed by the VCU; BMS-limit timeout ⇒ zero regen (FW-11) |
| Opening the battery path during regen | BMS/VCU ask for zero torque first (FW-08) | emergency opening: FW-06 hardware compare → ASC request ≤ 15.6 µs, ASC entry ≤ 7.56 µs → zero torque + ASC/SPO per §6; below n_x zero current while winding current remains (FW-08) |
| Uncontrolled regen with the inverter off at speed | inverter + motor spec | §6: LS-ASC above n_x; total LV loss is energy-safe only below the E_LL,pk limit |
| Device heating in regen | inverter | the diode carries regen current — diode Tj verified per SKU (design-verify) |
| Energy that cannot go to the battery | vehicle friction brakes | none — the discharge resistors are shutdown bleeders, not a regen dump |

## 4c. ASC entry and exit (round 7, RR05 + cross-check)

The high sides have no ASC input. ASC acts on the three low-side drivers and outranks their EN
and IN. Two datasheet facts set the design (NSI6611 DS 1.2):

- **Priority.** ASC outranks VCC1-UVLO, EN and IN, but not VCC2-UVLO.
- **DESAT over ASC.** DESAT is honoured over ASC **only with EN/RST high and the driver's own
  input commanding on** (IN+ high, IN− low; §8.12 function table, Fig. 8.11). With EN low and ASC
  high, the table marks DESAT "irrelevant" and the output stays on.

So entry is ordered **without using EN** on the path where the MCU is alive. That keeps the
low-side short-circuit protection active during a commanded ASC.

**MCU path** (FW-06 over-voltage, resolver invalid, contactor open at speed):
1. **High sides off.** The ADC-watchdog fault channel of the eFlexPWM is mapped (DISMAP) to the
   **high-side outputs only**, so hardware forces PWM_xH low. The high sides start turning off
   within 0.13 µs (IN+ low). A firmware-detected trigger does the same in software.
2. **Latch.** The fault ISR asserts `ASC_REQ`. The power board's delay (CASCD 12 nF) holds the
   low-side ASC pins below threshold ≥ 4.03 µs, plus the NSI6611's own 0.39–1.1 µs, so the ASC pins
   engage **≥ 4.42 µs** after the request and complete ≤ 7.56 µs (design-verify, Safety A.8: "ASC
   break-before-make: HS off before LS on" and "ASC entry, latch set → LS gates on", with the UCC14141-Q1
   bias since A.12; round 17 aligned these from 3.4 / 7.5 µs).
3. **PWM-ASC.** No sooner than the dead time after step 1 (2.5 µs IGBT, 1.0 µs SiC), the ISR sets
   the low sides to 100 % on (PWM_xL = 1), with `MCU_GATE_EN` high:
   - the low sides are on through IN+ with EN high, so their DESAT is documented to act;
   - each high side is held off by the fault state and by its own IN− interlock.

   The latch stays set as the hold that survives an MCU reset. The DESAT fault channels (FW-15)
   still map to all six outputs.

**FS1B path** (MCU dead): FS0B drops `DRV_EN` and the high sides turn off ≈0.2 µs later. FS1B
sets the latch, and CASCD delays the low sides ≥ 4.42 µs. That is break-before-make in hardware.
In this state EN is low, so a high side that **fails short during** an FS1B-ASC is not bounded
by the low-side DESAT. That takes a double fault (MCU dead plus a new HS short). A NOVOSENSE
statement on DESAT with ASC high and EN low is a release gate. Until then, FW-12 chooses FS1B-ASC
only for motors that need it (§6).

**A DESAT while ASC is active** (round 8, R7-01/A7-N01). Example: a high side fails short during
PWM-ASC. The NSI6611 behaviour comes from DS 1.2 Fig. 8.11 (ASC high, DESAT, then IN low, then an
RST/EN pulse).

| Step | Faulted low side | Healthy low sides | High sides |
|---|---|---|---|
| PWM-ASC (EN 1, IN+ 1, IN− 0, ASC 1) | on | on | off (IN− interlock) |
| Its DESAT trips (documented: EN high, IN+ high) | soft turn-off, FLT latched | on | off |
| FLT → **UASCG masks ASC_CMD** (hardware, ≤ 0.01 µs; pins release ≤ 1.07 µs — design-verify, Safety A.8 "Latched driver FLT masks ASC on every path"); FW-15 fault forces PWM low | **held off by its own latched fault**: it stays off through IN low, EN low and ASC | off (IN+ low, no ASC) | off |
| Fault latch drops DRV_EN 22–53 µs later | held off | off | off |
| **Result** | | **SPO**, as §6 requires for an LS DESAT | |

The faulted driver can come back on only at an RST/EN **rising edge after its mute time**, which is
the deliberate FW-15 recovery. It then follows IN and ASC, so FW-15 **always** clears the ASC latch
first (UASCG masks ASC only while FLT is latched). ASC returns only through the MCU path below,
after FW-15 step 4 (cross-check R8X-01). The mask also covers FS1B-ASC: a reset after a DESAT
(a causally related second event, review T7-02) cannot bring ASC back while the fault is latched.
That is one bounded SC event per recovery, never a sustained shoot-through.

- **FW-06a** ASC exit is MCU-commanded:
  1. `ASC_CLR` pulse while PWM-ASC still holds the low sides on.
  2. Move PWM from PWM-ASC to the next state (SPO or modulation); the eFlexPWM inserts the dead
     time.
  3. No high-side pulse before the low sides are off (round 17, decided). The latch clears on the ASC_CLR falling
     edge; the low-side ASC pins release ≤ 1.06 µs after ASC_CMD falls — VOW3120 tpHL 0.5 µs + DASCR discharge
     0.08 µs + NSI6611 tASC_f 0.48 µs (DS 1.2 §6.1, ASC to output falling edge) — ≤ 1.07 µs from the clear with
     the latch and UASCG logic (design-verify, Safety A.8: "ASC entry, latch set → LS gates on" and "Latched
     driver FLT masks ASC on every path"). The low-side outputs then follow IN, already low, and the switches turn
     off within the SKU dead time — the design's allowance for a complementary transition, which covers the
     NSI6611 IN→OUT delay (≤ 130 ns) and the switch's own turn-off (SiC: HCS600 td(off) + tf ≤ 0.24 µs at 3.3 Ω,
     ≈ 0.5 µs at the 6.8 Ω RG_OFF — datasheet extract §24a). So the first high-side pulse waits `cal_asc_release_ns` (1500 ns, range
     1070–5000 ns: never below the release) plus the dead time, counted from the falling edge, plus one count of
     the microsecond timer: it comes 3–4 µs (SiC, 1.0 µs dead time) / 4–5 µs (IGBT, 2.5 µs) after the clear,
     against deadlines of 2.07 / 3.57 µs. (The earlier "≥ 1 µs after the clear, pins release ≤ 0.75 µs" predates
     the A.12 VOW3120 + UCC14141 opto path; the firmware then waited 1 µs after its 1 µs clear pulse, 2.0 µs from
     the falling edge — short of both deadlines.)

  An exit is never needed in a hurry. After an MCU reset, the latch is **not** cleared blindly;
  §9 decides from speed.

**Gate-logic supply loss (V5GD off, V5A on)** (round 9, A8-01; closes R8X-10 and P-03 by design).
The NSI6611 rates FLT/RDY to VCC1 with no +0.3 V, so their pull-ups and the diode-OR pull-up sit on
V5GD, the drivers' own VCC1, brought to the card on harness pin 1. A lost V5GD that falls below
≈ 1 V therefore reads as FLT and RDY low:
- the fault latch sets and UASCG masks ASC;
- the eFlexPWM fault forces PWM low;
- DRV_EN falls.

No card output then drives the dead domain (IN/EN low). The bridge goes to SPO, as in §6's V5GD
row. At the normal rail corners the driver inputs (IN±, RST/EN) sit ≤ 0.2 V above VCC1, inside their
VCC1 + 0.3 V rating.

**A dead V5GD can hover** (round 9 cross-check R9X-06). Two paths can hold it at 1–4 V, where the
FLT/RDY lines sit between logic thresholds:
- the module-NTC clamp diodes, fed by the card's 5.1 k NTC pull-ups;
- DRV_EN and PWM, through the NSI6611 input clamps.

The firmware therefore reads V5GD directly (PTD27; A.12 ball map). Outside 4.75–5.25 V it forces SPO: `ASC_CLR`,
MCU_GATE_EN low, PWM low (with a FLT line reading low — V5GD below 1 V — the EN drop waits for the DESAT hold
of FW-22, so MCU_GATE_EN falls `cal_desat_en_hold_us` plus at most one current-loop period, 60–160 µs, after
the ASC_CLR; with the FLT lines still high, a hovering V5GD, it falls at once — round 17). That removes the second path. It also marks V_DC invalid (FW-07), sets a
supply DTC, and does not arm. The V5GD-off bench item covers both paths. If the NTC path alone can
hold V5GD up, the NTC clamps move to a zener to ground.

At power-down V5GD outlives V5A by ≈ 1–3 ms and feeds ≤ 0.9 mA per FLT/RDY line (5.1 k) into the
unpowered MCU pads. That is inside the S32K39 injection rating (R9X-13).

## 5. Monitoring (slow paths)

| Req | Signal | Rule |
|---|---|---|
| FW-09 | HVIL (`INTRLOK_N` ADC signature 3.0 / 2.0 / 2.5 V) | open ⇒ ramp torque to zero, report to VCU; the safe state follows §6 — HVIL open is **not** a licence to open contactors at speed. Reaction ≤ 100 ms. (R-F12: there is no hardware comparator.) |
| FW-10 | Resolver (SDADC sin/cos + excitation monitor) | amplitude (sin²+cos²) window, tracking error, angle-rate plausibility vs current model, excitation-monitor level (its planes: FW-30); any fault ⇒ no angle-dependent torque, §6 state. Each check debounced over `cal_rslv_debounce` consecutive frames — round 23 (§10k item 8): one bad frame is one count, kept out of the observer, never a fault |
| FW-11 | CAN torque command | counter + CRC, ≤ 20 ms staleness ⇒ ramp to zero torque (not hold last value) — round 15, confirmed round 17: a stale command also leaves the contactor state unknown, so while armed it is also the §6 battery-lost row ("contactor/precharge feedback invalid"), and that row wins — zero current at the current-loop rate below n_x, LS-ASC above; the ramp remains for a command lost with the battery path proven (e.g. HVIL open); BMS limit timeout ⇒ zero regen only (motoring keeps the FW-03 envelope; the relayed discharge limit is not held stale) |
| FW-12 | FS26 | OTP set of design-basis §8a verified by SPI readback at every boot (R-F38); FS1B policy per §6; Q&A watchdog configured before FS0B release with **WD_ERR_LIMIT = 2** (not the default 6), WD_FS_REACTION = RSTB + FS0B (default), window ≤ 3 ms, so runaway code asserts FS0B within about two windows (round 7, RR03). **FS1B_TDELAY = 0 (required)**: FS1B asserts with FS0B, FW-16 step a reads the ASC preset at once, and a delay would only lengthen the SPO interval after an FS0B event; the ASC entry is break-before-make in hardware (§4c). **FS1B_TDUR = 100 ms** (default): the ASC latch holds after FS1B releases, and the bounded pulse also bounds RFS4 on a FAULT_OUT wire short (§9). Keep BACKUP_SAFETY_PATH_FS0B = 1 (default: an FS0B short-to-high asserts RSTB). Set **BACKUP_SAFETY_PATH_FS1B = 0** (round 9, A8-03). An FS1B short-to-high comes from FAULT_OUT shorted to KL30 (or a board-level short on FS1B_N). With this setting it still counts in the fault error counter, and FW-12 reads it as a DTC with no arming until repaired. It does not reset the MCU. A reset would restore neither the FS1B→ASC preset nor FAULT_OUT, it would interrupt the MCU's §6 control during the very fault that asserted FS1B, and a reset loop would repeat the RFS4 stress. FS_GPIO1 (flyback-enable OR input) stays low from POR until §9 step 6, then high, so gate power is held through an MCU reset but is never up while FS1B is still asserted at boot. The OTP must configure GPIO1 **push-pull and not slotted** (design-basis §8a): a slotted push-pull GPIO1 goes high by itself at power-up (DS Table 133). The FW-16 boot test asserts FS0B with FS0B_REQ (FS_SAFE_IOS_1 bit 6). FS1B-ASC only for motors that need it (§4c: with EN low the LS DESAT is not documented). Outside that policy, only FW-16 switches the low sides on through ASC. It does so in states without documented LS DESAT: step a (EN low, via FS1B), step f (EN high, IN+ low) and step h (both, around the FLT injection). Each runs only under its measured no-HV, standstill conditions |
| FW-13 | Temperatures | module NTCs (open/short/rate — round 23: the rate over a window with a deadband, §10k item 1), motor sensors, board NTCs; derate per FW-04 |
| FW-14 | Gate power | RDY_HS/RDY_LS low ⇒ no PWM; flyback enables sequenced before DRV_EN |
| FW-33 | LV supply (KL30 at the FS26 VSUP, read through its AMUX — round 17) | an overvoltage inside the vehicle's profiles (IR-03 test B, IR-02 jump start) is information — RUN and the torque unchanged, a DTC whose stamps give the duration; longer ⇒ the §6 command-lost ramp. "FW-33 LV supply supervision" below |

**FW-12 read-back and release, as implemented (round 17, decided).** "OTP set verified by SPI readback" means what
the application can read at every boot: M_PROGID against the procured OTP variant (`cal_fs26_prog_id`; 0xFFFF =
unbound ⇒ no arming), FS_STATES OTP_CORRUPT and DBG_MODE, and the INIT_FS registers FW-12 fixes (WD_CFG, FSSM and
WDW_DURATION with their NOT complements, and SAFE_IOS_2) — written in INIT_FS, and after an MCU-only reset, when
the FS26 is already past INIT_FS, read back against the same values. The OTP bank itself is visible only in the
FS26's debug/OTP mode, so the per-field comparison with design-basis §8a is an EOL step. FS0B/FS1B are released
only with FLT_ERR_CNT = 0, which a reset leaves above zero until good watchdog answers count it down; a release
not achieved within `cal_sensor_selftest_ms` + `cal_fs0b_release_ms` of entering the sensor self-test is a FAULT. Any mismatch is a DTC and no
arming. The answer arithmetic and the FS26 behaviour after an MCU reset on silicon are checklist items
(`firmware/docs/target-bringup.md`).

**FW-12 refresh cadence (round 17, decided).** The FS26 window restarts at every answer, good or bad (DS Rev.3
§22.6), and lasts 3 ms with the first half closed (FS_WDW_DURATION: WDW_PERIOD 0011, WDW_DC 010 — Tables 82,
144, 145). It is timed by the fail-safe oscillator, 20 MHz ± 5 % (Table 143, FFSOSC_ACC), so a good answer must
come later than 1.5 ms / 0.95 = 1.579 ms and earlier than 3.0 ms / 1.05 = 2.857 ms after the previous one at
every oscillator corner. The firmware answers first thing in the 1 ms task, whose ticks are exact (STM compare),
once ≥ 1500 µs have passed since the previous answer — on every second task, never the first or the third, for
any answer offset below 500 µs. The design assumes the answer trails its tick by 20–130 µs: task-start latency
and preemption by the priority-0–2 interrupts ≤ 100 µs, two 32-bit SPI frames at 4 MHz (≤ 10 µs each, token read
and answer) and one retry frame for a CRC error. Successive answers are then 2000 ± 110 µs apart, 1890–2110 µs:
311 µs after the closed window's latest end and 747 µs before the open window's earliest end. The latency
assumption is measured with the WCET (checklist T-36) and the answer spacing on silicon (T-32). Before round 17
the firmware waited ≥ 2000 µs from a stamp that trails its tick, which first holds on the third task: answers
every ≈ 3.0 ms, at the open window's end — late whenever the FS26 runs fast or a tick carries more work, and
one late answer already reaches WD_ERR_LIMIT = 2 (a failure counts 2, §22.6.3 and Table 146): FS0B.

**FW-33 LV supply supervision (round 17, decided).** The LV entry lets the vehicle's overvoltage profiles through
to parts rated for them (let-through, verification report F189: no TVS on a KL30-derived net conducts below
36.7 V), so the FS26 sees them on VSUP. Above VSUP_OV (19.3 / 20 / 20.7 V, FS26 DS Rev.3 Table 9) it latches
VSUPOV_I (M_VSUP_FLG bit 2, Table 32) — an interrupt, no fail-safe reaction — and between 18 and 36 V it runs in
its High Voltage Extended Operation (Fig. 8: full function for a limited time; load dump and jump start named;
the time limit is VR-29). The firmware measures VSUP itself: at every boot it sets the FS26 AMUX to VSUP / 14
(M_AMUX_CTRL 0x13: AMUX_EN, AMUX_DIV, AMUX 10001 — Tables 56 and 130; input 4.2–36 V, ratio ± 1.5 %, offset
± 7 mV, Table 131) and reads it on SBC_AMUX (ADC0_S14) every 1 ms. An event starts above 20 V (`vsup_ov_v`, the
typical VSUP_OV) and ends below 19.5 V. It is **information**, not a fault and not a reason to leave RUN: the
torque is not derated, no §6 row is raised, and `DTC_LV_OVERVOLTAGE` is set in every millisecond of the event,
so its first/last stamps give the duration. It is tolerated for as long as the vehicle interface allows, in two
bands:
- above `cal_vsup_jump_max_v` (27 V; range 24.5–30 V) — a load-dump pulse, IR-03 (ISO 16750-2 test B, up to
  35 V, td ≤ 400 ms): for `cal_vsup_ld_ms` (500 ms; range 400–1000 ms) of continuous time above that ceiling;
- at or below it — a jump start, IR-02 (24 V, ≤ 60 s): for `cal_vsup_jump_ms` (65 s; range 60–120 s) for the
  whole event.

The ceiling sits above the 2023 edition's 26 V jump start at the AMUX's highest reading (26 V × 1.015 + 0.1 V =
26.5 V) and below the test-B plateau at its lowest (VSUP 34.6 V at the 35 V plateau, ≥ 34.0 V read); the range
floor keeps IR-02's 24 V inside the jump-start band at the highest reading (24.46 V). An event that outlasts its
band is **sustained** (`DTC_LV_OV_SUSTAINED`): it is outside the vehicle's profiles and takes the §6 command-lost
row — the orderly ramp HVIL open takes (FW-09): torque ramped to zero, then SPO below n_x and current control
kept above it while the battery is present (the row's cells), no FAULT state — until VSUP is back below 19.5 V,
when the requested torque returns. The firmware had no VSUP path before round 17 (VSUPOV_I was never
read and INTB is not used), so this row is the path a sustained overvoltage now takes; it degrades the function
and tells the vehicle — the parts carry the profiles in hardware (verification report, test-B rows). A reading
below 4.2 V (the AMUX not configured, or its pin at 0 V) is `DTC_LV_VSUP_UNKNOWN`: no supervision, information
only. On silicon and the bench: checklist T-39.

## 6. Safe-state decision matrix (review R-F13/F15)

The inverter provides three actuators: **SPO** (DRV_EN low — three-phase open), **LS-ASC**
(latched, overrides EN; high-side ASC is not implemented) and torque-controlled ramp-down.
Which one is safe depends on the **motor**: the crossover speed n_x where the line-line
back-EMF peak √3·ω_e·ψ_f equals the allowed link voltage (880 V 8XX / 530 V 4XX, cold
magnets). Motor data are a commissioning input (R-F16); the table is the rule.

**The columns split on speed; the energy condition applies in both.** Every cell that ends in SPO — at any
speed, including standstill — is released only under rule (a) or (b) below for the current at the fault
(round 13, A11-R01: winding energy 1.5·L·I² does not depend on speed; the 0.35 mH screening motor at
340 A rms carries 61 J at 0 rpm, and an isolated 8XX link takes it to 1065–1089 V). "n < n_x" means the
back-EMF cannot charge the link on its own; it is not an energy exemption.

| Condition | n < n_x | n ≥ n_x |
|---|---|---|
| Healthy, command/CAN lost (armed, a stale command also leaves the contactor state unknown: the battery-path row below applies and wins — FW-11, round 17). The same row serves HVIL open (FW-09), an LV overvoltage outside the vehicle's profiles (FW-33, round 17) and the overspeed warning band (FW-42, round 23) | ramp to zero torque, then SPO | ramp to zero torque; keep current control (field weakening) while the battery is present |
| Battery path lost (contactor open, contactor/precharge feedback invalid, or V_DC leaving the pack voltage) | zero current (i_d = i_q = 0, FW-08) at the current-loop rate; if the link crosses the OV trip while current is still flowing (winding energy with no sink), FW-06 fires **LS-ASC** at any speed — the current then circulates and decays in the winding instead of charging the link; release to SPO once the current is gone (round 13) | **LS-ASC** (FW-06), release to SPO below n_x |
| BMS charge limit → 0 with the battery still connected (full or cold pack) | zero torque, then SPO | ramp regen to zero at the current-loop rate and keep current control (field weakening) as in the healthy row — the connected pack is a voltage source, so this is not an ASC case; FW-06 stays armed as the backstop if the pack is then opened (round 12, R1-F02/R2-F35: the old row merged the two events) |
| Resolver invalid, or control lost (phase-current sensing invalid or stale, a phase-overcurrent trip — FW-05, non-finite control, arming evidence lost while armed — round 17; the overspeed trip band — FW-42, round 23) | SPO | LS-ASC (no angle needed) |
| V_DC invalid with V5GD healthy (FW-07: VOFS, disagreement, fail-safe or stale channel — round 17) | SPO under rule (a) or (b); where neither holds, LS-ASC — the winding current circulates instead of charging a link nobody can measure | same |
| DESAT on a **high-side** switch (FLT_HS) | SPO | SPO first. The fault latch holds EN low, and LS-ASC with EN low has no documented LS DESAT. After the FW-15 reset (≥ 1.5 ms), LS-ASC is permitted **only as PWM-ASC** with EN high (§4c). **Assumption:** the HS DESAT came from a shorted LS device or a phase-to-DC− fault, which LS-ASC completes into a symmetric short. **If an HS device has itself failed short**, the LS DESAT (documented with EN high and IN+ high) soft-turns the LS off, FLT_LS follows and the row below applies (SPO): one bounded extra SC event, never a sustained shoot-through. The ASC latch alone must not be used for this row. **Energy during the SPO interval** (round 8, R7-06): for ≥ 1.5 ms the rectified motor current flows into the battery, so the inverter asks the VCU/BMS to keep the contactors closed until ASC is back or n < n_x (FW-08b). Whether the link stays inside its rating if the battery path is lost inside that window is decided by the motor/vehicle release rule below (DC-link energy 32.8 J 8XX / 28.6 J 4XX to U_N); no brake chopper by default |
| DESAT on a **low-side** switch (FLT_LS) | SPO | **SPO only** — the cause may be a shorted HS switch; LS-ASC would short the link through the motor |
| MCU hang / reset | hardware: FS0B ⇒ SPO; FS1B ⇒ LS-ASC per FW-12 policy | same — choose the FS1B policy from the motor's n_x. Unconditional FS1B-ASC is safe only if the motor tolerates ASC braking torque at low speed. In FS1B-ASC EN is low (§4c), so a new HS short during it is not bounded by the drivers — a double fault. After the reset, §9 decides from speed whether to keep ASC |
| Gate-logic supply (V5GD) loss, or the power board's LV feed lost (QLVS, FVBx) | SPO (FLT/RDY read low or V5GD out of window: latch set, ASC masked or cleared, PWM/EN low) | SPO — release rule (a) or (b) below; the HV backup bias cannot help here (ASC is masked, or V15 and the ASC opto are dead too) |
| Total LV (KL30) loss | SPO (gates park low by UVLO) — energy-safe only under rule (a) for the current at the fault, or with the battery retained (b) | SPO — **energy-safe only under rule (a)** (back-EMF *and* winding energy; the old "E_LL,pk(n_max) < U_N" test alone was incomplete, round 13). Motors that fail (a) need the HV-fed backup-bias option (c) before release — S10: gate reservoirs hold ASC only 0.5–3.1 ms (round 7: worst case now starts at the 13.54 V low-corner rail) and the command path ≈1 ms |

**The speed after a resolver fault — a physical bound (round 23, §10k item 6; replaces the round-17 hold).** After a
resolver fault or a stale resolver the speed is not forgotten; it is bounded: |n| ≤ |n_last| + a_max·t, with n_last
the last valid speed, t the time since it and a_max = `cal_speed_accel_max_rpm_s` (2 500 rpm/s, range 500–50 000: the
fastest credible shaft acceleration of the driveline while the inverter makes no torque — a wheel spinning up, a gear
change; set on the vehicle, T-37). The bound serves the column choice only, never as angle feedback: every decision
that asks "n < n_x" asks it of the bound — the §6 rows (SPO under its energy rule while the bound stays below n_x, the
n ≥ n_x column once it reaches n_x), a latched row's reset, the FW-15 retry, the §9 step-5 ASC decision, the ASC exit,
field weakening. The speed is **unknown** — the n ≥ n_x column, rule (a) evaluated at n_max — when the resolver was
never valid since the boot, without a valid calibration record, or once the bound passes the record's n_max (nothing
is known beyond the speed the release rules are proven to). Rationale: the round-17 hold (`cal_speed_hold_ms`, 200 ms)
forgot a known speed and turned a 2 000 rpm resolver fault into PWM-ASC 200 ms later (≈ 32 N·m of braking on the
screening motor) although the shaft cannot have gained the 6 000 rpm to n_x (8 086 rpm at 880 V) in that time — with
a_max at its default it takes 2.4 s. The bound errs high by construction (it never decreases while the resolver is
out), and FW-06 remains the backstop if the physics is ever outrun: a back-EMF above the link trips the OV compare and
LS-ASC at any speed.

**Motor/vehicle release rule for SPO energy** (round 9, A8-G02). Every SPO interval at n ≥ n_x sends
the rectified motor energy into the battery. These intervals are the ≥ 1.5 ms FLT_HS reset, an FLT_LS
until n < n_x, a V5GD loss and a total LV loss. A battery that opens inside such an interval is **not**
treated as an independent second fault: the inverter's own fault, a crash, a CAN loss or a full/cold
pack can each cause it. A motor/vehicle combination is released only when one of these holds:
- **(a)** SPO is energy-safe without the battery: at every speed up to n_max (cold magnets), with the
  largest current the calibration allows there, generating, at the worst rotor angle, the diode-bridge
  freewheel into the isolated link (C_min, starting at the OV trip 880 V / 530 V) peaks below U_N
  (1000 V / 600 V). Screen, valid only while E < V₀:
  V_pk ≤ E + √((V₀ − E)² + 1.5·(L_d·î_d² + L_q·î_q²)/C_min), with E = √3·ω_e·(ψ_f + |L_q − L_d|·Î/2).
  If the screen fails, or E ≥ V₀ (the current does not decay), a three-phase diode-bridge simulation
  with L_d(i), L_q(i), ψ_f and R_s decides. Round 12 (R1-F01/R2-F08): the A.9 rule tested the
  back-EMF alone and missed the stored winding energy ¾·(L_d·i_d² + L_q·i_q²) plus the back-EMF work
  during the decay (19–76 J for the screening motor — as large as the 52 J of winding energy). With
  the screening motor (0.35 mH, 25 mΩ) the 8XX link covers the zero-EMF freewheel only up to
  ≈ 277 A rms from 850 V (250 A from the 880 V trip); a 340 A rms motor of that inductance is
  released under rule (b).
- **(b)** The VCU/BMS integration shows, on HIL/dyno, that the contactors stay closed through inverter
  fault recovery **at every operating point where rule (a) fails** — high current at any speed, not only
  n ≥ n_x (round 13, A11-R01) — for every opening cause they can be driven by, the inverter's own
  faults included. The pack must also accept the rectified charge (FW-08b). Rule (b) cannot cover the
  rows whose premise is that the battery path is already gone; those rows rely on FW-06 LS-ASC
  absorbing the winding energy (above) or on rule (a).
- **(c)** For **total KL30 loss** only, the HV-fed backup-bias ASC option is fitted. It cannot
  restore ASC for the V5GD row (masked by design) or for a lost power-board feed (R9X-09).

Otherwise that motor is not released with this inverter.

**Commissioning check (cross-check R8X-13).** A DESAT during FW-06 ASC now always ends in SPO. So the
peak phase current at ASC entry (n_max, cold magnets) must stay below the low-side DESAT minimum
with margin (SiC 7.2 V at the switch ≈ 1.3 kA hot; IGBT from its V_CE(sat) curve at the 4.2 V
minimum, design-verify "DESAT trip"). Otherwise ASC entry alone trips DESAT and FW-06 lands in SPO
exactly when the battery is gone.

**keep_hv — the runtime side of rule (b) (round 14, A12-R08 / F148).** Whenever the chosen safe state relies
on rule (b) — rule (a) fails and the battery is credited as the energy sink, at ANY speed — the firmware asserts
`keep_hv` in the CAN status (the "keep the battery connected" request) and holds it until rule (a) holds
again or ASC (an independent sink) is active. It is not derived from speed. Rows whose premise is the lost
battery cannot borrow rule (b): they raise the energy DTC and report "no safe state proven". The request is
not itself proof that the contactors stay closed — that is the vehicle-level rule (b) evidence.

## 7. Fault-latch recovery (review R-F06 — NSI6611 DS 1.2 §8.10/§9.4)

The NSI6611 releases FLT **only at an RST/EN rising edge after its mute time**. The mute time is
0.55–1.3 ms from the fault, and the driver ignores any reset during it. The DS is not consistent
about the reset itself:
- §8.10 and Fig. 8.8: any RST/EN low pulse ≥ t_RST_FIL (≤ 0.8 µs) after the mute time releases
  FLT at its rising edge;
- §9.4: the low must be held ≥ t_FLT_MUTE.

FW-15's ≥ 1.5 ms low satisfies both. Under the shorter reading, a pin re-pulsed **slowly** by runaway
code can also deliver a valid reset after the mute time (FW-15 step 3). That case therefore rests on
the eFlexPWM lock and the watchdog, not on the driver's own latch (cross-check R8X-09).
Until the reset, the driver holds its own gate off through IN, EN and ASC changes (Fig. 8.11). All six RST/EN pins
are DRV_EN, which the fault latch holds low. So:

1. **FW-15** On FLT_HS/FLT_LS: PWM inputs to zero (the eFlexPWM fault input has already forced
   them low in hardware); latch which bank in retained RAM at once and **queue** the NVM write
   (round 9, R9X-08; round 12, R2-F19: the write is asynchronous and bounded, and never sits in front
   of the §6 action — the hardware chain has already taken it: DRV_EN low, ASC masked).
   V5GD now switches off with V5A, so any FS26 restart (LPOFF, a deep-fail-safe retry, a brown-out)
   also clears the drivers' own latched fault; the NVM record is what survives. The record is A/B: a
   brown-out that also loses retained RAM before the queued write completes loses that one record, and the
   previous one survives — accepted (round 17): the hardware response did not depend on it, and a short
   still present trips DESAT again at the next modulation and is recorded then. Then apply §6. Software
   does not lower `MCU_GATE_EN` before `cal_desat_en_hold_us` after the FLT was first seen (FW-22; the
   fault latch takes DRV_EN low 22–53 µs after the FLT by itself); the PWM inhibit is immediate. **Hardware PWM
   inhibit (round 7, RR03):** `FLT_HS_N`/`FLT_LS_N` (PTC26/PTC25) are routed through the SIUL2
   input mux to eFlexPWM FAULT inputs. They run in fail-safe, manual-clear mode, set at init and
   write-protected. While any FLT is asserted, the PWM outputs are forced low inside the MCU,
   whatever the code does. If the input mux cannot reach a FAULT input from these pins, swap to
   pins that can; that is a card pin swap at zero BOM cost, confirmed before layout.
2. Wait ≥ 1.5 ms with DRV_EN low (latch holds it). **Always clear the ASC latch** (`ASC_CLR`).
   UASCG masks ASC only while FLT is latched. A latch left set would bring ASC back at the step-3
   reset edge while PWM is still forced low: low sides on with IN+ low, where DESAT is not
   documented (Fig. 8.11, cross-check R8X-01). If §6 wants LS-ASC after the recovery, it is
   re-entered only after step 4, through the §4c MCU path (clear FFLAG, PWM-ASC, then `ASC_REQ`).
3. Keep PWM inputs low and MCU_GATE_EN high; drive `PTD9_FLTCLR` high→low once. (The step-2 `ASC_CLR` precedes
   any PWM by ≥ 1.5 ms, far beyond the FW-06a step-3 release deadline.) The 15 nF
   one-shot holds CLR low for 72–210 µs (measured at the Schmitt buffer output). /Q goes high
   and DRV_EN rises 16–46 µs later (delay RC). The drivers see their rising edge and release
   FLT, and PRE returns high while CLR is still low, so the latch ends cleared. If FLT does not
   release, the latch re-sets when CLR returns (fail-safe). **What the one-shot bounds (RR03):**
   it bounds one stuck-low pin, not code that keeps re-pulsing it. That case is covered three
   ways:
   - the eFlexPWM fault lock above keeps PWM forced low while FLT is asserted;
   - a faulted driver stays off while the re-pulsing is **fast**: each falling edge inside the
     72–210 µs one-shot keeps DRV_EN high, so there is no RST/EN edge. **Slower** re-pulsing lets
     the latch re-set and drop DRV_EN between pulses, and a rising edge after the mute time is a
     valid reset (§8.10). The driver then follows IN, which the eFlexPWM lock still holds low.
     FFLAG is cleared only in FW-15 step 4, for the §6 PWM-ASC re-entry or the one VCU-authorised
     retry, and at the end of FW-16 step h;
   - the FS26 watchdog asserts FS0B (FW-12), and FS0B's release needs a token-derived SPI
     write.
4. Confirm FLT_HS/FLT_LS high, RDY high and `ASC_CMD_RB` 0 (latch cleared in step 2) before
   clearing FFLAG and re-enabling PWM. **FLT is a DESAT (short-
   circuit) event, so there is no automatic retry** (round 7, A6-R05): one VCU-authorised retry per
   key cycle, no sooner than 1 s after the event and at reduced torque; a second DESAT latches the
   DTC. Three quick retries into a hard short would be four SC events in ≈5 ms. Gate-supply
   undervoltage is signalled on RDY, not FLT, and recovers when RDY returns.
5. **FW-16** Boot self-test through the `DRV_EN_RB` and `ASC_CMD_RB` read-backs (round 8,
   R7-04/A7-N02). Each shutdown term is tested **while every other term is known permissive**,
   with both polarities, so no term can hide behind another (with gate power off, RDY holds the
   chain low and would mask FS0B).

   **Conditions (measured, cross-check R8X-02):**
   - gate power up (RDY high);
   - **residual energy ≤ 0.1 J** (round 9, A8-N03). "Below 60 V" is not zero energy, and steps a, f
     and h switch gates on through ASC where DESAT is not credited, so the threshold is an energy
     limit:
     - The low-voltage V_DC reading is not trusted as an energy measure: the chain is about ±9 V
       uncalibrated at this level (R9X-07). So either both channels read below **3 V** (≤ 12 V true:
       ≤ 26 mJ 8XX / ≤ 64 mJ 4XX at C_max), or, from any reading below 60 V, QDIS is fired for
       **2 τ** first (≤ 1.35 s 8XX / ≤ 1.64 s 4XX; ≤ 69 V true ends ≤ 9.3 V, ≤ 38 mJ). The top-up
       puts ≤ 1.6 J into the resistors, outside FW-17's thermal count.
     - Both channels must be valid (VOFS and V5GD in their windows).
     - The VCU must report the main contactors open.
     - The VCU starts precharge only after the inverter reports "self-test done" (§9 step 7).
     - 0.1 J is a design limit two orders of magnitude below a rated module short-circuit event. The
       energy-limited fixture confirms that a latent high-side short stays harmless at it.
   - **standstill**: resolver valid and |n| < n_ss, with n_ss chosen from the motor data so that
     E_LL,pk(n_ss) ≤ 12 V;
   - all six PWM outputs held low, so EN high cannot turn a gate on;
   - fault latch cleared.

   If a condition fails, FW-16 is **skipped** rather than run on assumption. That happens after an
   MCU reset with the contactors closed, or at speed. The inverter then arms on the stored result of
   the last complete pass if it dates from this or the previous key cycle (NVM), and reruns FW-16 at
   the next eligible key-on. With no stored pass it does not arm and sets a DTC. A skip never delays
   PWM-ASC after a reset at speed (§9 step 5).

   | Step | Stimulus | Expected | Proves |
   |---|---|---|---|
   | a | MCU_GATE_EN 1, FS0B **asserted** (`FS0B_REQ`) | DRV_EN_RB 0; ASC_CMD_RB 1 within 20 µs (FS1B asserts with FS0B, FS1B_TDELAY = 0 per FW-12, and presets the ASC latch) | FS0B path (USCH ch3, UAND1) not stuck high; FS1B → ASC preset path works |
   | b | release FS0B/FS1B (token write), `ASC_CLR` | DRV_EN_RB **1**; ASC_CMD_RB 0 | chain and read-back not stuck low; ASC clear works; USCH2 alive (a dead one reads as FLT through RFCB) |
   | c | MCU_GATE_EN 0, then 1 | 0, then 1 | UAND1.B |
   | d | FS_GPIO1 **low** (SPI) and MCU_EN_FLYBK_HS 0 until RDY_HS low (record the time), then MCU_EN_FLYBK_HS 1 until RDY_HS high | 0, then 1 | RDY_HS path through USCH3 ch1 into UAND1 (the OR output can only fall with both inputs low, cross-check R8X-03) |
   | e | the same with MCU_EN_FLYBK_LS | 0, then 1 | RDY_LS path through USCH3 ch2 into UAND2 |
   | f | `ASC_REQ`, then `ASC_CLR` | ASC_CMD_RB 1, then 0 | ASC latch clock and clear, ASC gate with FLT healthy |
   | g | FS_GPIO1 **high**, both MCU flyback enables 0 for twice the RDY drop time recorded in d/e, then 1 | RDY_HS and RDY_LS stay high | the FS_GPIO1 inputs of UOR1/UOR2, which hold gate power for FS1B-ASC through an MCU reset |
   | h | `ASC_REQ`; drive PTC26 (FLT_HS_N) low, release, clear through the one-shot; the same with PTC25 (FLT_LS_N); `ASC_CLR` | each time: ASC_CMD_RB 0 at once, DRV_EN_RB 0 within 60 µs, eFlexPWM FFLAG set; after the clear DRV_EN_RB 1 and ASC_CMD_RB 1; finally ASC_CMD_RB 0 | FLT diode-OR (DFLT1/2, RFLTC), USCH2 ch1, fault-latch preset, UASCG mask and the eFlexPWM FAULT routing (cross-check R8X-17) |

   **Step h pad rule.** The FLT pins are pulled low only by toggling the pad's output-buffer
   enable, with its data register held at 0. The pad configuration is re-locked afterwards and
   FFLAG is cleared at the end of the step. A stuck setting can only pull FLT low, which is
   fail-safe; it can never mask a real FLT.

   Steps a, f and h switch the low sides on through ASC at standstill with ≤ 0.1 J in the link
   (measured). That bounds a latent high-side short to a harmless energy. Any mismatch ⇒ no arming
   and a DTC.

   **Field coverage.** Step h now reaches the FLT → fault-latch → FLT_OKB path and the FLT → UASCG
   mask at every eligible key-on; before round 8's cross-check these waited for the EOL rig. The EOL
   rig still drives the FLT lines from the power-board side, which step h cannot reach: the harness
   and the drivers' own FLT outputs.

## 8. Discharge (review R-F22/F23)

- **FW-17** Fire QDIS only when the VCU/BMS reports the main contactors **open**, auto-release
  after 5 s, at most 3 discharges per 5 min (32 J/resistor pulses; thermal recovery).
- **FW-18** Witness on both V_DC channels: expected τ (§2); no decay within 200 ms ⇒ stuck-off
  DTC; the passive bleeder still guarantees < 60 V in 65 s (8XX) / 89 s (4XX) worst case. With
  either witness invalid (FW-07: VOFS, V5GD or disagreement) the HV state is reported **unknown**,
  never safe; service isolation then follows the independent measurement (round 12, R1-F18).
  Round 14 (FW-26): a stuck-ON QDIS (a shorted switch, the battery still connected) is detected at the next
  contactor opening — V_DC decays at the active rate with no command — as a latched DTC, "service required /
  do not re-energise" and "open the contactors" on CAN, kept in NVM; clearing it is the FW-32 UDS routine.
- **FW-19** Precharge plausibility: a link that plateaus ≈5 % below the pack or charges with a
  short time constant indicates a shorted QDIS/string ⇒ refuse to arm. The plateau is the primary check
  (round 17, decided: with a 5 % plateau the τ signature is weak). The verdict is taken when the link
  settles — no new maximum (by more than 0.2 % of the pack) for `cal_precharge_plateau_ms` after its 63 %
  point — or when the contactors close: a link below (1 − `cal_precharge_low_frac`) = 97.5 % of the pack,
  half the 5 % signature, refuses; a 63 % time shorter than `cal_precharge_tau_min_s` (the vehicle's
  R_pre·C_min, a commissioning value) refuses as the second signature; no verdict within
  `cal_precharge_timeout_ms` refuses. A refusal forbids arming for the key cycle. (A stuck-ON QDIS with
  the battery connected dissipates 384 W / 284 W — the fail-open flameproof wirewound class
  bounds it; it is never demonstrated on a live battery.)

## 9. Start-up sequence

FS26 asserts **FS0B and FS1B after every POR or wake-up** (DS §22.11.3). FS1B holds the ASC latch
preset until it is released, and the latch holds ASC once gate power exists. The order therefore
is (round-7 cross-check):

1. KL30 → FS26 rails → MCU boot. V5A also switches on the power board's LV feed (QLVS, round 9, N17),
   so V5GD and V15 come up with the card. In sleep the power board is unpowered, with no parking drain.
   The switch follows V5A, so the firmware parks the FS26 in **LPOFF**. If a Standby mode is ever
   used, LDO2 must be configured off in it; otherwise the power board stays fed.
   DRV_EN, PWM, ASC_REQ, the discharge command and the flyback enables are held low by pull-downs and
   FS0B. The `ASC_CLR` output latch is written high before
   its pin driver is enabled.
2. FW-12 readback → FW-01/FW-02 identity → fault-latch clear through the one-shot.
3. Resolver and both V_DC channels valid (VOFS and the healthy-zero 0.5 V), so the speed n is
   known. The resolver validates only once the SWG ramp reaches its setpoint (FW-30): 20 / 25 / 35 ms
   after init at the high / typical / low MAXAPP corner (host model). After an MCU reset at speed the speed
   is unknown for that time, so step 5 keeps ASC; a resolver not yet valid is not "control lost" (round 17).
4. Release FS0B/FS1B, with MCU_GATE_EN still low, so DRV_EN cannot rise.
5. ASC latch decision:
   - n < n_x (or the battery present and current control ready): `ASC_CLR`.
   - n ≥ n_x: `ASC_REQ` (idempotent). ASC is kept, and PWM-ASC takes over once armed.
   Round 14 (FW-24): FW-16 itself, like every energisation, requires the five arming-evidence items — routing
   bound, configuration matching its image, REG_PROT locked (CTRL2 excepted, INDEP read back), and the EOL/HIL
   record of the pad-to-PWM-fault injection and of the FW-06 chain (≤ 15.6 µs) with CRC, firmware ID, SKU and
   device UID. Missing evidence fails INIT: FS0B is never released and MCU_GATE_EN never rises.
6. Enable the flybacks (S1: one burst, 73–240 ms to rails) and FS_GPIO1 high.
7. RDY → **FW-16** if its measured conditions hold (no HV after the QDIS top-up when the link reads
   3–60 V, standstill; step a re-asserts FS0B by SPI), otherwise the stored pass (FW-16) → report
   "self-test done". A DESAT record in NVM from this or the previous key cycle blocks automatic
   arming (FW-15 one-retry rule), whatever FLT reads now — and names itself: the recorded bank's
   `DTC_DESAT_HS`/`_LS` is raised at the power-up (round 23, §10k item 4). The VCU precharges only after
   that → precharge monitoring (FW-19).
8. Arm with MCU_GATE_EN high — only with the FW-24 evidence complete (status byte 15 names anything missing):
   the FS0B edge has already settled, and RDY rises while MCU_GATE_EN
   is still low.
9. Zero torque.

The firmware's operating states follow this order (round 17, decided): INIT (steps 1–2) → SENSOR_SELFTEST
(steps 3–6) → VEHICLE_HANDSHAKE (a fresh VCU command) → GATE_SELFTEST (step 7: FW-16 or the stored pass) →
PRECHARGE_WAIT (FW-19) → ARMED_ZERO_TORQUE (step 8) → RUN/DERATE. The gate self-test comes before precharge
because the VCU precharges only after "self-test done".

**A driver FLT still low at boot** is a pending DESAT from before the reset (review T7-02,
cross-check R8X-14). The step-2 one-shot cannot release it while FS0B holds DRV_EN low, so the
fault latch re-sets and FW-16 cannot start. The result is no arming, and SPO with ASC masked, which
is what §6 wants. A **V5GD loss** looks similar but is told apart by the V5GD reading on PTD27
(outside 4.75–5.25 V). It is a supply DTC, not a DESAT (§6 V5GD row), and no arming follows.
Round 9 cross-check R9X-01: the first version tested "both V_DC channels invalid", but a dead V5GD
makes the receivers read a valid-looking 0 V. For a real pending DESAT, log the DTC and
apply FW-08b. Run the FW-15 recovery (ASC latch cleared) only
after step 6 and the FS0B release, raising MCU_GATE_EN just for its reset pulse, and under the
one-retry rule.

**Vehicle `FAULT_OUT` (JVEH pin FAULT), electrical interface (round 7, A6-R01 + cross-check):**
- Active-low and **sink-only**: the FS26 FS1B output through 1 kΩ and a Schottky (DFO).
- The **VCU must provide the pull-up**, ≥ 10 kΩ to ≤ 5 V, into a 5 V CMOS input with ≤ 1 nF.
  It reads ≤ 1.2 V when asserted.
- A wire shorted to ground, a sleeping VCU input or a negative spike **cannot** reach the
  ASC-latch preset.
- A short to KL30 is clamped to ground at the latch input. ZSET is a BZT52-B5V6: ≤ 5.96 / 6.12 /
  6.33 V at 16 V / a 24 V jump start / a 35 V load dump, at 125 °C, which stays under the buffer's
  6.5 V absolute maximum. No low-impedance path leads into the V5A logic rail: ≤ 0.14 mA with V5A
  up, ≤ 0.63 mA with it off (round 8, A7-N04 + cross-check R8X-05).
- While FS1B is released the short is silent: the pin reads high, as it should. At the next FS1B
  assertion the outcome depends on the part's current limit, 4–22 mA (cross-check P-01):
  - FS1B still pulls the node low, and the preset works.
  - FS1B cannot pull the node low. The SBC counts an FS1B short-to-high. With
    BACKUP_SAFETY_PATH_FS1B = 0 (FW-12, round 9) that is a DTC, not an MCU reset. The MCU's own §6
    ASC path is unaffected. ABIST1 at the next power-up may refuse the FS0B release: no arming until
    the wire is repaired.
- Either way RFS4 carries the fault current (round 9, A8-03). With the 22 mA limit end that is
  0.23 / 0.48 / 0.65 W at 16 / 24 / 35 V, for ≤ 0.1 s per FS0B event (FS1B_TDUR) or ≤ 0.3 s at a
  boot. If FS1B is never released (MCU never boots, or deep fail-safe), 0.23 W continues at 16 V.
  At a 24 V jump start, a high-limit FS1B current-limits and its pin reads high. ABIST then refuses
  the release, so FS1B stays asserted for the key-on: 0.48 W for 60 s. That is above the nameplate,
  with the element ≈ 149 °C at 25 °C, and is accepted for this triple condition (R9X-14).
  RFS4 is therefore the anti-surge **ROHM ESR03EZPF1001**: AEC-Q200, 0.27 W at 85 °C, overload
  ≈ 1.3 W for 5 s. A plain 0.1 W 0603 does not cover the continuous case.
- FAULT_OUT is a 100 ms low pulse per FS0B event (FS1B_TDUR), and it is also low from power-up
  until §9 step 4. The VCU latches it and reads the fault state over CAN.
- A deliberate 12 V pull-up is not allowed.
- The budget keeps FS1B inside its 2 mA V_OL point, so the SBC's own read-back (low < 0.7 V)
  stays valid.

## 10. Calibration, updates, diagnostics (R-F42/F43, RV-09)

- **FW-20** Versioned, CRC- and range-checked calibration records (current offset/gain/sign,
  V_DC gain/offset per channel, resolver gain/phase/offset, electrical zero, pole pairs) tied to
  hardware serial, SKU and motor ID; any failure ⇒ no torque.
- **FW-21** Signed images, rollback, a no-torque update state; DRV_EN is hardware-inhibited
  through reset, erase/program and debug (pulldowns + FS0B — verified structurally). FW-21 is the bootloader
  + HSE deliverable, not the application image's (round 17, decided); the application's part is the
  default-off outputs above and the image identity its EOL/HIL record is bound to (FW-24). **Round 23: implemented as
  FW-38 (§10f)** — the signed-image check, anti-rollback, the no-torque update state and the bootloader's decision are in
  the application tree and proven on the host; the target bootloader binary, the HSE secure boot, debug lock and
  monotonic counter, and the flash driver remain target items (`firmware/docs/target-bringup.md` T-44…T-50).

## 10a. Round-14 requirements (rev A.13 — reviews of cfd35a7)

- **FW-22** DESAT / global-enable sequencing (A12-R05, F146). `MCU_GATE_EN` is an UNDELAYED input of the
  DRV_EN AND gate; the hardware fault-latch path delays FLT → DRV_EN by 22–53 µs so the NSI6611 can complete
  its local soft turn-off (RST/EN behaviour during soft-off is vendor-unspecified). While either FLT line is
  low, no software path may lower `MCU_GATE_EN` (or change RST/EN) before `cal_desat_en_hold_us` has
  elapsed — 60 µs default, ≥ the RC upper corner plus the FLT filter (verifier row); PWM inhibit stays
  immediate (the hardware fault input already did it). The hold lives in the bridge module so every caller
  is covered; non-DESAT emergencies (no FLT low) keep the immediate drop. The hold starts from a time read
  taken after the FLT lines were read: the bridge service reads its own time, never a caller's stamp, which
  could predate the FLT edge and end the hold early (round 17).
- **FW-23** Single monotonic time base (A12-R06, F147). All timestamps derive from one 64-bit monotonic
  microsecond clock (the 32-bit hardware counter extended on every read, read at least once per 71.6 min,
  atomic across ISR/task); milliseconds are derived from it so that unsigned ages are valid across every
  wrap. No timestamp producer divides the raw 32-bit counter.
- **FW-24** Arming evidence — fail closed (F01/F02/F06, F150/F151). Gate enable, the FW-16 self-test
  energisation and any torque are refused unless ALL of: PWM fault-input routing bound (IMCR/SSS values
  from the reference manual — placeholders are a build error; filled values must be non-zero with two
  different IMCR indices, while the two SSS values select inside different IMCRs and may be equal — round 17), the fault/complementary configuration
  matches its image, the eFlexPWM protection is write-protected (REG_PROT/XRDC lock bits read back — a
  readback of the image is NOT a lock witness), and the EOL/HIL record (NVM, CRC, tied to hardware and
  firmware identity) certifies the physical PWM-fault route (PTC26/PTC25, CPU halted) and the ADC-watchdog
  → PWM-fault OVP chain (≤ 15.6 µs). The CAN status names the missing evidence.
- **FW-25** Torque-request feasibility (F23, F155) — **superseded in round 23 by FW-37 (§10e): the reduction is a joint solve on the torque curve, not "i_q, then i_d"; this item is kept as the history of the witness.** After the demagnetisation and current-circle clamps the
  request carries a final voltage-magnitude witness (ω_e, ψ_f, L_d, L_q, R_s, the reserved dynamic
  voltage); iq, then id, are reduced until feasible; if infeasible at iq = 0 the request is refused
  explicitly — zero torque, a speed-limit request to the VCU and a DTC — never a finite but unattainable
  current pair.
- **FW-26** Unexpected-discharge detection (F09, F157). V_DC falling at the active-discharge rate while
  discharge is not commanded (a shorted QDIS: 0.45 A / 96 W per resistor at 850 V) latches a
  no-re-energise DTC, requests contactor opening and blocks the next precharge until service — cleared only by
  the FW-32 routine. The resistor's benign failure at that power stays the hardware gate (㉖).
- **Vehicle interface (round 14).** Status bytes 14–15 carry: no safe state proven, service required, open
  the contactors, speed-limit request, and the missing arming evidence — to be added to the DBC. Clearing the
  service lock is the FW-32 UDS routine (round 17); the EOL/HIL rig that writes the validation record is
  outside this repository — its record format is `arm_validation_t` and its steps are checklist items
  (`firmware/docs/target-bringup.md`).
- **FW-05 addendum** — KCL coverage (F24, F156). Σi = 0 does not detect a channel stuck at zero at zero
  current nor an equal gain error on all channels; each channel must show activity when |i*| exceeds a
  threshold, and the 0.2–4.8 V window, range and stale checks stay. Coverage per operating state is
  tabulated in `firmware/docs/traceability.md` ("Phase-current diagnostic coverage per operating state").
  An equal gain error on all three channels is bounded there, not diagnosed at run time (round 17, decided):
  by the EOL calibration record (FW-20: gain 1.9–2.6 mV/A, CRC- and serial-bound), by the FW-05 compare
  being built from the same calibrated gains (the hardware and software trips move together), and by DESAT
  (≈ 2.8× the rated peak) as the independent short-circuit path — an EOL/periodic-calibration item.

## 10b. Round-16 requirements (rev A.15 — rechecks of 32214be)

- **FW-27** Phase-current acquisition is all-or-nothing (A14-R03/R04, F171). `hal_adc_read_phase()` delivers a
  complete U/V/W triplet of one trigger with its conversion stamp, or reports failure and writes nothing — never a
  zero-filled channel, never an unwritten stamp. The caller consumes only complete triplets; a lost triplet marks
  the current sample invalid through the sensor-failure path (a stale-sample fault, the last good stamp kept, never
  "now"), while V_DC and resolver acquisition continue. A current loop that stops running is caught by the task
  (liveness), not by the last duty cycle standing.
- **FW-28** Resolver validity expires by age, every tick (A14-R01, F172). The rotor angle is valid only while the
  age of the last accepted coherent frame is under a calibrated hold derived from the angle-error envelope
  (standstill, low speed across the microsecond wrap, full speed); past it the angle is withdrawn and the
  resolver-invalid safe-state selection of §6 runs. An empty read by itself never faults. Re-acquisition starts
  from scratch (FW-30 ramp), and a resolver that was never valid raises no "control lost" row in a no-arm state.
- **FW-29** One resolver frame is one epoch of all three SDADC channels (A14-R02, F173). EXC, SIN and COS are
  published only after each channel's own DMA completion for the same epoch, copied atomically with the epoch and
  the DMA position checked across the copy (a bounded copy time); a frozen, late or overwritten channel yields no
  frame and feeds the FW-28 age instead of a mixed-generation tuple. The host simulation models each channel's DMA
  and its interrupt separately, so the tests can freeze, delay or preempt any one of them.
- **FW-30** Excitation amplitude planes (A14-N01, review 3 A14-R04, F167). FW-10's monitor level is defined at the
  protected node (after RSX, before the PTC and the harness): winding = monitor × 70/72.6 cold, × 70/80 after a
  PTC trip. The trim setpoint is **7.2 V pp at the monitor**, reached by a ramp from a low SWG code (the untrimmed
  maximum would slew-limit); the firmware carries the planes and checks headroom at every parameter/calibration
  load — the SWG low corner (1.884 V pp MAXAPP), the −40 °C slew ceiling and the 6.5 V pp cold floor at the winding —
  and refuses a record that credits a post-trip PTC. Ready/saturation states are explicit (a saturated trim is a
  DTC, not an armed inverter at 6.1 V pp); the resolver becomes valid only once the ramp reaches the setpoint. The
  calibration record is layout 2 (monitor gain in codes per V pp); `TI_FW_ID` 0x0A0F0010 — a new EOL/HIL validation
  record and a new calibration record are required before this image arms (FW-20; round 17 moves the image to
  0x0A0F0011, §10c).
  Round 17 (decided, as implemented). **Planes:** the setpoint is at the monitor; the 6.5 V pp floor is judged at
  the winding, estimated as monitor × `cal_rslv_wind_per_mon` (70/72.6 with the PTCs cold; EOL with the real
  harness replaces it) × the resolver's own ratiometric output relative to its EOL ratio (calibration record),
  so a PTC, harness or resolver change since EOL shows; at the trim band's low edge (0.95 × 7.2 V pp) the cold
  winding is 6.60 V pp, 1.5 % above the floor — EOL/HIL confirm no nuisance trip (checklist). **RSX** (2.2 Ω per
  line) sits on the amplifier side of the monitor tap (A.14 net list, A13-R01): amplifier = monitor × 77/72.6 =
  7.64 V pp; downstream of the tap only the PTCs and the harness (70/72.6 cold, 70/80 after a trip). **Exciter
  gain:** the A.15 MFB (13 k / 28 k) has |H(10 kHz)| ≈ 2.07 (2.072 from the verifier's own formula), so the SWG
  needs 7.64 / (2 × 2.072) = 1.843 V pp, 2.2 % under the 1.884 V pp MAXAPP low corner, and each ALM2402 output
  swings **1.91 V pk per output**, under the 2.07 V pk −40 °C slew ceiling. **SWG code law:** the ramp starts at
  `cal_swg_code_init` = 8 (≈ 1.45 V pp at the MAXAPP maximum corner), which assumes IOAMPL linear between MINAPP and
  MAXAPP (the datasheet gives the two ends only) — a silicon/RM checklist item; the trim closes on the monitor
  and does not rely on the law. **First acquisition:** valid only once the ramp reaches the setpoint (§9 step 3).
- Incidental (round 16): FW-15's "no sooner than 1 s" retry gate compares microsecond stamps (a floored
  millisecond count let a retry through at 999.8 ms).

## 10c. Round-17 closure (rev A.15 — the firmware's open items decided)

Every item of the firmware's former list of "contract contradictions and open items" (35 items,
`firmware/README.md`) now ends in one of two places: the implemented decision written into this contract (the
"round 17" text of §2, FW-03/04/05/06/07/08/08b, §4c, FW-11/12, §6, §7, §8, §9 and §10–10b, listed per item in
the README), or the silicon / RM / HIL / EOL checklist `firmware/docs/target-bringup.md`. Nothing is left as
"the contract should say".
Round 18 (rev A.17, the rechecks of 4425af9) adds FW-34…FW-36 — sample freshness, the resolver time base, the
latency sign — in §10d.
Round 19 (rev A.18, the rechecks of e315bf1) rewrites FW-35 in §10d: the resolver cadence's origin is the SWG start,
never a completion; re-sync from the clock; the synchronized producer restart.
Round 23 (the external review of the torque → current path) adds FW-37 in §10e: the torque solved jointly with the
voltage and current constraints, a postcondition on every current vector.

- **FW-31** Current-loop liveness (round 16, named in round 17). The 1 ms task checks that the current-loop
  interrupt ran within `cal_isns_stale_us` (200 µs, range 50–1000); otherwise the phase currents count as lost
  (FW-27: the "control lost" row) and the resolver ages (FW-28). A stopped BCTU trigger or a list that never
  completes is caught within one task period instead of leaving the last duty cycle standing.
- **FW-32** Service-lock clear (FW-18/FW-26, round 17). The stuck-on QDIS lock is cleared only by a UDS
  RoutineControl start of routine 0xF010 on the diagnostic bus (ISO 14229-1 over single-frame ISO 15765-2;
  request 0x7E1, response 0x7E9 until the OEM diagnostic specification binds them), after a SecurityAccess
  (0x27) seed/key unlock. The key function is a build-time hook: the default build has none, so every seed
  request is refused (NRC 0x22) and the lock cannot be cleared — fail closed; a product build supplies the
  OEM algorithm and a TRNG seed (checklist). Three invalid keys refuse further seeds until the MCU restarts
  (power-up or reset); one unlock allows one completed run. The routine is refused (NRC 0x22, nothing written) while HV is present
  or unknown (FW-18) or the bridge is armed. It rewrites the NVM lock record as cleared, with the key cycle of
  the clear, and sets a "service lock cleared" DTC; a clear that cannot be queued is refused (NRC 0x72) and the
  lock stays. The clear takes effect at the next power-up: the running key cycle keeps the lock, its CAN status
  bits and its no-arming.
- **FW-06a ASC exit** (round 17): the first high-side pulse waits `cal_asc_release_ns` (1.5 µs; the ASC pins release
  ≤ 1.07 µs after the clear) plus the SKU dead time from the ASC_CLR falling edge — §4c FW-06a step 3; the ASC
  entry and release figures follow design-verify (Safety A.8): ≥ 4.42 µs, ≤ 7.56 µs, ≤ 1.06/1.07 µs.
- **FW-12 refresh cadence** (round 17, checklist T-32): the answer first thing in the 1 ms task, every second
  task, 1890–2110 µs apart, inside the FS26 window (1.579–2.857 ms at the fail-safe oscillator's ± 5 %) with
  margin at both ends — §5, "FW-12 refresh cadence".
- **FW-33** LV supply supervision (round 17, the let-through LV entry): VSUP through the FS26 AMUX; an
  overvoltage is information for as long as the vehicle interface allows — above 27 V for 500 ms (IR-03 test B),
  at or below it for 65 s (IR-02 jump start), each a range-checked CAL — and the §6 command-lost ramp beyond
  that — §5, "FW-33 LV supply supervision"; checklist T-39.
- **Image identity.** `TI_FW_ID` 0x0A0F0015 since the round-23 gap closure (0x0A0F0014 the round-23 torque-solver image; 0x0A0F0011 round 17, 0x0A0F0012 round 18, 0x0A0F0013 round 19): round 17 changed the image (zero current under the battery-lost
  row, the RUN-only DC-link trim, FW-32, the FW-12 refresh cadence, the FW-06a release wait, FW-33). A new
  EOL/HIL validation record is required before it arms (FW-24); the calibration record stays layout 2. (Round 18
  moves the image to 0x0A0F0012 and round 19 to 0x0A0F0013, §10d; round 23 to 0x0A0F0014, §10e, and to 0x0A0F0015 with §10f–§10k.)

## 10d. Round-18 requirements (rev A.17 — rechecks of 4425af9; FW-35 rewritten in round 19, rev A.18 — rechecks of e315bf1)

- **FW-34** A sample's freshness is judged at or after its acquisition (A16-R01). The target stamps a sample
  when it reads it — in the current-loop ISR, after the ISR read its entry time — and a higher-priority
  interrupt may publish a newer stamp while a lower context holds an older time. Every freshness check (phase
  currents, the V_DC channels, the resolver frame's age, the 1 ms task's current-loop liveness) therefore uses
  a time read after the acquisition reads, and the age is signed: a stamp that postdates the check time by an
  ISR's execution is fresh, never 2^32 µs old; a stamp the hold or more before it — or implausibly the hold or
  more after it — is stale. The same holds for a timer whose start a higher-priority context stamps: the FW-15
  recovery's ≥ 1.5 ms low runs from the fault ISR's stamp on the bridge's own clock, read when it is compared —
  never the 1 ms task's earlier time (the rule of FW-22: the bridge reads its own time, never a caller's
  stamp). The PWM-ASC entry's dead-time reference is the same: with the PWM already inhibited, the bridge
  counts the dead time from the newer of its own turn-off stamp — every turn-off it makes, and the hardware inhibit the
  fault ISR notes on entry — and the caller's, so a task whose time predates a fault that preempted it cannot shorten it. The ISR's entry time stays the ISR's own: its liveness stamp (FW-31), its WCET reference, the angle the
  FOC uses (the currents were sampled at its trigger) and every bridge action. The host simulation models the
  target's order (each ADC read takes simulated time before it stamps, the other interrupts running meanwhile);
  the checks hold with 1, 5 and 50 µs per read, across the 32-bit microsecond wrap, with the current-loop ISR
  preempting the task, and with a fault preempting it before its FW-15 recovery.
- **FW-35** The resolver frame's time stamp is independent of interrupt latency (A16-R02), and so is its origin
  (round 19, A17-R01). The SDADCs are triggered by the SWG period start (TRGMUX), so block k starts at
  t_org + (k − k_org)·T_carrier on the carrier cadence, where t_org is the **SWG start** on the microsecond timer:
  `hal_swg_start()` brackets the generator enable with two reads of the 64-bit timer (PRIMASK: nothing runs between
  them) and anchors the ring at the later read; k_org is the first carrier period's block. The origin is uncertain by
  ± u = the bracket (+ 1 µs of timer resolution) + `cal_swg_start_lat_us` (the SGEN's start to its first period plus
  the TRGMUX/SDADC trigger latency: 2 µs, range 0–20 µs; checklist T-42) — 3 µs by default, which bounds every stamp's
  error (0.7° el at 10 000 rpm, 4 pole pairs); an anchor with u ≥ T_carrier/4 is refused (lost). No completion ever
  sets or moves the origin, and an unanchored ring counts and publishes nothing. **Every** completion — the first after
  the start and the first after a break included — is judged against the absolute cadence, u added on both sides: a
  block's first completion must come within [−u, `cal_sd_irq_lat_max_us` + u] of the block's end (30 µs, range 5–45 µs,
  measured from the carrier boundary: the SDADC's own output latency counts in it — checklist T-40), a later channel's
  within [−u, T_carrier/2 + u]. Earlier is impossible (a completion never precedes its block's end); later is ambiguous —
  the 4-slot ring may have lapped, or the channel holds another period's block — so the block is not published and the
  ring breaks. The reader refuses a frame whose slot the cadence says may have been rewritten. A broken ring re-syncs
  from the clock — the block that ended within [−u, T_carrier/2 + u] of a completion — with the DMA write positions only
  confirming it: every DMA past that block, or a later channel still on it (its own completion decides). A DMA
  anywhere else, the completing DMA still on the block while another is past it, or no agreement within four carrier
  periods of completions is a DMA out of phase with the carrier: the ring is **lost**. Equal DMA positions alone
  establish nothing. The platform sets `lost` as well for the eDMA error, the SDADC FIFO overrun and a missed or
  erroneous conversion trigger, so a channel cannot resume out of phase silently (checklist T-41). A lost ring stays
  down until a **synchronized producer restart** — the DMA rings re-armed, the SWG restarted at its present amplitude
  code and re-anchored (the demodulation takes its phase from the EXC channel, so the re-phased excitation is
  harmless; the trim goes on from its code) — which the application requests at most `cal_rslv_restart_max` times per
  key cycle (3, range 0–10; the count is retained across an MCU reset inside the key cycle). Each restart and each
  re-acquisition after a break is one occurrence of the information DTC `DTC_RSLV_REACQUIRED`, which selects no safe
  state by itself; while frames are absent the resolver ages out (FW-28) and re-primes when they return; beyond the
  restart limit it stays invalid for the key cycle (FW-28, the §6 "resolver invalid" row). Times are 64-bit, so the
  clock-derived block index holds across the 32-bit microsecond wrap and any silence. The SWG start latency and the
  first block's carrier phase 0 (T-42), and the completion latency and the cadence against the STM (T-40, T-30), are
  target measurements. Round 18's residual — the origin carrying the latency of the completion that anchored it — is
  gone: a late first completion, a constant delay and a delay rejected once are judged against the SWG start every
  time and never become the reference.
- **FW-36** The resolver chain latency is compensated with its physical sign (A16-R03). `cal_rslv_latency_us`
  is a positive delay: the block's angle is the rotor's that long before its mid-block reference, so the
  extrapolation to the control instant adds it — θ(now) = θ_block + ω·((now − t_ref) − t_mid + L). It was
  subtracted (−12 / −24° el at 10 000 rpm, 4 pole pairs, 25 / 50 µs). The calibration measures it with this
  sign — the reported angle lagging the rotor is positive (checklist T-37) — and a test with an independent rotor
  model (both directions, two speeds, two delays) checks the compensated angle against the true angle at `now`.
- **CALs** (round 18, round 19): `cal_sd_irq_lat_max_us` 30 µs [5, 45]; `cal_swg_start_lat_us` 2 µs [0, 20] (the
  origin's uncertainty is the enable bracket + 1 µs + this); `cal_rslv_restart_max` 3 [0, 10] per key cycle. Each is
  range-checked at every boot (`ti_params_validate` ⇒ DTC_PARAMS_INVALID, no arming).
- **Image identity.** `TI_FW_ID` 0x0A0F0012 (round 18): a new EOL/HIL validation record is required before
  this image arms (FW-24); the calibration record stays layout 2. Round 19 (FW-35's origin) moves the image to
  **0x0A0F0013**: a new EOL/HIL validation record again, the calibration record still layout 2.

## 10e. Round-23 requirements (the torque → current review)

- **FW-37** — torque solved jointly with the voltage and current constraints; a returned current vector never
  represents more torque than requested; postcondition. The external review of round 23 found that field weakening
  moved i_d more negative (and the current circle clamped it) with i_q kept: with L_q > L_d that i_d adds reluctance
  torque at the same i_q, and the FW-25 witness checked the voltage only — on a salient motor inside the calibration
  ranges (L_d 0.2 mH, L_q 0.8 mH, 25 mΩ, 0.1 Wb, 4 pole pairs) ±100 N·m became +195.8 / −205.1 N·m at 400 V,
  2000 rad/s el, 250 A rms; and the MTPA fallback was an 8-step fixed point without a convergence criterion
  (+8.6 % current at 200 N·m on a more salient motor). Every (i_d, i_q) now lies on the torque hyperbola
  T = 1.5·p·(ψ + (L_d − L_q)·i_d)·i_q of the torque it stands for, with i_d in [−min(I_max, I_demag), 0]:
  (a) the MTPA point — a calibration LUT's only where its point reproduces the torque within 0.1 %, else the
  least-current point of the hyperbola, by bisection with an interval criterion; (b) where that does not fit, the
  least-current point of the hyperbola inside the voltage ellipse (the FW-25 reserve kept), the current circle and
  the demagnetisation limit — on a hyperbola |i|² and |v|² are convex in i_d, so each limit holds on one interval and
  the search is exact to 0.01 A (TQ_OK: the torque requested); (c) only where no point of the hyperbola fits, the
  torque is reduced by bisection (to 0.05 N·m) to the largest feasible torque of the same sign (TQ_LIMITED) —
  monotone, since the feasible set is convex and holds a zero-torque point. This replaces FW-25's "i_q, then i_d"
  reduction; FW-25's refusal when not even i_q = 0 fits is unchanged. (d) A postcondition on every returned vector,
  whatever path produced it: its torque, recomputed, has the request's sign (or is zero) and |T| ≤ |T_req|·1.001 +
  1 mN·m (within 0.1 % of it for TQ_OK), and its voltage, current and demagnetisation limits hold (to 1e-6); a
  failure returns no vector (TQ_POSTCOND): a DTC, zero torque, no current reference and, while the bridge is armed,
  the §6 "control lost" row at once. INV_STATUS (20 bytes since round 23) reports in b4–5 the torque the issued
  current references represent — the torque applied (FW-08), below the command when the limits reduce it — and in
  b16–17 the command beside it; the DBC carries both (checklist T-38).
- **Image identity.** `TI_FW_ID` 0x0A0F0015 (round 23, second image; 0x0A0F0014 before the gap closure): a new EOL/HIL validation record is required before this
  image arms (FW-24); the calibration record stays layout 2.

## 11. What this contract does not close

Double-pulse (turn-off overshoot at 850 V, R_G_OFF), contained short-circuit tests per silicon,
thermal/coldplate, EMC, LV transients (ISO 16750-2/7637-2 per the OEM contract), insulation
coordination and mechanical DV are hardware gates — listed in
[`review-A6-disposition.md`](review-A6-disposition.md) and, for round 7, in
[`review-A7-disposition.md`](review-A7-disposition.md). The round-7 gates are:
- the LV-only gate-supply bench (six-domain VCC2 across KL30/temperature, start/stop/jump start);
- the ASC entry measurement (HS gate vs LS gate at no HV);
- the HIL measurement of the FW-06 chain.

## 10f. Round 23 — firmware update (FW-38)

- **FW-38** Firmware update: signed images, anti-rollback, a no-torque update state — FW-21's requirement, implemented in
  the application and as a host-testable bootloader decision; the opposite of an unsigned loader that takes any
  CRC-correct image (`docs/firmware-vs-vesc.md`, gap 1). What stays the target's is in the checklist
  (`firmware/docs/target-bringup.md`, "FW-38": the flash driver and map, the release key in the HSE/OTP, the reset, the
  bootloader binary and its FS26 handling, the HSE secure boot and debug lock, the HSE monotonic counter, EOL).
  **Container** (`firmware/src/boot/image.h`, built by `firmware/tools/sign-image.mjs`): a 128-byte header — magic "TIFW",
  format 1, header length, target (the SKU), payload length, the payload's `TI_FW_ID`, a security version (anti-rollback),
  8 reserved zero bytes, the SHA-256 of the payload — and an Ed25519 signature over its first 64 bytes by the release key.
  **Check** (`boot/verify.c`, the same code in the application and the bootloader): the structure, then the signature
  (Ed25519 vendored from TweetNaCl, public domain, with RFC 8032's S < L added; constant-time comparison), then the signed
  target against the card's SKU, the security version ≥ the device's counter, and the SHA-256 of the payload read back
  from flash — each refusal with its own reason. The public key is a `const` table (a host build: the TEST key only; a
  target build: the release build's `-DTI_FW38_PUBKEY`), overridden by the platform's OTP/HSE copy; no key, or an
  all-zero one, verifies nothing.
  **Update state** (`boot/update.c`, `boot/uds_update.c`; UDS request 0x7E1 / response 0x7E9, one dispatch in `uds.c`):
  DiagnosticSessionControl programmingSession enters it only with the bridge disarmed and the link discharged (the FW-32
  conditions) and the motor at standstill (the FW-16 condition: resolver valid, |n| < n_ss; without a valid calibration
  no standstill is proven) — else NRC 0x22 and nothing changes. Entering withdraws the EOL/HIL-validated arming evidence
  (FW-24) and forbids arming until the next power-up (`DTC_FW_UPDATE`, FAULT, INV_STATUS b15 names the missing evidence):
  the state cannot arm and applies no torque; the §6 protective actions keep their authority. RequestDownload needs the
  SecurityAccess unlock (address 0 = the staging region, size within it, no compression or encryption); TransferData
  checks the block sequence counter (from 1, wrapping; the previous block repeated is acknowledged, not written twice),
  the length and the announced size, and answers busyRepeatRequest while the background owes flash work;
  RequestTransferExit needs every announced byte; RoutineControl 0xFF01 verifies the staged image (start / results:
  running, passed, failed + the reason) and 0xF038 activates it (verified, the conditions again, the boot record IDLE →
  ACTIVATE); ECUReset once nothing is owed to flash or NVM and the request reads back from NVM. The flash work runs in
  the background loop, never in the 1 ms task or an ISR. Round 23 (§10k item 11): TransferData travels as an ISO 15765-2
  segmented request (FW-40's transport with its receiving side, §10h): `UPD_BLOCK_MAX` 4095 (the SID, the counter and
  4093 data bytes; the single-frame transport carried 5), a 1 MiB image in 257 blocks, ≈ 4.6 s host-simulated.
  **Bootloader decision** (`boot/boot.c`, at every reset): the boot record (NVM `NV_REC_BOOT`, A/B; magic, layout, the
  card's SKU, the anti-rollback counter, a last-known-good flag, the outcome of the last activation) is a write-ahead
  journal and every flash step copies from an intact source. An activation verifies STAGE again (signature, hash, target,
  security version ≥ the counter): refused, nothing is touched; accepted, INSTALL is written before STAGE is copied to
  EXEC, EXEC is verified, and TRIAL runs the new image once. The image confirms itself after 5 s of running; the next
  reset commits it (EXEC → LKG) and raises the counter to its security version. A reset in TRIAL (a crash, a watchdog, a
  power loss before the confirmation), an interrupted INSTALL, or an EXEC that fails its check at any boot restores the
  last known good (LKG → EXEC); nothing valid ⇒ no image starts and the gate stays hardware-inhibited. Without a record an
  EXEC that verifies is adopted under its own signed target and security version.
  **Power loss (host-proven):** a cut at every write of the swap boots the last known good; a cut during the restore or
  the commit resumes it; a torn record write keeps the last durable record — the counter never goes back. Found on the
  way: a record that ends in its own CRC-32 makes the NVM slot's CRC-32 blind to it (a CRC-32 over M ‖ CRC-32(M) is the
  same for every M), so a torn write validated with the stale bytes left in the slot — the record from two writes back;
  the boot record carries no CRC of its own for that reason.
  **Open, not FW-38's to change:** (1) the FW-20 calibration record (`calib_t`) and the FW-24 validation record
  (`arm_validation_t`) end in their own CRC-32 of the same polynomial, so a torn write of either can resurrect the one
  from two writes back: the NVM layer's slot check needs another polynomial (or those records their CRC first).
  **Closed in round 23 (§10k item 9):** the slot check is a CRC-32C over the slot header and the payload.
  (2) An updated image has a new `TI_FW_ID`, and FW-24 binds the EOL/HIL validation record to it: after a field update
  the inverter does not arm until a record for the new image exists on that card — the release process must deliver it,
  or FW-24's binding must move from the image identity to what the rig measured. (3) FW-21's text ("the bootloader + HSE
  deliverable, not the application image's") and its rows in `firmware/README.md` and `firmware/docs/traceability.md`
  predate this item. **Closed in round 23 (§10k item 12):** they point to FW-38.

**Root of trust, reported (second pass of round 23).** DID 0xFD23 (5 bytes) says which key the verifier holds: kind 0 none
(every image refused), 1 the host build's TEST key (`sign-image.mjs --key test`), 2 the release build's `-DTI_FW38_PUBKEY`,
3 the platform's OTP/HSE copy — then the key id, the first four bytes of SHA-256(public key). The simulator's hello carries
the same (`root.kind`, `root.key_id`), the service tool shows a banner on a TEST root, and QP-EOL-13 reads the DID and
refuses kind 1 at the final station: a unit whose verifier holds the public test key is a development unit, never a
shippable one. It is deliberately not a DTC — an always-active information DTC would fail every "no active DTC"
precondition (FW-39's among them) on every development unit. The TEST key's id (0xD51131AD) is pinned in the test suite so
a changed test key is noticed.

## 10g. Round 23 — motor self-commissioning (FW-39)

- **FW-39** Motor self-commissioning in an interlocked service mode (the VESC comparison's gap 2, built the other way round:
  never a bypass, never trusted live). `firmware/src/app/commission.c`.
  - **Entry.** Only through the FW-32 SecurityAccess unlock (one start per unlock), with the service tool's attestation of
    the rig in the request — `LK` (0x4C4B: the rotor held by the rig's brake) for the standstill routines, `DF` / `DR`
    (0x4446 / 0x4452: a dyno drives the rotor forward / backward at a constant low speed) for the back-EMF routine — and
    only when all of these hold, checked in this order (the first failure is the refusal, NRC 0x22, its reason readable):
    armed through the normal path (ARMED_ZERO_TORQUE with the bridge armed idle: the FW-24 evidence, the FW-20 record,
    FW-16 and precharge are behind it); the evidence still complete, arming not forbidden, the record valid, a gain set;
    no §6 row; no active DTC (the service records `DTC_SERVICE_LOCK_CLEARED`, `DTC_MC_ABORTED`, `DTC_MC_CAL_WRITTEN`
    excepted); a fresh VCU command without enable; the VCU's vehicle speed valid and ≤ 0.5 km/h; V_DC valid inside the
    SKU's normal range with the battery path proven (contactors CLOSED, the BMS frame fresh, V_DC at the pack); the
    resolver valid and the speed the routine needs (≤ 5 rpm for the standstill routines; 150–450 rpm and ≤ 0.25·n_x for
    the back-EMF routine); every service CAL inside its range.
  - **While a routine runs**, every 1 ms: the same list, plus the tool's heartbeat (any request of RID 0xF020 at least
    every 200 ms), the locked rotor's motion (≤ 2° el from its start angle, ≤ 5 rpm), the dyno's steadiness (within 5 % of
    its start speed), the current (≤ 1.5 × the routine's amplitude + 20 A: this also bounds the start transient of a
    record's ψ far from the machine's, Δψ·ω_e/k_p), the loop's voltage headroom (no saturation) and the routine's
    schedule. Any loss aborts it to the normal safe state — no service modulation: the bridge armed idle with the PWM off
    from the next current-loop ISR, or the §6 decision's when a row caused it — with `DTC_MC_ABORTED` (not for the tool's
    own stop) and no result.
  - **No torque command is accepted.** From a routine's start the state machine does not see the VCU's enable (it stays
    in ARMED_ZERO_TORQUE; the current references are the routine's, per sample); an enable is itself a precondition loss
    that ends the routine and is not executed: the bar holds after it until the VCU has withdrawn its enable once.
  - **Routines.** The current loop regulates in every one — no open-loop voltage is applied. Measured: the voltage the
    bridge is commanded and the phase currents, accumulated in the current-loop ISR over 8 blocks, estimated in the 1 ms
    task at the end.
    - **R_s**: two DC levels (30 and 60 A) along phase U's axis on the locked rotor — every phase carries at least half
      the current, so the inverter's dead-time error is saturated at both — R_s = ΔV/ΔI from the duties: the constant
      inverter error cancels between the levels.
    - **L_d, L_q**: a sinusoidal current reference of 20 A at f_isr / 40 (500 / 400 / 250 Hz at a 20 / 16 / 10 kHz loop)
      along d, then along q, at standstill, on a 50 A DC bias along phase U's axis (≥ 2 × the HF amplitude: no phase
      current crosses zero, so the inverter error is linearised at one operating point for both runs — without it that
      error is a different real matrix in each run and the solve turns the difference into reactance). **Current
      injection, not voltage injection**: the existing loop regulates the test amplitude by construction, whereas a
      voltage injection needs a summing point inside `foc_step` and drives an unbounded current into an inductance not
      yet known. The voltage is the duties' (they contain the FOC's dead-time compensation, which acts on a current
      sampled 1.5 periods earlier and would read as reactance in its own v_d/v_q). The 2 × 2 impedance from both runs'
      phasors, exact whatever cross current the loop leaves; the actuation delay (1.5 periods, `s6_delay_tsw`) and the
      hold (V/I = jωL·sinc(ωT/2)·e^{jω·1.5T} for the sampled current) undone; L = Im Z / ω; L_d, L_q its eigenvalues in the
      frame of the zero used — the back-EMF zero of this key cycle when the dyno routine gave a VALID one, else the
      record's. Which eigenvalue is d needs that zero within 45° el of the truth; a saliency axis more than 15° el from it
      is AXES (after a resolver or motor change, the dyno routine first).
    - **ψ and the electrical zero**: i_d = i_q = 0 with the dyno turning the rotor; the back-EMF in the controller frame
      e_d = ω_e ψ sin ε, e_q = ω_e ψ cos ε (the loop's own v_d/v_q, the small R and ωL terms taken off): ψ = |e|/|ω_e|, the
      new zero = the record's + ε. The resolver direction: the voltage vector's turn from the duties (independent of the
      resolver) against the resolver's speed — DIR_PHASES, a resolver turning against the phase sequence — and the
      resolver against the attested dyno direction — DIR_DYNO. The zero identified is the record's `zero_rad` (the
      offset between the resolver's zero and the d-axis); the record's `phase_trim_deg` is the demodulator's carrier
      phase, an EOL item, not identified here.
  - **Results**: value, standard uncertainty (the spread of the 8 block estimates, a type-B floor of 1 % for the gains
    of the current and V_DC chains; for the zero ω_e × 20 µs of timing), verdict: VALID, NOISY (uncertainty above 5 %,
    1° el for the zero), CLASS, NOT_REACHED (the loop did not reach the test current), AXES, DIR_PHASES, DIR_DYNO.
  - **Never trusted live.** A VALID value outside the FW-20 class limits (`calib_check`'s own ranges) is CLASS. Compared
    with the active record: inside its band (R_s 10 %, L_d/L_q 10 %, ψ 5 %, the zero 2° el) it is staged; beyond it, only
    after a second run agrees within 3 combined standard uncertainties (then their mean). RoutineControl 0xF021 (a fresh
    unlock, no routine running, the bridge not switching: the record's NVM copy is a PRIMASK section of ≈ 1 µs, which
    would add to the FW-06 action segment) copies the active record, puts the staged values in (a stored MTPA table solved for the old L_d, L_q, ψ is
    dropped: the closed form until a new table is sealed), seals it (`calib_seal`), checks it (`calib_check`: layout,
    CRC, ranges, SKU, serial = the device UID, motor ID — any failure writes nothing) and queues it as a new NV_REC_CALIB
    version (the A/B slot's sequence number), with `DTC_MC_CAL_WRITTEN`. The running key cycle keeps its record — nothing
    is applied mid-routine or mid-cycle; the next key cycle's init validates the new one (FW-20) before anything arms
    (FW-24). The motor ID is the record's: a motor of another type is an EOL record, not this routine.
  - **CALs** (`mc_cal_t`: 31 values, each with its range, `firmware/src/app/commission.c`): range-checked at every start —
    one outside refuses the service mode (reason CAL), never driving. They are not in `ti_params_t` for that reason: a
    commissioning parameter must not be able to forbid arming.
  - **Vehicle interface**: VCU_CMD b4 [5] vehicle speed valid, b6–7 vehicle speed in 0.01 km/h (a sender without the field
    leaves [5] at 0: not valid, never "stationary"); the DBC carries it (checklist T-38).
  - **UDS** (single frame, the FW-32 identifiers 0x7E1 / 0x7E9): `31 01 F0 20 [routine] [attestation BE16]` start,
    `31 02 F0 20` stop, `31 03 F0 20 [index]` results — also the tool's heartbeat — and `31 01 F0 21` commit.
  - **On the host** (the virtual PMSM `firmware/src/platform/host/sim_pmsm.c`: L_d 0.30 / L_q 0.55 mH, R_s 21 mΩ,
    ψ 0.128 Wb, the zero 20° el off the record, a dead time 10 % longer than the FOC compensates with a softer knee):
    R_s, L_d, L_q within 0.2 %, ψ within 0.15 %, the zero within 1.8 mrad (0.10° el) on the 8XX SiC and 4XX IGBT sets
    (stated tolerances 1 %, 1 %, 0.5 %, 3.5 mrad). Round 23 (§10k item 7): 0.75 mrad before the resolver angle was
    referred to the demodulator's own centroid; the zero moved by −0.97 mrad (ω_e at the dyno's 300 rpm × 7.7 µs: 8XX
    +0.54 → −0.50 mrad, 4XX −0.75 → −1.72 mrad against the plant's true zero), the other quantities by less than 0.1 %.
    What only the dyno can prove: `firmware/docs/target-bringup.md`, FW-39.
  - **Image identity.** FW-39 does not change `TI_FW_ID` by itself (round 23's identity, §10e).

## 10h. Round 23 — diagnostic services (FW-40)
- **FW-40** Diagnostic services for a service tool: the fault history and the DTCs exported, read-only telemetry
  (gaps 3 and 6 of `docs/firmware-vs-vesc.md`). ISO 14229-1 on the diagnostic bus beside FW-32 and FW-41, request
  0x7E1 / response 0x7E9 (this repository's until the OEM diagnostic specification binds them); `comms/uds_diag.c`, one
  line in the dispatcher `uds_handle()` and one call in the 1 ms task. Nothing it does is read by control: every read is a
  bounded copy of state the application already holds, and its one write — 0x14 — changes the DTC store, of which control
  reads only `DTC_DESAT_REPEAT` (the FW-15 retry gate), a DTC 0x14 keeps.
  - **Transport** (ISO 15765-2:2016 on the CAN-FD bus, TX_DL 64). A request is one single frame: classic (PCI 0x0L, up to
    7 bytes) or the escape format (0x00, SF_DL up to 62) in a CAN-FD frame — round 23 (§10k item 11): for the FW-38
    programming services also a first frame and consecutive frames under this ECU's flow control (block size 4, STmin 0,
    N_Cr 1 s; another service's segmented request is NRC 0x13). A response is queued and sent by the 1 ms task,
    one frame per task, retried while the TX mailbox is busy (N_As 1 s): up to 7 bytes a classic 8-byte single frame, up to
    62 an escape single frame in the smallest CAN-FD length that holds it, longer a first frame (12-bit length, 62 bytes)
    and consecutive frames of 63 bytes under the tester's flow control — block size, STmin (0–127 ms, 100–900 µs, a
    reserved value as 127 ms), WAIT (16 at most), overflow (abandons), N_Bs 1 s (abandons). Padding 0xAA. A new FW-40
    request abandons a pending response. The longest response, 0x19 04 with the whole fault ring, is 854 bytes (buffer
    1024).
  - **DTC numbers.** Three-byte ISO 14229-1 DTCs (DTCFormatIdentifier 0x01): number = 0xD10000 | the DTC's position in
    `dtc_id_t` — `dtc_code()`, the numbering the DTC store always had. The list is append-only: an entry inserted in the
    middle renumbers every DTC after it (the generator refuses explicit values). The table — number, name, description —
    is generated from `src/comms/dtc.h` by `firmware/tools/dtc-table.mjs`: JSON on stdout for the service tool, `--c`
    writes `src/comms/dtc_table.h` (the unit test requires a row, `dtc_code()`'s number and a description for every DTC;
    it fails until the table is regenerated after a DTC is added), `--md` the table below, `--check` compares.
  - **Status byte** — availability mask 0x7F, the bits the store (`comms/dtc.c`) tracks, as it tracks them. The store is RAM:
    at every power-up each DTC starts at 0x50; what survives a power-up is the NVM fault ring (0x19 04), the FW-15 DESAT
    record and the service lock.

    | Bit | Name | Meaning as tracked |
    |---|---|---|
    | 0 (0x01) | testFailed | the monitor reported a failure and no pass since; a monitor that reports passes clears it (`DTC_RSLV_REACQUIRED`, `DTC_OVERSPEED`; round 23: `DTC_TEMP_MODULE`, `DTC_TEMP_BOARD`, `DTC_TEMP_MOTOR`, `DTC_OVERTEMP`, `DTC_ASC_OC_TRANSIENT`), the others report failures only, so it holds until a clear or the next power-up |
    | 1 (0x02) | testFailedThisOperationCycle | failed in this key cycle |
    | 2 (0x04) | pendingDTC | failed since the last clear or power-up (not aged by passing cycles) |
    | 3 (0x08) | confirmedDTC | set at the first failure: every monitor debounces before it reports |
    | 4 (0x10) | testNotCompletedSinceLastClear | neither failed nor passed since the last clear or power-up |
    | 5 (0x20) | testFailedSinceLastClear | failed since the last clear or power-up |
    | 6 (0x40) | testNotCompletedThisOperationCycle | neither failed nor passed in this key cycle |
    | 7 (0x80) | warningIndicatorRequested | not tracked: never set, outside the availability mask |

  - **0x19 ReadDTCInformation.** `01` reportNumberOfDTCByStatusMask → `59 01 7F 01` count (u16); `02`
    reportDTCByStatusMask → `59 02 7F` {DTC (3), status}, every DTC with status AND mask AND 0x7F ≠ 0, ascending; `0A`
    reportSupportedDTC → every DTC with its status; `04` reportDTCSnapshotRecordByDTCNumber (DTC, record number) →
    `59 04` DTC status {record number, 1 identifier, DID 0xFD2F, 49 bytes}: the records of the NVM fault ring (`nv_fault_t`,
    16 events, written by the fault ISR at every DESAT, FW-15 step 1), newest first — record n is the n-th newest event of
    that DTC, 0xFF all of them; a record number with no event answers the DTC and its status only; record 0x00,
    0x11–0xFE and an unknown DTC are NRC 0x31; length NRC 0x13, an unknown sub-function (bit 7 set included) 0x12. The ring
    is read two records per task (each a 512-byte slot copy and its slot check — a CRC-32C since round 23), so the answer
    comes within 9 tasks; a record
    written during the scan makes it start again once (a second one: NRC 0x22 — repeat the request). FW-40 appended the
    operating context to `nv_fault_t` (`nv_fault_ctx_t`: the state, the torque command and the torque applied, the
    hottest module NTC, the two motor sensors and their validity), filled by `ctx_now()` for every fault context and
    recorded with a DESAT; a record written before FW-40 reads 0 in its "recorded" byte and zeros after it. Only DESAT
    events write the ring today: 0x19 04 for any other DTC answers no record. A 0x14 clear does not erase the ring.
  - **0x22 ReadDataByIdentifier.** Up to 8 DIDs per request (more: NRC 0x13); the supported ones are answered, the others
    left out, none supported is NRC 0x31. FW-40 answers every 0x22 that reaches it: the DIDs of other items are answered
    before it — FW-41's single reads of 0xFD40/0xFD41 earlier in `uds_handle()`, FW-39's and FW-43's (0xFE43) before
    `uds_handle()` in the task — so an unknown DID is NRC 0x31, never 0x11. Each DID is copied between two
    current-loop ISRs (taken again if one ran during the copy, three attempts). Multi-byte values big-endian; f32 = IEEE 754
    binary32, big-endian; flag b0 = the least significant bit. The telemetry DIDs (0xF2xx) are also the periodic ones.

    | DID | Name | Bytes | Layout — offset: type, field |
    |---|---|---|---|
    | 0xF200 | Operating state | 10 | 0 u8 state (`sm_state_t`: 0 OFF, 1 INIT, 2 SENSOR_SELFTEST, 3 VEHICLE_HANDSHAKE, 4 PRECHARGE_WAIT, 5 GATE_SELFTEST, 6 ARMED_ZERO_TORQUE, 7 RUN, 8 DERATE, 9 FAULT, 10 DISCHARGE, 11 SAFE_POWERDOWN); 1 u8 bridge (0 disarmed, 1 armed idle, 2 modulating, 3 PWM-ASC); 2 u8 HV state (0 unknown, 1 safe, 2 present); 3 u8 flags: b0 FAULT, b1 derate, b2 keep HV (FW-08b), b3 no safe state proven, b4 service required, b5 speed-limit request, b6 self-test done, b7 torque enabled; 4 u8 §6 action in force (`ss_action_t`, 0 none); 5 u8 the row deciding it (`ss_row_t`, 0xFF none); 6 u16 §6 rows active (bit per row); 8 u16 rows latched |
    | 0xF201 | Arming (FW-24) | 12 | 0 u8 evidence present (b0 ROUTE_BOUND, b1 CONFIG_MATCHES, b2 PROTECTION_LOCKED, b3 FAULT_ROUTE_VALIDATED, b4 OVP_ROUTE_VALIDATED); 1 u8 evidence missing; 2–6 u8 the five items one by one, in that order (1 present); 7 u8 gates: b0 arm permitted by the state machine, b1 the key cycle's no-arming latch, b2 init OK, b3 calibration valid, b4 gains valid, b5 FS0B/FS1B released, b6 gate power ready, b7 MCU_GATE_EN high; 8 u8 FW-16 self-test (0 not done, 1 passed or stored pass, 2 failed); 9 u8 its failed step; 10 u8 DESATs this key cycle; 11 u8 FW-15: b0 retry used, b1 a DESAT blocks arming, b2 recovery running |
    | 0xF202 | Speed and torque (FW-37) | 20 | 0 f32 speed (rpm mechanical; the last valid one while held for the §6 column); 4 u8 b0 resolver valid, b1 speed known, b2 resolver valid once; 5 f32 torque request (VCU, N·m); 9 f32 torque command (after the limits, ramps and trims: INV_STATUS b16–17); 13 f32 torque applied (what the issued current references represent: b4–5); 17 u8 gear requested; 18 u8 gear active (0 N, 1 D, 2 R, 3 P); 19 u8 b0 modulation requested, b1 zero current (FW-08), b2 reduced torque (the DESAT retry), b3 enable requested |
    | 0xF203 | Currents | 33 | 0 u32 the current-loop ISR entry of this sample (µs); 4, 8, 12 f32 i_a, i_b, i_c (A); 16, 20 f32 i_d, i_q measured (A, amplitude-invariant); 24, 28 f32 i_d, i_q references (the FOC's); 32 u8 b0 valid, b1 fresh, b2–b4 channel U, V, W valid, b5 sum fault, b6 stuck channel, b7 an open wire |
    | 0xF204 | DC link | 26 | 0 f32 V_DC (the validated pair, V); 4 f32 channel 1; 8 f32 channel 2; 12 f32 VOFS (pin V); 16 f32 V5GD (V); 20 f32 pack voltage (BMS, V); 24 u8 b0 V_DC valid, b1 channel 1 valid, b2 channel 2 valid, b3 channels disagree, b4 VOFS OK, b5 V5GD OK, b6 off the pack, b7 a channel in fail-safe; 25 u8 HV state |
    | 0xF205 | Temperatures | 40 | 0–27 seven f32 (°C): module U, V, W, board H, board A, motor MT1, MT2; 28 u8 valid, bit per channel in that order; 29 u16 faults, two bits per channel (0 OK, 1 open, 2 short, 3 rate); 31 f32 hottest valid module NTC (−273 none); 35 f32 coolant (VCU); 39 u8 b0 coolant valid |
    | 0xF206 | Limits in force (FW-03, FW-04, FW-11) | 33 | 0 f32 current allowance (A rms); 4 f32 motoring torque limit; 8 f32 regeneration torque limit (N·m, magnitudes); 12 f32 thermal derate (0–1); 16 f32 coolant factor (0–1); 20 f32 30 s peak budget used (s); 24 f32 BMS charge (regeneration) power limit (W); 28 f32 BMS discharge power limit (W); 32 u8 b0 derate active, b1 peak exhausted, b2 BMS limits fresh, b3 speed-limit request, b4 reduced torque |
    | 0xF207 | Watchdog and heartbeat | 43 | 0 u8 FS26: b0 initialised, b1 watchdog running, b2 FS0B/FS1B released, b3 FS1B short high; 1 u8 FS26 watchdog error count; 2 u32 FS26 answers; 6 u32 µs since the last (0xFFFFFFFF none); 10 u32 FS26 SPI errors; 14 u32 current-loop ISRs; 18 u32 µs since the last ISR entry; 22 u8 b0 VCU command fresh, b1 ever received, b2 BMS limits fresh, b3 ever received; 23 u32 ms since the last VCU_CMD; 27 u32 ms since the last VCU_BMS (0xFFFFFFFF never); 31 u32 alive-counter repeats; 35 u32 alive-counter jumps; 39 u32 length errors |
    | 0xF208 | Uptime and DTCs | 16 | 0 u32 uptime (ms since the MCU started); 4 u32 key cycle; 8 u8 1 power-on, 0 an MCU reset inside the key cycle; 9 u16 active DTCs (testFailed); 11 u16 confirmed DTCs; 13 u24 the first active DTC's number (0 none) |
    | 0xFD20 | Firmware identity | 17 | 0 u32 `TI_FW_ID`; 4 u8 SKU of the parameter set; 5 u8 SKU read from HW_ID; 6 u8 SKU of the calibration record (`ti_sku_t`); 7 eight u8 the hardware serial (device UID); 15 u16 the DTCs this image defines (the JSON table's length) |
    | 0xFD21 | Calibration record (FW-20) | 21 | 0 u16 layout of the loaded record; 2 u16 layout this image requires (2); 4 u32 check result (`CAL_ERR_*` bits, 0 valid); 8 u32 motor ID; 12 u32 the record's CRC-32; 16 u32 f_sw selected (Hz); 20 u8 parameter-set violations (`ti_params_validate`, saturated at 255) |
    | 0xFD22 | Validation record (FW-24) | 22 | 0 u8 record present (a valid CRC in NVM); 1 u8 the items it validates for this image and card (bits 3–4 as in 0xF201); 2 u8 flags stored; 3 u8 SKU stored; 4 u32 firmware ID stored; 8 u32 FW-06 chain measured (ns); 12 eight u8 serial stored; 20 u16 record layout |
    | 0xFD2F | Fault snapshot (0x19 04 only) | 49 | 0 u32 key cycle; 4 u32 time (ms); 8 u8 §6 row; 9 u8 action stored; 10 f32 speed (rpm); 14 f32 i_d (A); 18 f32 i_q (A); 22 f32 V_DC (V); 26 u8 operating context recorded (1; 0 a record from before FW-40); 27 u8 state; 28 u8 validity: b0 module NTC, b1 MT1, b2 MT2; 29 f32 torque command; 33 f32 torque applied; 37 f32 hottest module NTC; 41 f32 MT1; 45 f32 MT2 (°C, N·m) |

  - **0x2A ReadDataByPeriodicIdentifier — the stream.** Transmission mode 01 slow (every 100 ms), 02 medium (10 ms), 03
    fast (1 ms — 1 kHz, the ceiling), 04 stop (the listed pDIDs, or all); a pDID is the low byte of 0xF200–0xF208. Up to 4
    scheduled: a request that would exceed them, or names none supported, is NRC 0x31 and changes nothing; a scheduled
    pDID takes the new rate. Positive response `6A`. A periodic frame is a UUDT frame on **0x6E9** (this repository's):
    the pDID, then the DID's bytes, in the smallest CAN-FD length. At most one periodic frame per 1 ms task — round robin
    among the due ones (four at the fast rate come every 4 ms each), none in a task that sends a response frame; a frame
    the mailbox refuses is dropped and counted, never retried nor caught up. Not gated (read-only); it runs until stopped
    or the power-down, whatever the session (FW-38).
  - **0x14 ClearDiagnosticInformation.** Group 0xFFFFFF (all DTCs) or one DTC's number (another value: NRC 0x31; length:
    0x13), gated exactly as the FW-32 routine: SecurityAccess (without it NRC 0x33; a completed clear consumes the unlock),
    HV absent — unknown counts as present — and the bridge disarmed (else NRC 0x22, nothing cleared, the unlock kept). It
    returns each DTC it clears to its power-up state (status 0x50, no occurrence, no times). It erases no NVM record and
    releases no latch, so it must not hide one — **kept, never cleared by 0x14**: the DESAT class — `DTC_DESAT_HS`,
    `DTC_DESAT_LS`, `DTC_DESAT_REPEAT` (the second DESAT: permanent for the key cycle, no retry), `DTC_DESAT_PENDING_BOOT`,
    `DTC_FLT_RECOVERY_FAIL`; the service lock `DTC_QDIS_STUCK_ON` (NVM-kept, released only by the FW-32 routine); and every
    failure that forbids arming for the key cycle — cleared, the inverter would still refuse to arm with nothing saying
    why: `DTC_FS26_PROGID`, `_OTP_CORRUPT`, `_DEBUG_MODE`, `_INIT_READBACK`, `_SPI`, `_RELEASE`, `_GPIO1_OTP`,
    `DTC_FS1B_SHORT_HIGH`, `DTC_HWID_OPEN`, `_SHORT`, `_UNKNOWN`, `DTC_SKU_MISMATCH`, `DTC_CALIB_INVALID`,
    `DTC_PARAMS_INVALID`, `DTC_GAINS`, `DTC_PWM_LOCK`, `DTC_ARM_EVIDENCE`, `DTC_SELFTEST_FAIL`, `DTC_SELFTEST_NO_PASS`,
    `DTC_GATE_POWER`, `DTC_PRECHARGE_PLATEAU`, `_TAU`, `_TIMEOUT`, and `DTC_FW_UPDATE` (FW-38's programming session). A group
    clear answers positively and leaves them; one of them alone is NRC 0x22. Each is judged again at the next power-up
    (round 23, §10k item 4: a DESAT record that still blocks arming raises its `DTC_DESAT_HS`/`_LS` again there).
    Every other DTC's monitor runs on and sets it again while its fault persists. A DTC added later is clearable unless it
    joins this list (`uds_diag.c: kept`).
  - **Timing** (`firmware/docs/timing.md`): per 1 ms task at most one frame (a response frame first, else one periodic DID),
    a DID copy of 43 bytes at most, and while a 0x19 04 is answered two ring records (+ one at its end); no wait, no loop
    over time. Host (Apple M1 Pro, clang -O2): 41 ns for a periodic frame, 1.4 µs for a ring step; the target figure is
    T-36's measurement. The diagnostic layer takes no simulated time and changes no control output (a 400-task run at
    3000 rpm with the stream at its limit is bit-identical to one without).
  - **Target.** T-26: the flow-control frames of a segmented response come on the request ID the RX filter already has;
    the periodic ID needs no filter; TX goes through the one mailbox (a busy one delays a response frame, drops a periodic
    one). T-36: the WCET of the diagnostic path (above). The coherent copy against a preempting current-loop ISR runs
    only on the target (the host never preempts the task there).
  - **Not closed here:** the OEM diagnostic specification (IDs, DID and DTC numbers, the sessions they need); a DTC store
    that survives a power-up; snapshot records for DTCs other than DESAT (the ring's only producer is FW-15 step 1).

The DTC table at this revision (80 DTCs; `node tools/dtc-table.mjs --md` prints it from `dtc.h`):

| Id | DTC number | Name | Description |
|---|---|---|---|
| 1 | 0xD10001 | `DTC_FS26_PROGID` | Safety SBC (FS26) OTP variant: M_PROGID unbound or not the procured one - no arming (FW-12) |
| 2 | 0xD10002 | `DTC_FS26_OTP_CORRUPT` | Safety SBC (FS26) OTP CRC monitor reports corruption - no arming (FW-12) |
| 3 | 0xD10003 | `DTC_FS26_DEBUG_MODE` | Safety SBC (FS26) in debug mode at boot - no arming (FW-12) |
| 4 | 0xD10004 | `DTC_FS26_INIT_READBACK` | Safety SBC (FS26) INIT registers or their complements read back wrong - no arming (FW-12) |
| 5 | 0xD10005 | `DTC_FS26_SPI` | Safety SBC (FS26) SPI communication failed at boot - no arming (FW-12) |
| 6 | 0xD10006 | `DTC_FS26_WD` | Safety SBC (FS26) watchdog answer refused or not sent (FW-12) |
| 7 | 0xD10007 | `DTC_FS26_RELEASE` | Safety SBC (FS26) did not release FS0B/FS1B - no arming (start-up step 4) |
| 8 | 0xD10008 | `DTC_FS1B_SHORT_HIGH` | FS1B / FAULT_OUT line shorted to KL30 - no arming until repaired |
| 9 | 0xD10009 | `DTC_FS26_GPIO1_OTP` | Gate power up before start-up step 6: FS_GPIO1 slotted in the FS26 OTP - no arming |
| 10 | 0xD1000A | `DTC_HWID_OPEN` | HW_ID input open (power-board identity resistor missing) - no arming (FW-01) |
| 11 | 0xD1000B | `DTC_HWID_SHORT` | HW_ID input shorted to ground - no arming (FW-01) |
| 12 | 0xD1000C | `DTC_HWID_UNKNOWN` | HW_ID reading outside every SKU window or unstable - no arming (FW-01) |
| 13 | 0xD1000D | `DTC_SKU_MISMATCH` | HW_ID, parameter set and calibration record disagree on the SKU - no gate enable (FW-02) |
| 14 | 0xD1000E | `DTC_CALIB_INVALID` | Calibration record missing or wrong (layout, CRC, range, SKU, serial, motor ID) - no torque (FW-20) |
| 15 | 0xD1000F | `DTC_PARAMS_INVALID` | Parameter set fails its range or consistency check - no arming |
| 16 | 0xD10010 | `DTC_PWM_LOCK` | PWM fault configuration not read back, or its register lock not set - no arming (FW-24) |
| 17 | 0xD10011 | `DTC_DESAT_HS` | High-side gate driver desaturation (short circuit) (FW-15) |
| 18 | 0xD10012 | `DTC_DESAT_LS` | Low-side gate driver desaturation (short circuit) (FW-15) |
| 19 | 0xD10013 | `DTC_DESAT_REPEAT` | Second desaturation in the key cycle - latched, no further retry (FW-15) |
| 20 | 0xD10014 | `DTC_DESAT_PENDING_BOOT` | Gate driver fault line low at boot: a desaturation from before the reset |
| 21 | 0xD10015 | `DTC_FLT_RECOVERY_FAIL` | Gate driver fault latch not reset by the FW-15 recovery sequence |
| 22 | 0xD10016 | `DTC_OVERCURRENT` | Phase overcurrent: hardware compare or software backstop, outside the ASC-entry window (FW-05) |
| 23 | 0xD10017 | `DTC_OVERVOLTAGE` | DC-link overvoltage: hardware compare, active short circuit requested (FW-06) |
| 24 | 0xD10018 | `DTC_ISNS_OPEN` | Phase current sensor output open (below its valid window) (FW-05) |
| 25 | 0xD10019 | `DTC_ISNS_RANGE` | Phase current sensor output outside its valid window (FW-05) |
| 26 | 0xD1001A | `DTC_ISNS_SUM` | Phase currents do not sum to zero (FW-05) |
| 27 | 0xD1001B | `DTC_ISNS_OFFSET` | Phase current zero-current offset outside its tolerance at key-on |
| 28 | 0xD1001C | `DTC_ISNS_STALE` | Phase current samples missing or stale: lost triplets or a stopped current loop (FW-27, FW-31) |
| 29 | 0xD1001D | `DTC_VDC_DISAGREE` | DC-link voltage channels disagree beyond 5 % (FW-07) |
| 30 | 0xD1001E | `DTC_VOFS` | DC-link sense offset reference (VOFS) out of range (FW-07) |
| 31 | 0xD1001F | `DTC_V5GD` | Gate-drive side 5 V supply (V5GD) out of range - pulses off, short circuit cleared |
| 32 | 0xD10020 | `DTC_VDC_FAILSAFE` | DC-link voltage channel in its isolated amplifier's fail-safe state (FW-07) |
| 33 | 0xD10021 | `DTC_VDC_STALE` | DC-link voltage samples stale (FW-07) |
| 34 | 0xD10022 | `DTC_VDC_BMS` | DC-link voltage leaving the BMS pack voltage with the contactors closed (battery path) |
| 35 | 0xD10023 | `DTC_RSLV_AMPLITUDE` | Resolver sin/cos amplitude outside its window (FW-10) |
| 36 | 0xD10024 | `DTC_RSLV_EXCITATION` | Resolver excitation outside its band at the monitor or the winding (FW-10, FW-30) |
| 37 | 0xD10025 | `DTC_RSLV_TRACKING` | Resolver tracking error above its limit (FW-10) |
| 38 | 0xD10026 | `DTC_RSLV_ACCEL` | Resolver acceleration implausible (FW-10) |
| 39 | 0xD10027 | `DTC_RSLV_RATE` | Resolver angle rate disagrees with the voltage-model speed (FW-10) |
| 40 | 0xD10028 | `DTC_TEMP_MODULE` | Power-module temperature sensor invalid: open, short or rate (FW-13) |
| 41 | 0xD10029 | `DTC_TEMP_BOARD` | Board temperature sensor invalid (FW-13) |
| 42 | 0xD1002A | `DTC_TEMP_MOTOR` | Motor temperature sensor invalid (FW-13) |
| 43 | 0xD1002B | `DTC_OVERTEMP` | Hottest module temperature at the end of its derating band - no torque left (FW-04, FW-13) |
| 44 | 0xD1002C | `DTC_HVIL_OPEN` | HVIL loop open (FW-09) |
| 45 | 0xD1002D | `DTC_HVIL_SHORT` | HVIL loop shorted to ground or a supply, or implausible (FW-09) |
| 46 | 0xD1002E | `DTC_CAN_TIMEOUT` | Vehicle command (VCU_CMD) missing or older than 20 ms (FW-11) |
| 47 | 0xD1002F | `DTC_BMS_TIMEOUT` | BMS limits (VCU_BMS) missing or stale - no regeneration (FW-11) |
| 48 | 0xD10030 | `DTC_CAN_E2E` | Vehicle CAN frame refused by its E2E check: CRC, alive counter or length (FW-11) |
| 49 | 0xD10031 | `DTC_QDIS_STUCK_OFF` | Active discharge commanded but the link does not discharge (FW-17, FW-18) |
| 50 | 0xD10032 | `DTC_QDIS_STUCK_ON` | Active discharge conducting without a command - service lock, do not re-energise (FW-26) |
| 51 | 0xD10033 | `DTC_QDIS_RATE_LIMIT` | Active discharge requests beyond the rate limit (FW-17) |
| 52 | 0xD10034 | `DTC_TAU_MISMATCH` | Key-off discharge time constant more than 20 % off the SKU's capacitor bank (FW-02) |
| 53 | 0xD10035 | `DTC_PRECHARGE_PLATEAU` | Precharge plateau below 97.5 % of the pack - arming refused (FW-19) |
| 54 | 0xD10036 | `DTC_PRECHARGE_TAU` | Precharge time constant implausible - arming refused (FW-19) |
| 55 | 0xD10037 | `DTC_PRECHARGE_TIMEOUT` | Precharge not complete in time - arming refused (FW-19) |
| 56 | 0xD10038 | `DTC_SELFTEST_FAIL` | Gate shutdown-path self-test step mismatch - no arming (FW-16) |
| 57 | 0xD10039 | `DTC_SELFTEST_NO_PASS` | Gate self-test skipped with no stored pass - no arming (FW-16) |
| 58 | 0xD1003A | `DTC_GATE_POWER` | Gate-drive supply not ready in time, or lost (RDY) (FW-14) |
| 59 | 0xD1003B | `DTC_SPO_ENERGY` | Pulses held off with neither energy rule proven: no safe state proven (FW-08b) |
| 60 | 0xD1003C | `DTC_CTRL_NONFINITE` | Non-finite value caught in the control path before the PWM |
| 61 | 0xD1003D | `DTC_NVM` | Data-flash (NVM) write failed |
| 62 | 0xD1003E | `DTC_SENSOR_SELFTEST` | Sensor self-test at start-up failed (start-up step 3) |
| 63 | 0xD1003F | `DTC_GAINS` | No current-loop gain set within the crossover ceiling - no arming |
| 64 | 0xD10040 | `DTC_ARM_EVIDENCE` | Arming evidence missing: fault route unbound or no valid EOL/HIL validation record - no arming (FW-24) |
| 65 | 0xD10041 | `DTC_TORQUE_INFEASIBLE` | No voltage-feasible current even at zero torque: zero torque, speed limit requested (FW-25, FW-37) |
| 66 | 0xD10042 | `DTC_ISNS_STUCK` | Phase current channel shows no current where its reference asks for it: stuck sensor (FW-05) |
| 67 | 0xD10043 | `DTC_RSLV_STALE` | No coherent resolver frame within the hold time - angle withdrawn (FW-28) |
| 68 | 0xD10044 | `DTC_RSLV_SWG_SAT` | Resolver excitation trim at its top code with the monitor still below the setpoint (FW-30) |
| 69 | 0xD10045 | `DTC_SERVICE_LOCK_CLEARED` | Service lock cleared by the UDS routine 0xF010 - a record, not a failure (FW-32) |
| 70 | 0xD10046 | `DTC_LV_OVERVOLTAGE` | LV supply (VSUP) above 20 V - information; first and last time give the duration (FW-33) |
| 71 | 0xD10047 | `DTC_LV_OV_SUSTAINED` | LV overvoltage longer than its band allows - orderly ramp to zero torque (FW-33) |
| 72 | 0xD10048 | `DTC_LV_VSUP_UNKNOWN` | No LV supply reading from the FS26 AMUX - LV supervision off (information, FW-33) |
| 73 | 0xD10049 | `DTC_RSLV_REACQUIRED` | Resolver frame ring re-acquired, or its producer restarted - information (FW-35) |
| 74 | 0xD1004A | `DTC_TORQUE_POSTCOND` | Torque-to-current solver refused its own result: no current reference, control lost (FW-37) |
| 75 | 0xD1004B | `DTC_OVERSPEED` | Measured speed in the overspeed warning or trip band (FW-42) |
| 76 | 0xD1004C | `DTC_FW_UPDATE` | Programming session entered (firmware update) - no arming until the next power-up; a record (FW-38) |
| 77 | 0xD1004D | `DTC_FW_FALLBACK` | Bootloader refused or abandoned the last activation, or restored the last good image (FW-38) |
| 78 | 0xD1004E | `DTC_MC_ABORTED` | Commissioning routine aborted - its reason is in the routine's results; a record (FW-39) |
| 79 | 0xD1004F | `DTC_MC_CAL_WRITTEN` | Commissioning wrote a new calibration record version - effective at the next key cycle (FW-39) |
| 80 | 0xD10050 | `DTC_ASC_OC_TRANSIENT` | Phase overcurrent inside the ASC-entry window: the short-circuit transient - information (FW-05) |

## 10i. Round 23 — sampled-waveform capture (FW-41)
- **FW-41** Sampled-waveform capture with a fault trigger, read-only (gap 4 of `docs/firmware-vs-vesc.md`). A static RAM
  ring of `CAP_N` records (a build-time power of two: 2048 × 32 B = 64 KiB, 12.5 % of the S32K396's 512 KB of system SRAM
  — S32K39 data sheet Rev.3, Table 1: 800 KB in total, 288 KB of it the three cores' TCMs), one record per current-loop
  ISR, written as that ISR's last statement from values it already holds: no conversion started, nothing waited for, no
  loop over the ring. Nothing in control reads anything of it. At 2·f_sw the ring covers 102 / 128 / 205 ms (20 / 16 /
  10 kHz). It lives in RAM only: an MCU reset or a power-down loses it.
  - **Record** (32 bytes, little-endian): u32 the ISR's entry time (µs); u32 flags; 12 × i16 — i_a, i_b, i_c (0.05 A),
    i_d, i_q measured and their references (0.05 A: the FOC's own, from its last step — it steps only while modulating,
    which the flags say), v_d, v_q (0.05 V: the FOC's voltage references), V_DC (0.05 V), the resolver observer's angle
    (2π/32768 rad, resolver electrical, at its newest block, wrapped) and its speed (1 rpm mechanical). A value that is
    not a number is −32768; the others round to nearest and saturate at ±32767. Flags: bits 0–9 the active §6 rows (one
    per `ss_row_t`), 10–11 the bridge mode, 12–15 the operating state, 16 currents valid, 17 V_DC valid, 18 resolver
    valid, 19 modulation requested, 20 zero current (FW-08), 21 FOC voltage limit reached, 22 FOC guard tripped, 24–26
    the §6 action, 31 the trigger record.
  - **Triggers** (while armed): a new DTC occurrence (a DTC going to testFailed — a count `dtc_events()` never resets)
    or a new §6 row (a rising bit of the active rows): DESAT, over-current, over-voltage, resolver or current loss, the
    FW-37 torque postcondition and every other; the command; a level on one channel with hysteresis — rising, it fires at
    a value ≥ the level once a value below level − hysteresis was recorded since the arm (falling: mirrored), never on a
    value that is not a number. A sources mask selects the DTC, row and level triggers; the command always works. The
    trigger record lands at index `pre` — the pre-/post-trigger split, 0 … `CAP_N` − 1 records before it (`CAP_N`/2 by
    default), calibratable with every arm request — when that many records preceded it since the arm, else at the number
    that did; `CAP_N` − 1 − pre records follow it; then the ring **freezes**: nothing is written until the next arm. A
    read-out does not end the freeze (a transfer can be repeated); a fault while frozen is not captured (its DTC and the
    FW-15 records keep it). Armed at every start-up with the default configuration (pre `CAP_N`/2, DTCs + rows): the
    first fault of a key cycle is captured without a tester.
  - **Concurrency.** The ISR owns every field it writes. The 1 ms task (the diagnostic path) only posts requests — an arm
    with its configuration, a command — which the ISR takes at its next run, and reads the ring only while it is frozen
    with no arm pending, when the ISR writes nothing. No lock and no wait on either side.
  - **Read-out** (ISO 14229-1 over single-frame ISO 15765-2 on the CAN-FD diagnostic bus, 0x7E1 / 0x7E9: a request up to
    7 bytes classic or up to 62 in the CAN-FD escape format; a response up to 7 bytes classic, a longer one in the
    smallest CAN-FD frame that holds it). `22 FD 40` → `62 FD 40 [state | reason << 4] [capture id] [blocks BE16]`
    (state 1 armed, 2 triggered, 3 frozen; reason 1 fault, 2 command, 3 level; blocks 0 unless frozen with no arm
    pending). `22 FD 41` → `62 FD 41 [capture id] [block BE16] [≤ 56 image bytes]`: the block at the read cursor, which
    then moves on; `2E FD 41 [block BE16]` sets the cursor, which starts at 0 for every new frozen capture. Every block
    carries its number and the capture's id: a gap, a repeat or blocks of two captures are seen by the reader.
    `31 01 F0 41` re-arms with the present configuration, `31 01 F0 41 [pre BE16] [sources] [level channel (2 … 13) |
    0x80 falling] [level BE16] [hysteresis BE16]` (raw units of that channel) with a new one; `31 01 F0 42` is the
    command trigger (armed only). NRC 0x13 length, 0x21 an arm the ISR has not taken yet (repeat), 0x22 not frozen / not
    armed, 0x31 out of range (nothing changed). Not gated by SecurityAccess: nothing here changes anything but the
    capture's own RAM. Every request is handled in the 1 ms task (at most four per tick, FW-32), a block ≤ 56 bytes
    copied. The DIDs and routines are this repository's definition until the OEM diagnostic specification binds them.
  - **Image** (what the blocks carry, little-endian): a 320-byte header — magic "TICP", format 1, flags layout 1, header
    bytes, `TI_FW_ID`, capture id, reason, the triggering DTC (the newest new occurrence; 0 none), the new §6 rows, the
    trigger index, the record count, pre, the sample rate (2·f_sw, Hz), sources, level channel and edge, level,
    hysteresis, the SKU, record bytes (32), channel count (14) and 20 bytes per channel: name (8 ASCII), unit (6 ASCII),
    type (0 u32, 1 i16), offset in the record, unit per LSB (float32) — then the records in time order.
    `firmware/tools/capture-decode.mjs` (Node, no dependencies) checks the blocks (every one present, one capture, no
    conflicting repeat, the image its header accounts for) and writes CSV; the service tool reads the same format.
  - **Cost** in the current-loop ISR: 12–15 ns per call on the host reference (arm64, −O2 / −O1); ≈ 300 Thumb-2
    instructions on the common path for the M7 (branch-free per channel), ≤ 1 µs at 320 MHz estimated — ≤ 4 % of the
    ≈ 23 µs budget at 20 kHz (`firmware/docs/timing.md`); on silicon: checklist T-43. FW-41 adds no control behaviour and
    leaves `TI_FW_ID` (0x0A0F0015) to the round's release (T-06).

## 10j. Round 23 — overspeed, run-time statistics, offset refresh (FW-42, FW-43, FW-44)

From `docs/firmware-vs-vesc.md`, ranked gaps 5, 7 and 9. Implemented in `firmware/src/safety/overspeed.c`,
`firmware/src/nvm/runstats.c`, `firmware/src/sense/offtrack.c` and the 1 ms task (`app.c`); tests
`firmware/tests/test_fw42_44.c`.

- **FW-42** Overspeed protection. Before round 23 a speed above the motor's maximum was caught only indirectly, when the
  back-EMF it produces tripped the FW-06 DC-link compare. Now the 1 ms task compares the MEASURED speed — the resolver's,
  and only while the resolver is valid; the §6 speed bound after a resolver fault (round 23: `cal_speed_accel_max_rpm_s`,
  §6) is never counted — in both directions of rotation with the calibration record's `n_max_rpm` (FW-20): the speed up to which
  the §6 release rules are proven (rule (a) evaluated at n_max, the ASC entry current of R8X-13). Two bands:
  - **warning** from `cal_ovs_warn_frac` × n_max (1.00): the §6 "Healthy, command/CAN lost" row — the orderly ramp HVIL open
    (FW-09) and a sustained LV overvoltage (FW-33) take: torque ramped to zero at `cal_torque_ramp_nm_s`, then SPO below
    n_x, current control (field weakening) kept at or above it with the battery present (LS-ASC without it); in addition
    the speed-limit request (INV_STATUS b14.3, FW-25's bit), `DTC_OVERSPEED`, and **no arming**: MCU_GATE_EN is not raised
    from the disarmed bridge while a band is active (the §9 step-5 ASC hold at n ≥ n_x is a safe state, not arming, and
    keeps its own exit). The row is evaluated in ARMED_ZERO_TORQUE/RUN/DERATE, as for FW-09/FW-33;
  - **trip** from `cal_ovs_trip_frac` × n_max (1.05): in addition the §6 "Resolver invalid, or control lost" row, latched —
    SPO below n_x under the energy rule (LS-ASC where neither rule (a) nor (b) holds), LS-ASC at or above n_x; cleared only
    by a VCU fault reset below n_x, as FW-05's. It has no ASC of its own: the row's cell decides. Evaluated in the
    monitoring states (VEHICLE_HANDSHAKE onwards), as the resolver-invalid row.

  A band is entered or left only after `cal_ovs_debounce_ms` (10 ms) consecutive samples asking for it, and is left
  `cal_ovs_hyst_frac` × n_max (0.02) below its start; without a measured speed the band is held and nothing counts. A single
  corrupt resolver frame does not trip it: the amplitude window keeps a frame out of the observer, and — round 23 (§10k
  item 8) — so does an angle error beyond `cal_rslv_debounce` × the acceleration threshold, while a smaller one is absorbed
  by the observer and only counted: the resolver's own acceleration/tracking plausibility (FW-10, its own §6 row) latches
  after `cal_rslv_debounce` consecutive frames, never on one (host, before item 8: a 60° frame 850 µs into the
  millisecond, 17.5–18.7 krpm sampled once at 15.8 krpm, no band). `DTC_OVERSPEED` is set in every
  millisecond of a band (its first/last stamps give the duration) and passed when the band ends: one occurrence per event.
- **FW-43** Run-time statistics — no safety relevance: nothing reads them back into a decision.
  - Energy drawn from and returned to the DC link, motoring and regenerating separately: ∫ V_DC·I_DC dt in the 1 ms task.
    The card has no DC shunt, so I_DC is the bridge's DC current from the current loop's own voltage references and measured
    currents, V_DC·I_DC = 1.5·(v_d·i_d + v_q·i_q), counted while the bridge modulates — not in SPO or ASC (the rectified
    energy of an SPO at speed is not counted), and without the inverter's own losses (the power at the motor terminals).
    64-bit joules, reported in Wh.
  - Key-on time (every state but OFF and SAFE_POWERDOWN) and time in RUN/DERATE.
  - The highest valid module-NTC, coolant (the VCU's, in a fresh command) and motor-sensor temperatures.
  - DTC occurrences per class: power stage, sensors, supply/FS26, vehicle interface, operating limits, HV path, integrity,
    information (a DTC no class names counts as information).
  - Key cycles: the existing key-cycle counter (FW-15's), reported, not duplicated.
  - **Distance is not kept**: the contract has no vehicle-speed or wheel signal (VCU_CMD and VCU_BMS carry none), and the
    motor speed gives no distance without the final drive and the wheel, which are not inputs of this inverter.

  Persistence: the run-time record (NVM, A/B with sequence and the slot check — CRC-32C since round 23; a layout version
  in the payload) is queued every
  `cal_rs_save_s` (600 s), once on entering SAFE_POWERDOWN — which waits for the queue before LPOFF, so a controlled
  shutdown keeps everything — and at an FW-44 adoption. **Caveat (VESC's, now ours, explicitly): an unclean shutdown — KL30
  lost, a reset or a crash outside SAFE_POWERDOWN — loses what accumulated since the last queued record, at most
  `cal_rs_save_s` of it**; a write torn by the power loss leaves the previous record. Read-only access: DID 0xFE43
  (ReadDataByIdentifier 0x22 on the diagnostic bus; the 58-byte record in one CAN-FD single frame, big-endian — layout in
  `firmware/src/nvm/runstats.h`); there is no write service. The DID is this repository's until the OEM diagnostic
  specification binds it (FW-41's capture holds 0xFD40/0xFD41).
- **FW-44** Key-on current-offset refresh. The key-on check (§9 step 3) is unchanged: each phase channel's standstill
  zero-current mean against the EOL calibration record's offset, arming refused beyond `cal_isns_offset_tol_v` — always
  against the EOL record, which is never modified. In addition, when the check passes, the WORKING offset — the one the
  current conversion and the FW-05 hardware compare use — moves toward the fresh mean by at most `cal_isns_ofs_step_v`
  (2 mV = 0.9 A at 2.22 mV/A): once per key cycle (the record carries the key cycle of the last adoption, so an MCU reset
  inside the key cycle adopts nothing more), only with the bridge disarmed, the PWM off and the rotor at standstill (below
  FW-16's n_ss) — never while armed or moving, never mid-run. A step from a value inside the tolerance toward a mean
  inside it stays inside it, so the tracked value can never widen the refusal. The FW-05 compare is re-programmed from the
  working offsets. The tracked offsets persist in the run-time record, bound to the calibration record's CRC: a new
  calibration record (EOL, or FW-39's commissioning) starts again from its own offsets; a tracked value outside the EOL
  tolerance, or non-finite, is ignored.
- **CALs** (round 23), each range-checked at every boot (`ti_params_validate` ⇒ DTC_PARAMS_INVALID, no arming):
  `cal_ovs_warn_frac` 1.00 [0.90, 1.00]; `cal_ovs_trip_frac` 1.05 [1.02, 1.20] (the floor above every warning start: the
  bands cannot swap); `cal_ovs_hyst_frac` 0.02 [0.005, 0.05]; `cal_ovs_debounce_ms` 10 [2, 100]; `cal_rs_save_s` 600
  [60, 3600]; `cal_isns_ofs_step_v` 0.002 V [0.0005, 0.02].
- **NVM**: the run-time record takes slots 28/29 — records after the fault ring now take A/B pairs in enum order (FW-38's
  boot record 30/31); the 32 slots are all used. FW-43 and FW-44 share the one record for that reason. Round 23 (§10k
  item 10): the map has 64 slots, 32–63 free.
- **Image identity**: changed once for all the round-23 items — TI_FW_ID 0x0A0F0015 (§10e's 0x0A0F0014 was the torque-solver image); the calibration record is layout 4 (FW-45/FW-46, §10l/§10m; layout 3 before them).

## 10k. Round 23 — defects found by the closed-loop simulator and the update work

Found by running the unmodified firmware against the bridge's plant (`tool/README.md`, "Findings"), by the FW-38 and
FW-42 work, and by the bridge self-test; each fixed with a test that failed before it, mutation checks, and a row in
`firmware/docs/traceability.md` ("Round 23 — Fixes"). `TI_FW_ID` is not changed by these fixes (the round's identity,
§10e).

1. **FW-13 temperature rate check** (`sense/temp.c`). The rate was |ΔT|/dt between consecutive 1 ms samples against
   `cal_temp_rate_c_s` (20 °C/s): one ADC code (0.04–0.5 °C) read as 40–500 °C/s, so any code step invalidated the
   channel and a slow rise above ≈ 70 °C latched all three module NTCs for the key cycle (derating stuck at the
   continuous rating). Now the rate is judged on the mean over `cal_temp_rate_win_ms` (200 ms, [100, 250]) against the
   last accepted mean, only once it has moved more than `cal_temp_rate_db_codes` (4 codes, [1, 32]); the latch after
   three implausible windows, open and short per sample, are unchanged.
2. **Field weakening from the measured link** (`app.c: torque_path`). At zero torque the bridge idled below n_x — a
   speed defined at the 880 V OV trip — while the back-EMF already exceeded the measured link (750 V: from ≈ 6 900 rpm),
   so the diodes braked the shaft and charged the pack uncommanded. Now current control is needed when the speed is
   unknown, the speed bound reaches n_x (the floor), or ω_e·ψ ≥ (1 − `cal_fw_emf_margin_frac`)·v_available(V_DC measured)
   (0.05, [0, 0.3]); the §6 rows still use n_x.
3. **Torque-command slew** (`app.c: torque_path`). A fresh VCU command was applied at once (only the FW-11 command-lost
   ramp existed): +200 → −150 N·m at 10 000 rpm ran the loop out of voltage and tripped FW-05. Now the command moves at
   `cal_torque_slew_nm_s` (2000 N·m/s, [200, 20 000]) in both directions; the §6 zeroing (ZERO_CURRENT and above) and the
   solver's refusals still act in the same task, the FW-11 ramp keeps `cal_torque_ramp_nm_s`.
4. **Every declared DTC is set** (`app.c`). `DTC_DESAT_PENDING_BOOT` (a driver FLT low at boot), `DTC_SENSOR_SELFTEST`
   (the §9 step-3 timeout) and `DTC_FS26_RELEASE` (the step-4 timeout, which had none either) where the sensor self-test
   exits to FAULT; `DTC_ISNS_OFFSET` at the key-on offset check; `DTC_TEMP_MODULE` / `_BOARD` / `_MOTOR` while a channel of
   their group is invalid, passed when all are valid; `DTC_OVERTEMP` at the hottest module NTC's derating end, passed
   `cal_derate_hyst_c` below it. Found by the bridge self-test: a DESAT record from this or the previous key cycle blocks
   arming at the power-up (FW-15, FAULT) with nothing naming it (the DTC store is RAM) — the recorded bank's
   `DTC_DESAT_HS` / `_LS` is now raised again there. None was superseded; none removed.
5. **The LS-ASC entry transient** (`app.c: over_current`, `asc_oc_rearm`; `bridge.c: t_asc_us`). Every ASC entry at speed
   peaked 730–900 A against the 601 A FW-05 compare and latched `DTC_OVERCURRENT` and the control-lost row. Analysed on
   the host safety chain sample by sample (battery loss at 8 500 / 10 000 rpm): the compare's FAULT1 is mapped to the
   high sides only, which the ASC holds off; the low sides stayed on through every sample of the transient — no safety
   defect. Now an over-current inside `cal_asc_oc_window_ms` (20 ms, [5, 100]) of an LS-ASC entry is information,
   `DTC_ASC_OC_TRANSIENT` (id 80), no row; after the window, with the current back inside the compare, the compare's
   flags (and FAULT1's, unless an over-voltage shares it) are re-armed and the DTC passes. Outside the window, or
   outlasting it, an over-current is the latched fault it always was.
6. **The speed after a resolver fault** (§6, `app.c: app_speed_hi_rpm`). The last speed was forgotten
   `cal_speed_hold_ms` (200 ms) after the fault — then "unknown", the n ≥ n_x column: PWM-ASC at 2 000 rpm. Now a
   physical bound, |n_last| + `cal_speed_accel_max_rpm_s`·t (2 500 rpm/s, [500, 50 000]; it replaces
   `cal_speed_hold_ms`), decides every "n < n_x" question; unknown once it passes n_max. The rule and its rationale: §6.
7. **The resolver angle's reference instant** (`control/resolver.c: centroid_us`). The block's angle was dated at the
   samples' mean instant, but the demodulator weights each sample by the lagged carrier (sin² of its phase): the
   weights' centroid is 7.7 µs later at the −24° compensation (N = 16, 100 µs blocks) — the 1.85° el lead at 10 000 rpm
   the bridge measured. Not a host artefact: the firmware's own arithmetic, the same on the target. The angle is now
   dated at the centroid; `cal_rslv_latency_us` (T-37) is the chain's delay beyond it.
8. **FW-10's debounce** (`control/resolver.c: observer`). One frame outside the amplitude window made the resolver
   invalid at once (the current loop then raised the latched control-lost row), and one angle error of a few degrees rang
   in the observer long enough to latch the acceleration plausibility — the `cal_rslv_debounce` count (3) was bypassed.
   Now one out-of-window frame is one count; once locked, a frame whose innovation exceeds the tracking limit or
   `cal_rslv_debounce` × the acceleration threshold (`cal_rslv_accel_max_rad_s2`/ω_n²) is kept out of the observer (one
   count, the angle hold not renewed); the plausibility checks latch after `cal_rslv_debounce` consecutive frames as
   before.
9. **The NVM slot check** (`nvm/nvlog.c`). A CRC-32 over a slot whose payload ends in its own CRC-32 (the same
   polynomial) depends only on the header and the length, so a torn write read back as the record from two writes back —
   `calib_t` (FW-20) and `arm_validation_t` (FW-24) have that layout. Now the slot check is a CRC-32C over the header and
   the payload (magic "TIN2"); slots in the old format ("TINV", CRC-32) stay readable and are replaced by the next write;
   the records' layouts are unchanged.
10. **The NVM slot map** (`hal/nvm.h`). All 32 slots were used. Now 64 (32–63 free: 16 more A/B records), the existing
    slots unmoved: 32 KiB of emulated EEPROM on the target (T-25).
11. **FW-38 TransferData over ISO 15765-2** (`comms/uds_diag.c`: the receiving side; `boot/update.h`). A block was
    5 bytes (≈ 200 s per MiB). Now a segmented request — first frame, this ECU's flow control (block size 4, STmin 0),
    consecutive frames, N_Cr 1 s — carries up to `UPD_BLOCK_MAX` 4095 (4093 data bytes) to the programming services
    only (another service's: NRC 0x13): 1 MiB in 257 blocks, 4.6 s host-simulated; every power-loss and sequence test
    unchanged.
12. **FW-21's rows** (§10, `firmware/README.md`, `firmware/docs/traceability.md`) said the update path was out of scope;
    they point to FW-38.
13. **The tool's bench** (`tool/bridge`, `tool/PROTOCOL.md`; not a firmware change): an ISO 15765-2 tester (flow control
    for segmented responses, the periodic frames on 0x6E9), `dtc_clear` through the image's 0x14 with a provisioned bench
    key, and the VCU vehicle speed (VCU_CMD b4 [5], b6–7) so FW-39's service mode is entered from the tool.
14. **Counts**: 188 fields per parameter set, 89 CAL rows (`include/cal_ranges.h`); 80 DTCs (the table in §10h).

## 10l. Round 23 — saturation-dependent inductance (FW-45)

From `docs/firmware-vs-vesc.md`, ranked gap 8. Implemented in `firmware/src/control/motor.h` (the maps, `motor_sat`),
`torque.c` (the model and the solve), `foc.c` (the current loop), `firmware/src/nvm/calib.c` (the record) and
`firmware/src/app/commission.c` (the measurement); tests `firmware/tests/test_fw45_46.c`.

- **FW-45** L_d(|i_d|) and L_q(|i_q|) in the torque solve, the current loop and FW-39's commissioning.
  - **The record** (FW-20 **layout 4**; a layout-3 record is refused — no torque): per axis the APPARENT (secant)
    inductance at six fixed breakpoints, 0, 0.2 … 1.0 × the SKU's current limit (`i_crest_a`: 480.8 A 8XX, 565.7 A
    4XX) — the twelve values, and the full scale, which `calib_check` binds to the SKU's limit. Default: the scalar
    repeated (flat). A map is used as its shape — each point over point 0 — at the level of the scalar `ld_h` / `lq_h`:
    point 0 is the scalar in every record the nominal record and the FW-39 commit write, a flat map is the scalar model
    bit for bit, and a scalar FW-39 commits alone moves the whole map's level. Linear between the breakpoints, the last
    point beyond. `calib_check`: each point inside the inductance class range (20 µH – 5 mH), non-increasing with the
    current (saturation only lowers it), the last ≥ 0.3 × the first; else CAL_ERR_RANGE — no torque, as any bad record.
    The §6 energy screen and the back-EMF bound keep the scalars: on a non-increasing map the unsaturated inductance
    bounds the apparent and the differential one, so the screen stays conservative.
  - **The model**: λ_d = ψ + L_d(|i_d|)·i_d, λ_q = L_q(|i_q|)·i_q, T = 1.5·p·(λ_d·i_q − λ_q·i_d); the voltage
    ellipse, the current circle, the demagnetisation limit, the postcondition and the torque applied (INV_STATUS b4–5)
    all use these λ.
  - **The solve**: FW-37's structure — the MTPA point, the voltage boundary nearest it, the reduction — on the maps'
    torque contour: at each i_d the contour's i_q exactly (the first breakpoint whose torque reaches the request
    brackets it; that segment's quadratic in its stable form), the cost's slope along it from the differential
    inductances (i_q' = (λ_q − L_d'·i_q)/(λ_d − i_d·L_q') from dT = 0). On a map |i|² and |v|² along the contour need
    not be convex (a saturating L_q below L_d, or a saturating L_d on an SPM, gives two local minima), so: every search
    keeps its bracket and its step bound (terminating: ≤ 40 halvings a search, ≤ 24 reductions); the MTPA and
    least-voltage searches read the slope's sign at five points first and refine each bracketed minimum, the least
    taken; the ψ_e > 0 guard takes the maps' extremes (max L_d − min L_q: the contour exists wherever the solve looks);
    and the postcondition guards every return (a failure: TQ_POSTCOND — the DTC, zero torque, the §6 "control lost"
    row; never a vector). A flat map never takes this path: FW-37 bit for bit.
  - **Proof on the host**: 20 000 random cases — FW-37's reference distribution (SPM to reverse-salient, 0.05–1 mH,
    0.02–0.3 Wb, 2–8 pole pairs), the four SKUs, all four quadrants, a random physical map per axis (the secant of
    L₀·I_s·atan(i/I_s) or a knee, one axis in six flat): no TQ_POSTCOND, no TQ_NONFINITE; every TQ_OK / TQ_LIMITED inside
    the ellipse, the circle and the demagnetisation limit and never above the request (checked in double against an
    independent model); TQ_OK within 0.1 % of the request; no TQ_LIMITED where the request fits; the least current
    (every fourth TQ_OK) within 0.2 %. Eight TQ_LIMITED of 8 507 are more than 0.1 N·m below the most that fits — valid,
    suboptimal: five where the guard from the maps' extremes excludes an i_d that would fit (weak magnets with a
    saturating L_q below L_d, or reverse-salient machines with a saturating L_d; the worst 91.5 of 127.5 N·m on a
    0.02 Wb machine whose L_q falls to 46 %), three where the reduction's feasibility is not monotone on the map
    (0.1–2.4 N·m). The guard is what keeps the contour defined: 20 000 weak-magnet machines (0.01–0.06 Wb) with a q axis
    saturating below L_d give no TQ_POSTCOND with it, 25 with a guard from the scalars (the §6 row while driving). The
    same 20 000 with flat maps are bit-identical to the scalar solve — and to FW-37's own `torque.c` (a differential run
    of the two sources, −O1 / −O2, with and without floating-point contraction: 0 of 20 000 differ in the result, i_d,
    i_q, torque or voltage); on the saturated machines the scalar solve (FW-37 without the maps) is more than 0.1 % off
    the torque in 1 271 of its 4 733 TQ_OK (worst 217 N·m).
  - **The current loop**: per axis the proportional gain scaled by the map's differential inductance at the measured
    current over the unsaturated one, clamped to [0.3, 1] (the floor for a steep or non-physical segment): the
    crossover `gains.c` placed stays; the integral gain needs no scaling (the zero R/L follows the pole). The speed
    voltages from the maps' flux, and FW-10's back-EMF speed model (with the scalar, an L_d 20 % saturated at −300 A
    reads 25 % high: the rate check's whole tolerance). **Scheduling is needed**: on the host, a q axis at 0.37 of its
    unsaturated differential inductance (8XX SiC) makes a fixed gain cross at ≈ 2.9 kHz — ≈ 12° of phase margin
    (90° − 360°·f_c·75 µs; ≈ 61° by design) — a 20 A step overshoots 93 % and rings back 87 %; scheduled: 6 %, none.
  - **Commissioning**: FW-39's L_d/L_q routine at a bias index k = 0…5, one optional byte after the attestation
    (`31 01 F0 20 02 4C 4B k`, a CAN-FD escape single frame; without it the routine as before: the scalars): the DC
    bias of breakpoint k — at least the routine's 50 A, at most the SKU's limit (the d run: the demagnetisation limit)
    less the HF 20 A — on the true axes, the d run at −i_d, the q run at +i_q (the back-EMF zero of the key cycle, else
    the record's; on the host with the record's zero 20° el off, the d run's mean i_d within 2 % of −b and i_q within 2 %
    of 0 in the rotor's own frame). The HF impedance gives the DIFFERENTIAL inductance there; each point is judged and confirmed as a
    quantity (inside `band_l_rel` of the record's differential inductance at that current: staged; beyond it: a second
    agreeing run). Results: 0x40+k / 0x50+k the L_d / L_q point (BE16, 0.1 µH); 0x02 the staged points of each map (bit
    k); for a biased run the flags of 0x11 / 0x12 carry bit 3 and k in bits 4–6 and are that point's; index 0x01 bit 5:
    a map point staged. The commit (RID 0xF021) turns a whole staged axis — all six points — into the apparent map by
    trapezoids (λ_k = λ_{k−1} + (D_{k−1} + D_k)/2·Δi, L_k = λ_k/i_k, L_0 = D_0) and sets that axis' scalar to its point 0
    unless the scalar is staged too; some points only: NRC 0x22 and nothing is written. Point 0 is the differential
    inductance at the routine's 50 A — FW-39's own convention for the scalar.
  - **On the host plant** (the FW-39 PMSM saturating, I_sat 600 A on d and 320 A on q; 8XX SiC, 750 V): at 80 % of the
    current limit (384.7 A; its MTPA torque 310.9 N·m) at 1000 rpm the flat map (FW-37 as it was) delivers −9.8 % at
    347 A, the plant's map −0.1 % at 384 A. The sweep (ten runs: four points confirmed) measures the six differential
    inductances within 0.2 %; the committed map is within 2.7 % of the plant's apparent inductance (point 0 2.5 % below
    L(0), the points above inheriting part of it), and the next key cycle delivers the 80 % torque within 0.05 %.
  - **Cost**: `foc_step` +8 ns on the host (+28 %; ≈ +0.6 µs on the M7 by FW-41's scale); the solve with a saturating
    map 3.0× FW-37's mean on the host and its slowest case 2.8× FW-37's slowest — ≈ 180 µs at 320 MHz against FW-37's
    ≈ 65 µs (`docs/timing.md`); a flat map costs nothing more. T-57 measures it on the M7.
  - **Not modelled**: cross-saturation (L_d at i_q ≠ 0, L_q at i_d ≠ 0), the sign of i_d (the map is over |i_d|),
    temperature. Target: T-57 (the sweep against an LCR / saturation reference on the dyno, the rig, the time).
  - **Image identity**: the round's (§10e); the record layout 4.

**Map tolerance (second pass).** The record check refuses a map that rises with current by more than 2 % between neighbouring
points (MOTOR_MAP_RISE_TOL): saturation only lowers inductance, but FW-39's six points scatter by about 0.1 % on a machine that
does not saturate, and a strict never-rising rule refused every such commit (found by the service tool's map sweep). The
tool's ripple-map import over CAN reads ψ and the pole pairs from DID 0xFD25 (the record's motor data); DID 0xFD26 carries
the active record's maps, so a committed map can be read back after the key cycle (the routine's per-point results belong
to the running key cycle only).

## 10m. Round 23 — torque-ripple feed-forward (FW-46)

From `docs/firmware-vs-vesc.md`, ranked gap 10. Implemented in `firmware/src/control/torque.c` (`torque_ripple_at`,
`torque_ripple_scale`), `firmware/src/app/app.c` (`init_calibration`, `torque_path`, `control_fast`),
`firmware/src/app/commission.c` (the staging) and `firmware/src/comms/uds_diag.c` (the DID); tests
`firmware/tests/test_fw45_46.c`.

- **FW-46** A cogging / torque-ripple feed-forward on i_q.
  - **The record** (layout 4): 36 values over one electrical period, 10° el apart, as int16 in 0.01 A — float A does not
    fit: the record would be 548 bytes of the NVM slot's 496; as 0.01 A counts it is 476 — within ±30 A
    (`calib_check`, the CAL's ceiling). Default all zero.
  - **CALs**: `cal_ripple_ff_max_a` 0 A [0, 30]: each value is applied clamped to ± it and the write refused beyond it;
    0 — the default — applies nothing: the image without FW-46, bit for bit. `cal_ripple_ff_fmax_hz` 200 Hz [0, 500]:
    applied only while the cogging fundamental **6·f_e** — six per electrical period, a three-phase machine's — is below
    it (a table's lower harmonics are gated at the same speed, which costs nothing at 500 rpm where the loop follows
    them; its 12th and higher switch off with the 6th). 190 fields per parameter set, 91 CAL rows.
  - **Application**: in the current-loop ISR at the FOC's own angle, i_q,ref += k·clamp(table(θ_e) − mean): linear
    interpolation; the table's mean is never applied (a ripple has none — the mean torque stays the solved one). k ∈
    [0, 1] comes from the 1 ms task: non-zero only for a solved vector (TQ_OK / TQ_LIMITED) the bridge modulates for
    torque — no §6 decision, no zeroing (the ISR checks `zero_now` too), no commissioning — with the resolver valid and
    6·f_e below the CAL; and the largest k for which the vector with the table's extremes (k·max, k·min) still fits the
    current circle and the voltage ellipse (`torque_ripple_scale`; i_d unchanged, so the demagnetisation limit holds):
    the feed-forward is scaled down, never the solved vector. The ISR preempts the task: until the new references are
    out the scale is the smaller of the old and the new, so no ISR combines a scale with a vector it was not computed
    for. The postcondition's torque band is on the solved vector; the feed-forward's own torque is the cogging's
    opposite (bounded by the CAL, zero mean).
  - **Service tool**: DID 0xFD46. `2E FD 46` + 36 × int16 big-endian (0.01 A) — 75 bytes, one ISO 15765-2 request (first
    and consecutive frames under this ECU's flow control, FW-38's receiver) → `6E FD 46`; under FW-39's interlocks: the
    SecurityAccess unlock (NRC 0x33; one write per unlock), no routine running and not in torque — RUN/DERATE, the bridge
    modulating or in ASC — (0x22), exactly 75 bytes (0x13: a single frame never holds it), each value within
    `cal_ripple_ff_max_a` (0x31). Staged in RAM, committed by RID 0xF021 with the rest (FW-20: sealed, checked, a new
    NV_REC_CALIB version), active from the next key cycle. `22 FD 46` reads the table the next commit writes — the staged
    one, else the active record's — in a segmented response.
  - **On the host plant** (the FW-39 PMSM with cogging, 4.0 N·m at the 6th and 1.5 N·m at the 12th harmonic; 8XX SiC):
    the tool's dyno measurement (the bridge idle at 100 rpm, three electrical periods, the 6th and 12th fitted) makes the
    table; it is written, read back, committed and key-cycled; at 100 rpm and 30 N·m the shaft's torque ripple falls
    from 3.02 to 0.51 N·m rms (−83 %; the 6th 3.99 → 0.40, the 12th 1.50 → 0.49 N·m — linear interpolation of 36 points
    keeps sinc² of a harmonic: 0.91 of the 6th, 0.68 of the 12th); at 600 rpm (6·f_e = 240 Hz) nothing is applied and
    the ripple is the one without the table (3.01 N·m both). At the current limit (185 A rms) a TQ_LIMITED vector gets
    k = 0.001, one at 98.5 % of it k = 0.85, and no current-loop reference leaves the circle; with the default CAL a
    table changes nothing (the shaft torque identical at every millisecond).
  - **Target**: T-58 (the table from a dyno torque transducer at ≤ 100 rpm; the two CALs against the measured reduction).
  - **Image identity**: the round's (§10e); the record layout 4.

