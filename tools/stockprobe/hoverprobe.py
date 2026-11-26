#!/usr/bin/env python3
"""Move the real mouse pointer over stock DreamSeeker's map and log what the world's mouse procs saw. Windows only.

  hoverprobe.py --world tests/data/hover-world --out <file.json> [--switches CLIENT_ENTER,..] [--params lag=10] SCRIPT

The world is prepared with its own prepare.py <dir> [switches] and compiled once; one DreamDaemon and one stock client
run every case of SCRIPT in turn. Each line of SCRIPT is "<case> ACTION...", and an ACTION is one of

  X,Y            a real pointer move to that MAP pixel (SetCursorPos: the window under it gets a real move). Points
                 outside the map are allowed (with a wider skin, its other controls are reached this way): -40,100 is left of the window, 100,-60 above its client area
  post:X,Y       a WM_MOUSEMOVE posted to the window at that point; the real pointer stays where it is
  sweep:X0,Y0,X1,Y1,N,MS   N real moves from the first point to the second, MS milliseconds apart
  down up rdown rup        a real press or release of the left or right button where the pointer is
  tap            a real left press and release at once
  key:K kdown:K kup:K      a real key pressed and released, pressed, or released: K is ctrl, shift, alt or esc
  shot:NAME      a grab of the screen around the window, NAME.png beside --out (to find where things are drawn)
  wait:SECONDS   give the world time

An action is followed by --settle seconds before its lines are read; with a "!" after it (tap!) the next one follows
at once.
Before each case the pointer is put outside the window and the probe waits a second, so that a case starts with the
pointer off the map. The stock window is moved to --at and kept topmost while the run lasts, so that real moves reach
it. events.log lines the world wrote during each action are kept per action, with the probe's own clock (seconds since
the case began) on the action. The user's pointer is put back at the end. Only processes started here are stopped.
"""
import argparse, ctypes, json, re, shutil, subprocess, sys, tempfile, time
from ctypes import wintypes
from pathlib import Path

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--world', required=True)
ap.add_argument('--byond', default='C:/Program Files (x86)/BYOND/bin')
ap.add_argument('--port', type=int, default=18870)
ap.add_argument('--params', default='')
ap.add_argument('--switches', default='')
ap.add_argument('--at', default='300,200', help='screen position of the stock window')
ap.add_argument('--settle', type=float, default=.35, help='seconds after each action before its lines are read')
ap.add_argument('--out', required=True)
ap.add_argument('script')
a = ap.parse_args()

user32 = ctypes.windll.user32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
user32.ChildWindowFromPointEx.restype = wintypes.HWND
user32.ChildWindowFromPointEx.argtypes = [wintypes.HWND, wintypes.POINT, wintypes.UINT]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
user32.MapWindowPoints.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_void_p, wintypes.UINT]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.c_void_p]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.c_void_p]
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.c_void_p]
user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.c_void_p]
user32.SetForegroundWindow.argtypes = [wintypes.HWND]


def main_window(pid):
    found = []
    def each(hwnd, _):
        owner = wintypes.DWORD(); user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            r = wintypes.RECT(); user32.GetClientRect(hwnd, ctypes.byref(r)); found.append((r.right * r.bottom, hwnd))
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(each), 0)
    return max(found)[1] if found else None


def text(path): return path.read_text(errors='replace') if path.exists() else ''


def wait(done, failure, seconds=40):
    end = time.time() + seconds
    while not done():
        if time.time() > end: raise SystemExit(failure)
        time.sleep(.1)


world = Path(a.world).resolve(); byond = Path(a.byond)
work = Path(tempfile.mkdtemp(prefix='hoverprobe-'))
switches = [s for s in a.switches.split(',') if s]
subprocess.run([sys.executable, str(world / 'prepare.py'), str(work)] + switches, check=True)
built = subprocess.run([str(byond / 'dm.exe'), 'world.dme'], cwd=work, capture_output=True, text=True)
if ' 0 errors' not in built.stdout: raise SystemExit(built.stdout)
cases = [line.split() for line in Path(a.script).read_text().splitlines() if line.strip() and not line.startswith('#')]
FORM = re.compile(r'(-?\d+,-?\d+|post:\d+,\d+|sweep:(-?\d+,){5}\d+|down|up|rdown|rup|tap|(key|kdown|kup):(ctrl|shift|alt|esc))!?$'
                  r'|wait:\d+(\.\d+)?$|shot:\w+$')
KEYS = {'ctrl': 0x11, 'shift': 0x10, 'alt': 0x12, 'esc': 0x1b}
for case in cases:
    bad = [x for x in case[1:] if not FORM.match(x)]
    if bad: raise SystemExit('not an action: ' + ' '.join(bad))

saved = wintypes.POINT(); user32.GetCursorPos(ctypes.byref(saved))
procs = []; log = work / 'events.log'; results = []
at = [int(v) for v in a.at.split(',')]
try:
    out = open(work / 'dd.out', 'w')
    procs.append(subprocess.Popen([str(byond / 'dd.exe'), 'world.dmb', str(a.port), '-trusted', '-invisible', '-params',
                                   a.params], cwd=work, stdout=out, stderr=subprocess.STDOUT))
    wait(lambda: 'World opened' in text(work / 'dd.out'), 'DreamDaemon did not open the world')
    procs.append(subprocess.Popen([str(byond / 'dreamseeker.exe'), f'byond://127.0.0.1:{a.port}'], cwd=work))
    wait(lambda: 'READY' in text(log), 'no READY in events.log')
    wait(lambda: main_window(procs[1].pid), 'stock has no window')
    time.sleep(2)
    hwnd = main_window(procs[1].pid)
    user32.SetWindowPos(hwnd, wintypes.HWND(-1), at[0], at[1], 0, 0, 0x0001 | 0x0040)   # topmost, no size change
    user32.SetForegroundWindow(hwnd)
    time.sleep(1)

    def screen(x, y):
        p = wintypes.POINT(x, y); user32.ClientToScreen(hwnd, ctypes.byref(p)); return p.x, p.y

    def lines(): return text(log).splitlines()

    def act(action):
        kind, _, value = action.rstrip('!').partition(':')
        if not value: kind, value = ('move', kind) if ',' in kind else (kind, '')
        if kind == 'move':
            user32.SetCursorPos(*screen(*map(int, value.split(','))))
        elif kind == 'post':
            x, y = map(int, value.split(','))
            p = wintypes.POINT(x, y); target = user32.ChildWindowFromPointEx(hwnd, p, 0) or hwnd
            user32.MapWindowPoints(hwnd, target, ctypes.byref(p), 1)
            user32.PostMessageW(target, 0x200, 0, (p.y << 16) | (p.x & 0xffff))
        elif kind == 'sweep':
            x0, y0, x1, y1, n, ms = map(int, value.split(','))
            start = time.perf_counter()
            for i in range(1, n + 1):
                while time.perf_counter() < start + (i - 1) * ms / 1000: pass   # sleep() is too coarse for this
                user32.SetCursorPos(*screen(round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n)))
            return time.perf_counter() - start
        elif kind in ('down', 'up', 'rdown', 'rup', 'tap'):
            user32.mouse_event({'down': 2, 'up': 4, 'rdown': 8, 'rup': 16, 'tap': 6}[kind], 0, 0, 0, 0)
        elif kind in ('key', 'kdown', 'kup'):
            if kind != 'kup': user32.keybd_event(KEYS[value], 0, 0, 0)
            if kind != 'kdown': user32.keybd_event(KEYS[value], 0, 2, 0)
        elif kind == 'shot':
            from PIL import ImageGrab
            r = wintypes.RECT(); user32.GetWindowRect(hwnd, ctypes.byref(r))
            ImageGrab.grab((r.left, r.top, r.right + 200, r.bottom + 200), all_screens=True).save(
                Path(a.out).with_name(value + '.png'))
        elif kind == 'wait':
            time.sleep(float(value))

    for case in cases:
        user32.SetCursorPos(*screen(-80, 112)); time.sleep(1)
        begin = time.perf_counter(); said = []
        for action in case[1:]:
            before = len(lines()); clock = time.perf_counter() - begin
            took = act(action)
            if not action.startswith(('wait:', 'shot:')) and not action.endswith('!'): time.sleep(a.settle)
            said.append({'action': action, 'at': round(clock, 3), 'logged': lines()[before:]})
            if took is not None: said[-1]['took'] = round(took, 4)
        results.append({'case': case[0], 'actions': said})
        print(case[0])
        for row in said:
            print(f"  {row['at']:7.3f} {row['action']}" + (f"  (took {row['took']:.4f} s)" if 'took' in row else ''))
            for line in row['logged']: print('      ' + line)
    version = re.search(r'Version ([\d.]+)', text(work / 'dd.out'))
    Path(a.out).write_text(json.dumps({'stock': version.group(1) if version else '?', 'switches': switches,
                                       'params': a.params, 'cases': results}, indent=1) + '\n')
finally:
    for key in KEYS.values():   # nothing is left held for the user
        if user32.GetAsyncKeyState(key) & 0x8000: user32.keybd_event(key, 0, 2, 0)
    for button, up in ((1, 4), (2, 16)):
        if user32.GetAsyncKeyState(button) & 0x8000: user32.mouse_event(up, 0, 0, 0, 0)
    user32.SetCursorPos(saved.x, saved.y)
    for p in reversed(procs):
        if p.poll() is None: p.kill()
    for p in procs: p.wait()
    shutil.rmtree(work, ignore_errors=True)
