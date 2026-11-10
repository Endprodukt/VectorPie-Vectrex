"""scsheet.py OUT.png SCENE[,SCENE..] [step] : the scene shots of shots/ in one sheet (6 per row)"""
import sys, glob, os
from PIL import Image
D = os.path.dirname(os.path.abspath(__file__))
out, names = sys.argv[1], sys.argv[2].split(",")
step = int(sys.argv[3]) if len(sys.argv) > 3 else 1
fs = []
for n in names: fs += sorted(glob.glob(f"{D}/shots/{n}_*.png"))[::step]
fs = fs[:36]
W, H = 233, 273; cols = 6; rows = (len(fs) + cols - 1) // cols
o = Image.new("RGB", (W * cols, H * rows))
for i, f in enumerate(fs):
    o.paste(Image.open(f).convert("RGB").resize((W, H)), (W * (i % cols), H * (i // cols)))
o.save(f"{D}/{out}")
print(len(fs))
