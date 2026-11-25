# Plane-master trees with matrices

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18660. No game assets in Git; `filters/` holds
stock grabs of this synthetic world (see below).
`prepare.py <dir>` writes the world, a 224x224 skin, a floor and two boxes (`box.png` red, `blue.png`, 16x16 at
8..23). A HUD plane master (plane 1, `screen_loc = "1,1"`) carries an overlay image (`blue.png`, pixel offset 64,32,
plane 1); a world object (`box.png`) stands on plane 1 at (5,4). `tools/stockprobe/stockprobe.py pair` runs dd with
`-params set=<case>` (the case name picks the matrices, see `set_master()`), stock and Fugue on one server and grabs
both at 1:1; `compare.py <dir> <case>...` keeps the stock bounding boxes of the red and blue boxes and Fugue's
difference, in `stock.json`. `entitymap_test::masterTreeTransformsLikeStock` consumes it.

## Independent conclusions

- **A matrix on the master** turns its whole plane about the view centre, the world object and the overlay alike
  (`master-turn`, `master-scale`, `master-shift`), as for a master without overlays (`solo-master-turn`).
- **The overlay also takes the master's matrix about its own centre**, before the plane turns: with `master-scale` the
  16 px overlay is 64 px wide (2x as an overlay, 2x again with the plane) while the world object is 32. Under
  Turn(90) this hides on a square (two quarter turns on the box itself); `master-turn-child-scale` shows it.
- **A matrix on the overlay** applies first, about its own centre (`child-turn`, `master-scale-child-turn`).
- **KEEP_TOGETHER** on the master hides the overlay with or without a matrix on it (`group-plain`, `group-child-turn`),
  and with a matrix on the master nothing on the plane is drawn (`group-master-turn`).

## Fugue after the change

Master-tree admission accepts a matrix on the root master (without KEEP_TOGETHER) and on leaves; leaves inherit the
root master's matrix about their own pivot, and the plane keeps its master matrix (the existing relay path). Every case
matches stock within 1/255. KEEP_TOGETHER with a master matrix stays refused, which draws nothing, as stock does.

## A master's matrix with filters (`fx-*` cases)

Stock **516.1689**, 2026-11-25. The master has no overlay; a red 16 px box stands at (5,4) and, for the `edge` cases, a
whole blue tile at (7,4), on the view's right edge. `grab.py <workdir> <port> <case>...` grabs stock alone and keeps
the 224 px map as `filters/<case>.png`. `compositiongpu_test::masterTransformFilters` rebuilds each scene from sprites
and compares every pixel with the grab (at most 8 levels per channel).

- **The filters run first, on the untransformed plane, and the matrix turns their result.** Under `scalex2` an
  outline of 1 is 2 px wide left and right and 1 px above and below (`fx-scalex2-outline1`), and a blur of 2 keeps
  its vertical profile and doubles its horizontal one (`fx-scalex2-blur2`). Under `turn90` a shadow offset 6 px to
  the right lands below the box (`fx-turn90-shadow6`).
- **The pivot is the view's centre**, however the filters grew the surface: the box under `fx-turn90-shadow6` is
  where `fx-turn90` puts it, although the shadow grows one side only.
- **What a filter draws past the view is kept until the matrix has run.** With an outline of 4 round the edge tile,
  the outline's right part lies outside the view and is not seen (`fx-edge-outline4`); scaled by a half it is drawn,
  2 px wide, right of the halved tile (`fx-half-edge-outline4`).
- **A scaled plane is sampled bilinearly**: the box under `fx-scalex2` has half-covered columns at 159 and 192.

Fugue now does all four for blur, outline and drop shadow on a master without a tree. Other filter kinds with a
matrix are still left out (the plane is turned, unfiltered), as are filters on a master tree with a matrix. A plane
with both is not clickable.

## Not covered

Clicks on a transformed master tree (the plane's inverse matrix is used for input as before, not checked here). Nested
masters with a matrix (still refused), animated master matrices, and non-32 px worlds. For filters with a matrix:
wave, ripple, bloom, alpha mask and colour filters, turns that are not quarter turns, a master with a render target,
PIXEL_SCALE on the master, and clicks.
