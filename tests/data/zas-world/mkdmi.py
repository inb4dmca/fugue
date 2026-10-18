# zas contrast-world icons: grey checker floor, a plain light tile, a mob, a crate, gasA a translucent full-tile cloud cycling magenta, yellow, green (3 frames, delay 2, like vg's plasma), gasB an opaque cyan half-tile.
import sys
from PIL import Image, PngImagePlugin
def dmi(path, w, h, states):
    cells = []
    desc = "# BEGIN DMI\nversion = 4.0\n\twidth = %d\n\theight = %d\n" % (w, h)
    for name, fs in states:
        fs = fs if isinstance(fs, list) else [fs]
        desc += 'state = "%s"\n\tdirs = 1\n\tframes = %d\n' % (name, len(fs))
        if len(fs) > 1: desc += "\tdelay = %s\n" % ",".join(["2"] * len(fs))
        cells += fs
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
dmi(W + "/thing.dmi", 32, 32, [
    ("floor", lambda x, y: (96, 96, 96, 255) if (x + y) % 8 else (64, 64, 64, 255)),
    ("tile", lambda x, y: (150, 150, 150, 255)),
    ("mob", lambda x, y: (40, 40, 255, 255) if 8 <= x < 24 and 4 <= y < 28 else None),
    ("crate", lambda x, y: (200, 200, 200, 255) if 4 <= x < 28 and 8 <= y < 24 else None),
    ("gasA", [lambda x, y: (255, 0, 255, 160), lambda x, y: (255, 255, 0, 160), lambda x, y: (0, 255, 0, 160)]),
    ("gasB", lambda x, y: (0, 255, 255, 255) if 0 <= y < 16 else None),
])
