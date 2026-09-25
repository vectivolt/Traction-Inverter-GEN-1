# Hardware → firmware contract (rev A.15)

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
| FW-10 | Resolver (SDADC sin/cos + excitation monitor) | amplitude (sin²+cos²) window, tracking error, angle-rate plausibility vs current model, excitation-monitor level (its planes: FW-30); any fault ⇒ no angle-dependent torque, §6 state |
| FW-11 | CAN torque command | counter + CRC, ≤ 20 ms staleness ⇒ ramp to zero torque (not hold last value) — round 15, confirmed round 17: a stale command also leaves the contactor state unknown, so while armed it is also the §6 battery-lost row ("contactor/precharge feedback invalid"), and that row wins — zero current at the current-loop rate below n_x, LS-ASC above; the ramp remains for a command lost with the battery path proven (e.g. HVIL open); BMS limit timeout ⇒ zero regen only (motoring keeps the FW-03 envelope; the relayed discharge limit is not held stale) |
| FW-12 | FS26 | OTP set of design-basis §8a verified by SPI readback at every boot (R-F38); FS1B policy per §6; Q&A watchdog configured before FS0B release with **WD_ERR_LIMIT = 2** (not the default 6), WD_FS_REACTION = RSTB + FS0B (default), window ≤ 3 ms, so runaway code asserts FS0B within about two windows (round 7, RR03). **FS1B_TDELAY = 0 (required)**: FS1B asserts with FS0B, FW-16 step a reads the ASC preset at once, and a delay would only lengthen the SPO interval after an FS0B event; the ASC entry is break-before-make in hardware (§4c). **FS1B_TDUR = 100 ms** (default): the ASC latch holds after FS1B releases, and the bounded pulse also bounds RFS4 on a FAULT_OUT wire short (§9). Keep BACKUP_SAFETY_PATH_FS0B = 1 (default: an FS0B short-to-high asserts RSTB). Set **BACKUP_SAFETY_PATH_FS1B = 0** (round 9, A8-03). An FS1B short-to-high comes from FAULT_OUT shorted to KL30 (or a board-level short on FS1B_N). With this setting it still counts in the fault error counter, and FW-12 reads it as a DTC with no arming until repaired. It does not reset the MCU. A reset would restore neither the FS1B→ASC preset nor FAULT_OUT, it would interrupt the MCU's §6 control during the very fault that asserted FS1B, and a reset loop would repeat the RFS4 stress. FS_GPIO1 (flyback-enable OR input) stays low from POR until §9 step 6, then high, so gate power is held through an MCU reset but is never up while FS1B is still asserted at boot. The OTP must configure GPIO1 **push-pull and not slotted** (design-basis §8a): a slotted push-pull GPIO1 goes high by itself at power-up (DS Table 133). The FW-16 boot test asserts FS0B with FS0B_REQ (FS_SAFE_IOS_1 bit 6). FS1B-ASC only for motors that need it (§4c: with EN low the LS DESAT is not documented). Outside that policy, only FW-16 switches the low sides on through ASC. It does so in states without documented LS DESAT: step a (EN low, via FS1B), step f (EN high, IN+ low) and step h (both, around the FLT injection). Each runs only under its measured no-HV, standstill conditions |
| FW-13 | Temperatures | module NTCs (open/short/rate), motor sensors, board NTCs; derate per FW-04 |
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
| Healthy, command/CAN lost (armed, a stale command also leaves the contactor state unknown: the battery-path row below applies and wins — FW-11, round 17). The same row serves HVIL open (FW-09) and an LV overvoltage outside the vehicle's profiles (FW-33, round 17) | ramp to zero torque, then SPO | ramp to zero torque; keep current control (field weakening) while the battery is present |
| Battery path lost (contactor open, contactor/precharge feedback invalid, or V_DC leaving the pack voltage) | zero current (i_d = i_q = 0, FW-08) at the current-loop rate; if the link crosses the OV trip while current is still flowing (winding energy with no sink), FW-06 fires **LS-ASC** at any speed — the current then circulates and decays in the winding instead of charging the link; release to SPO once the current is gone (round 13) | **LS-ASC** (FW-06), release to SPO below n_x |
| BMS charge limit → 0 with the battery still connected (full or cold pack) | zero torque, then SPO | ramp regen to zero at the current-loop rate and keep current control (field weakening) as in the healthy row — the connected pack is a voltage source, so this is not an ASC case; FW-06 stays armed as the backstop if the pack is then opened (round 12, R1-F02/R2-F35: the old row merged the two events) |
| Resolver invalid, or control lost (phase-current sensing invalid or stale, a phase-overcurrent trip — FW-05, non-finite control, arming evidence lost while armed — round 17) | SPO | LS-ASC (no angle needed) |
| V_DC invalid with V5GD healthy (FW-07: VOFS, disagreement, fail-safe or stale channel — round 17) | SPO under rule (a) or (b); where neither holds, LS-ASC — the winding current circulates instead of charging a link nobody can measure | same |
| DESAT on a **high-side** switch (FLT_HS) | SPO | SPO first. The fault latch holds EN low, and LS-ASC with EN low has no documented LS DESAT. After the FW-15 reset (≥ 1.5 ms), LS-ASC is permitted **only as PWM-ASC** with EN high (§4c). **Assumption:** the HS DESAT came from a shorted LS device or a phase-to-DC− fault, which LS-ASC completes into a symmetric short. **If an HS device has itself failed short**, the LS DESAT (documented with EN high and IN+ high) soft-turns the LS off, FLT_LS follows and the row below applies (SPO): one bounded extra SC event, never a sustained shoot-through. The ASC latch alone must not be used for this row. **Energy during the SPO interval** (round 8, R7-06): for ≥ 1.5 ms the rectified motor current flows into the battery, so the inverter asks the VCU/BMS to keep the contactors closed until ASC is back or n < n_x (FW-08b). Whether the link stays inside its rating if the battery path is lost inside that window is decided by the motor/vehicle release rule below (DC-link energy 32.8 J 8XX / 28.6 J 4XX to U_N); no brake chopper by default |
| DESAT on a **low-side** switch (FLT_LS) | SPO | **SPO only** — the cause may be a shorted HS switch; LS-ASC would short the link through the motor |
| MCU hang / reset | hardware: FS0B ⇒ SPO; FS1B ⇒ LS-ASC per FW-12 policy | same — choose the FS1B policy from the motor's n_x. Unconditional FS1B-ASC is safe only if the motor tolerates ASC braking torque at low speed. In FS1B-ASC EN is low (§4c), so a new HS short during it is not bounded by the drivers — a double fault. After the reset, §9 decides from speed whether to keep ASC |
| Gate-logic supply (V5GD) loss, or the power board's LV feed lost (QLVS, FVBx) | SPO (FLT/RDY read low or V5GD out of window: latch set, ASC masked or cleared, PWM/EN low) | SPO — release rule (a) or (b) below; the HV backup bias cannot help here (ASC is masked, or V15 and the ASC opto are dead too) |
| Total LV (KL30) loss | SPO (gates park low by UVLO) — energy-safe only under rule (a) for the current at the fault, or with the battery retained (b) | SPO — **energy-safe only under rule (a)** (back-EMF *and* winding energy; the old "E_LL,pk(n_max) < U_N" test alone was incomplete, round 13). Motors that fail (a) need the HV-fed backup-bias option (c) before release — S10: gate reservoirs hold ASC only 0.5–3.1 ms (round 7: worst case now starts at the 13.54 V low-corner rail) and the command path ≈1 ms |

**Unknown speed (round 17, decided).** After a resolver fault or a stale resolver the last valid speed is held
for the column choice only — never as angle feedback — for `cal_speed_hold_ms` (200 ms, range 0–1000: inertia;
FW-06 backstops a rise above n_x). After that, or when the resolver was never valid since the boot, the speed is
unknown: the n ≥ n_x column applies and rule (a) is evaluated at n_max.

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
   arming (FW-15 one-retry rule), whatever FLT reads now. The VCU precharges only after
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
  default-off outputs above and the image identity its EOL/HIL record is bound to (FW-24).

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
- **FW-25** Torque-request feasibility (F23, F155). After the demagnetisation and current-circle clamps the
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
- **Image identity.** `TI_FW_ID` 0x0A0F0011: round 17 changes the image (zero current under the battery-lost
  row, the RUN-only DC-link trim, FW-32, the FW-12 refresh cadence, the FW-06a release wait, FW-33). A new
  EOL/HIL validation record is required before it arms (FW-24); the calibration record stays layout 2.

## 11. What this contract does not close

Double-pulse (turn-off overshoot at 850 V, R_G_OFF), contained short-circuit tests per silicon,
thermal/coldplate, EMC, LV transients (ISO 16750-2/7637-2 per the OEM contract), insulation
coordination and mechanical DV are hardware gates — listed in
[`review-A6-disposition.md`](review-A6-disposition.md) and, for round 7, in
[`review-A7-disposition.md`](review-A7-disposition.md). The round-7 gates are:
- the LV-only gate-supply bench (six-domain VCC2 across KL30/temperature, start/stop/jump start);
- the ASC entry measurement (HS gate vs LS gate at no HV);
- the HIL measurement of the FW-06 chain.
