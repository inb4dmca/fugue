# Building

## Prerequisites

For Linux and macOS: a C++20 compiler (g++ 12 or newer, or a recent Xcode/Apple Clang), CMake 3.20 or newer, Ninja, pkg-config, and:

* Qt 6.7 or newer: Base (Core, Gui, Widgets, Network, Test), matching Gui private headers, Shader Tools, WebEngine (Widgets) and WebChannel
* zlib and libjpeg (libjpeg-turbo recommended)
* optional: libopenmpt, which plays tracker modules (MOD/S3M/XM/IT). Without it such music is refused with a line in
  `audio.log`.

| Distribution | Install |
|---|---|
| Debian / Ubuntu | `sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev qt6-base-private-dev qt6-shadertools-dev qt6-webengine-dev qt6-webchannel-dev zlib1g-dev libjpeg-dev libopenmpt-dev libgl-dev` |
| Fedora | `sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtshadertools-devel qt6-qtwebengine-devel qt6-qtwebchannel-devel zlib-devel libjpeg-turbo-devel libopenmpt-devel mesa-libGL-devel` |
| Arch | `sudo pacman -S base-devel cmake ninja pkgconf qt6-base qt6-shadertools qt6-webengine qt6-webchannel zlib libjpeg-turbo libopenmpt` |

The distribution must supply Qt 6.7+ and matching private headers. Older packages
(such as Ubuntu 24.04's Qt 6.4) cannot build the MAP renderer; use a newer Qt SDK or
configure `-DFUGUE_HEADLESS_ONLY=ON` for the non-GUI modules.

On Linux, audio at run time requires one of `pw-play` (PipeWire), `paplay` (PulseAudio) or `aplay` (ALSA) on the `PATH`.
macOS uses the system Core Audio framework; no external audio player is needed.

## Windows (x64)

Requires Windows 10 version 1903 or newer (or Windows 11), Visual Studio 2022 with **Desktop development with C++**,
CMake, Ninja, Git, and the **MSVC 2022 64-bit** Qt SDK. Qt 6.11.3 is what CI builds with on every platform (6.7 is the oldest that can build the renderer); select Qt WebEngine, WebChannel,
Shader Tools, and Positioning in the Qt installer, with matching Qt private headers.
Do not use the MinGW Qt kit: WebEngine requires MSVC.

### One-command local builds

With Qt and the repository's vcpkg dependencies already installed, copy
`build.local.example.psd1` to `build.local.psd1` and edit its paths once. The local
file is gitignored and loaded automatically. Then run:

```powershell
./tools/build.ps1 Debug
./tools/build.ps1 Release
```

Alternatively pass `-QtDir` and `-VcpkgRoot`, or `-DependencyRoot` pointing directly
at an installed `x64-windows` tree (including its debug libraries/DLLs).
Precedence is explicit parameters, then local-file settings, then `QTDIR` /
`VCPKG_ROOT` environment variables. Optional local settings are `DependencyRoot`
and `BuildRoot`; relative SDK/output paths are relative to the repository.
Use `-ConfigFile <path>` to select another data file; unknown settings or an
explicitly selected missing file are errors.
The script uses Ninja Multi-Config with the Visual Studio 2022 x64 compiler,
initializing MSVC automatically for child processes. Builds use separate
`build/ninja/debug` and `build/ninja/release` trees; old Visual Studio trees are
left untouched. Override their parent with `-BuildRoot` (use a new directory).
Build concurrency defaults to physical cores, capped at one job per available GiB
after reserving 2 GiB; `-Jobs N` overrides it. The limit is recomputed for each build. Executables are under each tree's `app/Debug` or `app/Release`.
It sets `FUGUE_RELEASE` appropriately, deploys Qt/WebEngine and matching dependency
DLLs, then checks `--version`. Release opens the launcher by default; use the
`pager` argument to open it in Debug.

**Before configuring or building, it deletes all existing executables in the
selected application's output directory, including deployed helpers.** A locked
executable stops the script immediately: close Fugue and its helpers, then retry.
It does not kill running processes. It does not delete executables in the other
configuration or in older build trees.

All DLLs in the selected dependency tree's `bin` (Release) or `debug/bin` (Debug)
are copied, including transitive dependencies. Prefer a dedicated dependency tree
to avoid copying unrelated packages. Keep the output directory together; do not
copy just the `.exe`. For distributable packages, use `packaging/windows/package.ps1`
with `-BuildDir build/ninja/release` and the matching `-DependencyBin`.

The deletion checks have a standalone Windows test:
`./tools/tests/build_test.ps1`.

### Manual build

Install [vcpkg](https://github.com/microsoft/vcpkg) and bootstrap it. Qt comes from the Qt installer, while the
repository's `vcpkg.json` installs zlib, libjpeg-turbo and libopenmpt. From an **x64 Native Tools** PowerShell
prompt in the repository root (adjust the two SDK paths):

```powershell
$qt = "C:/Qt/6.11.3/msvc2022_64"
$vcpkg = "C:/vcpkg"
cmake -S . -B build/ninja/release -G "Ninja Multi-Config" -DCMAKE_CXX_COMPILER=cl `
  "-DCMAKE_PREFIX_PATH=$qt" `
  "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows -DFUGUE_RELEASE=ON -DFUGUE_OPENMPT=ON
cmake --build build/ninja/release --config Release --parallel (cmake -P cmake/BuildJobs.cmake)
$env:PATH = "$qt/bin;$pwd/build/ninja/release/vcpkg_installed/x64-windows/bin;$env:PATH"
./build/ninja/release/app/Release/fugue-qt.exe
```

Native Windows audio uses the default Windows playback device (waveOut); no external PCM player is required.
`--audio=none` and `--audio=wav:<path>` also work. Linux `cmd:` audio sinks are not supported on Windows.

Run tests:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
$env:FUGUE_AUDIO = "none"
$env:QTWEBENGINE_CHROMIUM_FLAGS = "--no-sandbox --disable-gpu"
ctest --test-dir build/ninja/release -C Release -LE 'gpu|native' --output-on-failure --timeout 120
Remove-Item Env:QT_QPA_PLATFORM, Env:FUGUE_AUDIO, Env:QTWEBENGINE_CHROMIUM_FLAGS
```

Those WebEngine flags are for tests only; do not disable the sandbox for normal use.
The Git attributes preserve captured resource bytes on Windows; do not apply newline conversion to `tests/data`.

Create a portable directory (including Qt plugins, WebEngine helper/resources, MSVC runtime and dependency DLLs):

```powershell
./packaging/windows/package.ps1 -QtDir $qt -BuildDir build/ninja/release `
  -DependencyBin build/ninja/release/vcpkg_installed/x64-windows/bin
./dist/bin/fugue-qt.exe --version
```

Distribute the **whole `dist` directory**, not just the executable. Windows CI also uploads this directory as
`fugue-windows-x64`. The application retains console output for command-line modes.
`fugue-qt.exe pager -install-desktop` registers `byond://` for the current Windows user without administrator access;
it does not create a Start menu shortcut. Re-run it if you move the installation.

## Linux build

From the repository root:

```bash
cmake -G Ninja -S . -B build -DFUGUE_RELEASE=ON
cmake --build build --parallel "$(cmake -P cmake/BuildJobs.cmake)"
ninja -C build install        # installs fugue-qt to bin/ under the install prefix
```

`-DFUGUE_RELEASE=ON` builds the player-facing binary: it starts as the launcher, the developer test hooks are compiled
out, and on Linux/macOS the client disables core dumps (and refuses ptrace on Linux). Without it, `fugue-qt` with no subcommand is the game client.

| Option | Meaning |
|---|---|
| `-DFUGUE_OPENMPT=AUTO\|ON\|OFF` | tracker-module playback via libopenmpt. `AUTO` (default) uses it when found; `ON` makes it required. A libopenmpt in a private prefix is found with `-DCMAKE_PREFIX_PATH=<prefix>`. |
| `-DFUGUE_RELEASE=ON` | the release build described above |
| `-DFUGUE_HEADLESS_ONLY=ON` | only the Qt-light modules, tools and tests; no window binary and no WebEngine |

## macOS

Install Xcode Command Line Tools (`xcode-select --install`) and [Homebrew](https://brew.sh), then:

```bash
brew install cmake ninja pkgconf qtbase qtshadertools qtwebengine qtwebchannel qtsvg libopenmpt jpeg-turbo
cmake -G Ninja -S . -B build -DFUGUE_RELEASE=ON -DFUGUE_OPENMPT=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)" -DJPEG_ROOT="$(brew --prefix jpeg-turbo)" \
  -DPKG_CONFIG_EXECUTABLE="$(brew --prefix)/bin/pkg-config"
cmake --build build --parallel "$(cmake -P cmake/BuildJobs.cmake)"
open build/app/Fugue.app
```

Use a native Homebrew installation for your architecture (Apple Silicon or Intel); do not mix arm64 and x86_64
libraries. The minimum supported macOS version is determined by your Qt distribution. The bundle's
`LSMinimumSystemVersion` is `CMAKE_OSX_DEPLOYMENT_TARGET` when set, else the building Mac's version; CI builds for 15.0,
the macOS its Homebrew libraries (libopenmpt, libjpeg) are built for, with Qt's own 6.11.3 build. Official Qt installations
also work (include the Qt SVG module for bundle deployment): set `CMAKE_PREFIX_PATH` to the Qt macOS kit and the prefixes containing libopenmpt and libjpeg.

The build-tree app uses your installed libraries. To make a self-contained app, including Qt WebEngine's helper,
resources, plugins, and third-party libraries:

```bash
cmake --install build --prefix "$PWD/build/macos"
open build/macos/Fugue.app
# Optional: copy build/macos/Fugue.app to /Applications or ~/Applications.
```

Installation runs the selected Qt's `macdeployqt` and ad-hoc signs/verifies the complete bundle. No Homebrew packages
are needed on the destination Mac. This is **not** Developer ID signing or notarization; public distribution requires
those additional Apple signing steps. CI builds and tests both architectures and uploads ad-hoc-signed app archives.

Command-line options are unchanged; the executable is inside the bundle:

```bash
build/app/Fugue.app/Contents/MacOS/Fugue --version
build/app/Fugue.app/Contents/MacOS/Fugue client --connect host:port
```

Finder launch opens the launcher in release builds. Opening the app registers its `byond://` scheme with Launch
Services; clicked links are handled both at startup and while the launcher is running. The launcher joins a link as
its Join button does, so not while its game runs; a link opened while a game window runs is followed as a clicked page
link (at most one second game window). `pager -install-desktop`
is not used on macOS. Settings and credentials remain under `~/.fugue`, as on Linux. BYOND CTRL macros use the physical
Control key on macOS, not Command; paste and the browser find bar also take Command+V and Command+F.

## Tests

Build first, then run the QtTest executables through CTest. **CTest does not
rebuild**: stop after a failed build and rerun after the last relevant source edit.
The automated tests need no BYOND installation or game server; some use loopback
network connections. Python is not required to build or run the application tests.

For a single-config `build/` tree:

```bash
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build -LE 'gpu|native' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^shell_(composition|mapinput|map)$' --output-on-failure
```

For Windows multi-config builds, use the configured build directory and `-C Release`
as shown above. Keep compiler/test logs locally; inspect failures rather than
rerunning tests merely to collect counts.

`QT_QPA_PLATFORM=offscreen` lets the widget and WebEngine tests run without a display. Some tests skip with a message
when optional input is absent. The Core Audio device smoke test is opt-in (it writes silence):

```bash
FUGUE_TEST_COREAUDIO=1 ctest --test-dir build -R '^audio_sink$' --output-on-failure
```

### Master-opacity scenario

`masteropacity_test` reuses one set of stock wire, resources, mouse receipts and pixel expectations through three
CTest entries. The scenario needs no BYOND installation. Each selected profile must execute successfully; unavailable
capabilities and skipped cases fail that profile.

| CTest entry | Evidence |
|---|---|
| `shell_masteropacity_portable` | 45 stock mouse receipts and nine menu stacks, plus Fugue lifetime and fallback bounds |
| `shell_masteropacity_gpu` | Production QRhi composition compared exactly with the three stock maps |
| `shell_masteropacity_native` | Exact framebuffer pixels, the same receipts/stacks through Qt events, pending versus rendered frames and warm uploads |

Run one profile or all three with the standard CTest selectors:

```sh
ctest --test-dir build -R '^shell_masteropacity_portable$' --output-on-failure
ctest --test-dir build -R '^shell_masteropacity_gpu$' --output-on-failure
ctest --test-dir build -R '^shell_masteropacity_native$' --output-on-failure
ctest --test-dir build -L master-opacity --output-on-failure
```

On Windows, use `--test-dir build/ninja/release -C Release` and put Qt/dependency DLLs on PATH. CTest sets each
profile's environment: portable uses offscreen Qt; GPU uses software D3D11 on Windows and OpenGL on Linux/macOS;
native uses the platform window system and requires an interactive desktop. On Linux, a virtual display is sufficient:
`LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a ctest --test-dir build -L master-opacity --output-on-failure`.
Native checks exercise Qt event delivery and the window renderer; they do not drive desktop mouse input or a live server.

The general suite commands above exclude the explicit `gpu` and `native` labels. Unfiltered CTest runs include them
and need their capabilities. These labels currently classify master opacity; other tests still have their existing
offscreen/native behavior. Linux CI runs all three profiles, Windows CI runs portable and GPU, and macOS CI runs portable.
See the [fixture README](../tests/data/master-opacity-world/README.md) for the retained evidence and its limits.

### Native Windows MAP checks

Offscreen passes do not establish native rendering/input coverage. On an unlocked
interactive Windows desktop, with the configured Qt/dependency DLLs on PATH and
no global backend overrides, run the affected native checks directly:

```powershell
$env:QT_QPA_PLATFORM = "windows"
$env:FUGUE_AUDIO = "none"
$env:QTWEBENGINE_CHROMIUM_FLAGS = "--no-sandbox"
./build/ninja/release/shell/Release/map_test.exe `
  positionedMasterPixelsAndInput `
  masterMaterialPixelsAndInput stockOutlinePrecisionAndWorkload `
  stockShadowPixels popupReleaseTiming -o -,txt
Remove-Item Env:QT_QPA_PLATFORM, Env:FUGUE_AUDIO, Env:QTWEBENGINE_CHROMIUM_FLAGS
```

Adjust the executable path for a single-config generator. The named tests include
native-default/OpenGL rows; they are not the entire native suite. Report QtTest
failures/skips separately from CTest target counts. A skipped native check is not
verification. The test-only sandbox/audio settings are not normal launch settings.

## Reproducing fixtures

Retained worlds under `tests/data/` are optional independent reproduction sources.
Stage copies under a fresh `build/` directory, generate assets where required,
and compile the staged DME with the recorded BYOND version. Some generators need
Python/Pillow; automated C++ tests use the checked-in wire, observations and goldens.

Launch a separate DreamDaemon on an unused port with `-safe -invisible`, then a
separate client with isolated state. Never replace an existing server, edit an
external game checkout, or stop processes by name/port. Close only processes you
started and can identify; a launcher forwarding to an existing client does not own
that client. A stock guest-mode request does not itself prove account isolation.

`tools/stockprobe/stockprobe.py` provides `record`, `pair` and `click` workflows for supported worlds on Windows.
Other fixtures, including master opacity, retain their documented manual collection procedure.
Use each world's documented control file and wait for its readiness acknowledgement.
For worlds reading `probe-request.json`, supply fresh `token`, `phase` and `action`
fields and inspect the matching PROBE reply. A server acknowledgement is not a GPU
presentation fence. Record native DPI, coordinate units, foreground/geometry and
resize/reset history; equal outer windows alone do not establish comparable MAPs.
Keep raw captures locally and never regenerate stock expectations from Fugue output.
Strip account names, machine paths and process metadata before retaining fixture
expectations.

## Flatpak

The manifest builds the release binary and bundles libopenmpt.

```bash
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.kde.Sdk//6.10 org.kde.Platform//6.10 io.qt.qtwebengine.BaseApp//6.10
flatpak-builder --user --install --force-clean --jobs=2 build-flatpak packaging/flatpak/io.github.toomykins.Fugue.yml
flatpak run io.github.toomykins.Fugue
```

Inside the sandbox the state directory is `~/.var/app/io.github.toomykins.Fugue/.fugue`, separate from a
native install's. `~/.byond/cfg` is visible read-only so an existing key can be imported.

## AppImage

The AppImage CI job is currently disabled. For local builds, the
`install` step puts `fugue-qt`, the desktop file, icons and metainfo under the
prefix (the same files the Flatpak uses).

**AppImage**: `packaging/appimage/build.sh` needs a Qt 6 (CI: 6.11.3) with WebEngine, WebChannel and ShaderTools, for example from
`aqt install-qt linux desktop 6.11.3 linux_gcc_64 -m qtwebengine qtwebchannel qtpositioning qtserialport qtshadertools`. CI builds
on Ubuntu 22.04 for the oldest glibc, with g++ 12; its apt package list is in `.github/workflows/ci.yml` (the Qt libraries it
bundles are linked against X11, xkbcommon, Wayland and CUPS development packages).

```bash
QT_ROOT_DIR=$HOME/Qt/6.11.3/gcc_64 packaging/appimage/build.sh out    # out/Fugue-x86_64.AppImage
```

The script downloads linuxdeploy, its Qt plugin, appimagetool and the AppImage runtime at the versions and sha256 pinned in
it, bundles Qt (including `QtWebEngineProcess`, its resources and locales) and libopenmpt, and adds the launch hook
`packaging/appimage/fugue-sandbox.sh`. The hook keeps Chromium's user-namespace sandbox on where the system allows it
(an AppImage's mount is `nosuid`, so the setuid helper is not an option) and otherwise disables it for that run with a
warning on stderr; `FUGUE_KEEP_SANDBOX=1` turns the fallback off.

`fugue-qt webcheck` is the install smoke test: it starts WebEngine, runs a script in one page and prints
`webcheck ok`. CI runs it, offscreen, in clean Ubuntu, Debian and Fedora containers.

## Releases

The version is set once, by `project(... VERSION)` in the top-level `CMakeLists.txt`. It becomes `FUGUE_VERSION`
(`fugue_version.h`) and is what `fugue-qt --version`, the About box and the `fugue-version` winget answer report.
Configuring fails unless `packaging/flatpak/*.metainfo.xml` has a `<release>` for that version. Pushing a `v*` tag
makes CI build the Flatpak bundle and attach it to the GitHub release, then start the `Flatpak repository` workflow. The
AppImage job is switched off until the web sandbox tiers land.

The workflow runs `packaging/flatpak/publish-repo.sh`, which imports the latest release's bundle into a new repository,
signs it, and deploys it to GitHub Pages at <https://toomykins.github.io/fugue/>. It needs the repository secret
`FLATPAK_REPO_GPG_KEY` (the ASCII-armoured private key) and GitHub Pages set to deploy from Actions. Installed clients
trust that key, so changing it means every user adding the repository again. Run the workflow by hand to republish
without a new release.
