# Pointer hover: MouseEntered, MouseExited and MouseMove

Independent DreamSeeker/DreamDaemon **516.1689**, 2026-11-26, Windows 11 with the display at 144 dpi; stock drew the
map one screen pixel per icon pixel, so map pixels below are window pixels. No game assets in Git.
`prepare.py <dir> [CLIENT_ENTER] [CLIENT_MOVE]` writes the world, a 288x224 skin (a 7x7 view of 32 px tiles between
32 px of letterbox left and right) and solid full-tile icons. `CLIENT_ENTER` adds client/MouseEntered and
client/MouseExited, `CLIENT_MOVE` client/MouseMove; each calls `..()`. `world.params` `lag=<ds>` sets `world.tick_lag`.

Every mouse proc logs `world.time`, the proc, the atom, its location, control and params; presses, releases, clicks and
drops are logged by the client procs, so that no atom gets flags from them. The map shows the whole 7x7 world: tile
x,y is at map pixels 32+32(x-1) .. and 32(7-y) .. (the letterbox is 0..31 and 256..287).

| atom | where | procs |
|---|---|---|
| `enter`, `enter2` | 1,7 and 5,7 | MouseEntered, MouseExited |
| `move` | 2,7 | MouseMove |
| `both` | 3,7 | all three |
| `none` | 4,7 | none |
| `big` | 1,4, a 64x64 icon over four tiles | all three |
| `slider` | 5,4, pixel_x stepped 0..6 by 2 every half second on the server | all three |
| `glider` | 6,5, pixel_x animated 0..6 and back, looped | all three |
| `hopper` | stepping between 5,2 and 5,3 every two seconds (and gliding) | all three |
| `exitonly`, `dragonly`, `downonly`, `wheelonly`, `moveexit`, `clickonly` | row 1 | MouseExited; MouseDrag; MouseDown; MouseWheel; MouseMove and MouseExited; Click |
| `enteronly` | 6,2 | MouseEntered |

## How the data was taken

`tools/stockprobe/hoverprobe.py --world tests/data/hover-world [--switches ..] [--params lag=..] --out <file> <script>`
moves the real pointer over stock's map (the window kept on top), presses and releases real buttons and posts moves to
the window; per action it keeps what the world logged. `hover.py hover.json 2026-11-26 <run>=<output>[@<replay>]`
keeps the runs in `hover.json`: `base` and `base2` (the plain world), `lag10` (tick_lag 10), `center`, `cmove` and
`cboth` (the client procs), and `pace`, `pace10` and `pace025` (one-pixel sweeps at tick_lag 0.5, 10 and 0.25).
`pace/p33` was left out: the user's own pointer moved during it. `stockprobe.py --world tests/data/hover-world --map
288x224 --settle 4 record base` (with `HOVER_SWITCHES=CLIENT_ENTER` and so on for `center`, `cmove`, `cboth`) wrote
`replay/<name>.txt`, what DreamDaemon sent Fugue for each compile, and `replay/<name>.png`, stock's map.

`mapinput_test::hoverLikeStock` replays the runs in order through the real hit test, `MapHover` and a model of the
server (below) and compares the procs with stock's, action by action (216 actions, 162 procs); cases with posted moves,
sweeps or a moving scene are left out of it. `hoverPaceLikeStock` replays the sweeps; `hoverTransitionsAndStaleIdentities`
and `releaseForgetsHover` hold the rules below on hand-made picks.

## What stock does

Each point says whether it confirms or contradicts the static reading of the client this measurement was planned from.

- **Appearance flags on the wire** (from `replay/base.txt`): MouseEntered or MouseExited give 0x800; MouseMove gives
  0x180800; MouseWheel alone 0x100800; MouseDrag, MouseDown and Click nothing. So 0x80000 is MouseMove's own bit, and an
  atom with only MouseMove has 0x800 too. Client hover procs: client/MouseEntered sets MouseSubs bit 0x02000000,
  client/MouseMove sets that bit and extra bit 0x01.
- **The record and its two reports** (confirmed): one record of what the pointer is over. A pick naming another atom or
  control (or map) is an over report, sent when a client hover proc exists, or the new atom has 0x800, or the atom the
  last report sent named has 0x800. A pick that only moves the pixel inside the screen tile, over an atom, is a move
  report, sent when client/MouseMove exists or the atom has 0x80000; client/MouseEntered alone reports no moves
  (`center` `leave` at 145,16). Every one-pixel step over `both` gives MouseMove (`base` `steps`).
- **No report for icon-x/y or tile changes alone** (confirmed): a whole-tile jump inside `big` at the same pixel of the
  tile (41,102 to 41,70 and 73,70) reports nothing; `slider` and `glider` moving under a still pointer give no MouseMove
  (`still`, `glidestill`).
- **The server's side**: it runs MouseExited (with the params it was entered with, not those of later moves) and
  MouseEntered when a report names another atom than the last one it got, and MouseMove when the same. Entering
  `move` therefore runs nothing (it has 0x800, so the report goes out); its first pixel step runs MouseMove.
- **A still pointer is picked again as the map changes** (contradicts "no per-frame re-pick"): `hopper` gliding onto
  the pointer is entered at icon-y 4 or 32, the first glide frame that covers the pointer, not when the server moved it;
  stepping away it is exited at once and, with client/MouseEntered, the floor entered. This stops after a release until
  the pointer moves again: no report at a click's own point while `slider` and `glider` move (`click`, `clickleave`).
- **Leaving** (confirmed): to the letterbox, `floor` or outside the window, an over report naming nothing (or the floor)
  goes out when the last report's atom had 0x800 or a client hover proc exists; MouseExited gets the params it entered
  with. Over the letterbox no client/MouseEntered runs (no object). Going from `enter` to `move` or `none` names the new
  atom; the old one is exited either way.
- **Releases** (confirmed for clicks, contradicts the old Fugue rule for drops): any release, click or drop, clears the
  record. After a click the next step on the same atom is an over report, which the server, getting the same atom,
  turns into MouseMove; leaving the window before any move sends nothing (`clickleave`: no MouseExited; back on the same
  pixel, MouseMove). A drop makes the server enter the atom dropped on, with the drop's params and without exiting the
  last one; leaving right after it sends nothing (`dropleave`), after a move it exits with the drop's params
  (`dropmoveleave`).
- **Button held** (confirmed): no hover at all while a left or right button is down (`held`, `rheld`); the drag path
  takes the moves.
- **Posted moves** (contradicts the plane-click-world note): a WM_MOUSEMOVE posted to the window is taken at its own
  point (`posted`, `parkpost`, `parkpost2`). With the real pointer outside the window the leave follows at once
  (MouseEntered then MouseExited); with it parked over the map, the next map change picks the real pointer again
  (MouseMove back to its pixel, or MouseExited if it is over something else).
- **Pacing** (contradicts "one server tick"): at most about one report per 1/64 s of pointer motion, at tick_lag 0.25,
  0.5 and 10 alike; a later pick replaces a waiting one (20 one-pixel steps 4, 8, 12, 14 and 16 ms apart reach the
  world as 5-7, 10-11, 14-15, 17-19 and 19 MouseMoves, 18 ms and more apart all 20; two steps a millisecond apart, one).
  Sweeping from `none` onto `both` in 10 ms steps, `both` is entered once, with the last step's params (`base2`
  `enterquick`). Whether stock holds the report or Windows merges the pointer moves stock reads was not settled, nor
  whether a replacing report goes out where its own gate would not let it (Fugue sends it, as the static reading has
  it).

## Fugue

Before: the pick was redone after every frame and any change of the whole report (icon-x/y, the tile, modifiers, the
plane path) sent MouseMove, so `glider` under a still pointer got a MouseMove every frame; the move bit tested was
0x100000 (so `wheelonly` was sent moves); a step to an unflagged atom sent the null leave instead of naming it; the
leave went out whenever anything was recorded; a drop adopted its destination and a click kept the record, so leaving
after either sent MouseExited; reports were not paced. After: `MapHover` (`shell/mapinput.h`) keeps stock's record and
gates, clears it at every release, paces reports to 1/64 s with replacement; `MapWidget` re-picks a still pointer only
after the pointer has moved since the last release, and names nothing (with control and screen-loc) over an unrendered
pixel instead of leaving. The replay matches stock in every compared action; the sweeps within one MouseMove.

## Not measured

A secondary map and info grids (the record is said to be shared with them), modifiers during hover, what stock sends
over a pixel inside the view where nothing is drawn (taken to be the letterbox's report), whether a press writes the
record, the server's handling of a drop over nothing beyond "no MouseEntered", `right=1` in a hover report, and the
pacing on a machine whose timer runs faster than 1/64 s.
