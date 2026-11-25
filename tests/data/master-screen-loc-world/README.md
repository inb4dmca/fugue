# Plane masters without a usable screen_loc (stock 516.1689)

Does a plane master in `client.screen` act on its plane when its own `screen_loc` is null, empty, invalid or a
range? Windows BYOND **516.1689**, one DreamSeeker, Normal presentation, a 7x7 view (`view = 3`) of 32-pixel tiles.
Sources and icons are synthetic.

## Setup

`world.dm` puts a 16x16 red square (`st` from `z.dmi`) on plane 1 at the centre turf over a dark floor. For each
phase it creates a fresh `/obj/master` (`PLANE_MASTER`, plane 1), sets its `screen_loc` and one effect, and adds it to
`client.screen`. A phase is `<screen_loc>|<effect>`: `null` and `empty` mean `screen_loc = null` and `""`; effects
are a white size-1 outline, a size-2 blur, a green drop shadow, `alpha = 0`, `color = "#00ff00"`, KEEP_TOGETHER with
`alpha = 0`, a 16x16 cyan overlay (`img`), that overlay with the outline, and `render_target = "*cap"` with a
relay object (`render_source = "*cap"`, `screen_loc = "2,2"`). Between phases the screen is emptied.

## Observations

`stock-observations.json` holds, for each phase, its capture and the number of pixels of each marker colour in the
top-left 200x200 of the client area (outline white, part red, shadow green, overlay cyan, blurred red). The captures
are the matching `stock-*.png`; each was taken 1.5 s after the world logged READY for the phase and was identical to
a second capture 0.3 s later.

- `screen_loc` null, `""` or `bogus`: the master does nothing. The square draws plainly: no outline, blur or shadow,
  `alpha = 0`, colour and KEEP_TOGETHER have no effect, and the overlay is not drawn. With null or `""` the render
  target is not made either: the square stays in place and the relay shows nothing (`bogus` was not tried with it).
- `screen_loc = "1,1"`: every effect applies (outline 64 pixels, square hidden by `alpha = 0` or the green colour;
  with the render target the square leaves its place and the relay draws it one tile right and one up).
- `screen_loc = "1,1 to 3,3"`: the master acts. The plane is outlined once (64 pixels, as at 1,1), `alpha = 0`
  hides it, and an overlay is drawn at each of the nine positions, outlined too.

Fugue implements the null/empty case (`EntityMapTest::masterWithoutScreenLocIsInert`). An invalid text and a range
on a master tree (filters, KEEP_TOGETHER/KEEP_APART or attachments) are still refused, which drops the plane; stock
draws the plane plainly for the first and acts for the second. The parser cannot yet tell an invalid screen_loc from
a valid form it does not support, so that case stays refused.

## Reproduction

Requires BYOND and Python/Pillow. Stage icons, then this world:

```sh
python -B tests/data/local-relay-world/prepare.py <runtime>
cp tests/data/master-screen-loc-world/world.dm tests/data/master-screen-loc-world/world.dmf <runtime>/
dm.exe <runtime>/world.dme
python -B tests/data/master-screen-loc-world/collect.py <runtime> <out> "1,1|outline" "null|outline" ...
```

`collect.py` hosts the world with DreamDaemon `-safe -invisible` on TCP port 18811, starts DreamSeeker, writes each
phase to `phase.txt`, waits for READY and captures the client area with `PrintWindow`. It stops only the two
processes it started. The window must not be minimised.
