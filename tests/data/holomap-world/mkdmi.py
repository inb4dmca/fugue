# Icons for the holomap world: a 96x96 transparent canvas for icon() to draw on, and solid floor/marker icons.
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
dmi(W + "/canvas.dmi", 96, 96, [("blank", lambda x, y: None)])
dmi(W + "/thing.dmi", 32, 32, [
    ("floor", lambda x, y: (96, 96, 96, 255)),
    ("marker", lambda x, y: (255, 0, 255, 255) if 8 <= x < 24 and 8 <= y < 24 else None),
])
