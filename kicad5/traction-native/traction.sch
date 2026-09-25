EESchema Schematic File Version 4
EELAYER 30 0
EELAYER END
$Descr User 12000 8000
encoding utf-8
Sheet 1 1
Title "Traction Inverter 220 kW pk - schematic set"
Date "2026-09-23"
Rev "A.17"
Comp "Traction Inverter"
Comment1 "Power board sheet 1 - Cap bank sheet 2 - Discharge board sheet 3 - Control card sheet 4"
Comment2 "800 V-class, 120 kW cont / 220 kW pk, ASIL-D-capable architecture"
Comment3 ""
Comment4 ""
$EndDescr
$Sheet
S 1000 1000 4200 1400
U 5E0002AF
F0 "Traction Inverter 220 kW pk - Power board (SiC 3-phase, 2-level)" 70
F1 "traction-power.sch" 70
$EndSheet
$Sheet
S 6200 1000 4200 1400
U 5E0002B0
F0 "Traction Inverter - Cap bank (laminated busbar + 16x 20 uF film can array)" 70
F1 "traction-capbank.sch" 70
$EndSheet
$Sheet
S 1000 3200 4200 1400
U 5E0002B1
F0 "Traction Inverter - Discharge board (bolt-on bleeder + active discharge)" 70
F1 "traction-disch.sch" 70
$EndSheet
$Sheet
S 6200 3200 4200 1400
U 5E0002B2
F0 "Traction Inverter - Control Card (S32K396 + FS26, ASIL D)" 70
F1 "traction-card.sch" 70
$EndSheet
$EndSCHEMATC
