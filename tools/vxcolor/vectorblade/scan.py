"""scan.py : runs the scenes in scenes.py in parallel; at every MARK it dumps `pieces` and a shot. Writes scan.json:
[{scene, mark, shot, pieces: [{lines:(a,b), cx, cy, list:(lo,hi), code, obj, parts, dots}]}]"""
import subprocess, os, re, json, sys
from concurrent.futures import ThreadPoolExecutor
VX = r"F:/VectorPie Vectrex Emulator/build/pc/vxcolor.exe"
D = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
sys.path.insert(0, D)
from scenes import SCENES

PR = re.compile(r"piece (\d+): lines (\d+)-(\d+), centre (-?\d+),(-?\d+), list (\w+)-(\w+), code (\w+), object (\w+), (\d+) parts?(, dots)?")


def run(scene):
    name, c = scene
    os.makedirs(f"{D}/shots", exist_ok=True)
    parts = c.split("MARK")
    cc = parts[0]; shots = []
    for i, p in enumerate(parts[1:]):
        f = f"{D}/shots/{name}_{i:03d}.png"; shots.append(f)
        cc += f"shot {f}; pieces; uncoloured" + p
    r = subprocess.run([VX, "vectorblade", cc], capture_output=True, text=True, cwd=r"F:/VectorPie Vectrex Emulator")
    out = []
    blocks = r.stdout.split("> shot ")[1:]
    for i, b in enumerate(blocks):
        ps = [dict(lines=(int(m[1]), int(m[2])), cx=int(m[3]), cy=int(m[4]), list=(m[5], m[6]), code=m[7], obj=m[8],
                   parts=int(m[9]), dots=bool(m[10])) for m in PR.findall(b)]
        unc = re.findall(r'(figure \d+: .*without a rule)', b)
        out.append(dict(scene=name, mark=i, shot=shots[i] if i < len(shots) else None, pieces=ps, unc=unc))
    if not blocks:
        print(name, "NO OUTPUT", r.stdout[-500:], r.stderr[-500:])
    return out


if __name__ == "__main__":
    only = sys.argv[1:]
    sc = [s for s in SCENES if not only or s[0] in only]
    with ThreadPoolExecutor(8) as ex:
        res = [x for r in ex.map(run, sc) for x in r]
    old = []
    if only and os.path.exists(f"{D}/scan.json"):
        old = [x for x in json.load(open(f"{D}/scan.json")) if x["scene"] not in only]
    json.dump(old + res, open(f"{D}/scan.json", "w"))
    print(len(res), "marks,", sum(len(x["pieces"]) for x in res), "pieces")
