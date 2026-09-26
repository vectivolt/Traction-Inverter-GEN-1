EASYEDA-IMPORT VARIANT (round 15, A13-R05). The symbol library is pre-mirrored about Y because EasyEDA's
KiCad-legacy importer places pins at (ux+px, uy+py) and ignores the "1 0 0 -1" orientation matrix.
Do NOT open this folder in native KiCad: use ../traction-native/ (same sheets, un-mirrored library).
Both variants are verified pin-by-pin with their consumer's placement rule (calculations/kicad5-verify.mjs).

FOOTPRINTS (round 22, F210): every symbol's F2 field is "traction:<name>" and resolves in traction.pretty, shipped here
with fp-lib-table (patterns copied verbatim from the KiCad 10 libraries or drawn from the archived datasheets — see
traction.pretty/README.md, SOURCES.json and MANIFEST.md). Off-board parts (the LEM sensors USNSU/V/W on the busbar) carry
no footprint on purpose. calculations/kicad-sch-verify.mjs proves every symbol pin has a pad of its number.
