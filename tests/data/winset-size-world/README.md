# Runtime winsets of a window's and a control's size (2026-11-26)

What stock DreamSeeker **516.1689** (DreamDaemon 516.1689, display at 144 dpi) does with `winset` of `size` on the
main window and on its controls, and what `winget` reads back. The skin (`world.dmf`) is one 224x224 main window
holding a map anchored `0,0` to `100,100` (as in `master-screen-loc-world`), a 32x32 label `fixed` with no anchors and
a 64x32 label `half` at `0,192` anchored `0,100` to `100,100`.

A click on the map runs `client/steps()` in `world.dm`: each step sends one winset, waits a second, then logs
`winget` of `size` and `pos` of `main`, `map`, `fixed` and `half`, the window's `inner-size` and `outer-size` and the
map's `view-size`. `stock.log` is what stock logged, line for line (one line per step); the window's client
rectangle and child windows were also read with `GetClientRect` / `GetWindowRect` while it ran, and agreed with every
winget answer below.

## What stock does

- `main.size=288x288` (or `winset(src, "main", "size=256x256")`) sets the window's client area, the status strip below
  it not counted: the frame grew from 246x305 to 310x369, the client area from 224x249 (with the 25-pixel strip) to
  288x313. `winget` of `main.size` and `main.inner-size` answer the new size. The controls follow their anchors as
  for a drag of the frame: the map becomes 288x288, `half` 128x32 at 0,256, `fixed` stays 32x32.
- `map.size=160x160` in that 288x288 window makes the map 160x160 there, and its anchors move it from there: back at
  224x224 the window holds a 96x96 map (160 - 64), at 288x288 a 160x160 one again. The same for `half`: set to
  100x20 at 288x288 it is 36x20 at 0,192 in the 224x224 window and 68x20 at 0,224 in the 256x256 one. So a winset
  size is the control's size now, in the box it has now, not a size in the skin's design coordinates.
- `fixed.size=64x64` (no anchors) gives 64x64 and it stays so.
- A zero extent fills to the window's far edge: `map.size=0x0` gave 288x313 and `map.size=100x0` 100x313 in the
  288x288 window, the 25-pixel status strip included.
- `main.size=288x288;map.size=288x288` in one winset (the form `master-screen-loc-world` uses) gives a 288x288
  window and map.
- `map.view-size` stayed 224x224 throughout (`icon-size = 32`, a 7x7 view): the control's size does not change it.
- `main.pos` is the window's place on the screen and `main.outer-size` its frame.

## Fugue

`ShellTest::runtimeSizeWinsets` (shell/tests/shell_test.cpp, offscreen) replays the steps on a `SkinWindow` with this
skin and checks every `size`, `pos` and `inner-size` answer of `stock.log` and the window and widget geometry they
describe. Still different, and not checked: a zero extent fills to the bottom of the client area, not over the status
strip (288x288 rather than 288x313); `main.outer-size` is not answered; `main.pos` is the screen place, which differs
by nature. `stockprobe.py click` of this world (below) showed the same answers in both clients but for those three.

## Reproduction

Requires BYOND and Python/Pillow. `stockprobe.py`'s own `prepare.py` stages this world.

```sh
python -B tools/stockprobe/stockprobe.py --world tests/data/winset-size-world click winset-size 100,100 wait:30
```

prints, step by step, what stock and Fugue logged (`--stock-only` for stock alone). `click` clicks one window and
waits before it clicks the other, so each client's steps run and are read on their own.
