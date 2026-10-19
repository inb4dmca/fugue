# Icons for the hudstack world: floor, player, an opaque slot background, a jumpsuit-shaped item with a transparent margin
# and a wholly transparent state.
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
# PIL rows count from the top. The item is opaque only over x 8..23, y 4..27 of its 32x32 cell.
dmi(W + "/thing.dmi", 32, 32, [
    ("floor", lambda x, y: (96, 96, 96, 255) if x and y else (64, 64, 64, 255)),
    ("player", lambda x, y: (40, 40, 40, 255) if 8 <= x < 24 and 8 <= y < 24 else None),
    ("slot", lambda x, y: (224, 224, 224, 255)),
    ("item", lambda x, y: (32, 80, 224, 255) if 8 <= x < 24 and 4 <= y < 28 else None),
    ("blank", lambda x, y: None),
])
