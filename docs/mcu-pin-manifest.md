# S32K396 pin manifest — rev A.12 pin freeze

**Package:** 289-MAPBGA (17 x 17, rows A-H J-N P R T U). **Source:** NXP EV-INVERTERGEN3 S32K396-HPWR-MC schematic SPF-91122 rev C, sheets 8-13 (MCU symbol ball numbers and pin functions); ball-to-name pairing anchored on the S32K39 DS supply balls J6 E7 B8 E9 G13 E11 F1 H5; FS26 48 pins verified against FS26 DS Table 3.

Every connected ball is drawn on the control-card sheet with the label `<ball>_<signal>`; the 158 unused GPIO
balls are left open (pads unconnected in layout). The ERC (`erc-audit.mjs`, round 13) asserts that every
`UMCU` pin label starts with a ball listed here, sits on the net listed here, and — for signal balls — that
the required peripheral function is in that ball's alternate-function list.

## Corrections to the A.4 "GEN3-exact" port list found by the freeze

| Signal | A.4 port | Problem | Frozen ball / port | Function |
|---|---|---|---|---|
| PWM_UL / PWM_VL / PWM_WL | PTG10 / PTA6 / PTA7 | PTG10 has no PWM output; the low sides must be the complements of the high sides on the same eFlexPWM submodules | M15 PTA6 / M16 PTA7 / N15 PTC8 | PWM_1_B[0] / B[1] / B[2] (highs: L14 PTC31 A[0], N17 PTC30 A[1], N16 PTC29 A[2]) |
| VDC2_SE | PTB0 | no ADC function | A5 PTE1 | ADC1_P6 (V_DC1 stays on A13 PTA0 ADC6_P4: separate instances) |
| HW_ID | PTB4 | no ADC | C12 PTC6 | ADC3_P6 |
| V5GD_SNS | PTB5 | no ADC | A15 PTD27 | ADC4_P6 |
| TMOD_V / TMOD_W | PTB2 / PTB3 | ADCx_MA[n] are external-mux ADDRESS outputs, not inputs | B10 PTE2 / F2 PTE5 | ADC3_P2 / ADC1_S8 |
| NTC_A | PTA10 | that ball is JTAG_TDO | C14 PTD29 | ADC5_P7 |
| NTC_H / MT1 / MT2 | PTA11 / PTA12 / PTA13 | not present on the 289-MAPBGA | B11 PTE6 / A11 PTA15 / C11 PTE18 | ADC3_P3 / ADC3_P4 / ADC3_P5 |
| INTRLOK_N | PTF5 | no ADC | D8 PTE23 | ADC1_P7 |
| SBC_AMUX | "ADC0_S12" (a name, not a port) | — | C17 PTB11 | ADC0_S14 |
| RDY_LS | PTB11 | moved to free the ADC ball above | U2 PTB5 | GPIO / EIRQ[13] |
| FLT_HS_N / FLT_LS_N | PTC26 / PTC25 | (confirmed) | P15 / R14 | PWM_1_FAULT[0] / PWM_1_FAULT[2] — closes gate ⑯ |
| Resolver SIN / COS / monitor | named SDADC pins | — | C15 PTB9 + A16 PTD26 / B13 PTD28 + B14 PTA1 / A12 PTA16 + D11 PTE17 | SDADC2 AN0/AN1 · SDADC3 AN0/AN1 · SDADC1 AN0/AN1 (GEN3 pairs the resolver the same way) |
| CAN | named | — | R13 PTC21 / U14 PTC23 · D3 PTA23 / C3 PTA22 | CAN0_TX/RX (GEN3) · CAN1_TX/RX |
| VDD_HV_B, VDD_DCDC | "VDD3" → V3B | the safety gates are 5 V LVC; HV_B at 3.3 V could not drive them | N4 R7 R10 → V5A; L5 → V5A | GEN3 runs both domains at 5 V; VDD_DCDC ≤ VDD_HV_A per DS |
| VDD_LVDS | — | 2.97–3.63 V only | N5 → V3B (LDO1) | unused LFAST, supplied per DS |

FS26 (USBC): all 48 pins + EP verified against the FS26 DS Table 3 (pin order 1 VBST_PG … 48 BATSENSE) — no change.

## Connected balls (130)

| Ball | S32K396 pin | Our net | Function used | Role |
|---|---|---|---|---|
| A2 | PTA8 | `ISNS_U` | ADC3_P1 | phase current U |
| A3 | PTA5 | `RESET_B` | RESET_b | reset |
| A5 | PTE1 | `VDC2_SE` | ADC1_P6 | V_DC channel 2 (was PTB0: no ADC) |
| A7 | VDDA_SWG01 | `V5A` | — | SWG analog supply (filtered 5 V) |
| A8 | SWG1_0 | `SWG1` | SWG1_0 | resolver excitation source |
| A9 | VREFH_R2R | `VREF5` | — | ADC reference high (FS26 VREF 5 V) |
| A11 | PTA15 | `MT1_SIG` | ADC3_P4 | motor temp 1 (PTA12 absent) |
| A12 | PTA16 | `VREXM_P` | SDADC1_AN[0] | excitation monitor + (SDADC1 AN0) |
| A13 | PTA0 | `VDC1_SE` | ADC6_P4 | V_DC channel 1 |
| A14 | PTB8 | `ISNS_V` | ADC4_P5 | phase current V |
| A15 | PTD27 | `V5GD_SNS` | ADC4_P6 | V5GD/2 monitor (was PTB5: no ADC) |
| A16 | PTD26 | `SIN_N` | SDADC2_AN[1] | resolver SIN- (SDADC2 AN1) |
| B2 | VSS3 | `DGND` | — | ground |
| B3 | PTA9 | `TMOD_U` | ADC0_P7 | module NTC U |
| B4 | PTE31 | `INTRLOK_P` | — | HVIL ladder drive |
| B8 | VSSA_SWG01 | `AGND` | — | SWG analog ground |
| B9 | VREFL_R2R | `AGND` | — | ADC reference low |
| B10 | PTE2 | `TMOD_V` | ADC3_P2 | module NTC V (was PTB2: mux-address output) |
| B11 | PTE6 | `NTC_H` | ADC3_P3 | board NTC hot zone (PTA11 absent on this package) |
| B12 | PTC7 | `SBC_INTB` | EIRQ[7] | FS26 INTB (EIRQ) |
| B13 | PTD28 | `COS_P` | SDADC3_AN[0] | resolver COS+ (SDADC3 AN0) |
| B14 | PTA1 | `COS_N` | SDADC3_AN[1] | resolver COS- (SDADC3 AN1) |
| B16 | VSS6 | `DGND` | — | ground |
| C2 | PTE16 | `FCCU_ERR1` | FCCU_ERR1 | FCCU error out 1 |
| C3 | PTA22 | `CAN1_RX` | CAN1_RX | FlexCAN1 RX |
| C12 | PTC6 | `HW_ID` | ADC3_P6 | SKU identity (was PTB4: no ADC) |
| C13 | PTD31 | `MCU_EN_FLYBK_LS` | — | flyback LS enable (OR) |
| C14 | PTD29 | `NTC_A` | ADC5_P7 | board NTC ambient (was PTA10 = JTAG_TDO) |
| C15 | PTB9 | `SIN_P` | SDADC2_AN[0] | resolver SIN+ (SDADC2 AN0) |
| C17 | PTB11 | `SBC_AMUX` | ADC0_S14 | FS26 AMUX (was a named ADC0_S12 pin) |
| D2 | PTE15 | `FCCU_ERR0` | FCCU_ERR0 | FCCU error out 0 |
| D3 | PTA23 | `CAN1_TX` | CAN1_TX | FlexCAN1 TX |
| D4 | VSS4 | `DGND` | — | ground |
| D8 | PTE23 | `INTRLOK_N` | ADC1_P7 | HVIL signature (was PTF5: no ADC) |
| D9 | VSS10 | `DGND` | — | ground |
| D11 | PTE18 | `MT2_SIG` | ADC3_P5 | motor temp 2 (PTA13 absent) |
| D12 | PTE17 | `VREXM_N` | SDADC1_AN[1] | excitation monitor - (SDADC1 AN1) |
| D13 | PTD30 | `MCU_EN_FLYBK_HS` | — | flyback HS enable (OR) |
| D14 | VDD_HV_A_6 | `V5A` | — | 5 V I/O and analog domain A |
| D15 | PTB10 | `RDY_HS` | EIRQ[24] | RDY HS bank (GPIO/EIRQ) |
| E5 | VDD_HV_A_2 | `V5A` | — | 5 V I/O and analog domain A |
| E6 | VREFH_SAR_456 | `VREF5` | — | ADC reference high (FS26 VREF 5 V) |
| E7 | VREFL_SAR_456 | `AGND` | — | ADC reference low |
| E8 | V11_1 | `V11` | — | 1.1 V core (from the external NMOS ballast) |
| E9 | VREFL_SDADC_01 | `AGND` | — | ADC reference low |
| E10 | VREFH_SDADC_01 | `VREF5` | — | ADC reference high (FS26 VREF 5 V) |
| E11 | VSSA_SDADC | `AGND` | — | SDADC analog ground |
| E12 | VDDA_SDADC | `V5A` | — | SDADC analog supply (filtered 5 V) |
| E13 | VSS11 | `DGND` | — | ground |
| F1 | NMOS_CTRL | `BCTRL` | — | gate of the external V11 ballast NMOS |
| F2 | PTE5 | `TMOD_W` | ADC1_S8 | module NTC W (was PTB3: mux-address output) |
| F5 | VSS23 | `DGND` | — | ground |
| F6 | PTA4 | `TMS` | JTAG_TMS | JTAG TMS / SWDIO |
| F7 | PTC4 | `TCK` | JTAG_TCK | JTAG TCK / SWCLK |
| F8 | PTC5 | `TDI` | JTAG_TDI | JTAG TDI |
| F13 | VREFH_SDADC_23 | `VREF5` | — | ADC reference high (FS26 VREF 5 V) |
| G5 | VSS24 | `DGND` | — | ground |
| G7 | VSS8 | `DGND` | — | ground |
| G8 | PTA10 | `TDO` | JTAG_TDO | JTAG TDO / SWO |
| G10 | VDD_HV_A_5 | `V5A` | — | 5 V I/O and analog domain A |
| G11 | VSS9 | `DGND` | — | ground |
| G13 | VREFL_SDADC_23 | `AGND` | — | ADC reference low |
| G15 | PTF16 | `SBC_CS` | LPSPI3_PCS0 | LPSPI3 PCS0 |
| H1 | PTA25 | `IGN_SNS` | ADC0_S8 | KL15 sense |
| H5 | VREFH_SAR_456 | `VREF5` | — | ADC reference high (FS26 VREF 5 V) |
| H6 | VREFH_SAR_0123 | `VREF5` | — | ADC reference high (FS26 VREF 5 V) |
| H7 | VDD_HV_A_4 | `V5A` | — | 5 V I/O and analog domain A |
| H8 | V11_3 | `V11` | — | 1.1 V core (from the external NMOS ballast) |
| H9 | V11_7 | `V11` | — | 1.1 V core (from the external NMOS ballast) |
| H10 | V11_2 | `V11` | — | 1.1 V core (from the external NMOS ballast) |
| H13 | VDD_HV_A_7 | `V5A` | — | 5 V I/O and analog domain A |
| H16 | PTB13 | `ISNS_W` | ADC0_S19 | phase current W |
| J1 | VSS18 | `DGND` | — | ground |
| J4 | VSS15 | `DGND` | — | ground |
| J5 | VSS_DCDC | `DGND` | — | internal DC/DC ground (PMIC option) |
| J6 | VREFL_SAR_0123 | `AGND` | — | ADC reference low |
| J7 | VSS (DS figure) | `DGND` | — | DS PMIC figure lists J7 as a ground ball ("V25" in the figure text); grounded |
| J8 | V11_8 | `V11` | — | 1.1 V core (from the external NMOS ballast) |
| J9 | VSS7 | `DGND` | — | ground |
| J10 | V11_5 | `V11` | — | 1.1 V core (from the external NMOS ballast) |
| J13 | V11_4 | `V11` | — | 1.1 V core (from the external NMOS ballast) |
| J14 | VSS17 | `DGND` | — | ground |
| K1 | EXTAL | `EXTAL` | — | 40 MHz crystal |
| K8 | V11_9 | `V11` | — | 1.1 V core (from the external NMOS ballast) |
| K9 | V11_6 | `V11` | — | 1.1 V core (from the external NMOS ballast) |
| K10 | V11_10 | `V11` | — | 1.1 V core (from the external NMOS ballast) |
| K11 | VDD_HV_A_3 | `V5A` | — | 5 V I/O and analog domain A |
| K15 | PTD20 | `SBC_MISO` | LPSPI3_SIN | LPSPI3 SIN |
| L1 | XTAL | `XTAL` | — | 40 MHz crystal |
| L5 | VDD_DCDC | `V5A` | — | internal DC/DC input — follows HV_A (DS: never above VDD_HV_A; GEN3 default) |
| L7 | VSS1 | `DGND` | — | ground |
| L8 | VDD_HV_A_1 | `V5A` | — | 5 V I/O and analog domain A |
| L11 | VSS2 | `DGND` | — | ground |
| L14 | PTC31 | `PWM_UH` | PWM_1_A[0] | PWM U high |
| L17 | PTA17 | `SBC_MOSI` | LPSPI3_SOUT | LPSPI3 SOUT |
| M5 | VSS16 | `DGND` | — | ground |
| M15 | PTA6 | `PWM_UL` | PWM_1_B[0] | PWM U low |
| M16 | PTA7 | `PWM_VL` | PWM_1_B[1] | PWM V low |
| M17 | PTE7 | `SBC_SCK` | LPSPI3_SCK | LPSPI3 SCK |
| N4 | VDD_HV_B_1 | `V5A` | — | 5 V I/O domain B (5 V like GEN3: the safety gates are 5 V LVC — a 3.3 V domain could not drive them) |
| N5 | VDD_LVDS | `V3B` | — | LVDS/LFAST supply 2.97–3.63 V (unused LFAST; DS: ramp after HV_A) — the LDO1 3.3 V rail |
| N6 | VSS21 | `DGND` | — | ground |
| N7 | V11_11 | `V11` | — | 1.1 V core (from the external NMOS ballast) |
| N8 | VSS25 | `DGND` | — | ground |
| N9 | VDD_HV_A_8 | `V5A` | — | 5 V I/O and analog domain A |
| N10 | VSS22 | `DGND` | — | ground |
| N15 | PTC8 | `PWM_WL` | PWM_1_B[2] | PWM W low |
| N16 | PTC29 | `PWM_WH` | PWM_1_A[2] | PWM W high |
| N17 | PTC30 | `PWM_VH` | PWM_1_A[1] | PWM V high |
| P2 | PTD16 | `MCU_GATE_EN` | — | gate enable (UAND1.B) |
| P4 | VSS19 | `DGND` | — | ground |
| P14 | VSS20 | `DGND` | — | ground |
| P15 | PTC26 | `FLT_HS_N` | PWM_1_FAULT[0] | driver fault HS bank -> eFlexPWM1 FAULT0 (hardware PWM inhibit, FW-15) |
| R7 | VDD_HV_B_2 | `V5A` | — | 5 V I/O domain B (5 V like GEN3: the safety gates are 5 V LVC — a 3.3 V domain could not drive them) |
| R8 | PTD8 | `ASC_CLR_M` | — | ASC clear |
| R10 | VDD_HV_B_3 | `V5A` | — | 5 V I/O domain B (5 V like GEN3: the safety gates are 5 V LVC — a 3.3 V domain could not drive them) |
| R13 | PTC21 | `CAN0_TX` | CAN0_TX | FlexCAN0 TX (GEN3 choice) |
| R14 | PTC25 | `FLT_LS_N` | PWM_1_FAULT[2] | driver fault LS bank -> eFlexPWM1 FAULT2 |
| R17 | PTB1 | `VOFS` | ADC4_S11 | receiver offset monitor |
| T2 | VSS5 | `DGND` | — | ground |
| T5 | PTD6 | `ASC_REQ` | — | ASC request (latch clock) |
| T6 | PTD10 | `DRV_EN_RB` | — | DRV_EN read-back |
| T7 | VSS13 | `DGND` | — | ground |
| T10 | VSS14 | `DGND` | — | ground |
| T16 | VSS12 | `DGND` | — | ground |
| U2 | PTB5 | `RDY_LS` | EIRQ[13] | RDY LS bank (GPIO/EIRQ) |
| U5 | PTD5 | `QDIS_M` | — | discharge command |
| U6 | PTD11 | `ASC_CMD_RB` | — | ASC_CMD read-back |
| U8 | PTD9 | `FLT_CLR_M` | — | fault-latch clear one-shot |
| U14 | PTC23 | `CAN0_RX` | CAN0_RX | FlexCAN0 RX (GEN3 choice) |

## Balls left open (159)

A1 (PTG10), A4 (PTE30), A6 (PTF0), A10 (PTA14), A17 (PTG11), B1 (PTA18), B5 (PTE0), B6 (PTF1), B7 (SWG0_0), B15 (PTF9), B17 (PTF10), C1 (PTA19), C4 (PTE28), C5 (PTE29), C6 (PTF2), C7 (PTF4), C8 (PTF5), C9 (PTF7), C10 (PTE20), C11 (PTE19), C16 (PTD25), D1 (PTA21), D5 (PTE26), D6 (PTF3), D7 (PTF6), D10 (PTF8), D16 (PTD24), D17 (PTA2), E1 (PTE11), E2 (PTE10), E3 (PTD1), E4 (PTA20), E14 (PTF13), E15 (PTF12), E16 (PTA3), E17 (PTD23), F3 (PTD0), F4 (PTG8), F9 (PTE21), F10 (PTG27), F11 (PTG26), F12 (PTG25), F14 (PTF15), F15 (PTF14), F16 (PTD2), F17 (PTD3), G1 (PTA24), G2 (PTG9), G3 (PTG13), G4 (PTG4), G6 (PTE4), G9 (PTE27), G12 (PTG24), G14 (PTF17), G16 (PTD4), G17 (PTD22), H2 (PTG1), H3 (PTG14), H4 (PTG5), H11 (PTF11), H12 (PTG23), H14 (PTF18), H15 (PTD21), H17 (PTB12), J2 (PTG0), J3 (PTG2), J11 (PTF19), J12 (PTG22), J15 (PTF20), J16 (PTB15), J17 (PTB14), K2 (PTG3), K3 (PTF30), K4 (PTF29), K5 (PMOS_CTRL), K6 (PTG7), K7 (PTG6), K12 (PTG21), K13 (PTH7), K14 (PTF21), K16 (PTB17), K17 (PTB16), L2 (PTA26), L3 (PTE14), L4 (PTD15), L6 (PTG15), L9 (PTF31), L10 (PTF28), L12 (PTG20), L13 (PTH8), L15 (PTD19), L16 (PTD18), M1 (LFAST_0_TxD_P), M2 (LFAST_0_TxD_N), M3 (PTA29), M4 (PTD14), M6 (PTB22), M7 (PTB23), M8 (PTG16), M9 (PTG17), M10 (PTG18), M11 (PTG19), M12 (PTF24), M13 (PTH9), M14 (PTF23), N1 (LFAST_0_RxD_P), N2 (LFAST_0_RxD_N), N3 (PTD17), N11 (PTH12), N12 (PTH11), N13 (PTH10), N14 (PTC9), P1 (PTA31), P3 (PTA30), P5 (PTD13), P6 (PTC16), P7 (PTB24), P8 (PTB25), P9 (PTB27), P10 (PTB28), P11 (PTC12), P12 (PTF25), P13 (PTF27), P16 (PTB0), P17 (PTC28), R1 (PTB18), R2 (PTB19), R3 (PTE9), R4 (PTE8), R5 (PTD12), R6 (PTC17), R9 (PTB26), R11 (PTC13), R12 (PTF26), R15 (PTC24), R16 (PTC27), T1 (PTB20), T3 (PTC3), T4 (PTC2), T8 (PTC1), T9 (PTC14), T11 (PTB2), T12 (PTC19), T13 (PTC20), T14 (DSPI_MSC0_SOUT_N), T15 (PTC11), T17 (PTC10), U1 (PTB21), U3 (PTB4), U4 (PTD7), U7 (PTC0), U9 (PTC15), U10 (PTB3), U11 (PTC18), U12 (PTB29), U13 (DSPI_MSC0_SOUT_P), U15 (DSPI_MSC0_SCK_P), U16 (DSPI_MSC0_SCK_N), U17 (PTG12)
