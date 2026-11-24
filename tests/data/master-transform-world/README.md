# Plane-master trees with matrices

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18660. No game assets or screenshots in Git.
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

## Not covered

Clicks on a transformed master tree (the plane's inverse matrix is used for input as before, not checked here). Nested
masters with a matrix (still refused), filters together with a master matrix (filters are dropped on a turned master
as before), animated master matrices, and non-32 px worlds.
