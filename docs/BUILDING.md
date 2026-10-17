# Building

## Prerequisites

For Linux: a C++20 compiler (g++ 12 or newer), CMake 3.20 or newer, Ninja, pkg-config, and:

* Qt 6.4 or newer: Base (Core, Gui, Widgets, Network, Test), WebEngine (Widgets) and WebChannel
* libjpeg (libjpeg-turbo)
* zlib
* optional: libopenmpt, which plays tracker modules (MOD/S3M/XM/IT). Without it such music is refused with a line in
  `audio.log`.

| Distribution | Install |
|---|---|
| Debian / Ubuntu | `sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev qt6-webengine-dev qt6-webchannel-dev libjpeg-dev zlib1g-dev libopenmpt-dev libgl-dev` |
| Fedora | `sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel qt6-qtwebengine-devel qt6-qtwebchannel-devel libjpeg-turbo-devel zlib-devel libopenmpt-devel mesa-libGL-devel` |
| Arch | `sudo pacman -S base-devel cmake ninja pkgconf qt6-base qt6-webengine qt6-webchannel libjpeg-turbo zlib libopenmpt` |

For Linux audio at run time one of `pw-play` (PipeWire), `paplay` (PulseAudio) or `aplay` (ALSA) must be on the `PATH`.

## Windows (x64)

Requires Windows 10 version 1903 or newer (or Windows 11), Visual Studio 2022 with **Desktop development with C++**,
CMake, Git, and the **MSVC 2022 64-bit** Qt SDK. Qt 6.8.3 or newer is recommended; select Qt WebEngine, WebChannel,
and Positioning in the Qt installer. Do not use the MinGW Qt kit: WebEngine requires MSVC.

Install [vcpkg](https://github.com/microsoft/vcpkg) and bootstrap it. Qt comes from the Qt installer, while the
repository's `vcpkg.json` installs libjpeg-turbo, zlib and libopenmpt. In PowerShell, from the repository root
(adjust the two SDK paths):

```powershell
$qt = "C:/Qt/6.8.3/msvc2022_64"
$vcpkg = "C:/dev/vcpkg"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_PREFIX_PATH=$qt" `
  "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows -DFUGUE_RELEASE=ON -DFUGUE_OPENMPT=ON
cmake --build build --config Release --parallel 4
$env:PATH = "$qt/bin;$pwd/build/vcpkg_installed/x64-windows/bin;$env:PATH"
./build/app/Release/fugue-qt.exe
```

Native Windows audio uses the default Windows playback device (waveOut); no external PCM player is required.
`--audio=none` and `--audio=wav:<path>` also work. Linux `cmd:` audio sinks are not supported on Windows.

Run tests:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
$env:FUGUE_AUDIO = "none"
$env:QTWEBENGINE_CHROMIUM_FLAGS = "--no-sandbox --disable-gpu"
ctest --test-dir build -C Release --output-on-failure --timeout 120
Remove-Item Env:QT_QPA_PLATFORM, Env:FUGUE_AUDIO, Env:QTWEBENGINE_CHROMIUM_FLAGS
```

Those WebEngine flags are for tests only; do not disable the sandbox for normal use.
The Git attributes preserve captured resource bytes on Windows; do not apply newline conversion to `tests/data`.

Create a portable directory (including Qt plugins, WebEngine helper/resources, MSVC runtime and dependency DLLs):

```powershell
./packaging/windows/package.ps1 -QtDir $qt
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
ninja -C build
ninja -C build install        # installs fugue-qt to bin/ under the install prefix
```

`-DFUGUE_RELEASE=ON` builds the player-facing binary: it starts as the launcher, the developer test hooks are compiled
out, and on Linux the client refuses ptrace. Without it, `fugue-qt` with no subcommand is the game client.

| Option | Meaning |
|---|---|
| `-DFUGUE_OPENMPT=AUTO\|ON\|OFF` | tracker-module playback via libopenmpt. `AUTO` (default) uses it when found; `ON` makes it required. A libopenmpt in a private prefix is found with `-DCMAKE_PREFIX_PATH=<prefix>`. |
| `-DFUGUE_RELEASE=ON` | the release build described above |
| `-DFUGUE_HEADLESS_ONLY=ON` | only the Qt-light modules, tools and tests; no window binary and no WebEngine |
| `-DFUGUE_COMPILE_JOBS=N` | cap on concurrent compile jobs (default 6). A Qt-heavy translation unit can use about 300 MB, so lower this on small machines. |

## Tests

The tests are QtTest executables run through CTest. They need no BYOND installation and no network.

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R rscstore -V      # one test, verbose
```

`QT_QPA_PLATFORM=offscreen` lets the widget and WebEngine tests run without a display. Some tests skip with a message
when optional input is absent.

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

## Releases

The version is set once, by `project(... VERSION)` in the top-level `CMakeLists.txt`. It becomes `FUGUE_VERSION`
(`fugue_version.h`) and is what `fugue-qt --version`, the About box and the `fugue-version` winget answer report.
Configuring fails unless `packaging/flatpak/*.metainfo.xml` has a `<release>` for that version. Pushing a `v*` tag
makes CI build the Flatpak bundle and attach it to the GitHub release.
