# particles contrast-world icons: p.dmi (32x32) a black floor, a blank, and "dust" (eight white specks, like vg's zas_dust);
# sq.dmi (8x8) a solid white square.
import sys
from PIL import Image, PngImagePlugin
def dmi(path, w, h, states):
    desc = "# BEGIN DMI\nversion = 4.0\n\twidth = %d\n\theight = %d\n" % (w, h)
    for name, _ in states:
        desc += 'state = "%s"\n\tdirs = 1\n\tframes = 1\n' % name
    desc += "# END DMI\n"
    img = Image.new("RGBA", (w * len(states), h), (0, 0, 0, 0))
    for i, (_, f) in enumerate(states):
        for y in range(h):
            for x in range(w):
                c = f(x, y)
                if c: img.putpixel((i * w + x, y), c)
    info = PngImagePlugin.PngInfo(); info.add_text("Description", desc, zip=True)
    img.save(path, format="PNG", pnginfo=info)
W = sys.argv[1]
specks = {(5, 7), (12, 3), (20, 9), (27, 14), (9, 18), (16, 16), (23, 24), (3, 28)}
dmi(W + "/p.dmi", 32, 32, [
    ("floor", lambda x, y: (0, 0, 0, 255)),
    ("blank", lambda x, y: None),
    ("dust", lambda x, y: (255, 255, 255, 255) if (x, y) in specks else None),
])
dmi(W + "/sq.dmi", 8, 8, [("sq", lambda x, y: (255, 255, 255, 255))])
