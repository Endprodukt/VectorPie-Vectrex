"""types.py : kinds (U blocks merged as kinds2.py) from scan_main.json + scan_anim.json, then animation links from
the frame-by-frame anim scenes -> types. Writes types.json [{tid, kinds:[{ranges, n}], scenes, codes, ex:{shot,pts}}]
and sheets types_N.png."""
import json, collections, os, math
from PIL import Image, ImageDraw
D = os.path.dirname(os.path.abspath(__file__))
main = json.load(open(f"{D}/scan_main.json")); an = json.load(open(f"{D}/scan_anim.json"))
d = main + an
GAP = 6
iv = []
for m in d:
    us = sorted({int(p["obj"], 16) for p in m["pieces"]})
    if not us: continue
    lo = prev = us[0]
    for u in us[1:]:
        if u - prev > GAP: iv.append((lo, prev)); lo = u
        prev = u
    iv.append((lo, prev))
iv.sort(); blocks = []
for lo, hi in iv:
    if blocks and lo <= blocks[-1][1] + GAP: blocks[-1][1] = max(blocks[-1][1], hi)
    else: blocks.append([lo, hi])
import bisect
starts = [b[0] for b in blocks]
def bid(u):
    i = bisect.bisect_right(starts, u) - 1
    return i if i >= 0 and u <= blocks[i][1] else None
for m in d:
    for p in m["pieces"]: p["b"] = bid(int(p["obj"], 16))
frames = collections.defaultdict(dict)
for mi, m in enumerate(d):
    for p in m["pieces"]:
        frames[p["b"]].setdefault(mi, []).append((p["cx"], p["cy"]))
parent = list(range(len(blocks)))
def find(a):
    while parent[a] != a: parent[a] = parent[parent[a]]; a = parent[a]
    return a
def union(a, b): parent[find(b)] = find(a)
# 1) kinds: same frames, near each other (only ROM U: RAM objects are shared by everything)
sig = collections.defaultdict(list)
for b, fr in frames.items():
    if blocks[b][0] >= 0xC800: continue
    sig[frozenset(fr)].append(b)
for s, bs in sig.items():
    marks = sorted(s)[:6]
    for i in range(len(bs)):
        for j in range(i + 1, len(bs)):
            a, b = bs[i], bs[j]
            if all(min(math.dist(p, q) for p in frames[a][mk] for q in frames[b][mk]) <= 3.5 for mk in marks): union(a, b)
kind_of = {b: find(b) for b in range(len(blocks))}
# 2) animation: instances (single link 3.5 %) per kind per frame; link kinds whose instances follow each other
def instances(pts):
    out = []
    for p in pts:
        for g in out:
            if any(math.dist(p, q) < 3.5 for q in g): g.append(p); break
        else: out.append([p])
    return [(sum(x for x, _ in g) / len(g), sum(y for _, y in g) / len(g), len(g)) for g in out]
links = collections.Counter()
base = len(main)
for mi in range(base, len(d) - 1):
    m0, m1 = d[mi], d[mi + 1]
    if m0["scene"] != m1["scene"] or m1["mark"] != m0["mark"] + 1: continue
    def inst(m):
        per = collections.defaultdict(list)
        for p in m["pieces"]:
            k = kind_of[p["b"]]
            if blocks[k][0] >= 0xC800: continue
            per[k].append((p["cx"], p["cy"]))
        return {k: instances(v) for k, v in per.items()}
    i0, i1 = inst(m0), inst(m1)
    for ka, la in i0.items():
        for (x, y, n) in la:
            for kb, lb in i1.items():
                if kb == ka: continue
                if any(math.dist((x, y), (u, v)) < 1.6 and 0.4 < n2 / n < 2.5 for (u, v, n2) in lb):
                    links[tuple(sorted((ka, kb)))] += 1
tparent = {}
def tf(a):
    tparent.setdefault(a, a)
    while tparent[a] != a: a = tparent[a]
    return a
for (a, b), n in links.items():
    if n >= 4: tparent[tf(b)] = tf(a)
types = collections.defaultdict(set)
for b in range(len(blocks)): types[tf(kind_of[b])].add(b)
out = []
for tid, (root, bs) in enumerate(sorted(types.items(), key=lambda t: min(blocks[b][0] for b in t[1]))):
    ks = collections.defaultdict(list)
    for b in bs: ks[kind_of[b]].append(b)
    marks = set().union(*[set(frames[b]) for b in bs])
    scenes = sorted({d[mi]["scene"] for mi in marks})
    codes = collections.Counter(p["code"] for mi in list(marks)[:200] for p in d[mi]["pieces"] if p["b"] in bs)
    # example from the main scan if possible
    best = None
    for mi in sorted(marks):
        pts = [pt for b in bs for pt in frames[b].get(mi, [])]
        g = max(instances(pts), key=lambda t: t[2])
        inst = [pt for pt in pts if math.dist(pt, g[:2]) < 10]
        sc = len(inst) + (1000 if mi < base else 0)
        if best is None or sc > best[0]: best = (sc, mi, inst)
    out.append(dict(tid=tid, kinds=[sorted((f"{blocks[b][0]:04X}", f"{blocks[b][1]:04X}") for b in v) for v in ks.values()],
                    blocks=sorted(bs), scenes=scenes, codes=dict(codes.most_common(3)), shot=d[best[1]]["shot"], pts=best[2],
                    lo=f"{min(blocks[b][0] for b in bs):04X}", hi=f"{max(blocks[b][1] for b in bs):04X}"))
json.dump(dict(blocks=blocks, types=out), open(f"{D}/types.json", "w"))
print(len(blocks), "blocks", len(set(kind_of.values())), "kinds", len(out), "types", sum(1 for v in links.values() if v >= 3), "links")
W = 150
for s in range(0, len(out), 40):
    part = out[s:s + 40]
    o = Image.new("RGB", (8 * W, 5 * (W + 14)), (25, 25, 25)); dr = ImageDraw.Draw(o)
    for i, k in enumerate(part):
        im = Image.open(k["shot"]).convert("RGB")
        xs = [p[0] for p in k["pts"]]; ys = [p[1] for p in k["pts"]]
        cx, cy = (min(xs) + max(xs)) / 2 * 7, (min(ys) + max(ys)) / 2 * 8.2
        r = max(40, (max(xs) - min(xs)) * 7 / 2 + 25, (max(ys) - min(ys)) * 8.2 / 2 + 25)
        cr = im.crop((int(cx - r), int(cy - r), int(cx + r), int(cy + r))).resize((W, W)).point(lambda v: min(255, v * 2))
        cd = ImageDraw.Draw(cr); sc = W / (2 * r)
        for (px, py) in k["pts"]:
            qx, qy = (px * 7 - (cx - r)) * sc, (py * 8.2 - (cy - r)) * sc
            cd.ellipse((qx - 2, qy - 2, qx + 2, qy + 2), outline=(255, 40, 40))
        X, Y = (i % 8) * W, (i // 8) * (W + 14)
        o.paste(cr, (X, Y + 14))
        dr.text((X + 2, Y), f"{k['tid']} {k['lo']} k{len(k['kinds'])}", fill=(255, 255, 0))
    o.save(f"{D}/types_{s // 40}.png")
with open(f"{D}/types.txt", "w") as f:
    for k in out:
        f.write(f"{k['tid']} {k['lo']}-{k['hi']} kinds={len(k['kinds'])} {k['codes']} {k['scenes'][:8]}\n")
