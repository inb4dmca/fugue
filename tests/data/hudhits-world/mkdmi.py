# Icons for the hudhits world: the 64x32 buttons, the 96x96 icon and image, and floor/player/slot icons.
import sys
from PIL import Image, PngImagePlugin
def dmi(path, w, h, states):
    cells = []
    desc = "# BEGIN DMI\nversion = 4.0\n\twidth = %d\n\theight = %d\n" % (w, h)
    for name, f in states:
        desc += 'state = "%s"\n\tdirs = 1\n\tframes = 1\n' % name
        cells.append(f)
    desc += "# END DMI\n"
    img = Image.new("RGBA", (w * len(cells), h), (0, 0, 0, 0))
    for i, f in enumerate(cells):
        for y in range(h):
            for x in range(w):
                c = f(x, y)
                if c: img.putpixel((i * w + x, y), c)
    info = PngImagePlugin.PngInfo(); info.add_text("Description", desc, zip=True)
    img.save(path, format="PNG", pnginfo=info)
W = sys.argv[1]
# PIL rows count from the top; icon y in DM counts from the bottom (icon y = h - row).
dmi(W + "/wide.dmi", 64, 32, [
    ("a", lambda x, y: None if 48 <= x < 56 else (32, 80, 224, 255)),
    ("b", lambda x, y: (224, 80, 32, 255)),
])
dmi(W + "/big.dmi", 96, 96, [
    ("c", lambda x, y: None if x >= 64 and y < 32 else (32, 192, 64, 255)),
    ("d", lambda x, y: None if x < 32 and y < 32 else (192, 32, 192, 255)),
])
dmi(W + "/thing.dmi", 32, 32, [
    ("floor", lambda x, y: (96, 96, 96, 255) if x and y else (64, 64, 64, 255)),
    ("player", lambda x, y: (40, 40, 40, 255) if 8 <= x < 24 and 8 <= y < 24 else None),
    ("slot", lambda x, y: (224, 224, 224, 255)),
])
