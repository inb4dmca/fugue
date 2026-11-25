"""Stage this world in a runtime directory: the icons local-relay-world generates, then this world and its skin."""
import shutil, subprocess, sys
from pathlib import Path
source = Path(__file__).resolve().parent
target = Path(sys.argv[-1]).resolve()
if target == source:
    raise SystemExit('Use a separate runtime directory')
subprocess.run([sys.executable, '-B', str(source.parent / 'local-relay-world' / 'prepare.py'), str(target)], check=True)
for name in ('world.dm', 'world.dmf'):
    shutil.copy2(source / name, target / name)
