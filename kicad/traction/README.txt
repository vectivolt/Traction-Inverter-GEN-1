KICAD 9/10 SET (rev A.21). Open traction.kicad_pro in KiCad 9 or 10: the root sheet holds the four boards
(power, cap bank, discharge, control card) as sheet instances. Each board is its own PCB: its labels are local,
so a net joins other boards only through the named connector/stud/tab, never by name across sheets.
Every sheet embeds its symbols (no library needed to open it); traction.kicad_sym + sym-lib-table are an identical
project library so KiCad's symbol-library checks pass. The MCU (UMCU) numbers its pins by BGA ball (H5, J7, ...);
every other part by its footprint pad. Footprint fields read "traction:<name>" and resolve in the shipped traction.pretty (fp-lib-table included):
patterns copied verbatim from the KiCad 10 libraries or drawn from the archived datasheets (traction.pretty/README.md, SOURCES.json,
MANIFEST.md) — "Update PCB from Schematic" needs nothing else; the LEM sensors USNSU/V/W are off-board (no footprint, excluded from the board).
Open traction-<board>.kicad_pro (power, capbank, disch, card) to lay out ONE board as its own PCB; docs/layout-handoff.md is the
layout engineer's entry point (stack, copper, creepage basis, rules by block, open decisions). Generated from the same sheets as the KiCad 5 set (kicad5/ in the repository, for KiCad 5-8 and EasyEDA;
KiCad 10 resolves none of its symbols) by calculations/kicad-sch-gen.mjs, and proved with kicad-cli (ERC + netlist
export compared net by net with circuit.json) by calculations/kicad-sch-verify.mjs.
