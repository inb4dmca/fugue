# Fugue icon sources

The icon is a FreeSerif Bold "F" with a small "u" set in the pocket under its middle arm, bone (#e8e2d4) on black
(#0a0a0a), in a dark red (#8a1c1c) frame. The glyph outlines are embedded in `../io.github.toomykins.Fugue.svg`, so no font
is needed at runtime. FreeSerif is GPLv3 with the font exception (GNU FreeFont).

- `fit.py` finds the largest u that sits on the F's baseline inside the pocket without touching it, and writes the SVG
  and `u-free.json` (the placement).
- `small.py` renders the 16-48 px bitmaps with the font's own hinting (`mono=True`: 1-bit, no anti-aliasing).
- `pixel.py` writes `../icons/io.github.toomykins.Fugue-{16,24,32,48}.png`: the 1-bit F, with the u drawn by hand at
  16/24/32 px (the `U` table) and the middle arm's lower serif trimmed (`ERASE`).
  Run from this directory: `python3 pixel.py` (needs Pillow, numpy, scipy, fontTools).

64/128/256 px are the SVG rendered with rsvg-convert.
