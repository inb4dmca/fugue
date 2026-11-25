# A plane drawn through a relay

Independent DreamSeeker/DreamDaemon **516.1689**, 2026-11-25, display at 144 dpi. No game assets in Git.
`prepare.py <dir>` writes the world, a 224x224 skin (a 7x7 view of 32 px tiles) and solid icons: a floor and 16 px boxes
at 8..23 (`box.png` red, `blue.png`, `green.png`).

Plane 1's master (`screen_loc = "1,1"`) has `render_target = "*p1"`. On plane 1 stand `part` (red) at (5,4), `other`
(blue) at (2,5) and a HUD object `button` (green, `screen_loc = "6,2"`). A screen object `relay` on plane 5 draws the
plane with `render_source = "*p1"` and `PASS_MOUSE | KEEP_TOGETHER | NO_CLIENT_COLOR`, as SS13 codebases relay their
planes. The case name's words (see `set_case()`): `norelay` (no relay), `nopass` (no PASS_MOUSE), `visible` (the target
is `p1`, so the plane draws itself too), `loc23`, `locpx`, `loccenter` (the relay at `2,3`, `2:5,3:-7`,
`CENTER-2,CENTER-1`), `rturn90`, `rt_45` (the relay turned), `rhalf`, `rscalex2` (scaled), `rflip` (`matrix(-1,0,0,0,1,0)`),
`rmove` (`matrix(1,0,10,0,1,5)`), `rgrowmove` (`matrix(1.5,0,100,0,1.5,0)`), `rturnmove` (turned by 30 degrees, then
moved 20 right and 10 up), `malpha` (the master's alpha 254, so its picture is processed), `mturn90` (the master
turned). `rm_a_b_c_d_e_f` puts any matrix on the relay; it was used to probe and is in no retained case.

Every atom logs `Click()` with its name and icon-x/y, and has one verb, `poke_<name>`, so that an entry of the
right-click menu is known by its verb.

## How the data was taken

`tools/stockprobe/stockprobe.py --world tests/data/plane-relay-world --map 224x224 --still 0.5 --jobs 3 record <case>...`
wrote `replay/<case>.txt` (what DreamDaemon sent the client), `replay/res/` and `replay/<case>.png` (stock's map).
`stockprobe.py --world tests/data/plane-relay-world --map 224x224 --params set={} --still 0.5 --live --stock-only click
@<script>`, one `<case> ACTION...` line per case: a left click `X,Y` and a right click `right:X,Y menu:` where the relay
draws each box, where the relay's matrix about the view's centre would put it (when that is elsewhere), where the box
stands, and on empty floor. `clicks.py <output> 516.1689 2026-11-25` kept stock's answers in `clicks.json`.
`mapinput_test::planeRelaysLikeStock` replays each case without a server and asks the real hit test and menu stack for
every point; `compositiongpu_test::planeRelayReplay` draws each case and compares it with stock's grab.

Matrices on the relay were also probed with `stockprobe.py ... --stock-only pair` over many more matrices (turns of
10, 15, 30, 45, 60, 120, 135, 270 and -45 degrees, scales of 0.5, 0.75, 1.5 and 2 on either axis, shears, moves and
their combinations): the rule below put every box within a pixel of where stock drew it. Those grabs are not retained.

## Independent conclusions

- **A relay draws the plane's picture moved as far as the relay stands from 1,1**, pixel offsets included (`loc23`,
  `locpx`), CENTER, EAST and NORTH counted in tiles as for any screen object (`loccenter`).
- **A relay with a matrix draws the picture through it**, about the view's centre, but stock moves a neutral plane's
  picture first by u per axis (Y down): half of what the view taken back through the matrix's linear part outgrows the
  view by, plus the matrix's own move taken back the same way, rounded up to a whole pixel and never negative. So a half
  scale lands in the view's bottom right quarter (`rhalf`), a turn of 45 degrees lies 66 px lower than one about the
  centre (`rt_45`), a quarter turn, a flip or an enlargement is about the centre (`rturn90`, `rflip`, `rscalex2`), and
  a positive move counts twice where a negative one counts once (`rmove`). A processed plane's picture is not moved
  (`malpha-*`).
- **Clicks go through a PASS_MOUSE relay to the plane's atoms**, back by the relay's offset and through the relay's
  matrix about the view's centre, never with the move above: under `rhalf` and `rt_45` the boxes are clicked where an
  unmoved picture would show them, and where they are drawn is floor. The pointer then goes back through the master's
  matrix (`mturn90`, `mturn90-loc23`). The pixel taken is the one whose corner the inverse lands in (`rturn90` at
  114,139 names `part` y=18).
- **A relay without PASS_MOUSE is clicked itself where the plane has ink** (empty floor stays floor), with the
  picture's own icon-x/y (`nopass-loc23` at 171,45: x=140 y=115, as `nopass` at 139,109); not through the master's
  matrix (`nopass-mturn90` at 114,139: x=115 y=85).
- **Where a relay draws the plane, its atoms take no clicks where they stand** (`loc23` at 139,109 is floor). **With no
  relay a `*` plane is not drawn but stays clickable** (`norelay`).
- **The right-click menu lists the plane's atoms where they stand** (through the master's matrix: `mturn90-loc23` at
  100,150, not at 139,109), relayed or not, and **also through a PASS_MOUSE relay** where it draws them (`loc23` at
  171,45). A relay without PASS_MOUSE is listed itself over the whole rectangle of the picture it draws (`nopass` at
  100,150) and lists nothing through it.
- A relay of a visible target draws the plane once more (`visible-loc23`).

## Fugue

Before: a relay of a plane was drawn only at `1,1` (or `SOUTHWEST`) and only with no matrix or a positive scale and
whole-pixel move; elsewhere or turned it was dropped, and the plane's atoms were then clickable where they stood. Clicks
did not pass through a relay over a master with a matrix, nor through a moved or turned one, and the menu listed no
relayed atom and no relay. After: every grab matches stock (all 23 cases, on Direct3D 11, OpenGL and Vulkan), and every
click and menu in `clicks.json` matches stock (155 clicks, 155 menus). The quarter-turn rows of
`relay-gate-world/matrix.json`, recorded earlier and left out until now, also pass.

## Not covered

A relay with an edge (`LEFT`, `TOP`, ...), percent or range screen_loc or a pixel_x/y of its own (left out), a relay
with filters together with a moved place, chains of relays with moves or matrices, a relay without PASS_MOUSE that is
turned (its menu rectangle is taken as the picture taken back through the matrix about the centre), hover and drag.
