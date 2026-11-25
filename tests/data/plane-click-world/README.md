# Clicks under filtered and turned planes

Independent DreamSeeker/DreamDaemon **516.1689**, 2026-11-25, port 18840. No game assets in Git.
`prepare.py <dir>` writes the world, a 224x224 skin (a 7x7 view of 32 px tiles) and solid icons: a floor, 16 px boxes at
8..23 (`box.png` red, `blue.png`, `green.png`) and `mask.png` (the left half of a tile opaque).

A HUD plane master (plane 1, `screen_loc = "1,1"`) gets a matrix, filters or both from the case name (see
`set_master()`). On plane 1 stand a red box `part` at (5,4); a client image (`green.png`, plane 1) on the iconless obj
`holder` at (3,4); and a vis_contents member `child` (`blue.png`, plane 1) of the iconless obj `visholder` at (4,6). On
plane 0, `leaner` at (2,6) is the red box turned by 45 degrees. Case words: `turn90` (Turn(90)), `scalex2`
(matrix(2,0,0,0,1,0)), `outline1` (outline of 1), `tree45` (an overlay on the master, `blue.png` at pixel 64,32 turned by
45 degrees, as master-transform-world's `child-turn`), and alpha masks moved 32 px right from an icon (`iconalpha`), from
an atom's render target (`rtalpha`: `maskatom`, render_target `*maskrt`, whose icon is `mask.png`) or, unmoved, from a
plane (`ptalpha`: plane 2's master has render_target `*maskplane`, and `mask.png` stands on plane 2 at (5,4));
`inverse` adds MASK_INVERSE.

Every atom logs `Click()` with its name and icon-x/y, and has one verb, `poke_<name>`, so that an entry of the right-click
menu is known by its verb (stock draws the entries' names itself, so they do not read back).

## How the data was taken

`tools/stockprobe/stockprobe.py --world tests/data/plane-click-world --map 224x224 --params set={} --still 0.5 --live
click @<script>`, one `<case> ACTION...` line per case: left clicks `X,Y` and right clicks `right:X,Y menu:` at MAP
pixels, in stock and in Fugue on one server. `clicks.py <output> 516.1689 2026-11-25` keeps stock's answers in
`clicks.json`: per click the atom and its icon-x/y, per right click the menu's entries, topmost first.
`stockprobe.py --world tests/data/plane-click-world --map 224x224 --still 0.5 --jobs 3 record <case>...` wrote
`replay/<case>.txt` (what DreamDaemon sent the client for the case), `replay/res/` and `replay/<case>.png` (stock's map).
`mapinput_test::planeClicksLikeStock` replays each case without a server and asks the real hit test and menu stack
for every point in `clicks.json`.

## Independent conclusions

- **A client image and a vis_contents member on a filtered or turned plane are clicked like the plane's own atoms**:
  by their ink where they stood (`outline1`: the outline round them falls through), and under a matrix where the plane
  draws them, with their own icon coordinates (`turn90` at 120,79 reads `holder` x=16 y=24; `scalex2` at 96,47 reads
  `child` x=9). The image's click names the atom it is on. The menu lists them there too.
- **A mask from a render target hides from clicks what it masks out**, as a mask from an icon does: an atom's target
  centred on the plane and moved by x, y (`rtalpha`: 139,111 is `part`, 147,111 the floor, the image and the member
  are hidden), a plane's target where it is drawn (`ptalpha`), MASK_INVERSE hides what it keeps (`*-inverse`), and
  under a matrix the mask applies before the matrix (`turn90-rtalpha`, `scalex2-ptalpha`). The plane that is only the
  mask's source takes no clicks itself (`ptalpha` at 139,111: stock names `part`, not what stands on plane 2).
- **The right-click menu takes the pointer back through a plane's matrix as a left click does**: the pixel whose corner
  the inverse lands in, not the pixel whose centre does. Under `turn90` the part's tile is listed at x=128 and not at
  x=96 (columns 97..128), with rows 128..159 either way.
- **The menu lists a part that carries a matrix of its own where its turned icon is, not over its bounds**: `tree45`
  at 66,162, a corner of the turned overlay's bounds, lists only the floor; 70,166 lists the master. So does `leaner`
  on plane 0 (34,34 and 38,38), and the overlay under a plane scale (`tree45-scalex2`, nine points along an edge).
- **The menu ignores alpha masks**: `iconalpha` at 147,111 and `turn90-rtalpha` at 111,148, both masked out of left
  clicks, list `part`. The menu is otherwise rectangles: a transparent part of an icon is listed.
- An iconless holder of a vis_contents member is listed at its own tile with the member (`plain` at 111,47).

## Fugue

Before: the image and the member on a filtered or turned plane were not clicked at all (the floor was), a render-target
mask hid nothing and the plane that was its source took clicks (`ptalpha` at 139,111 named `maskplanepart`), the menu
took the pixel whose centre the inverse landed in (it listed the part at 96,144 and not at 128,144), listed a turned
part over its whole bounds and left out what an icon mask hid. After: every click and every menu in `clicks.json`
matches stock (77 clicks, 50 menus).

## Not covered

An image or a member with a matrix of its own on a turned plane, an image overlay on a vis member, a mask whose source
has filters or a matrix of its own (the mask's alpha is taken from its parts' own pixels), render-target masks on an
atom's own filters, a plane render target that is hidden (`*`) but consumed by nothing, and hover or drag on these.
