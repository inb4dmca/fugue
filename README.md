# Fugue

Fugue is a native Linux client and launcher for BYOND games, written in C++20 with Qt 6. It replaces DreamSeeker on
servers that run stock BYOND, without a BYOND install and without Wine. It is engineered for /vg/station. It is
unofficial and not affiliated with BYOND.

**Status:** early software. It plays full /vg/station rounds, other BYOND games may work, and bugs are expected.

## Install

Build from source (details and prerequisites in [docs/BUILDING.md](docs/BUILDING.md)):

```bash
cmake -G Ninja -S . -B build -DFUGUE_RELEASE=ON
ninja -C build
```

Or build the Flatpak, also described in [docs/BUILDING.md](docs/BUILDING.md).

## Run

```bash
build/app/fugue-qt                              # the launcher: server list and sign-in
build/app/fugue-qt client --connect host:port   # the game window, as a guest
```

All options, state directories and environment variables are in [docs/USAGE.md](docs/USAGE.md).

## Documentation

* [docs/BUILDING.md](docs/BUILDING.md): prerequisites, build options, tests, Flatpak
* [docs/USAGE.md](docs/USAGE.md): launcher and client commands, state directories, environment, audio
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): modules, threads, the HTML surface, the resource cache
* [SECURITY.md](SECURITY.md): what Fugue stores and how it handles your credentials
* [CONTRIBUTING.md](CONTRIBUTING.md): code layout rules, tests, style
* [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md): third-party code and data

## License

The [Unlicense](LICENSE), except the third-party files listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
