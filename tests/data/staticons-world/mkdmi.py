import sys
from PIL import Image, PngImagePlugin
def dmi(path, w, h, states):
    # states: list of (name, dirs, frames(list of colour fns), delays)
    cells = []
    desc = "# BEGIN DMI\nversion = 4.0\n\twidth = %d\n\theight = %d\n" % (w, h)
    for name, dirs, frames, delay in states:
        desc += 'state = "%s"\n\tdirs = %d\n\tframes = %d\n' % (name, dirs, len(frames) // dirs)
        if delay: desc += "\tdelay = " + ",".join(str(d) for d in delay) + "\n"
        cells += frames
    desc += "# END DMI\n"
    cols = len(cells)
    img = Image.new("RGBA", (w * cols, h), (0, 0, 0, 0))
    for i, f in enumerate(cells):
        for y in range(h):
            for x in range(w):
                c = f(x, y)
                if c: img.putpixel((i * w + x, y), c)
    info = PngImagePlugin.PngInfo(); info.add_text("Description", desc, zip=True)
    img.save(path, format="PNG", pnginfo=info)
def solid(c, mark=(255,255,255,255), ms=8):
    return lambda x, y: mark if (x < ms and y < ms) else c
W = sys.argv[1]
dmi(W + "/i32.dmi", 32, 32, [
    ("red", 1, [solid((255,0,0,255))], None),
    ("white", 1, [solid((255,255,255,255), (0,0,0,255))], None),
    # dir order in a DMI: S N E W
    ("dirs", 4, [solid((0,0,255,255)), solid((0,255,0,255)), solid((255,0,255,255)), solid((255,255,0,255))], None),
    ("anim", 1, [solid((0,255,255,255)), solid((255,128,0,255))], [10, 10]),
    ("dot", 1, [lambda x, y: (0,160,0,255) if (x >= 24 and y >= 24) else None], None),
    ("floor", 1, [lambda x, y: (96,96,96,255) if (x+y)%8 else (64,64,64,255)], None),
])
dmi(W + "/i16.dmi", 16, 16, [("s", 1, [solid((128,64,0,255), (255,255,255,255), 4)], None)])
dmi(W + "/i64.dmi", 64, 64, [("b", 1, [solid((0,0,128,255), (255,255,255,255), 16)], None)])
