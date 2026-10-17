# Contributing to Fugue

Thanks for helping. Fugue is a native client for BYOND games; the goal is to behave like stock DreamSeeker, so the
most valuable contributions are fixes that make a real game look or act the way it does in DreamSeeker, with a test
that pins it.

## Build and test

Prerequisites, options and the test command are in [docs/BUILDING.md](docs/BUILDING.md). A development build (no
`-DFUGUE_RELEASE=ON`) keeps the test hooks. For work that does not touch the window, `-DFUGUE_HEADLESS_ONLY=ON` builds
only the Qt-light modules and their tests.

Each `tests/*_test.cpp` (and `*/tests/*_test.cpp`) is its own QtTest executable and CTest entry, picked up by a glob:
add a file, re-run `ninja`, and it exists. Tests must not need the network, a BYOND installation, or files outside
the repository. Tests that replay a recorded session use data under `tests/data/`; a recording should come from a
small purpose-written world rather than a real game, so the expected values are understood and the data is ours to
publish.

Please run the whole suite before sending a change, and do not report a test as passing if it skipped.

## Adding code

* Modules are directories whose `CMakeLists.txt` calls `fugue_module(<target>)`, which globs the directory's
  `*.cpp` into an object library (some modules add tools or options after it). New source files in an existing module
  need no build-list edit. A new module directory needs its own `CMakeLists.txt` and one `add_subdirectory` line in the
  top-level `CMakeLists.txt`.
* A protocol message decoder is one file, `proto/dec_<s2c|c2s>_<type hex4>_<name>.cpp`, that registers itself with
  `FUGUE_REGISTER_DECODER`. Parse with the bounds-checked `proto::Reader`; on a malformed body throw instead of
  guessing. The rule is correct framing or "unknown", never a guessed width.
* Includes are rooted at the repository root: `#include "net/cipher.h"`.
* C++20. `net/`, `proto/`, `model/`, `skin/`, `account/` stay free of Qt GUI types; Qt Widgets and WebEngine live in
  `shell/`, `pager/ui/` and `app/`.

## Code style

* Follow the formatting of the file you are editing: four-space indent, braces on the same line, short lines (the
  code base wraps near 120 columns).
* Comments explain why, not what, and stay on the code: no history, no account of who found a problem or how it was
  tested. When a behaviour exists because stock BYOND does it, state that fact in a line or two (and the stock
  version). If a rule only approximates stock, say so in the comment.
* A change that alters rendering or protocol behaviour should come with a test, and where possible with the stock
  behaviour it was checked against. If you cannot check against stock BYOND, say so in the commit message.
* Do not commit BYOND binaries, game assets, credentials, or output copied out of a decompiler. Describe facts and
  structure in your own words. Third-party code or data needs a licence note in `THIRD-PARTY-NOTICES.md`.

## Commit messages and pull requests

Describe what changed and why in the first line, then what you tested and how. Keep unrelated changes in separate
pull requests.
