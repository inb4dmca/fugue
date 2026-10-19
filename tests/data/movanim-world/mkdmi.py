# Icons for the movanim world: one solid colour per state.
import sys
from PIL import Image, PngImagePlugin
W = sys.argv[1]
states = [("floor", (96, 96, 96, 255)), ("player", (200, 40, 40, 255)), ("white", (240, 240, 240, 255)), ("sync", (10, 10, 10, 255)), ("img", (250, 200, 0, 255))]
states += [("c%d" % i, (40 + 13 * i, 250 - 11 * i, (60 * i) % 200 + 30, 255)) for i in range(16)]
desc = "# BEGIN DMI\nversion = 4.0\n\twidth = 32\n\theight = 32\n"
img = Image.new("RGBA", (32 * len(states), 32), (0, 0, 0, 0))
for i, (name, c) in enumerate(states):
    desc += 'state = "%s"\n\tdirs = 1\n\tframes = 1\n' % name
    img.paste(c, (i * 32, 0, i * 32 + 32, 32))
desc += "# END DMI\n"
info = PngImagePlugin.PngInfo(); info.add_text("Description", desc, zip=True)
img.save(W + "/probe.dmi", format="PNG", pnginfo=info)
