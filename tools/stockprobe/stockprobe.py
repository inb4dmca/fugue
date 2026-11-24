#!/usr/bin/env python3
"""Run stock DreamSeeker and this client on one DreamDaemon and look at both. Windows only (DreamSeeker is); needs BYOND,
a built fugue-qt with its Qt on PATH, and Pillow. For the maintainer who adds a fixture case or re-records after a BYOND
update: the tests themselves replay what this writes and need none of it.

  stockprobe.py record <set>...           parity fixture: writes replay/<set>.txt (the frames this client received),
                                          replay/res/ and replay/<set>.png (stock's map) for compositionplan_test
  stockprobe.py pair <name>               grabs both maps: <out>/<name>-stock.png, -client.png and -diff.png
  stockprobe.py click <name> X,Y [X,Y..]  posts the same left clicks to both windows (client-area pixels) and prints
                                          what the world logged for each

--world is a fixture directory with prepare.py and a world that logs to events.log ("READY <key>" at login; CLICK or
whatever else it logs is printed by click). Nothing takes the focus or moves the pointer: windows are read with
PrintWindow and clicked with posted messages. Only the processes started here are stopped.

Stock draws by the display scale its process started on (default-size maptext differs between 100 % and 150 %), so
replay/recorded.tsv keeps the scale of every recording; DreamSeeker's map also stays blank while the display is idle.
"""
import argparse, ctypes, datetime, os, re, shutil, subprocess, sys, tempfile, time
from ctypes import wintypes
from pathlib import Path
from PIL import Image, ImageChops

HERE = Path(__file__).resolve().parent
ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--world', default=str(HERE.parent.parent / 'tests/data/stock-parity-world'), help='fixture directory')
ap.add_argument('--params', default='', help='world.params for pair and click, e.g. set=images-groups (record passes set=<set>)')
ap.add_argument('--client', default=os.environ.get('FUGUE_CLIENT', ''), help='fugue-qt executable (or FUGUE_CLIENT)')
ap.add_argument('--byond', default=os.environ.get('BYOND_BIN', 'C:/Program Files (x86)/BYOND/bin'), help='BYOND bin directory')
ap.add_argument('--work', default='', help='scratch directory (default: a temporary one)')
ap.add_argument('--out', default='.', help='where pair writes its images')
ap.add_argument('--port', type=int, default=18790)
ap.add_argument('--settle', type=float, default=6, help='seconds between the last login and the grab')
ap.add_argument('--map', default='576x384', help='size of the map at the top left of both windows')
ap.add_argument('command', choices=['record', 'pair', 'click'])
ap.add_argument('args', nargs='+')
a = ap.parse_args()
if sys.platform != 'win32': sys.exit('stockprobe needs Windows: DreamSeeker runs nowhere else')
if not a.client or not Path(a.client).exists(): sys.exit('--client (or FUGUE_CLIENT): the fugue-qt executable to run')
world = Path(a.world).resolve(); byond = Path(a.byond); mapsize = tuple(int(v) for v in a.map.split('x'))
work = Path(a.work).resolve() if a.work else Path(tempfile.mkdtemp(prefix='stockprobe-'))

user32 = ctypes.windll.user32; gdi32 = ctypes.windll.gdi32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # sizes in real pixels
for f, res in ((user32.GetDC, wintypes.HDC), (gdi32.CreateCompatibleDC, wintypes.HDC), (gdi32.CreateCompatibleBitmap, wintypes.HBITMAP),
               (gdi32.SelectObject, wintypes.HGDIOBJ), (user32.ChildWindowFromPointEx, wintypes.HWND)):
    f.restype = res
user32.GetDC.argtypes = [wintypes.HWND]; user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.c_void_p]; user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.c_void_p]
user32.MapWindowPoints.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_void_p, wintypes.UINT]
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]; gdi32.DeleteDC.argtypes = [wintypes.HDC]
gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT, ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
user32.ChildWindowFromPointEx.argtypes = [wintypes.HWND, wintypes.POINT, wintypes.UINT]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]


def main_window(pid):
    """The largest visible top-level window of the process."""
    found = []
    def each(hwnd, _):
        owner = wintypes.DWORD(); user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            r = wintypes.RECT(); user32.GetClientRect(hwnd, ctypes.byref(r)); found.append((r.right * r.bottom, hwnd))
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(each), 0)
    if not found: sys.exit(f'process {pid} has no window')
    return max(found)[1]


def grab(pid):
    """The window's client area, drawn by the window itself (works behind other windows)."""
    hwnd = main_window(pid); r = wintypes.RECT(); user32.GetClientRect(hwnd, ctypes.byref(r)); w, h = r.right, r.bottom
    screen = user32.GetDC(None); dc = gdi32.CreateCompatibleDC(screen); bmp = gdi32.CreateCompatibleBitmap(screen, w, h)
    old = gdi32.SelectObject(dc, bmp); user32.PrintWindow(hwnd, dc, 3)   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    gdi32.SelectObject(dc, old)
    header = (ctypes.c_uint32 * 10)(40, w, ctypes.c_uint32(-h).value, 1 | 32 << 16, 0, 0, 0, 0, 0, 0)   # top-down 32-bit
    pixels = ctypes.create_string_buffer(w * h * 4); gdi32.GetDIBits(dc, bmp, 0, h, pixels, header, 0)
    gdi32.DeleteObject(bmp); gdi32.DeleteDC(dc); user32.ReleaseDC(None, screen)
    return Image.frombuffer('RGBA', (w, h), pixels.raw, 'raw', 'BGRA', 0, 1).convert('RGB')


def click(pid, points):
    hwnd = main_window(pid)
    for x, y in points:
        p = wintypes.POINT(x, y); target = user32.ChildWindowFromPointEx(hwnd, p, 0) or hwnd
        user32.MapWindowPoints(hwnd, target, ctypes.byref(p), 1); where = (p.y << 16) | (p.x & 0xffff)
        for message, keys, pause in ((0x200, 0, .08), (0x201, 1, .08), (0x202, 0, .5)):   # move, button down, up
            user32.PostMessageW(target, message, keys, where); time.sleep(pause)


def text(path): return path.read_text(errors='replace') if path.exists() else ''


class Run:
    """DreamDaemon with the world, stock DreamSeeker and this client, both logged in."""
    def __init__(self, params, record=None):
        self.log = work / 'events.log'; self.log.unlink(missing_ok=True); self.procs = []
        out = open(work / 'dd.out', 'w')
        self.start([str(byond / 'dd.exe'), 'world.dmb', str(a.port), '-trusted', '-invisible', '-params', params], stdout=out)
        self.wait(lambda: 'World opened' in text(work / 'dd.out'), 'DreamDaemon did not open the world: ' + text(work / 'dd.out'))
        self.stock = self.start([str(byond / 'dreamseeker.exe'), f'byond://127.0.0.1:{a.port}'])
        env = dict(os.environ, QT_ENABLE_HIGHDPI_SCALING='0')   # one map pixel per screen pixel, like stock's
        if record: env['FUGUE_RECORD_DIR'] = str(record)
        self.client = self.start([a.client, 'client', '--connect', f'127.0.0.1:{a.port}', '--state-dir', str(work / 'state')],
                                 env=env, stdout=open(work / 'client.log', 'w'))
        self.wait(lambda: text(self.log).count('READY') >= 2, 'the world did not log READY for both clients (events.log)')
        time.sleep(a.settle)
        version = re.search(r'Version ([\d.]+)', text(work / 'dd.out')); self.byond = version.group(1) if version else '?'
    def start(self, command, **more):
        self.procs.append(subprocess.Popen(command, cwd=work, stderr=subprocess.STDOUT, **more)); return self.procs[-1].pid
    def wait(self, done, failure):
        end = time.time() + 40
        while not done():
            if time.time() > end: self.stop(); sys.exit(failure)
            time.sleep(0.1)
    def maps(self):
        box = (0, 0) + mapsize; stock = grab(self.stock).crop(box); client = grab(self.client).crop(box)
        if len(stock.getcolors(2) or [0, 0]) < 2: self.stop(); sys.exit("stock's map is blank: is the display idle or the window minimised?")
        return stock, client
    def stop(self):
        for p in reversed(self.procs): p.kill()
        for p in self.procs: p.wait()
    def __enter__(self): return self
    def __exit__(self, *_): self.stop()


def differing(x, y):
    """Pixels with a channel more than 8 levels apart, as the tests count them, and a picture of where."""
    mask = ImageChops.difference(x, y).point(lambda v: 255 if v > 8 else 0).convert('L').point(lambda v: 255 if v else 0)
    return mask.histogram()[255], mask


subprocess.run([sys.executable, str(world / 'prepare.py'), str(work)], check=True, stdout=subprocess.DEVNULL)
built = subprocess.run([str(byond / 'dm.exe'), 'world.dme'], cwd=work, capture_output=True, text=True)
if not (work / 'world.dmb').exists() or ' 0 errors' not in built.stdout: sys.exit('the world did not compile:\n' + built.stdout)
dpi = user32.GetDpiForSystem()

if a.command == 'record':
    replay = world / 'replay'; (replay / 'res').mkdir(parents=True, exist_ok=True); notes = replay / 'recorded.tsv'
    rows = dict(line.split('\t', 1) for line in text(notes).splitlines() if '\t' in line and not line.startswith('#'))
    for st in a.args:
        frames = work / f'frames-{st}'; shutil.rmtree(frames, ignore_errors=True)
        with Run(f'set={st}', frames) as run:
            stock, client = run.maps(); off, _ = differing(stock, client); version = run.byond
        stock.save(replay / f'{st}.png', optimize=True)
        (replay / f'{st}.txt').write_bytes((frames / 'frames.txt').read_bytes().replace(b'\r\n', b'\n'))
        for resource in (frames / 'res').iterdir(): shutil.copyfile(resource, replay / 'res' / resource.name)
        rows[st] = f'{version}\t{dpi}\t{datetime.date.today()}'
        print(f'{st}: recorded; the live client differs from stock in {off} pixels')
    notes.write_text('# set\tBYOND\tdisplay dpi\trecorded\n' + ''.join(f'{k}\t{v}\n' for k, v in sorted(rows.items())), newline='\n')
elif a.command == 'pair':
    name = a.args[0]; out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    with Run(a.params) as run: stock, client = run.maps()
    off, mask = differing(stock, client)
    stock.save(out / f'{name}-stock.png'); client.save(out / f'{name}-client.png')
    Image.composite(Image.new('RGB', stock.size, (255, 0, 255)), stock.point(lambda v: v // 3), mask).save(out / f'{name}-diff.png')
    box = mask.getbbox()
    print(f'{name}: {off} pixels differ by more than 8 levels' + (f', within {box}' if box else '') + f' (display at {dpi} dpi)')
else:
    points = [tuple(int(v) for v in p.split(',')) for p in a.args[1:]]
    if not points: sys.exit('click <name> X,Y [X,Y ...]')
    with Run(a.params) as run:
        lines = lambda: text(run.log).splitlines()
        keys = [l.split()[1] for l in lines() if l.startswith('READY')]   # stock logs in first
        before = len(lines()); click(run.stock, points); time.sleep(1); middle = len(lines())
        click(run.client, points); time.sleep(1); logged = lines()
    plain = lambda l: re.sub('|'.join(map(re.escape, keys)) or '$^', '<key>', l)
    said = [[plain(l) for l in part] for part in (logged[before:middle], logged[middle:])]
    print(f'{a.args[0]}: {len(points)} clicks; the world logged {len(said[0])} lines for stock and {len(said[1])} for the client')
    for i in range(max(map(len, said))):
        s, c = (part[i] if i < len(part) else '-' for part in said)
        print(('  same   ' if s == c else '  DIFFER ') + s + ('' if s == c else '\n           ' + c))
if not a.work: shutil.rmtree(work, ignore_errors=True)
