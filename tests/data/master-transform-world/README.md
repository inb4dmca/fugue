# Plane-master trees with matrices

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18660. No game assets in Git; `filters/` and
`replay/` hold stock grabs of this synthetic world (see below).
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
the 224 px map as `filters/<case>.png`. `stockprobe.py --stock-only --map 224x224 --params set={} pair <case>...`
writes the same pixels as `<case>-stock.png`, and with `--still 0.5 --live --jobs 4` all thirty in about twelve
seconds (every one compared with the grab kept here); `grab.py` is kept for the record of how these were taken.
`compositiongpu_test::masterTransformFilters` rebuilds each scene from sprites
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
- **Unless the master has PIXEL_SCALE** (`pxs` in the case name), which samples it nearest-neighbour: the box under
  `fx-pxs-scalex2` is 160..191 with hard edges, and with an outline of 1 (`fx-pxs-scalex2-outline1`) 158..193.

The other filter kinds behave the same under `scalex2` and `turn90`: wave (`fx-*wave`), ripple, bloom, a colour
matrix (`swap`, red and green exchanged) and an alpha mask from an icon (`mask`, the left half of a tile, moved 32 px
right). So does a master tree, the first set's master with its overlay: `master-turn-outline1`,
`master-scale-outline1`, `master-scale-blur2` and `master-turn-shadow6` were grabbed from stock and Fugue together
(`stockprobe.py pair`) and match within 8 levels, as do the fifteen `fx-*` grabs of the other kinds, which are kept in
`filters/` as well.

Fugue now does all of this for every filter kind, with or without a tree.

## Which pixel a matrix takes (`fx-grid-*` cases)

Stock **516.1689**, 2026-11-25. In place of the box, one 224 px icon (`grid.png`, written by `prepare.py`) covers the
whole view on plane 1: a pixel's red is its column, its green its row, and its blue a one-pixel checker (255 where
column + row is even). Every pixel of a grab so names the plane pixel it came from, and a grey checker shows bilinear
sampling. The case's last part names the master's matrix (`shape()` in `world.dm`): `s75`, `s60`, `s50`, `s40`, `s25`
scale both ways by 0.75 .. 0.25, `sx50`, `sy50` and `sx50y75` one way or unevenly, `s50t90` and `s50t30` halve and
then turn 90 or 30 degrees, `s50m` and `s50m3` halve and move 16,8 and 3,-5 px, `s150` and `s200` enlarge, `plain`
is the identity; each is grabbed with PIXEL_SCALE on the master (`fx-grid-pxs-<shape>`) and without. All thirty were
taken in one run:
`stockprobe.py --world tests/data/master-transform-world --stock-only --map 224x224 --params set={} --still 0.5 --live
--jobs 4 pair fx-grid-<shape> fx-grid-pxs-<shape> ...`, the `<case>-stock.png` files renamed to `filters/<case>.png`.

- **A shrinking PIXEL_SCALE plane is sampled nearest-neighbour too, about the same pivot**: nothing is displaced. The
  plane lies where the matrix puts it about the view's centre (`fx-grid-pxs-s50`: 56..167 both ways, like
  `fx-grid-s50`; `s25`: 84..139; `s75`: 28..195; `s50m`: 72..183 by 48..159), every pixel is a whole plane pixel
  (the checker stays 0 or 255), and the pixel taken is the one the pixel's centre falls in.
- **On a texel boundary it takes the texel before it**, on each axis, left and up on the screen. Halving puts every
  pixel centre on a boundary: `fx-grid-pxs-s50` keeps columns and rows 0, 2, 4 .. 222 (pixel 56 takes 0, not 1);
  `s25` keeps 1, 5, 9 .. 221; `s75` takes 0, 1, 3, 4, 5, 7 ..; and enlarging by 1.5 has its own ties (`s150`, pixel 2
  takes 38). A pixel whose centre is not on a boundary takes the pixel it falls in (`s60`, `s40`, `s200`, `s50t30`).
  Every pixel of the twelve axis-aligned PIXEL_SCALE grabs and of `s50t30` fits this rule exactly.
- **A pixel whose centre lies outside the plane is not drawn**: no half-pixel border (`s60`: column 44 maps to -0.5
  and shows the floor).
- **Under a quarter turn the boundary goes the other way one time in seven** (`fx-grid-pxs-s50t90`: 1792 of its
  12544 pixels, sixteen whole columns, take the texel after the boundary). The columns are evenly spaced (every
  seventh pixel), which reads as the rounding of stock's own interpolation, not as a rule of the matrix.
- **Clicks follow the pointer's corner, not the drawing**, and are unchanged by this: `fx-grid-pxs-s25` at 85,85 reads
  x=5 y=220 although pixel 85 draws plane pixel 5,5 (icon x=6); `fx-grid-pxs-s50t90` at 56,56 hits the floor although
  that pixel draws the grid. Fugue already answered every one of 29 such clicks (`fx-grid-pxs-s50`, `-s75`, `-s25`,
  `-s50t90`, `fx-grid-s50`, at the plane's edges and inside) as stock did; they are not kept in `clicks.json`.
- **An enlarged bilinear plane is read as though it ended at the part of it the view shows**: the view's rectangle
  taken back through the matrix, its bounding box rounded outward to whole plane pixels, past which the edge texels
  repeat. Under `s200` that part is plane pixels 56..167, so pixel 0,0 is plane pixel 56,56 unmixed, where the bilinear
  weights would give a quarter of its neighbours. Every pixel of the grabs below fits this rule within 2 levels; with
  no limit, or with the part rounded to the nearest pixel or kept fractional, they do not:

  | case | matrix | shown part | what it decides |
  |---|---|---|---|
  | `fx-grid-s200` | 2 | 56..167 | the limit exists (892 border pixels otherwise) |
  | `fx-grid-s200t90` | 2, quarter turn | 56..167 | the same under a quarter turn (892) |
  | `fx-grid-sx200y300` | 2 by 3 | 56..167 by 74.67..149.33 | each axis on its own: only the x border is limited |
  | `fx-grid-s150` | 1.5 | 37.33..186.67 | outward, not fractional (596 off if kept fractional) |
  | `fx-grid-s300` | 3 | 74.67..149.33 | outward, not to nearest (1776 off if 75..148) |
  | `fx-grid-s200m3` | 2, moved 3,-5 | 54.5..166.5 by 53.5..165.5 | outward with a translation (892 off if to nearest) |
  | `fx-grid-s200t30` | 2, turned 30 degrees | 35.5..188.5 (box) | the box, not the turned square (4 off if fractional, 12 if to nearest) |

  Under `s150`, `s300`, `s200m3` and `s200t30` the outward box lies far enough out that no pixel's samples reach it:
  these grabs tell the rounding apart but are the same with no limit at all. In the turned case the shown part is a
  turned square, and only its bounding box limits anything; whether a tighter, turned limit exists is not shown by a
  grab (no pixel samples outside the square but inside its box).
- **Not with a filter**: `fx-grid-outline1-s200` (the same plane with an outline of 1, which lies wholly outside the
  view) blends the view's border with the plane past it, exactly as `s200` would with no limit (the 892 pixels).
- **A shrunk plane fades at its own edge**: where the shown part lies past the plane (every shrinking case) the plane's
  edge is mixed with what is behind over one texel, not repeated (`fx-grid-s50t30`: a pixel whose centre is 0.2 texel
  outside the plane's left edge is mostly floor).

The six new grabs, and `fx-grid-s200` again, were taken in one run, as the others:
`stockprobe.py --world tests/data/master-transform-world --stock-only --map 224x224 --params set={} --still 0.5 --live
--jobs 4 pair fx-grid-s300 fx-grid-sx200y300 fx-grid-s200m3 fx-grid-s200t90 fx-grid-s200t30 fx-grid-outline1-s200
fx-grid-s200` (516.1689, 2026-11-26; the new `fx-grid-s200` came out identical to the kept one).

`compositiongpu_test::masterTransformFilters` rebuilds each grid scene from one sprite and compares every pixel with
the grab (8 levels); for `fx-grid-pxs-s50t90` a pixel on a texel boundary may take either of the two texels.

Fugue before this blended an enlarged plane's border with the plane past the view (`fx-grid-s200`, `-s200t90`: 892
pixels; `-sx200y300`: 448). The planner now gives an unfiltered master's matrix the shown part, rounded outward
(`StaticAffine::sampled`), and the renderer's bilinear read of the turned surface clamps to it (`sprite.frag`,
operation 8).

Fugue before this left the choice on a boundary to its GPU's sampler, which took the texel after it in some columns
and the one before it in others (`fx-grid-pxs-s50`: 1260 pixels off, every 2nd to 3rd column), and drew half a texel
past the plane's edge (`s60`); the renderer now picks the texel itself (`sprite.frag`, texel.w) by stock's rule.

## Animated filters and matrices on the master (`ax-*` cases)

Stock **516.1689**, 2026-11-25. The `fx-*` master (no overlay; `tree` in the name keeps the first set's overlay) is
animated by `animate_master()` in `world.dm`: the filter's parameters (`animate(M.filters[1], size = 4)`: `size4`; a
shadow's `x = -6`: `left`; a wave's `offset = 0.5`: `phase`), the master's matrix (`animate(M, transform = ...)`: to a
2x horizontal scale, `grow2x`, or a quarter turn, `spin90`) or both (the filter step `ANIMATION_PARALLEL`). So that the
grabbed frame is well defined, each first step takes half a second and is followed by a second step to the same values
for ten minutes: a grab taken seconds after login sees the end of the first step while the animation is still running.
`replay/` holds, for the ten `ax-*` cases without `mid`, stock's 224 px map grabbed six seconds after login and what
DreamDaemon sent this client (`stockprobe.py --params set={} --map 224x224 --jobs 4 record <case>...`, which writes
`replay/<case>.png`, `replay/<case>.txt`, `replay/res/` and `replay/recorded.tsv`, as for stock-parity-world).

- **Stock keeps drawing the master's filters and its matrix while they animate**, at their animated values: every held
  `ax-*` grab is pixel-identical to the settled master with the end values where one was grabbed
  (`ax-scalex2-outline1-size4` = `fx-scalex2-outline4`, `ax-outline1-spin90` = `fx-turn90-outline1`,
  `ax-outline1-grow2x` = `fx-scalex2-outline1`, `ax-shadow6-spin90` = `fx-turn90-shadow6`), and the tree's overlay
  takes the grown outline and the matrix too (`ax-tree-scalex2-outline1-size4`).
- **Mid-flight** (`mid` in the name: a one-minute first step and no hold, grabbed about six seconds in, with
  `stockprobe.py pair` and no `--still`) stock draws the outline round a plane turned part of the way
  (`ax-mid-outline1-spin90`), a grown outline under the scale (`ax-mid-scalex2-outline1-size4`) and the shadow
  moving across under the turn (`ax-mid-turn90-shadow6-left`). These are not kept: the frame depends on when it is
  grabbed. Fugue's grabs from the same server agreed with stock's within that timing (0, 63 and 16 pixels over 8
  levels, at the moving edges).
- `animate(M, filters = ...)` changes nothing that is drawn: `ax-outline1-toshadow` keeps the outline, with no shadow,
  in stock and in Fugue (not kept).

Fugue drew none of this before: an animating master with a filter or a matrix was refused as a whole, so its plane was
drawn plain, with no filter, no matrix and no overlay (`stockprobe.py pair` of four cases: 580 to 3876 pixels off).
Now the sampled master carries its filters at their tweened parameters (the atom filter tween: numbers and colours move
with the eased factor when both ends have the same filter list, any other change switches at the step's midpoint) and
its tweened matrix in its plane effects. Every `ax-*` case matches stock within 1 level in `stockprobe.py pair`.
`compositiongpu_test::animatedMasterReplay` replays each `replay/` case through the real scene assembly and compares
every pixel with stock's grab (at most 8 levels per channel); `entitymap_test::animatedMasterKeepsFiltersAndMatrix`
holds the tweened values mid-flight, in one step and in a parallel one.

## Clicks (`clicks.json`)

Every atom logs `Click()` with its name and icon-x/y; `stockprobe.py click` clicks the same MAP pixels in stock and in
Fugue. `clicks.json` keeps what stock answered. Many cases go in one run as a script, one `<case> X,Y X,Y ...` line
each: `stockprobe.py --map 224x224 --params set={} click @<script>`.

- **A filter adds nothing to click and takes nothing away.** The box is clicked where it stood before the filter,
  under an outline, blur, drop shadow, wave, ripple or bloom; the outline, the blur's spread, the shadow, the glow
  and wave-displaced ink fall through to the floor. An alpha mask does hide what it masks out (`fx-mask`).
- **A matrix maps the pointer back.** The box is clicked where the turned plane draws it, with its own icon
  coordinates (`fx-scalex2` at 165,111 reads x=11; `fx-turn90` at 106,138 reads x=11 y=10), and no longer where it
  stood. With filters as well it is the same (`fx-scalex2-outline1`, `fx-turn90-shadow6`, `fx-turn90-wave`).
- **A master tree's overlay is clicked as the master**, where it is drawn, through the plane's matrix and then its own
  (`master-scale` at 20,170 reads x=10 y=22; `master-turn` at 41,73 reads x=24 y=9), with or without an outline.

- **The pointer is not rounded to a pixel between the two matrices.** Where the plane's inverse leaves a fraction (a
  scale of 2, 3 or 1.5: `master-scale`, `master-triple`, `master-grow`) and the overlay has a matrix that is more than
  that same scale (`child-turn`, 45 degrees; `child-lean`, 30 degrees; or the 1.5 it takes from the master), the
  fraction decides the icon cell and where the box's edge is: `master-scale-child-turn` at 47,175 reads x=16 where
  rounding down first gives 15, and at 19,172 the box is still hit (x=9). 136 more clicks across six such cases, on the
  box, on its edge and just outside, were taken to tell the two rules apart; every one fits the unrounded rule, with a
  point that lands exactly on a cell boundary going to the next cell (`master-grow` at 82,200 is the floor).

Fugue matches every click in `clicks.json`. `composition_test::outlinedTreeInputBoundaries`,
`entitymap_test::masterTreeTransformsLikeStock` and `entitymap_test::masterTreeClicksLikeStock` (the overlay clicks of
the `master-*` cases without an outline, through the real hit test) hold the rules.

## Not covered

Nested
masters with a matrix (still refused), and non-32 px worlds. For filters with a matrix:
turns that are not quarter turns, and a master with a render target (whether the view-edge limit of an enlarged plane
applies to its target or to a relay with a matrix was not grabbed; Fugue does not limit either). For clicks: mouse-opacity 2. Right-click menus, images and vis_contents members on a turned or
filtered plane, and render-target masks are in `tests/data/plane-click-world`. For animation: no mid-flight frame is
kept or compared pixel for pixel (only the holds are), clicks on an animating master were not taken, and a
KEEP_TOGETHER master with an animating matrix is still refused.
