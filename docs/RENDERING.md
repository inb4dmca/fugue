# Map rendering

The MAP renderer uses Qt's QRhi API and requires Qt 6.7 or newer, its matching
Gui private headers, and Shader Tools. It renders terrain, world entities, HUDs,
lighting and effects, but does not implement full BYOND appearance or input parity.
The supported combinations are narrower than the individual feature names below;
regression tests define the exercised cases.

## Architecture

- `SessionWorker` is the only reader/writer of the world model. `TurfMap` prepares
  camera/terrain geometry; `EntityMap` extracts the unified scene, including turf
  appearance parts, entities and supported HUD/image/vis occurrences.
- Appearance decoding, asset templates, animation sampling and instance placement
  are worker-side operations. Templates track appearance/resource dependencies,
  including missing resources, so later arrivals invalidate the right entries.
- Published `MapSprites`, geometry and image data are immutable and contain no
  model pointers. `MapMailbox` replaces its pending frame instead of queuing an
  unbounded history; each MAP has its own scene scope and mailbox.
- `CompositionPlan` resolves selected masters, target bindings, dependencies,
  cycles, bounds and backend admission from that immutable scene. Rendering and
  picking share this model-free plan.
- `MapWidget` and `SpriteRenderer` own GUI-thread QRhi resources. Native-resolution
  composition is separate from final viewport scaling. The old CPU turf compositor
  remains a reference utility, not a fallback live renderer.

## Assets and playback

DMI state/direction selection, PNG/JPEG assets, animated GIFs and a bounded legacy
image format are supported. Missing DMI states fall back to the default state.
Generated-icon recipes retain states, directions, frames and playback metadata for
supported operations. Unsupported recipes and hard allocation/storage refusals do
not publish a partial recipe result. Long supported recipes can resume across
worker preparations rather than raising a per-frame work limit.

Shared asset templates do not own instance playback starts. DMI/GIF playback,
object flicks and supported property-animation chains use authoritative ingest
and instance timing. Changing color matrices, arbitrary easing/transform chains,
movement-icon variants and general flick combinations are not fully supported.

Caches and traversal are bounded. Warm resident movement/playback should change
positions or select resident cells without uploading icon texture bytes; resource
arrival, eviction and QRhi recreation can require uploads. Output-only viewport
resizing does not invalidate resident icons.

## Composition and transforms

Turfs and entities share plane/layer ordering. Supported materials include RGBA,
color matrices/resets and normal/add/subtract/multiply/inset blends. Layer ties keep
tree order within one atom, additive parts included. Plane masters,
render targets/sources, local captures, KEEP_TOGETHER/KEEP_APART and anonymous
appearance groups have bounded implementations, not arbitrary tree parity.
A client image without a layer of its own draws at FLY_LAYER; on a KEEP_TOGETHER
target it is composed inside the target's surface. Particle sets are simulated on
the client frame clock and drawn over their holder, vis_contents copies included.

Composition uses dependency-ordered passes and reachable surfaces. Missing sources
and cut cycle edges are transparent, not stale previous-frame textures. Named MAP
masters and targets are isolated from the default MAP. Material order and byte
rounding boundaries matter; grouping or combining passes must preserve them.

Implemented filter subsets include ordered outline/blur chains, outer shadows,
wave, ripple and bloom, and leaf-master alpha masks (offsets, inverse, swap) and RGB
color filters anywhere in such a chain, with the master's own color applied after.
Inner shadows, the remaining filter types and masks whose source is not a whole
plane remain unsupported. Standalone blur, chained blur and master
filters have different admitted ranges; consult their tests before widening them.
Unsupported plane-master filter records are currently skipped; incompatible
remaining chains are omitted. This is degraded rendering, not stock parity.
Static ordinary affine geometry, bounded root-transform trees and some master
surfaces are supported; general transformed/animated attachment trees are not.

Pass/surface/pixel workload counters include advisory thresholds. Those are
separate from hard surface/storage/backend constraints; exceeding an advisory
counter is not itself permission to refuse a supported scene. A failed resource
or recording admission must not publish a partly executed composition plan.

## Visibility and lighting

Visibility includes invisibility thresholds, bounded geometric occlusion,
category/self/through sight, ambient and dark-vision illumination, supported light
anchors and plane-0 SEE_BLACKNESS coverage. Visibility follows the displayed glide
viewport, not just destination cells. Blackness coverage participates in ordinary
composition and is not an input target.

BLIND/SEE_PIXELS/infrared, exploration memory, nested/animated light sources and
arbitrary sight/effect combinations remain outside the verified subset.

## HUD and named maps

Object `client.screen` supports bounded edge/corner screen locations, ranges and
repetition. Named MAPs have separate HUD/image/vis scenes, masters, targets,
publication and picking. Browser-embedded runtime MAPs support lifecycle and
positive numeric views. Remote-turf/camera/cross-z scenes and general automatic
view expansion are not supported.

`client.images` and `vis_contents` support selected world/HUD attachment forms,
sharing and nesting. Mob vis holders, general vis flags, grouped/transformed
holders and arbitrary transformed image inheritance remain gaps. Do not infer
support for a combination merely because its parts render independently.

## Maptext and INFO

Maptext is rasterized on the worker with shared immutable results. Supported
formatting includes basic HTML, default-MAP stylesheet classes, nested uniform
alignment and a one-pixel outline. MeasureText uses the same layout. General
HTML/CSS, custom font loading, per-MAP styling and exact stock glyph/layout/DPI
parity are not implemented.

INFO row thumbnails are independently resolved on the worker and published as
immutable images. Their appearance/transform rules are not a claim of MAP parity.
Clicks must reject stale row identities.

## Presentation and input

Normal, Distort and Blur presentation modes compose at native resolution, then
apply fit/crop/zoom and resampling. Qt logical sizing, stock DPI behavior, zoom
quantization and origin rounding can still produce different display rectangles.
Equal outer window sizes do not establish equal MAP pixels or layout histories.

Explicit screenshots synchronously read back the last complete native MAP texture,
before presentation scaling, without consuming a pending worker frame. Normal
rendering performs no screenshot readbacks. Resource/session replacement and failed
compositions invalidate capture instead of exposing stale pixels.

Picking uses the last successfully rendered frame, geometry and composition plan,
never a newer pending scene. Source-ink hits, original-rectangle menus and generated
effect pixels have distinct rules. Supported relay/PASS_MOUSE paths preserve source
ownership and coordinates; they are not arbitrary composited-alpha picking.

The worker revalidates session, registry identity, local lifetime, HUD membership,
turf revision and MAP scope as applicable before dispatch. Slot reuse or a reopened
MAP must not retarget queued input. Click/hover, right-release menus and world/object-
HUD drag/drop have bounded implementations; general affine gestures, grouped/image/
vis input combinations and cross-MAP drags remain incomplete.

## Diagnostics

Set variables for an individual client process, not globally:

- `FUGUE_FRAME_TRACE=1`: frame/workload traces, including composition counters.
- `FUGUE_ATOM_TRACE=obj:42` or `mob:42`: one registry-qualified decimal slot.
- `FUGUE_ATOM_TRACE_INFO_ROW=7`: optional zero-based published INFO row, independent
  of the selected MAP atom.
- `FUGUE_ATOM_TRACE_PIXELS=1`: optional bounded alpha inspection of existing images.
- `FUGUE_GPU_BACKEND=opengl` (or `d3d11`, `metal`, `vulkan`): select the graphics API of MAP and of the rest of the
  window (`shell/gpubackend.h` settles it at start-up and sets Qt's `QSG_RHI_BACKEND` to match). Verify
  actual backend creation; a requested environment value is not backend evidence.
  `FUGUE_FRAME_TRACE` prints the backend and device that were created.
  Vulkan is experimental. Its off-screen suites pass. `shell_map_native` run by hand with `FUGUE_GPU_BACKEND=vulkan`
  has two failures (`stockOutlinePrecisionAndWorkload`, `standaloneBlurPixels`): a plane's intermediate surfaces
  come out empty in the first scene frame a MapWidget draws when that frame is a `grabFramebuffer()` taken before
  the window has presented at its new size. Presenting once first, or drawing any scene first, avoids it. The cause
  is not known, nor whether a frame presented on screen can be affected.
- `FUGUE_DUMP_REFUSED=<dir>`: write the bytes of an icon that could not be drawn there.

Atom traces describe the direct-world occurrence, not every HUD/image/vis copy.
Prepared/published counts do not prove selected-instance GPU output or visible
screen pixels. Tracing does not override admission or restart playback.

## Tests

CTest runs decoding, cache, animation, composition, visibility and input regressions.
Synthetic source worlds and independent stock expectations live under `tests/data/`;
fixture READMEs describe their specific conditions and limitations. Native tests
exercise QRhi composition and presentation separately from offscreen tests.
See [BUILDING.md](BUILDING.md#tests) for build and test-profile setup.

A passing fixture is evidence for that case, not full-game parity. Keep independent
stock expectations distinct from renderer-generated references, and report skipped
or failed native checks separately from CPU/offscreen results.
