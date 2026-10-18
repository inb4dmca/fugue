# visanim contrast-world icons: solid 32x32 states; "once" plays red, green, blue once (delay 10, loop 1), "cyc" loops
# yellow, cyan, magenta (delay 7), p0..p4 are the phase indicator.
import sys
from PIL import Image, PngImagePlugin
def dmi(path, w, h, states):
    cells = []
    desc = "# BEGIN DMI\nversion = 4.0\n\twidth = %d\n\theight = %d\n" % (w, h)
    for name, frames, delay, loop in states:
        desc += 'state = "%s"\n\tdirs = 1\n\tframes = %d\n' % (name, len(frames))
        if len(frames) > 1: desc += "\tdelay = %s\n" % ",".join([str(delay)] * len(frames))
        if loop: desc += "\tloop = %d\n" % loop
        cells += frames
    desc += "# END DMI\n"
    img = Image.new("RGBA", (w * len(cells), h), (0, 0, 0, 0))
    for i, c in enumerate(cells):
        for y in range(h):
            for x in range(w):
                img.putpixel((i * w + x, y), c)
    info = PngImagePlugin.PngInfo(); info.add_text("Description", desc, zip=True)
    img.save(path, format="PNG", pnginfo=info)
R, G, B = (255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255)
dmi(sys.argv[1], 32, 32, [
    ("floor", [(60, 60, 60, 255)], 0, 0),
    ("tile", [(150, 150, 150, 255)], 0, 0),
    ("tile2", [(110, 110, 110, 255)], 0, 0),
    ("mob", [(40, 40, 40, 255)], 0, 0),
    ("once", [R, G, B], 10, 1),
    ("plain", [(255, 255, 255, 255)], 0, 0),
    ("cyc", [(255, 255, 0, 255), (0, 255, 255, 255), (255, 0, 255, 255)], 7, 0),
    ("ordA", [(255, 128, 0, 255)], 0, 0),
    ("ordB", [(128, 0, 255, 255)], 0, 0),
    ("p0", [(10, 20, 30, 255)], 0, 0),
    ("p1", [(10, 20, 60, 255)], 0, 0),
    ("p2", [(10, 20, 90, 255)], 0, 0),
    ("p3", [(10, 20, 120, 255)], 0, 0),
    ("p4", [(10, 20, 150, 255)], 0, 0),
    ("mark", [(201, 202, 203, 255)], 0, 0),
])
