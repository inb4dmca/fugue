# Building

## Prerequisites

A C++20 compiler (g++ 12 or newer, or a recent Xcode/Apple Clang), CMake 3.20 or newer, Ninja, pkg-config, and:

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

On Linux, audio at run time requires one of `pw-play` (PipeWire), `paplay` (PulseAudio) or `aplay` (ALSA) on the `PATH`.
macOS uses the system Core Audio framework; no external audio player is needed.

## Build

From the repository root:

```bash
cmake -G Ninja -S . -B build -DFUGUE_RELEASE=ON
ninja -C build
ninja -C build install        # installs fugue-qt to bin/ under the install prefix
```

`-DFUGUE_RELEASE=ON` builds the player-facing binary: it starts as the launcher, the developer test hooks are compiled
out, and the client disables core dumps (and refuses ptrace on Linux). Without it, `fugue-qt` with no subcommand is the game client.

| Option | Meaning |
|---|---|
| `-DFUGUE_OPENMPT=AUTO\|ON\|OFF` | tracker-module playback via libopenmpt. `AUTO` (default) uses it when found; `ON` makes it required. A libopenmpt in a private prefix is found with `-DCMAKE_PREFIX_PATH=<prefix>`. |
| `-DFUGUE_RELEASE=ON` | the release build described above |
| `-DFUGUE_HEADLESS_ONLY=ON` | only the Qt-light modules, tools and tests; no window binary and no WebEngine |
| `-DFUGUE_COMPILE_JOBS=N` | cap on concurrent compile jobs (default 6). A Qt-heavy translation unit can use about 300 MB, so lower this on small machines. |

## macOS

Install Xcode Command Line Tools (`xcode-select --install`) and [Homebrew](https://brew.sh), then:

```bash
brew install cmake ninja pkgconf qtbase qtwebengine qtwebchannel qtsvg jpeg-turbo libopenmpt
cmake -G Ninja -S . -B build -DFUGUE_RELEASE=ON -DFUGUE_OPENMPT=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
  -DPKG_CONFIG_EXECUTABLE="$(brew --prefix)/bin/pkg-config"
cmake --build build
open build/app/Fugue.app
```

Use a native Homebrew installation for your architecture (Apple Silicon or Intel); do not mix arm64 and x86_64
libraries. The minimum supported macOS version is determined by your Qt distribution. Official Qt installations
also work (include the Qt SVG module for bundle deployment): set `CMAKE_PREFIX_PATH` to the Qt macOS kit and the prefixes containing libjpeg/libopenmpt.

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
Services; clicked links are handled both at startup and while the launcher is running. `pager -install-desktop`
is Linux-only. Settings and credentials remain under `~/.fugue`, as on Linux. BYOND CTRL macros use the physical
Control key on macOS, not Command.

## Tests

The tests are QtTest executables run through CTest. They need no BYOND installation and no network.

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R rscstore -V      # one test, verbose
```

`QT_QPA_PLATFORM=offscreen` lets the widget and WebEngine tests run without a display. Some tests skip with a message
when optional input is absent. The Core Audio device smoke test is opt-in (it writes silence):

```bash
FUGUE_TEST_COREAUDIO=1 ctest --test-dir build -R '^audio_sink$' --output-on-failure
```

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
