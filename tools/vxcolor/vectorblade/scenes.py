"""The scenes for scan.py: (name, vxcolor command with MARK where pieces + a shot are taken)."""
START = "run 1500; run 20 stick 0,1; run 10 stick 0,1 buttons 1; run 5 stick 0,1"
moves = ["-1,0", "1,0", "-0.5,0", "0.6,0"]


def block(b):
    m = moves[b % 4]
    s = (f"; run 40 stick {m} buttons 4; run 4 stick {m}; run 40 stick {m} buttons 4; run 4; "
         f"run 50 stick {moves[(b+1)%4]} buttons 4; run 4; run 50 buttons 4; run 8")
    if b % 10 == 9:
        s += "; run 4 buttons 1; run 4"
    return s


def level(L, blocks=30, every=2, immortal=True):
    c = START + ("; hold C8B5 33" if immortal else "")
    if L > 0:
        c += f"; hold C823 {L-1:02X}"
    pre = 23
    for b in range(pre + blocks):
        c += block(b)
        if b == pre - 1 and L > 0:
            c += "; hold off" + ("; hold C8B5 33" if immortal else "")
        if b >= pre and (b - pre) % every == 0:
            c += "; MARK"
    return c


SCENES = []
# boot: GCE, version, logo, the ship picture, the desktop and its intro
SCENES.append(("boot", "; ".join(f"run 40; MARK" for _ in range(40))))
# desktop: options (right), achievements (left), high scores (down), help (button alone)
for name, st in (("opt", "1,0"), ("ach", "-1,0"), ("hs", "0,-1")):
    c = f"run 1500; run 20 stick {st}; run 10 stick {st} buttons 1; run 5 stick {st}"
    c += "".join(f"; run 60; MARK" for _ in range(12))
    c += "; run 10 stick 0,-1; run 60; MARK; run 10 stick 0,1; run 60; MARK; run 10 buttons 4; run 100; MARK; run 10 buttons 1; run 100; MARK"
    SCENES.append((name, c))
SCENES.append(("help", "run 1500; run 10 buttons 1; run 30; MARK; run 100; MARK; run 200; MARK; run 10 buttons 4; run 100; MARK"))
# the game, from many levels on
for L in (0, 3, 6, 9, 12, 15, 18, 21, 24, 25, 26, 30, 35, 40, 45, 49, 50, 51, 55, 60, 65, 70, 74, 75, 76, 80, 85, 90, 95, 99, 100, 101):
    SCENES.append((f"L{L}", level(L)))
# game over (mortal)
SCENES.append(("over", START + "".join(block(b) + ("; MARK" if b % 3 == 0 else "") for b in range(80))))


def anim(L, at=(4, 12, 22), n=24):
    c = START + "; hold C8B5 33" + (f"; hold C823 {L-1:02X}" if L > 0 else "")
    pre = 23
    for b in range(pre + max(at) + 1):
        c += block(b)
        if b == pre - 1 and L > 0:
            c += "; hold off; hold C8B5 33"
        if b - pre in at:
            c += "".join("; run 1; MARK" for _ in range(n))
    return c


ANIM = [(f"A{L}", anim(L)) for L in (0, 3, 6, 9, 12, 15, 18, 21, 24, 25, 26, 30, 35, 40, 45, 49, 50, 51, 55, 60, 65, 70, 74, 75, 76, 80, 85, 90, 95, 99, 100, 101)]
if __import__("os").environ.get("VB_ANIM"):
    SCENES = ANIM


def boss(L, n=36):
    c = START + "; hold C8B5 33" + f"; hold C823 {L-1:02X}"
    pre = 23
    for b in range(pre + n):
        c += block(b)
        if b == pre - 1:
            c += "; hold off; hold C8B5 33; hold C864 40"
        if b >= pre - 6:
            c += "; MARK"
    return c


BOSS = [(f"B{L}", boss(L)) for L in (25, 51, 77, 103)]
if __import__("os").environ.get("VB_BOSS"):
    SCENES = BOSS
