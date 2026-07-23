"""Generate the Zeta Player robot-head icon (window + installer)."""
from PIL import Image, ImageDraw, ImageFilter
import os

S = 1024  # supersampled master
CX = S // 2

# ---------------- background: green smoky swirl ----------------
bg = Image.new("RGBA", (S, S), (30, 176, 36, 255))
bd = ImageDraw.Draw(bg)
greens = [
    (16, 120, 20), (46, 200, 52), (12, 96, 16), (80, 224, 84),
    (24, 150, 28), (110, 240, 110), (18, 130, 22), (60, 210, 64),
]
# concentric off-center rings to fake rolling smoke
import math
for i, g in enumerate(greens * 2):
    t = i / (len(greens) * 2)
    r = int(S * (0.95 - t * 0.9))
    ox = int(math.sin(i * 1.7) * S * 0.06)
    oy = int(math.cos(i * 1.3) * S * 0.06)
    bd.ellipse([CX - r + ox, CX - r + oy, CX + r + ox, CX + r + oy],
               outline=g, width=int(S * 0.055))
# bright center glow
glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
gd = ImageDraw.Draw(glow)
gd.ellipse([CX - S * 0.42, CX - S * 0.42, CX + S * 0.42, CX + S * 0.42],
           fill=(150, 245, 150, 120))
bg = Image.alpha_composite(bg, glow)
bg = bg.filter(ImageFilter.GaussianBlur(S * 0.02))

img = bg.copy()
d = ImageDraw.Draw(img)

GREY = (183, 187, 190, 255)
GREY_D = (120, 124, 128, 255)
BLACK = (17, 17, 17, 255)
WHITE = (245, 246, 248, 255)
LW = 26  # main outline width

# ---------------- neck ----------------
d.rounded_rectangle([410, 838, 614, 946], radius=22, fill=GREY, outline=BLACK, width=LW)
d.rectangle([398, 906, 626, 958], fill=BLACK)

# ---------------- head ----------------
d.rounded_rectangle([300, 178, 724, 862], radius=185, fill=GREY, outline=BLACK, width=LW)
# subtle side shading
shade = Image.new("RGBA", (S, S), (0, 0, 0, 0))
sd = ImageDraw.Draw(shade)
sd.rounded_rectangle([300, 178, 430, 862], radius=185, fill=(0, 0, 0, 30))
img = Image.alpha_composite(img, shade)
d = ImageDraw.Draw(img)

# ---------------- forehead arch (∩) ----------------
d.arc([430, 262, 594, 452], start=180, end=360, fill=GREY_D, width=24)
d.line([430, 356, 430, 486], fill=GREY_D, width=24)
d.line([594, 356, 594, 486], fill=GREY_D, width=24)

# ---------------- angry eyes ----------------
left_eye = [(348, 566), (478, 606), (470, 668), (340, 630)]
right_eye = [(676, 566), (546, 606), (554, 668), (684, 630)]
d.polygon(left_eye, fill=WHITE, outline=BLACK)
d.polygon(right_eye, fill=WHITE, outline=BLACK)
d.line(left_eye + [left_eye[0]], fill=BLACK, width=14, joint="curve")
d.line(right_eye + [right_eye[0]], fill=BLACK, width=14, joint="curve")

# ---------------- nose ridge ----------------
d.line([512, 664, 512, 706], fill=GREY_D, width=12)

# ---------------- mouth grille ----------------
d.rounded_rectangle([378, 704, 646, 796], radius=16, fill=BLACK)
for bx in range(430, 640, 52):
    d.line([bx, 712, bx, 788], fill=(70, 74, 78, 255), width=10)

# ---------------- export ----------------
out_dir = os.path.dirname(os.path.abspath(__file__))
res = os.path.join(out_dir, "..", "resources")
os.makedirs(res, exist_ok=True)
master = img.convert("RGBA").resize((256, 256), Image.LANCZOS)
master.save(os.path.join(res, "icon_256.png"))
master.save(os.path.join(res, "icon.ico"),
            sizes=[(16, 16), (24, 24), (32, 32), (48, 48),
                   (64, 64), (128, 128), (256, 256)])
img.resize((512, 512), Image.LANCZOS).save(os.path.join(res, "icon_preview.png"))
print("wrote icon.ico, icon_256.png, icon_preview.png to", os.path.abspath(res))
