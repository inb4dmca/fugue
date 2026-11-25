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
relay object (`render_source = "*cap"`, `screen_loc = "2,2"`). The `obj` effect is a control: the same object
without `PLANE_MASTER`, with the cyan square as its own icon. Between phases the screen is emptied.

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

Phases with their own `"crop": [0, 0, 224, 224]` count the whole map and also give each marker colour's `bounds`
(left, top, right, bottom) and the number of black pixels. In the plain view the square is at 96..111 both ways and
an overlay at 7,1 at 192..207 (`7,1|ov`).

- One position off the view (`8,1`, `0,4`, `1,9`): the master acts (`alpha = 0`, the green colour and KEEP_TOGETHER
  with `alpha = 0` hide the square, the outline is 64 pixels), and the map grows by the missing tiles exactly as it
  does for the control object: the larger map is centred, so the square moves 16 pixels left for `8,1`, 16 right
  for `0,4` and 32 down for `1,9`, with black where there is no map. The overlay is drawn at that position: for
  `8,1` at 208..223 across, the new column; for `0,4` and `1,9` it falls outside the 224 pixel window, and only the
  right edge of its outline shows for `0,4`. Every `|ov` capture equals its `|obj` capture.
- A pixel offset past the edge (`7:40,1`): the master acts, nothing is added to the map and the overlay is out of
  sight; at `7:24,1` its left 8 pixels show.
- A range partly or wholly off the view (`6,6 to 9,9`, `9,9 to 10,10`): the master acts, the map grows to hold the
  whole range (9x9 and 10x10 tiles), and an overlay is drawn at each position (nine of the sixteen are inside the
  window for `6,6 to 9,9`, none for `9,9 to 10,10`), again as for the control object.
- A range with different pixel offsets at its ends: the master acts and the grid of overlays is moved by the first
  end's offsets only. `1,1 to 3:8,3` and `1,1 to 3:40,3` draw the nine overlays of `1,1 to 3,3`; `1:8,1 to 3,3`
  draws them 8 pixels right; `1:4,1:8 to 3:12,3` 4 right and 8 up; `3:8,3 to 1,1` 8 right. The control object's
  icons are in the same places.
- A range with a percent end: the master acts, and the overlays are where the control object's icons are, which is
  not a plain grid between the two ends. `0%,0% to 50%,50%` and `1,1 to 50%,50%` draw one overlay at 1,1;
  `2,2 to 100%,3` draws two, at 7,2 and 7,3; `50%,50% to 6,6` draws sixteen, from 4,4 to the top right corner.

In the larger maps part of the world next to the added tiles is black as well (a 16 pixel strip on the left for
`8,1`; the top 64 rows for `1,9`); it is the same for the master and the control object and is not examined here.

Fugue does the first three. A master with no screen_loc, or with text that is not coordinates, is skipped
(`EntityMapTest::masterWithoutScreenLocIsInert`); `bogus` is the only such text that was tried, and anything the
screen_loc grammar cannot read as two axes is treated like it. A master with a range acts once and draws what it owns
at every position (`EntityMapTest::masterRangeDrawsOverlaysAtEveryPosition`).

A master off the view, alone or as a range, acts and asks for the larger map like any screen object; its overlays
are drawn there once the scene has grown, which is the frame after the one that learns of the placement, and are
left out until then or when the scene cannot grow (`EntityMapTest::masterOffViewActsInTheLargerMap`, which compares
the square's and the overlay's places with these captures). A range's grid follows its first end's pixel offsets for
masters and ordinary objects (`EntityMapTest::masterRangePixelOffsetsFollowTheFirstEnd`).

Still refused, which drops the plane, for a master with KEEP_TOGETHER, overlays, underlays or a blur, outline or
shadow filter: a range with a percent end, and a range written from its upper end down that has a pixel offset
(`3:8,3 to 1,1`). Stock places both as it does for an ordinary object; Fugue does not place those ranges for
ordinary objects either, the percent rule is not understood from these five captures, and the reversed form has one.
Different offsets on an edge-aligned end (`LEFT`, `TOP`) were not tried and stay unplaced.

## Reproduction

Requires BYOND and Python/Pillow. Stage icons, then this world:

```sh
python -B tests/data/local-relay-world/prepare.py <runtime>
cp tests/data/master-screen-loc-world/world.dm tests/data/master-screen-loc-world/world.dmf <runtime>/
dm.exe <runtime>/world.dme
python -B tests/data/master-screen-loc-world/collect.py <runtime> <out> "1,1|outline" "null|outline" ...
python -B tests/data/master-screen-loc-world/collect.py <runtime> <out> --port=18851 "8,1|ov" "8,1|obj" ...
```

`collect.py` hosts the world with DreamDaemon `-safe -invisible` on TCP port 18811 (or the one given by a leading
`--port=`), starts DreamSeeker, writes each
phase to `phase.txt`, waits for READY and captures the client area with `PrintWindow`. It stops only the two
processes it started. The window must not be minimised.
