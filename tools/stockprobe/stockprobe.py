#!/usr/bin/env python3
"""Run stock DreamSeeker and this client on one DreamDaemon and look at both. Windows only (DreamSeeker is); needs BYOND,
a built fugue-qt with its Qt on PATH, and Pillow. For the maintainer who adds a fixture case or re-records after a BYOND
update: the tests themselves replay what this writes and need none of it.

  stockprobe.py record <set>...           parity fixture: writes replay/<set>.txt (the frames this client received),
                                          replay/res/ and replay/<set>.png (stock's map) for compositionplan_test
  stockprobe.py pair <name>               grabs both maps: <out>/<name>-stock.png, -client.png and -diff.png
  stockprobe.py click <name> ACTION...    does the same things in both windows (client-area pixels) and prints what
                                          the world logged for each, action by action

An ACTION is one of
  X,Y                    a left click
  right:X,Y              a right click; the right-click menu each client then shows is printed as MENU lines
  down:X,Y  up:X,Y       a left press and a left release on their own (rdown:, rup: for the right button)
  move:X,Y               a pointer move, with the buttons as the last action left them
  wait:SECONDS           give the world time, e.g. between a press and its release
  menu:ATOM/VERB         choose that verb from the open menu; menu: alone closes it. ATOM is the entry's name or its
                         place from the top (1, 2, ..): stock draws the names itself, so its menu reads back without them

--world is a fixture directory with a world.dm (and a prepare.py, if the one next to this file does not do) that logs to events.log ("READY <key>" at login; CLICK or
whatever else it logs is printed by click). Nothing takes the focus or moves the pointer: windows are read with
PrintWindow and clicked with posted messages. Only the processes started here are stopped.

Stock draws by the display scale its process started on (default-size maptext differs between 100 % and 150 %), so
replay/recorded.tsv keeps the scale of every recording; DreamSeeker's map also stays blank while the display is idle.
"""
import argparse, ctypes, datetime, json, os, re, shutil, subprocess, sys, tempfile, time
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
user32.GetClassNameW.argtypes = [wintypes.HWND, ctypes.c_wchar_p, ctypes.c_int]
user32.GetSubMenu.restype = ctypes.c_void_p; user32.GetSubMenu.argtypes = [ctypes.c_void_p, ctypes.c_int]
user32.GetMenuItemCount.argtypes = [ctypes.c_void_p]
user32.GetMenuStringW.argtypes = [ctypes.c_void_p, wintypes.UINT, ctypes.c_wchar_p, ctypes.c_int, wintypes.UINT]
user32.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM, wintypes.UINT,
                                       wintypes.UINT, ctypes.c_void_p]
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


MOVE, LDOWN, LUP, RDOWN, RUP = 0x200, 0x201, 0x202, 0x204, 0x205
MOUSE = {'': [(MOVE, 0), (LDOWN, 1), (LUP, 0)], 'right': [(MOVE, 0), (RDOWN, 2), (RUP, 0)], 'move': [(MOVE, None)],
         'down': [(MOVE, 0), (LDOWN, 1)], 'up': [(MOVE, 1), (LUP, 0)], 'rdown': [(MOVE, 0), (RDOWN, 2)], 'rup': [(MOVE, 2), (RUP, 0)]}
held = {}   # pid -> the MK_ button bits its window was last left with


def mouse(pid, kind, x, y):
    hwnd = main_window(pid); p = wintypes.POINT(x, y); target = user32.ChildWindowFromPointEx(hwnd, p, 0) or hwnd
    user32.MapWindowPoints(hwnd, target, ctypes.byref(p), 1); where = (p.y << 16) | (p.x & 0xffff)
    for message, buttons in MOUSE[kind]:
        buttons = held.get(pid, 0) if buttons is None else buttons
        user32.PostMessageW(target, message, buttons, where); held[pid] = buttons; time.sleep(.08)


def windows(pid, wanted):
    """The process's visible top-level windows whose class name passes `wanted`, topmost first."""
    found = []
    def each(hwnd, _):
        owner = wintypes.DWORD(); user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(128); user32.GetClassNameW(hwnd, name, 128)
        if owner.value == pid and user32.IsWindowVisible(hwnd) and wanted(name.value): found.append(hwnd)
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(each), 0)
    return found


def native_menu(pid):
    """Stock's open right-click menu, read from the menu itself: [(atom, [verb, ...])], or None when none is open. An
    entry with no submenu has no verbs."""
    popups = windows(pid, lambda name: name == '#32768')
    if not popups: return None
    handles = []
    for hwnd in popups:
        result = ctypes.c_size_t(0); user32.SendMessageTimeoutW(hwnd, 0x01E1, 0, 0, 2, 1000, ctypes.byref(result))   # MN_GETHMENU
        handles.append(result.value)
    subs = lambda menu: [user32.GetSubMenu(menu, i) for i in range(max(0, user32.GetMenuItemCount(menu)))]
    root = next((m for m in handles if not any(m in subs(o) for o in handles if o != m)), handles[0])
    def label(menu, i):
        buf = ctypes.create_unicode_buffer(256); user32.GetMenuStringW(menu, i, buf, 256, 0x400); return buf.value.replace('&', '')
    return [(label(root, i), [label(sub, j) for j in range(user32.GetMenuItemCount(sub))] if sub else [])
            for i, sub in enumerate(subs(root))]


def press(hwnd, *names):
    code = {'down': (0x28, 0x150), 'right': (0x27, 0x14d), 'enter': (0x0d, 0x1c), 'escape': (0x1b, 0x01)}
    for name in names:
        vk, scan = code[name]
        user32.PostMessageW(hwnd, 0x100, vk, 1 | scan << 16); time.sleep(.05)
        user32.PostMessageW(hwnd, 0x101, vk, 1 | scan << 16 | 3 << 30); time.sleep(.15)


def menu_lines(menu):
    if menu is None: return ['MENU none']
    return [f'MENU {atom}: ' + ' | '.join(verbs) for atom, verbs in menu] or ['MENU empty']


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
        self.record = Path(record) if record else None
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
    def lines(self): return text(self.log).splitlines()
    def markers(self):
        return [l for l in text(self.record / 'markers.jsonl').splitlines() if '"kind":"menu"' in l] if self.record else []
    def menu(self, pid, seen):
        """The right-click menu `pid` shows after a right click: stock's own popup, and for this client what its
        recorder says it offered (it shows a popup only for a menu with entries)."""
        end = time.time() + 2
        while time.time() < end:
            if pid == self.stock:
                found = native_menu(pid)
                if found is not None: return found
            elif len(self.markers()) > seen:
                return [(entry['atom'], entry['verbs']) for entry in json.loads(self.markers()[-1])['menu']]
            time.sleep(.1)
        return None
    def choose(self, pid, menu, wanted):
        """Walks the open menu with the keyboard: down to the atom, into its verbs, down to the verb."""
        popups = windows(pid, (lambda name: name == '#32768') if pid == self.stock else (lambda name: 'Popup' in name))
        if not popups: return 'PICK no menu is open'
        if not wanted: press(popups[0], 'escape', 'escape'); return None
        atom, _, verb = wanted.partition('/')
        at = next((i for i, entry in enumerate(menu or [])
                   if atom in (entry[0], str(i + 1)) and (verb in entry[1] or not entry[1])), None)
        if at is None: press(popups[0], 'escape', 'escape'); return f'PICK {wanted} is not in the menu'
        verbs = menu[at][1]
        press(popups[0], *['down'] * (at + 1), *(['right'] + ['down'] * verbs.index(verb) if verbs else []), 'enter')
        return None
    def act(self, pid, actions):
        """Does the actions in one window; what the world logged and the menus shown, per action."""
        said = []; menu = None
        for action in actions:
            before = len(self.lines()); seen = len(self.markers()); extra = []
            kind, _, value = action.rpartition(':')
            if kind == 'wait': time.sleep(float(value))
            elif kind == 'menu':
                note = self.choose(pid, menu, value); extra = [note] if note else []; menu = None; time.sleep(.8)
            else:
                mouse(pid, kind, *(int(v) for v in value.split(','))); time.sleep(.5)
                if kind in ('right', 'rup'): menu = self.menu(pid, seen); extra = menu_lines(menu)
            said.append(self.lines()[before:] + extra)
        if menu: self.choose(pid, menu, '')
        return said
    def stop(self):
        for p in reversed(self.procs): p.kill()
        for p in self.procs: p.wait()
    def __enter__(self): return self
    def __exit__(self, *_): self.stop()


def differing(x, y):
    """Pixels with a channel more than 8 levels apart, as the tests count them, and a picture of where."""
    mask = ImageChops.difference(x, y).point(lambda v: 255 if v > 8 else 0).convert('L').point(lambda v: 255 if v else 0)
    return mask.histogram()[255], mask


own = world / 'prepare.py'   # a world with none is prepared by the one next to this file, which says what that gives it
subprocess.run([sys.executable, str(own), str(work)] if own.exists() else [sys.executable, str(HERE / 'prepare.py'), str(world), str(work)],
               check=True, stdout=subprocess.DEVNULL)
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
    actions = a.args[1:]
    form = re.compile(r'(?:(?:right|down|up|rdown|rup|move):)?\d+,\d+$|wait:\d+(?:\.\d+)?$|menu:.*$')
    wrong = [action for action in actions if not form.match(action)]
    if not actions or wrong: sys.exit('click <name> ACTION...' + (': not an action: ' + ' '.join(wrong) if wrong else ''))
    frames = work / 'frames-click'; shutil.rmtree(frames, ignore_errors=True)
    with Run(a.params, frames) as run:
        names = [l.split()[1] for l in run.lines() if l.startswith('READY')]   # stock logs in first
        said = [run.act(run.stock, actions), run.act(run.client, actions)]
    plain = lambda l: re.sub('|'.join(map(re.escape, names)) or '$^', '<key>', l)
    print(f'{a.args[0]}: {len(actions)} actions; {sum(map(len, said[0]))} lines for stock and {sum(map(len, said[1]))} '
          'for the client (what the world logged, and the probe\'s own MENU and PICK lines)')
    for action, stock, client in zip(actions, *said):
        print(f'  {action}')
        for i in range(max(len(stock), len(client))):
            s, c = (plain(part[i]) if i < len(part) else '-' for part in (stock, client))
            print(('    same   ' if s == c else '    DIFFER ') + s + ('' if s == c else '\n             ' + c))
if not a.work: shutil.rmtree(work, ignore_errors=True)
