"""vbmenu.py : the rules of the boot screens and the desktop (logo, intro ship, icons, the man). These pictures are lists in
bank 0 whose addresses other banks use for other things (the man's lists lie where the bat's tables are), so every rule here
also names the caller. MENU goes in front of the rules by address alone (mkprof.py)."""
import os
ROOT = r"F:/VectorPie Vectrex Emulator"
ROM = open(os.path.join(ROOT, "build/pc/roms/vectorblade.bin"), "rb").read()

SKIN, SUIT = "FFD8A8 *1.4", "80C8FF *1.4"
STEEL, GOLD, RED, WHITE = "A8CCFF", "FFC020", "FF4030", "E8ECFF"

def s8(v): return v - 256 if v > 127 else v

def synclist(a):
    """Entries (addr, mode, y, x) of the sync list at a in bank 0, up to and with its end (02): FF draw, 00 move, 01 sync."""
    out = []
    for k in range(400):
        m, y, x = ROM[a + 3 * k], s8(ROM[a + 3 * k + 1]), s8(ROM[a + 3 * k + 2])
        out.append((a + 3 * k, m, y, x))
        if m == 2: return out
        if m not in (0, 1, 0xFF): return None
    return None

def strokes(lst):
    """The drawn entries: (u tag = address + 3, x0, y0, x1, y1), y up. A sync entry starts again from the centre."""
    px = py = 0.0; out = []
    for a, m, y, x in lst:
        if m == 1: px, py = x * 1.12, y * 1.12
        elif m == 0: px += x; py += y
        elif m == 0xFF:
            out.append((a + 3, px, py, px + x, py + y)); px += x; py += y
    return out

def head(st):
    """The u tags of the man's head: the strokes with both ends in a circle under the top of the figure."""
    pts = [p for s in st for p in ((s[1], s[2]), (s[3], s[4]))]
    top = max(y for x, y in pts)
    tx = [x for x, y in pts if y > top - 30]
    cx, cy = sum(tx) / len(tx), top - 33
    near = lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= 46 ** 2
    return set(s[0] for s in st if near(s[1], s[2]) and near(s[3], s[4]))

def man_frames(a=0x27CB, end=0x3420):
    """The man's 24 running frames: sync lists one after the other."""
    out = []
    while a < end:
        l = synclist(a)
        if not l or l[0][1] != 1 or len(l) < 6: a += 1; continue
        out.append(l); a = l[-1][0] + 1
    return out

def ranges(us, hits):
    """The runs of neighbours in us (sorted) that are all in hits, as (lo, hi)."""
    out = []; run = None
    for u in us:
        if u in hits: run = (run[0], u) if run else (u, u)
        elif run: out.append(run); run = None
    if run: out.append(run)
    return out

MENU = []
def rule(name, caller, col, *spans):
    """One rule per span 'LO-HI' of u (none: every line of the caller)."""
    for i, s in enumerate(spans or [None]):
        MENU.append((name if len(spans) < 2 else "%s%d" % (name, i), "caller %s" % caller + (" u %s" % s if s else ""), col))

# --- the logo (boot): "Vector" gold, "blade" red, the two swooshes steel blue ---
LOGO = "7149-7172"
rule("logo_swoosh", LOGO, STEEL, "76B4-76E1", "7724-777E", "77B5-77D6")
rule("logo_blade", LOGO, RED, "778D-77A6", "77E5-7858", "7900-79FB")
rule("logo_vector", LOGO, GOLD)

# --- the fighter of the intro: blue tones, a white cockpit, of the weapons only the muzzle red, red and yellow flames ---
BSHIP = "7225-7242"
rule("iship_cockpit", BSHIP, "F0F4FF", "72BA-72BF", "7543-754D")
rule("iship_muzzle", BSHIP, "FF4030", "731C-7321", "7374-7379", "738D-7390", "748A-748F", "74CB-74D0", "752F-7539")
rule("iship_weapon", BSHIP, "6FA0F0", "7305-73B3", "7476-74A3", "74CB-7539")
rule("iship_engine", BSHIP, "D0E4FF", "73F9-743A")
rule("iship_hull", BSHIP, STEEL)
rule("iship_flame_y", "727D-727D", "FFD020", "75B1-75B1", "75C5-75C5", "75D9-75D9")
rule("iship_flame_r", "727D-727D", "FF4020")

# --- the desktop's icons ---
rule("ic_podium_1st", "3A29-3A29", "FFD020", "45FB-4603")      # the box in the middle
rule("ic_podium_2nd", "3A29-3A29", "C8D0E0", "45BF-45C5")      # the left box
rule("ic_podium_3rd", "3A29-3A29", "E08840", "45C8-45D0")      # the right box
rule("ic_podium_no", "3A29-3A29", WHITE)                       # the digits
rule("ic_ship_gun", "3A73-3A73", "FF6040", "21AA-21AA", "21BE-21BE", "21DC-21DC", "21E6-21E9")
rule("ic_ship_nose", "3A73-3A73", "E8F0FF", "21C7-21CA")
rule("ic_ship_fire", "3A73-3A73", "FFA020", "21FD-21FD")
rule("ic_ship", "3A73-3A73", STEEL)
rule("ic_driver_grip", "3ABA-3ABA", "FF5040", "462B-4637")
rule("ic_driver", "3ABA-3ABA", "D8E0F0", "4625-464D")
rule("ic_hammer_head", "3ABA-3ABA", "9FB8E0", "465D-466E")
rule("ic_hammer", "3ABA-3ABA", "D09050")
rule("ic_scroll_paper", "3AF8-3AF8", "F0E0B0", "4689-46B3", "46D1-46E0", "4732-4732")
rule("ic_scroll_roll", "3AF8-3AF8", "E09040")
# the console: grey, its displays (three strokes on the desk, three on the screen) red, green, blue and yellow
rule("ic_desk_red", "3B29-3B29", "FF4030", "4778-4778", "47F5-47F5")
rule("ic_desk_green", "3B29-3B29", "40FF60", "4782-4782", "47EB-47EB")
rule("ic_desk_blue", "3B29-3B29", "5080FF", "478C-478C")
rule("ic_desk_yellow", "3B29-3B29", "FFE040", "47E1-47E1")
rule("ic_desk_screen", "3B29-3B29", "D8DCE8", "4796-47A5")
rule("ic_desk", "3B29-3B29", "A0A8B8")

# --- the man: skin for the head, a light blue suit, in every frame ---
rule("man_head", "3B58-3B58", SKIN, "47FF-480D")               # standing
rule("man_suit", "3B58-3B58", SUIT)
for n, l in enumerate(man_frames()):                           # running: caller 2702
    st = strokes(l)
    for i, (lo, hi) in enumerate(ranges([s[0] for s in st], head(st))):
        MENU.append(("man%02d_head%d" % (n, i), "caller 2702-2702 u %04X-%04X" % (lo, hi), SKIN))
MENU.append(("man_run_suit", "caller 2702-2702", SUIT))

# --- the text on the desktop's screen: its addresses are the bee's wings in another bank ---
for n, c in enumerate(("3DC8", "5DA0", "01A0")):
    MENU.append(("desk_text%d" % n, "caller %s-%s u 4940-498F" % (c, c), "60FF80"))
