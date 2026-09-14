# Native Renderer Architecture Benchmark

## Initial Result (Before Static Slot Lowering)

The initial parametric path had a major steady-state performance
regression. Fewer runtime shader compilations must not be presented as an
overall performance improvement or as solved stuttering.

The correction and its new controlled measurements are recorded below under
**Static Slot Lowering**. The initial numbers are retained as regression evidence.

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

## Static Slot Lowering

The fragment program unnecessarily indexed the three local TEV operands and
four texture slots dynamically. The texture sampling loop also fed a dynamic
unit into the general sampler dispatcher. Both domains are fixed by PICA, not
by a scene or material. Lowering them to literal slots offline allows the GPU
compiler to optimize each operand and sampler path independently.

Changed boundaries:
- `runtime/three_ds_recomp/src/fast/renderer3ds/pica_tev_program.cpp`:
  literal texture source cases and three explicit calls to a shared operand
  decoder. Native register values, PREVIOUS aliasing, arithmetic, quantization,
  combiner buffer updates and the six-stage sequence are unchanged.
- `tools/oot3d/native_pica_frontend/oot3d_native_pica_fragment_shader_gen.cpp`:
  generate four literal sampler calls, retaining the native runtime usage mask.
- `runtime/three_ds_recomp/include/fast/renderer3ds/pica_native_fragment_binaries.h`:
  regenerate the same 16 offline binaries. No new per-material family or cache.
- `tools/renderer/tev_program/generator_consumer_tests.cpp`: guard fixed operand
  and sampler lowering alongside the existing semantic/GPU tests.

Confirmation uses the same method above, three repeats per arm in rotating
order (nine runs). Each run still executes 85,500 draws and 900 native updates.

| Configuration | Median native frames/s | Range | ms/frame from aggregate throughput |
| --- | ---: | ---: | ---: |
| Historical build | 112.05 | 100.99-113.27 | 8.92 |
| Current specialized | 97.91 | 95.03-101.20 | 10.21 |
| Corrected parametric | 102.80 | 98.78-104.00 | 9.73 |

The corrected path is approximately 5.0% above the current specialized median
and 8.3% below the historical median. The small lead over current specialized
should not be generalized beyond the variability of this test. The earlier
29.46 fps parametric result and the new 102.80 fps result belong to separate
batches; they show recovery, not a precisely isolated 3.49x hardware speedup.

Median renderer-frame-start total falls from 21.939 s in the earlier parametric
batch to 0.234 s (all 900 frames, including warmup). Guest execution is 5.516 s
versus 5.446 s previously. The shader-only change removes the dominant wait;
no game scheduling, synchronization policy, quality, draw count or resolution
was changed. Register spills versus sampler-dispatch cost have NOT been
separately isolated with GPU instruction profiling; the result establishes the
combined lowering improvement, not the contribution of each individual edit.

Cold application-cache parametric run: zero pass shader compilations, two
remaining legacy shader compilations (204.392 ms), one canonical program owner.
The collected shader pack remains absent. This does not yet satisfy the goal
of zero runtime compilation across the complete renderer.

Validation:
- All nine CTest suites pass, including native equations on GPU, generator
  invariance, uniform layout, program identity and offline artifact consistency.
- Field and boot comparisons each capture frames 120, 150 and 180 directly
  from the framebuffer: all six are pixel-identical to the specialized path.
- The TAA/instrumented comparison still FAILS exact parity at frame 150:
  one pixel differs by 8/255 in red; frames 120 and 180 are identical. This
  reproduces the pre-existing result from both program-owner TAA runs, without
  worsening it. It remains open; the comparison threshold was not relaxed.
- These comparisons do not establish all-scene coverage or frame-time tails.
  Linux and Android have not been benchmarked for this change.

Measured executable SHA256:
`096f7686d650239bd16d0639438f1fb19636be3e3b27379fecc71b4e32ef40f1`.
Built from `74c9eea` plus the static-slot changes documented here. Subsequent
edits add only a C++ comment, test assertions and documentation.

Private artifacts under `C:/Users/xander/AppData/Local/Temp/` (never package):
- `TriAevum-tev-static-operands-pilot-20260914`: one-repeat pilot, not headline results.
- `TriAevum-tev-static-operands-confirm-20260914`: nine-run confirmation,
  including executable hashes, invocations, timing phases and compiler counters.
- `TriAevum-tev-static-operands-parity-20260914`: Field framebuffer comparison.
- `TriAevum-tev-static-operands-boot-parity-20260914`: boot framebuffer comparison.
- `TriAevum-tev-static-operands-taa-parity-20260914`: unchanged TAA discrepancy.

Next priorities: measure frame-time tails and additional workloads, eliminate
the remaining legacy/instrumented fragment compiler paths with bounded offline
families, and profile the smaller residual cost before default promotion.
Keep the precompiled pass work and correct program ownership; do not restore
shader-cache dependence to conceal unfinished work.
