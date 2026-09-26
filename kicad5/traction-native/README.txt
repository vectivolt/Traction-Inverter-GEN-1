NATIVE KICAD 5 VARIANT (round 15, A13-R05). Same sheets as ../traction/, un-mirrored symbol library: KiCad 5
applies each component's orientation matrix to the library pin before adding its position. Open traction.sch
in KiCad 5.1.x. For EasyEDA import use ../traction/ instead. Both are verified by calculations/kicad5-verify.mjs.

FOOTPRINTS (round 22, F210): every symbol's F2 field is "traction:<name>" and resolves in traction.pretty, shipped here
with fp-lib-table (patterns copied verbatim from the KiCad 10 libraries or drawn from the archived datasheets — see
traction.pretty/README.md, SOURCES.json and MANIFEST.md). Off-board parts (the LEM sensors USNSU/V/W on the busbar) carry
no footprint on purpose. calculations/kicad-sch-verify.mjs proves every symbol pin has a pad of its number.
