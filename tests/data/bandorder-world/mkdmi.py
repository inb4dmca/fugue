# Icons for the bandorder scenes: `python3 mkdmi.py bands <dir>` (bands) or `pairs <dir>` (pairs, background, cells,
# cellsame). A floor that is a frame and a centre square, so what lies under it shows; bars that cross each other.
# The overlays/thresholds scenes keep the icons they were recorded with (res/).
import sys
from PIL import Image, PngImagePlugin
def dmi(path, w, h, states):
    desc = "# BEGIN DMI\nversion = 4.0\n\twidth = %d\n\theight = %d\n" % (w, h)
    for name, f in states: desc += 'state = "%s"\n\tdirs = 1\n\tframes = 1\n' % name
    desc += "# END DMI\n"
    img = Image.new("RGBA", (w * len(states), h), (0, 0, 0, 0))
    for i, (n, f) in enumerate(states):
        for y in range(h):
            for x in range(w):
                c = f(x, y)
                if c: img.putpixel((i * w + x, y), c)
    info = PngImagePlugin.PngInfo(); info.add_text("Description", desc, zip=True)
    img.save(path, format="PNG", pnginfo=info)
H = lambda c: (lambda x, y: c if 10 <= y < 22 else None)
V = lambda c: (lambda x, y: c if 10 <= x < 22 else None)
floor = ("floor", lambda x, y: (0, 255, 0, 255) if x < 2 or y < 2 or x > 29 or y > 29 or (13 <= x < 19 and 13 <= y < 19) else None)
player = ("player", lambda x, y: (40, 40, 40, 255) if 8 <= x < 24 and 8 <= y < 24 else None)
if sys.argv[1] == "bands":
    states = [floor, player, ("item", V((0, 0, 255, 255))), ("bar", H((255, 0, 0, 255)))]
else:
    states = [floor, player, ("sh", H((255, 0, 0, 255))), ("sv", V((255, 255, 0, 255))), ("mh", H((0, 255, 255, 255))), ("mv", V((0, 0, 255, 255)))]
dmi(sys.argv[2] + "/thing.dmi", 32, 32, states)
