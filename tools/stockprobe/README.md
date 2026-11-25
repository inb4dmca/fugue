# Stock observation tools

`stockprobe.py` prepares a small world, starts its DreamDaemon and two clients,
then records observations. It requires Windows, BYOND, Python/Pillow and a built
Fugue with its runtime dependencies on `PATH`. Automated replay tests do not need
these tools or an installed BYOND.

Existing `record`, `pair` and `click` commands handle settled snapshots and input
receipts. `capture` adds a reusable timed recording, independent of a particular
world's actions or expected values:

```powershell
python -B tools/stockprobe/stockprobe.py --world tests/data/clientanim-directset-world --client build/ninja/release/app/Release/fugue-qt.exe --work build/directset-runtime --out build/observations --map 352x352 --settle 1 --duration 4 --interval 0.025 --trigger-file capture.txt capture directset-01
```

The work directory must be new or empty; the named output directory must not
exist. Change the output name and work directory for another observation. Only
processes started by the tool are stopped. Capture failures retain partial
observations and logs with `complete: false` and an error in `capture.json`.

The fixture supplies `prepare.py` when its assets or skin need special setup.
It logs `READY <key>` to `events.log` once for each client. After two READY lines
and `--settle`, the optional trigger file receives a fresh token inside the work
directory. The world can use this to start its actions. The trigger is generic;
the world owns what it means and should log its own action/checkpoint markers.

## Retained evidence

Each output directory contains:

- Full client-area images under `stock/` and `client/`, without resizing or pixel
  normalization. A sample index groups successive captures, not simultaneous frames.
- `capture.json`: actual capture-call start/end bounds, requested sampling times,
  missed sample ranges, trigger bounds, per-image window/client geometry and DPI,
  executable versions and hashes, prepared-world hashes and state policy.
- `wire/`: Fugue's existing recorder output and resources. This is the separate
  client's received wire, not a trace of DreamSeeker's presentation.
- Prepared DM/skin sources under `world/`, plus compilation, server, client and
  event logs. Compiled worlds and executables are fingerprinted rather than copied.

Times are nanoseconds relative to `monotonic_origin_ns`. Capture bounds enclose
the window-read call; they do not reveal when its pixels were presented. READY
and the trigger also do not prove client presentation. Use a visible clock or
other same-image cue when a scenario needs to associate pixels with a checkpoint,
and retain the resulting timing uncertainty. Slow capture skips scheduled samples
and records the gaps. Stock and Fugue are read sequentially.

`PrintWindow` asks each window to draw its client area. It does not observe desktop
occlusion or compositor presentation. `--map` is a configured top-left crop for
the snapshot commands; timed capture retains the whole client area and records
that configuration separately from measured geometry. Stock keeps its existing
global configuration; Fugue uses a fresh state directory.

## Measuring observations

`measure.py` finds exact named RGB colour bounds in retained images:

```powershell
python -B tools/stockprobe/measure.py build/observations/directset-01/capture.json --crop 0 0 352 352 --colour player=200,40,40 --colour clock=255,255,255 --output build/observations/directset-01/measurements.json
```

It defaults to stock images and records image hashes and pixel coordinates.
The optional crop is checked against each image; bounds are relative to that crop.
Scenarios interpret these measurements into expectations; the capture and
measurement tools do not consult Fugue output to define a stock expectation.
Retain the source images and provenance with any published measurements.

The scheduling/error-retention checks run without Windows or BYOND:

```sh
python -B -m unittest discover -s tools/stockprobe -p test_capture.py
```
