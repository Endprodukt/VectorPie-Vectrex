"""detail.py : per frame (block) of the detailed families, each entry's mean distance from the figure's centre
(0 centre .. 1 rim), from all sightings; entries split into inner / outer; rules for the runs. Writes detail.json
{fam: [(lo, hi, tier)]} and prints the rule count."""
import json, collections, os, math, bisect
D = os.path.dirname(os.path.abspath(__file__))
t = json.load(open(f"{D}/types.json")); blocks = t["blocks"]
d = json.load(open(f"{D}/scan_main.json")) + json.load(open(f"{D}/scan_anim.json"))
starts = [b[0] for b in blocks]
def bid(u):
    i = bisect.bisect_right(starts, u) - 1
    return i if i >= 0 and u <= blocks[i][1] else None
# families: (first block lo, last block lo, min entries) - only blocks with many entries are split
FAM = {"bat": (0x2E7E, 0x33E1), "bird": (0x346A, 0x370F), "cat": (0x3764, 0x3A19), "cres": (0xB1B4, 0xB4E3),
       "rocket": (0xB8A3, 0xBB44), "darts": (0x3404, 0x342F), "saucer": (0x9EC0, 0xA4FF), "xwing": (0xA676, 0xA8AC),
       "ring": (0xA8B6, 0xAC98), "star": (0xAD7F, 0xB180), "tri": (0xB51C, 0xB85B), "sship": (0x3C16, 0x3F2A),
       "debris": (0x3A76, 0x3B13), "acb8": (0xACB8, 0xAD45)}
rad = collections.defaultdict(list)          # (block, offset) -> radii
offs = collections.defaultdict(set)
for m in d:
    per = collections.defaultdict(list)
    for p in m["pieces"]:
        u = int(p["obj"], 16); b = bid(u)
        if b is None or not any(a <= blocks[b][0] <= z for a, z in FAM.values()): continue
        offs[b].add(u - blocks[b][0])
        per[b].append((u - blocks[b][0], p["cx"], p["cy"]))
    for b, ps in per.items():
        # instances: single link 4 %
        groups = []
        for q in ps:
            for g in groups:
                if any(math.dist(q[1:], r[1:]) < 4 for r in g): g.append(q); break
            else: groups.append([q])
        for g in groups:
            if len({q[0] for q in g}) < max(3, 0.7 * len(offs[b])): continue
            cx = sum(q[1] for q in g) / len(g); cy = sum(q[2] for q in g) / len(g)
            R = max(math.dist((cx, cy), q[1:]) for q in g) or 1
            for q in g: rad[(b, q[0])].append(math.dist((cx, cy), q[1:]) / R)
out = {}; nrules = 0
for name, (a, z) in FAM.items():
    rows = []
    fam = [b for b in range(len(blocks)) if a <= blocks[b][0] <= z]
    # the frames of a family with the same count of entries are the same figure in another pose, drawn in the same
    # order: entry i is the same part in each. So the radii of entry i are pooled over these frames, and every frame
    # gets the same split, also one that was seen too seldom to tell by itself.
    pool = collections.defaultdict(list)     # (count of entries, index) -> radii
    for b in fam:
        ks = sorted(offs[b])
        for i, k in enumerate(ks): pool[(len(ks), i)] += rad.get((b, k), [])
    split = {}
    for n in {len(offs[b]) for b in fam}:
        for limit in (0.55, 0.60):           # a figure whose parts all lie further out: a wider centre
            tiers = []
            for i in range(n):
                r = pool[(n, i)]
                tiers.append("inner" if r and sum(r) / len(r) < limit else "outer")
            # smooth: a lone entry takes its neighbours' tier
            for i in range(len(tiers)):
                l = tiers[i - 1] if i else tiers[-1]; rr = tiers[(i + 1) % len(tiers)]
                if l == rr != tiers[i]: tiers[i] = l
            if "inner" in tiers: break
        split[n] = tiers
    for b in fam:
        lo, hi = blocks[b]
        ks = sorted(offs[b])
        if len(ks) < 6:
            rows.append((lo, hi, "small")); continue
        tiers = split[len(ks)]
        for i, k in enumerate(ks):
            if tiers[i] != "inner": continue
            ulo = lo + k; uhi = (lo + ks[i + 1] - 1) if i + 1 < len(ks) else hi
            if rows and rows[-1][2] == "inner" and rows[-1][1] + 1 == ulo: rows[-1] = (rows[-1][0], uhi, "inner")
            else: rows.append((ulo, uhi, "inner"))
    # merge small blocks that follow each other (nothing is drawn from the gaps)
    m2 = []
    for r in rows:
        if m2 and r[2] == 'small' and m2[-1][2] == 'small' and r[0] - m2[-1][1] < 24: m2[-1] = (m2[-1][0], r[1], 'small')
        else: m2.append(r)
    out[name] = m2; nrules += len(m2)
json.dump(out, open(f"{D}/detail.json", "w"))
print(nrules, "detail rules", {k: len(v) for k, v in out.items()})
