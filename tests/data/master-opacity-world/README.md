# Unfiltered opacity-2 master fallback

Independent Windows BYOND **516.1688** fixture: one iconless identity/unfiltered
HUD master at `1,1` on plane 1, one ordinary 32px world member at `(2,2)`, and a
plane-0 floor. It covers opacity 0/1/2, not filtered masters or general trees.
Captured player display names use the same-length synthetic `TestGuest`; all other
wire bytes are unchanged. This does not establish guest-account isolation.

## Retained expectations

- `stock/result.json`: the three stock phase records consumed by
  `MasterOpacityFixture::rows()`, including **45 mouse receipts and nine Menu
  receipts**. This file is a test input, not an unconsumed run report.
- `stock/frames-{control,two,zero}.txt` and `stock/res/`: independent wire/assets.
- `stock/{control,two,zero}-map.png`: the three stock pixel goldens consumed by
  native `map_test` rows.
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
