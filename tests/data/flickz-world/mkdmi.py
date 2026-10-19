# flickz contrast-world icons, 32x32: z.dmi, one still base state per atom kind and one 8-frame flick state per target
# (delay 5 each: 4 s), each target's flick its own colour.
import sys
from PIL import Image, PngImagePlugin
W = sys.argv[1]
def sq(c, inset=0):
    return lambda x, y: c if inset <= x < 32 - inset and inset <= y < 32 - inset else None
base = [("floor", 1, sq((40, 40, 40, 255))), ("obj", 1, sq((0, 160, 0, 255), 10)), ("mob", 1, sq((0, 0, 200, 255), 8)),
        ("blank", 1, lambda x, y: None)]
fl = {"f_oz1": (255, 0, 255), "f_oz2": (255, 128, 0), "f_oz2u": (255, 255, 0), "f_oz3": (0, 255, 255),
      "f_oz2far": (255, 255, 255), "f_oz1far": (128, 0, 255), "f_tz1": (255, 0, 0), "f_tz2": (0, 255, 0),
      "f_mz1": (255, 128, 128), "f_mz2": (128, 255, 128), "f_az2": (128, 128, 255), "f_null": (200, 100, 0)}
states = [(n, f, d) for n, f, d in base] + [(n, 8, sq(c + (255,), 4)) for n, c in fl.items()]
desc = "# BEGIN DMI\nversion = 4.0\n\twidth = 32\n\theight = 32\n"
tiles = []
for n, frames, f in states:
    desc += 'state = "%s"\n\tdirs = 1\n\tframes = %d\n' % (n, frames)
    if frames > 1: desc += "\tdelay = " + ",".join(["5"] * frames) + "\n\tloop = 1\n"
    tiles += [f] * frames
desc += "# END DMI\n"
cols = 8
rows = (len(tiles) + cols - 1) // cols
img = Image.new("RGBA", (32 * cols, 32 * rows), (0, 0, 0, 0))
for i, f in enumerate(tiles):
    ox, oy = (i % cols) * 32, (i // cols) * 32
    for y in range(32):
        for x in range(32):
            c = f(x, y)
            if c: img.putpixel((ox + x, oy + y), c)
info = PngImagePlugin.PngInfo(); info.add_text("Description", desc, zip=True)
img.save(W + "/z.dmi", format="PNG", pnginfo=info)
