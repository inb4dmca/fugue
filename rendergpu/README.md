# Experimental GPU executor

A QRhi executor for `render::DrawList`, built with `-DFUGUE_BUILD_GPU=ON` and selected with `--renderer=gpu` or
**Client > Renderer**. It covers what the CPU executor (`render/executor.cpp`) draws: ordered planes, sprite tint, alpha
and colour matrices, the five blend modes, plane-master colour, slates and relays, blur/outline/drop-shadow, plane-master
affine transforms, legacy HUD passes and the black-ground flatten. The CPU executor stays the reference.

| File | Contents |
|---|---|
| `executor.h`, `executor.cpp` | `Executor`: `supports()` (validation and resource budgets) and `record()` into an owned texture |
| `shaders/` | the composite, sprite and present shaders, compiled by `qt_add_shaders` |
| `timing.h` | thread-CPU clock for the opt-in recording profile |
| `../app/gpumapwindow.*` | the per-map `QWindow` swapchain presenter and `GpuMapController` |

* `record()` runs on the QRhi's thread (the GUI thread in the client), once per frame outside a render pass, and never
  reads back. The `shared_ptr` overloads treat the list as immutable, cache its validation and reuse unchanged plane
  outputs across frames.
* Each map is presented in a native window, which the windowing system draws over every sibling widget. Skin controls
  stacked above the map in the same window (vg's `mapwindow.credits` browser) are kept on top by masking that window
  out beneath them (`MapWidget::clipExternalView`, run whenever the skin places its controls).
* `supports()` refuses a list over the `Limits` (4096 px per side, 128 MiB of textures per list, capped counts of planes,
  layers, filters and passes) or that needs a missing device feature; the caller then draws it on the CPU.
* `Features` turns off the optimizations (pass fusion, packed uniforms, instanced sprites, plane caching); the tests use
  that unoptimized executor as the reference the optimized one must match byte for byte.
* Shaders compute in float32, the CPU in float64. The tests require exact bytes for most fixtures and allow 1-2 for some
  filter and blend cases.

## Tests

`ctest -L gpu` runs `gpu_executor` (GPU against CPU on synthetic lists), `gpu_renderthread` (renderer switching and
hand-off) and `gpu_present` (swapchain pixels, input, resize, fallback; needs a real windowing system, not the
`offscreen` platform). The backend is D3D11 on Windows, Metal on macOS and OpenGL elsewhere
(`FUGUE_GPU_TEST_BACKEND` overrides). Software adapters:

```sh
FUGUE_GPU_TEST_SOFTWARE=1 FUGUE_GPU_SOFTWARE=1 ctest --test-dir build -C Release -L gpu      # Windows, WARP
QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a ctest --test-dir build -L gpu       # Linux, Mesa llvmpipe
```

`FUGUE_GPU_BENCHMARK=1` enables the benchmark cases. In the client, `FUGUE_GPU_PROFILE=1` logs per-map submission
phases once a second and `FUGUE_GPU_TRACE=<path>` writes them as CSV; `FUGUE_GPU_VERIFY=1` compares a GPU frame
against the CPU executor once a second.
