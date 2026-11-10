"""mkprof.py : writes the Vectorblade profile from the RULES table (first match wins) to package/colors and
build/pc/colors."""
import os
ROOT = r"F:/VectorPie Vectrex Emulator"
HEAD = """; vectorblade: colour profile (src/render/colors.h), made with vxcolor (docs/AI_GUIDE.md).
; name = CONDITIONS : RRGGBB [*GAIN], the first rule that matches a line gives its colour.
; Vectorblade draws its sprites stroke by stroke from tables: the rules go by the table's address (u).
; Colours after Warblade: bright insect aliens on a dark starfield, a steel blue fighter.
; the cartridges it is made for (CRC32 without the padding at the end):
crc = 2A806F46
"""
from vbrules import RULES
import json
DET = {"bat": {"inner": "FFE040"}, "bird": {"inner": "FF4030"}, "cat": {"inner": "FF70C0"},
       "cres": {"inner": "FFE040"}, "rocket": {"inner": "E8F0FF"}, "darts": {"inner": "FFE040", "small": "FFE040"},
       "saucer": {"inner": "40E8FF"}, "xwing": {"inner": "E8F0FF", "small": "FF5040"}, "ring": {"inner": "9FA8FF", "small": "FFE040"},
       "star": {"inner": "FF8020", "small": "FF4040"}, "tri": {"inner": "FFD040", "small": "E8ECFF"},
       "sship": {"inner": "FFC040", "small": "FFE040"}, "debris": {"inner": "FFB060", "small": "FF9030"}, "acb8": {}}
det = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "detail.json")))
DETAIL = [
    # the fighter: steel blue (the base rule), only the cockpit lighter (frames 1F1B / 1F5E: entries 4-7)
    ("ship_nose", "u 1F27-1F31", "E0F0FF"), ("ship2_nose", "u 1F6A-1F74", "E0F0FF"),
]
for fam, rows in det.items():
    for i, (lo, hi, tier) in enumerate(rows):
        col = DET.get(fam, {}).get(tier)
        if col: DETAIL.append((f"{fam}_{tier[0]}{i}", f"u {lo:04X}-{hi:04X}", col))
RULES = RULES[:5] + DETAIL + RULES[5:]
lines = [HEAD]
for name, cond, col in RULES:
    lines.append(f"{name:<14} = {cond} : {col}\n")
txt = "".join(lines)
for p in ("package/colors/vectorblade.ini", "build/pc/colors/vectorblade.ini"):
    open(os.path.join(ROOT, p), "w", newline="\n").write(txt)
print(len(RULES), "rules")
