# Unfiltered opacity-2 master fallback

Independent Windows BYOND **516.1688** fixture: one iconless identity/unfiltered
HUD master at `1,1` on plane 1, one ordinary 32px world member at `(2,2)`, and a
plane-0 floor. It covers opacity 0/1/2, not filtered masters or general trees.
Captured player display names use the same-length synthetic `TestGuest`; all other
wire bytes are unchanged. This does not establish guest-account isolation.

## Retained expectations

- `stock/result.json`: the three stock phase records consumed by
  `masteropacity_test`, including **45 mouse receipts and nine Menu
  receipts**. This file is a test input, not an unconsumed run report.
- `stock/frames-{control,two,zero}.txt` and `stock/res/`: independent wire/assets.
- `stock/{control,two,zero}-map.png`: the three stock pixel goldens consumed by
  the GPU and native profiles of `masteropacity_test`.
- `MasterOpacityFixture::menu()`: separately transcribed original-rectangle menu
  ownership, not an inference from Click callbacks or generated pixels.

The mode-2 iconless master owns alpha holes/empty plane space behind member ink;
member ink remains member-owned. In mode 1 those holes hit the floor. Menu stacks
remain independent: member then floor in the member rectangle, floor elsewhere.
Coordinates and callback fields are checked against actual stock receipts.

Tests also cover stale master/member/HUD lifetimes and rendered-versus-pending
publication. General affine/effected masters, image opacity-2, hover/drag and full
popup UX are outside this fixture's acceptance scope.

Unconsumed default/OpenGL run reports, comparison summaries and process-provenance
archives have been removed. Process-owner metadata is also omitted from the
retained stock result. Phase/action expectations are unchanged; no popup screenshot
archive is required by the tests.

## Automated checks

One executable, `shell/tests/masteropacity_test.cpp`, consumes this scenario through three CTest entries:

- `shell_masteropacity_portable`: 45 stock mouse receipts and nine menu stacks, plus fallback bounds and stale lifetimes.
- `shell_masteropacity_gpu`: production QRhi output compared exactly with each stock PNG, without a MapWidget.
- `shell_masteropacity_native`: MapWidget pixels and Qt-delivered input on the native default backend and OpenGL,
  including pending versus rendered publication and zero warm uploads.

All profiles use the scenario's `MasterOpacityFixture` in the same test file, which builds the scene through the shared
`ReplayFixture`. The retained frames
describe static phases; these checks do not reconstruct the timing of the original stock session. Native checks validate
the window/input boundary, not desktop mouse automation, popup UX or live server callbacks.

Build first, then run `ctest --test-dir build -L master-opacity --output-on-failure`, or select one entry with `-R`.
Windows multi-config builds also need `-C Release` and the configured build directory. Required capability failures
are failures, not passing skips. See [profile setup and commands](../../../docs/BUILDING.md#master-opacity-scenario).

## Reproduction

Requires BYOND and Python/Pillow:

```sh
python -B tests/data/master-opacity-world/prepare.py build/master-opacity-repro
```

Compile the staged `world.dme` and host it with DreamDaemon `-safe -invisible` on
an unused isolated port. Select `control`, `two` and `zero` through `phase.txt`
with fresh tokens; enable `popup.txt` for menu observations as defined by the world.
After READY, collect stock pixels and receipts manually at the points retained in
`stock/result.json`. Use separate state and matched DPI/resize/reset history for
Fugue; only stock supplies oracle pixels/receipts. Close only processes you started.

See [renderer input contracts](../../../docs/RENDERING.md#presentation-and-input).
