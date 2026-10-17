# Icons for the progbar world: a do_after bar (32x32, the bar in rows 25..31,
# columns 4..27), its fill growing with the state; distinct solid floor/mob/crate/hud icons.
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
def bar(n):
    fill = 4 + 24 * n // 100      # columns 4..fill-1 are filled
    def f(x, y):
        if not (25 <= y <= 31 and 4 <= x <= 27): return None
        if y in (25, 31) or x in (4, 27): return (0, 0, 0, 255)          # frame
        return (0, 255, 0, 255) if x < fill else (80, 0, 0, 255)          # fill / empty
    return f
W = sys.argv[1]
dmi(W + "/doafter.dmi", 32, 32, [("prog_bar_%d" % n, bar(n)) for n in range(0, 101, 10)])
dmi(W + "/thing.dmi", 32, 32, [
    ("floor", lambda x, y: (96, 96, 96, 255) if (x + y) % 8 else (64, 64, 64, 255)),
    ("mob", lambda x, y: (40, 40, 255, 255) if 8 <= x < 24 and 4 <= y < 28 else None),
    ("crate", lambda x, y: (200, 200, 200, 255) if 4 <= x < 28 and 8 <= y < 24 else None),
    ("white", lambda x, y: (255, 255, 255, 255)),
    ("hud", lambda x, y: (255, 0, 255, 255) if 2 <= x < 30 and 2 <= y < 30 else None),
])
