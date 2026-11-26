"""The runtime directory of the hover probe: prepare.py <dir> [CLIENT_ENTER] [CLIENT_MOVE] (requires Pillow).

Copies world.dm, writes world.dme (with a #define for each switch named, or each named in HOVER_SWITCHES, comma
separated, which is how stockprobe.py record gets them), a 288x224 skin (a 7x7 view of 32 px tiles between 32 px of
letterbox left and right) and solid icons: one colour per probe atom over the whole tile, and big.png, 64x64.
"""
import os
from pathlib import Path
import shutil
import sys
from PIL import Image

source = Path(__file__).resolve().parent
if len(sys.argv) < 2: raise SystemExit(__doc__)
t = Path(sys.argv[1]).resolve()
if t == source or source in t.parents: raise SystemExit('Use a separate runtime directory')
switches = sys.argv[2:] or [s for s in os.environ.get('HOVER_SWITCHES', '').split(',') if s]
if any(s not in ('CLIENT_ENTER', 'CLIENT_MOVE') for s in switches): raise SystemExit(__doc__)
t.mkdir(parents=True, exist_ok=True)
shutil.copy2(source / 'world.dm', t / 'world.dm')
(t / 'world.dme').write_bytes(''.join(f'#define {s}\n' for s in switches).encode() +
                              b'#include "world.dmf"\n#include "world.dm"\n')
skin = '\n'.join(['window "main"', '    elem "main"', '        type = MAIN', '        size = 288x224',
                  '        is-default = true', '    elem "map"', '        type = MAP', '        pos = 0,0',
                  '        size = 288x224', '        anchor1 = 0,0', '        anchor2 = 100,100', '        icon-size = 32',
                  '        is-default = true', ''])
(t / 'world.dmf').write_bytes(skin.encode())
for name, fill in {'floor': (16, 16, 16), 'red': (200, 60, 60), 'orange': (230, 140, 40), 'green': (60, 190, 60),
                   'yellow': (230, 220, 50), 'grey': (128, 128, 128), 'magenta': (200, 60, 200), 'blue': (50, 80, 230),
                   'cyan': (50, 200, 210), 'white': (235, 235, 235)}.items():
    Image.new('RGBA', (32, 32), fill + (255,)).save(t / f'{name}.png')
Image.new('RGBA', (64, 64), (90, 110, 150, 255)).save(t / 'big.png')
