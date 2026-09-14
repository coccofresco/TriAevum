# Native Renderer Architecture Benchmark

## Result

The new parametric path currently has a major steady-state performance
regression. Fewer runtime shader compilations must not be presented as an
overall performance improvement or as solved stuttering.

| Configuration | Median native frames/s | Range, three runs | ms/frame from aggregate throughput |
| --- | ---: | ---: | ---: |
| Historical build, 2026-09-10 | 113.70 | 104.57-115.19 | 8.79 |
| Current build, specialized path | 107.34 | 104.18-109.01 | 9.32 |
| Current build, parametric path | 29.46 | 29.36-29.54 | 33.95 |

Parametric throughput is 74.09% below the historical build and 72.56% below
the specialized path in the SAME current executable. Historical/current code
differences alone therefore cannot explain the regression. The new path takes
approximately 3.86 times as long per frame as the historical build in this test.

## Method

- Windows, NRI/Vulkan, NVIDIA GeForce RTX 3060.
- Same title DLL, assets, Hyrule Field savestate and copied save/config inputs.
- Output 1280x720, RenderScale 1.0, Authentic preset, optional effects Off.
  Output size is not a claim that every native internal target is 1280x720.
- Native30 fixed delta, visual interpolation disabled. No VSync, host frame
  limiter or realtime pacing. This is uncapped throughput, not normal-speed play.
- Three runs per configuration in rotating order; 900 presented frames each,
  first 180 excluded, remaining 720 timed by the existing runtime benchmark.
- Every run reports 900 game-state updates, 900 direct-current presentations,
  zero interpolated draws/reused presentations and 85,500 executed draws.
- No screenshots, effective shader inventory or extended diagnostics during
  timing. Loading and warmup are excluded from the FPS window.
- Fresh application cache for each configuration's first run, reused for its
  subsequent runs. OS/driver caches were NOT cleared. No claim of cold GPU state.
- Historical/specialized paths retain the fixture shader pack. The parametric
  path has no collected shader pack. This reflects its cache-independent goal.

The preliminary run was discarded: an incorrect persisted VSync field left it
near 60 fps despite host throughput flags. The final runner sets
`Graphics.Presentation.VSync=false` and checks the runtime's limiter flags.
Absolute guest refresh counts include restored state and internal ticks; they
must not be compared directly to presentation counts to detect interpolation.

## Compilation Versus Execution

First run of the Field fixture, application cache initially empty:

| Configuration | Runtime shader compilations | Reported compiler time |
| --- | ---: | ---: |
| Historical with fixture pack | 25 (21 passes + 4 others) | 2.939 s |
| Current specialized with fixture pack | 42 | 4.136 s |
| Current parametric without pack | 2 | 0.222 s |

The current specialized shaders no longer all match the older fixture pack.
These are measured cache conditions, not a claim of optimal historical packing.
The parametric path eliminates most compiler work, but this saving is overwhelmed
by its per-frame cost. Total first-run wall time, INCLUDING loading and all 900
frames, was 15.06 s historical versus 36.19 s parametric; this is not a standalone
startup-latency measurement.

Median phase totals over all 900 frames (these include warmup):

| Phase | Historical | Current specialized | Current parametric |
| --- | ---: | ---: | ---: |
| Guest execution | 5.328 s | 5.374 s | 5.446 s |
| Renderer frame start | 0.133 s | 0.116 s | 21.939 s |

The dominant extra time is at renderer frame start, consistent with GPU/backpressure
or synchronization waits. This localizes the next investigation but does NOT
identify the precise GPU cause without GPU timestamps/profiling. Compilation
counts and average FPS do not measure stutter percentiles; no p95/p99 claim is made.

## Reproduction and Provenance

Runner: `tools/renderer/tev_program/benchmark_native_paths.py`.
Historical executable: `J:/TriAevum-verify-20260910/package/TriAevum.exe`,
release manifest source commit `c7f96869a9bf07c4dada763b067102d94f24d9bd`.
It predates the architecture work but is not the immediately preceding commit.
Current executable: `J:/TriAevum-verify-20260910/runtime/TriAevum.exe`, `aadc050`.

SHA256:
- Historical: `29365168792bdf275e49c7985cdc98b9630daa2599dbab2c06261582977a89b8`
- Current, both modes: `c0ea84727a016fc8c68a55add8f7ca8a87417f197349ff4d577396cf0f37dbea`

Private measurements, invocation copies and logs (never package):
`C:/Users/xander/AppData/Local/Temp/TriAevum-architecture-benchmark-unlimited-20260914`.

```text
python tools/renderer/tev_program/benchmark_native_paths.py --invocation <private-invocation.json> --baseline <historical-TriAevum.exe> --output <new-private-directory>
```

Priority: profile and reduce the new fragment path's execution/synchronization
cost before promoting it to default. Keep the precompiled pass work and correct
program ownership; do not hide this regression by restoring shader-cache dependence.
