import sys
from PIL import Image, PngImagePlugin
# A state: (name, dirs, cells frame-major) or (name, dirs, cells, delays, movement).
def dmi(path, w, h, states):
    cells = []
    desc = "# BEGIN DMI\nversion = 4.0\n\twidth = %d\n\theight = %d\n" % (w, h)
    for s in states:
        name, dirs, fs = s[:3]
        delays = s[3] if len(s) > 3 else None
        desc += 'state = "%s"\n\tdirs = %d\n\tframes = %d\n' % (name, dirs, len(fs) // dirs)
        if delays: desc += "\tdelay = %s\n" % ",".join(str(d) for d in delays)
        if len(s) > 4 and s[4]: desc += "\tmovement = 1\n"
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
def grad(x, y):
    a = 128 if x < 4 else (0 if y < 2 else 255)
    if a == 0: return None
    return (x * 8, 255 - y * 8, 0x80 if (x + y) % 2 else 0x20, a)
def green(x, y):
    if 8 <= x < 24 and 8 <= y < 24: return (200, 30, 30, 255)
    if y < 4: return None
    return (0, 255, 0, 255)
def dirc(c):
    return lambda x, y: (c[0], c[1], c[2], 255) if x < 20 or y < 10 else None
# A cell telling its dir d and frame f apart: a bar whose width grows with f, over a colour set by d.
def walk(d, f, moving):
    def px(x, y):
        if x < 2 + 6 * f and y >= 16: return (255, 255, 255, 200)
        if (x + y) % 5 == 0: return None
        return (40 * d + 20, 255 - 30 * d, 120 if moving else 30, 255 if moving else 160 + 10 * d)
    return px
def eight(d):
    return lambda x, y: (30 * d, 200 - 20 * d, 255 - 30 * d, 255) if x + y < 8 + 6 * d else None
dmi(W + "/src.dmi", 32, 32, [
    ("grad", 1, [grad]),
    ("green", 1, [green]),
    ("blank", 1, [lambda x, y: None]),
    ("dirs", 4, [dirc((255, 0, 0)), dirc((0, 255, 0)), dirc((0, 0, 255)), dirc((255, 255, 0))]),
    ("walk", 4, [walk(d, f, False) for f in range(2) for d in range(4)], [2, 3]),
    ("walk", 4, [walk(d, f, True) for f in range(3) for d in range(4)], [1, 1, 4], True),
    ("eight", 8, [eight(d) for d in range(8)]),
])
dmi(W + "/canvas.dmi", 64, 64, [("blank", 1, [lambda x, y: None]), ("fill", 1, [lambda x, y: (x * 4, y * 4, 100, 255)])])
