# Icons for the hudoverlay world: a parent with a transparent 4 px border, a centre overlay, a full underlay, a small
# nested overlay, a full magenta, the floor.
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
sq = lambda a, b, c: (lambda x, y: c if a <= x <= b and a <= y <= b else None)
dmi(sys.argv[1] + "/h.dmi", 32, 32, [
    ("floor", lambda x, y: (96, 96, 96, 255) if x and y else (64, 64, 64, 255)),
    ("base", sq(4, 27, (32, 80, 224, 255))),
    ("ov", sq(10, 21, (224, 32, 32, 255))),
    ("un", sq(0, 31, (32, 192, 64, 255))),
    ("ov2", sq(14, 17, (240, 224, 32, 255))),
    ("full", sq(0, 31, (192, 32, 192, 255))),
])
