"""entries.py : for each family, its frames (blocks) with their entry offsets; for one frame per family a big crop
with the entry indexes at the strokes' centres -> ent_<fam>.png. Writes fam.json {fam: [[lo, [offs]], ...]}"""
import json, collections, os, math, bisect
from PIL import Image, ImageDraw, ImageEnhance
D = os.path.dirname(os.path.abspath(__file__))
t = json.load(open(f"{D}/types.json")); blocks = t["blocks"]
d = json.load(open(f"{D}/scan_main.json")) + json.load(open(f"{D}/scan_anim.json")) + json.load(open(f"{D}/scan_boss.json"))
starts = [b[0] for b in blocks]
def bid(u):
    i = bisect.bisect_right(starts, u) - 1
    return i if i >= 0 and u <= blocks[i][1] else None
offs = collections.defaultdict(set)
for m in d:
    for p in m["pieces"]:
        u = int(p["obj"], 16); b = bid(u)
        if b is not None: offs[b].add(u - blocks[b][0])
import sys
FAM = {"ship": (0x1F1B, 0x1F40), "bat": (0x2E7E, 0x33E1), "bird": (0x346A, 0x370F), "cat": (0x3764, 0x3A19),
       "debris": (0x3A76, 0x3B13), "sship": (0x3C16, 0x3F2A), "xwing": (0xA676, 0xA8AC), "acb8": (0xACB8, 0xAD45),
       "star": (0xAD7F, 0xB180), "cres": (0xB1B4, 0xB4E3), "tri": (0xB51C, 0xB85B), "rocket": (0xB8A3, 0xBB44),
       "boss1": (0x4920, 0x49E0), "boss2": (0x59A0, 0x5DE0), "boss3": (0x5E00, 0x6250), "boss4": (0x4230, 0x4460),
       "saucer": (0x9EC0, 0xA4FF), "darts": (0x3404, 0x342F), "ring": (0xA8B6, 0xAC98)}
if len(sys.argv) > 1:
    FAM = {f'f{a}': (int(a, 16), int(a, 16)) for a in sys.argv[1:]}
fam = {}
for name, (a, z) in FAM.items():
    fam[name] = [[blocks[b][0], sorted(offs[b])] for b in range(len(blocks)) if a <= blocks[b][0] <= z]
if len(sys.argv) == 1: json.dump(fam, open(f"{D}/fam.json", "w"))
# one picture per family: the frame (block) seen most, the mark where one instance shows most of its entries
for name, (a, z) in FAM.items():
    bs = [b for b in range(len(blocks)) if a <= blocks[b][0] <= z]
    best = None
    for m in d:
        for b in bs:
            ps = [p for p in m["pieces"] if bid(int(p["obj"], 16)) == b]
            if not ps: continue
            c0 = (ps[0]["cx"], ps[0]["cy"])
            inst = {}
            for p in ps:
                if math.dist((p["cx"], p["cy"]), c0) < 9: inst.setdefault(int(p["obj"], 16) - blocks[b][0], (p["cx"], p["cy"]))
            sc = len(inst) * 10 + (5 if m["scene"][0] in "LB" else 0)
            if best is None or sc > best[0]: best = (sc, m["shot"], b, inst)
    if not best: continue
    _, shot, b, inst = best
    ks = sorted(offs[b])
    xs = [v[0] for v in inst.values()]; ys = [v[1] for v in inst.values()]
    cx, cy = (min(xs) + max(xs)) / 2 * 7, (min(ys) + max(ys)) / 2 * 8.2
    r = max(45, (max(xs) - min(xs)) * 7 / 2 + 20, (max(ys) - min(ys)) * 8.2 / 2 + 20)
    W = 520
    im = Image.open(shot).convert("RGB").crop((int(cx - r), int(cy - r), int(cx + r), int(cy + r))).resize((W, W))
    im = ImageEnhance.Brightness(im).enhance(2.5); dr = ImageDraw.Draw(im); sc = W / (2 * r)
    for off, (px, py) in inst.items():
        k = ks.index(off) if off in ks else -1
        qx, qy = (px * 7 - (cx - r)) * sc, (py * 8.2 - (cy - r)) * sc
        dr.text((qx - 4, qy - 6), str(k), fill=(255, 60, 60))
    dr.text((4, 4), f"{name} frame {blocks[b][0]:04X} entries {len(ks)}", fill=(255, 255, 0))
    im.save(f"{D}/ent_{name}.png")
print({k: len(v) for k, v in fam.items()})
