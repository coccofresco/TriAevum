# Canonical PICA hash CPU costs

2026-09-07. Baseline project `0d7251df6` (runtime code `899366255`), renderer
`485dcc5c`. Continuation of [title/audio CPU costs](TRIAEVUM_TITLE_AND_AUDIO_CPU_COSTS.md).
The installed title DLL, game timing, renderer effects and user presets are unchanged.

## Final implementation

The previous symbolized profile attributed 7.03% of main-thread samples to
canonical draw identity construction. Native register files and uniform arrays
contain many zero words, but their FNV-1a hashes performed four dependent
multiplications for each zero word.

`tools/oot3d/native_pica_frontend/oot3d_native_pica_canonical_hash.h` provides
allocation-free, portable FNV updates. For N zero bytes the exact update is
`hash * prime^N` modulo 2^64. Word encoding remains little-endian; byte streams
use an alignment-safe 64-bit zero test, then the original byte order for nonzero
blocks and tails. The existing offset basis and final zero-sentinel handling
are preserved, including the project's existing offset-basis value.

`oot3d_native_pica_program_descriptor.cpp` uses this primitive for canonical
32-bit fields and byte streams. All fields, including unknown registers, still
participate. Schema version 3, persistent IDs and shader-pack compatibility do
not change. There are no new caches, allocations, invalidation rules, settings,
quality reductions, game-specific shortcuts or changes to the NRI effect graph.

## Measurements

Four final runs in A/B/A/B order, same Kokiri `hudtest.oot3dsav`, title DLL and
private copies of configs/saves. 640x360, AA/effects off, audio retained,
interpolation explicitly disabled, VSync/pacer/SDL limiter off. Each run has
1,200 presentations and 1,199 actual updates, zero synthetic/repeated lists.
The FPS window excludes 120 warmup presentations; 1,080 remain measured. No
capture, save, validation, profiler, trace or build ran in these timed tests.

| Run | Native throughput FPS | Planner seconds, whole run | Guest seconds, whole run |
| --- | ---: | ---: | ---: |
| A4, before | 70.993 | 1.8186 | 11.3482 |
| E1, final | 76.350 | 1.2076 | 10.9664 |
| A5, before | 73.778 | 1.7698 | 10.9399 |
| E2, final | 75.864 | 1.2348 | 11.2234 |

Mean native capacity: **72.386 -> 76.107 FPS, observed +5.14%** in this fixture.
Planner cost: **1.495 -> 1.018 ms/presentation, -31.94%**, including warmup.
Phase counters can overlap: do not add planner and submission times together.
Mean guest time changes by only about -0.44%; its code was not modified. Host
load still varies, so this is a measured local result, not a universal FPS gain.
Throughput mode processes native ticks faster than wall time for measurement;
ordinary gameplay remains native-speed with the existing optional interpolation.

## Verification

- 1,048,576 word-update differential cases from arbitrary hash states and
  8,208 byte-stream cases against the original bytewise FNV algorithm: pass.
  Dense/sparse/zero input, empty data, unaligned starts and tails are included.
- Program-descriptor and native Vulkan draw-plan suites: pass.
- Four 640x360 framebuffer pairs at native frames 120/240/360/480: byte-identical.
- Separate 12-presentation traces: all **1,321 records / 13,972,171 bytes** identical,
  including 1,254 draws, shader sources, canonical IDs and display transfers.
- Both visual runs retain 107 draws, 107 lighting/fog states, 20 skeleton states,
  zero scene-record rejections and zero Vulkan/NRI errors. The same 18 existing
  Vulkan warnings remain in both; they are not claimed fixed.
- Start-from-boot enhanced intro with existing effects and x2, private 1280x720
  output: 600 presentations / 288 updates, exit 0, framebuffer inspected. This
  interpolation/capture run is functional evidence, not an FPS benchmark.

The benchmark harness now rejects native-throughput savestate runs without
approximately one actual update per presentation. Traces, saves, runtime
profiling and GPU-validation runs are explicitly labeled non-benchmark evidence.

## Reproduction and rejected experiment

Runtime: `I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`.
Before binary: same directory, `TriAevum.before-canonical-costs.exe`.
Matching before PDB/binary: `I:/oot3dre_work/canonical-costs-20260907-before/`.
Evidence and generated comparison: `J:/TriAevum-diagnostics/canonical-costs-20260907/`.
`a4/e1/a5/e2` are the final measurements; `visual-*`, `trace-*` and
`enhanced-intro` must not be used as performance runs.

Use `tools/triaevum_release/tests/benchmark_gameplay.mjs` with the current release
launch profile, `hudtest`, `overrides=J:/TriAevum-diagnostics/canonical-costs-20260907/native-640.json`,
`frames=1200 warmup=120 interpolation=false`, and a fresh output directory.
Each invocation records the executable and title-plugin hashes and its options.

An exact-byte, bounded identity cache was evaluated first: about 37% hits and
extra serialization/storage. The direct arithmetic optimization lowered planner
cost further with substantially less code and no retained state, so the cache
was removed rather than layered on. Exploratory runs `a1/b1/a2/c1/a3/c2/d1` and
`rejected-cache-experiment.patch` are retained only as development evidence.

Only frontend/consumer objects and the runtime were rebuilt, with at most two
compiler workers and temporary files on I:. No title IR regeneration or whole
title compilation/link was needed. Larger remaining costs are the measured
title memory, block-entry and VFP helpers; they require a deliberately paired
title-module change, not another renderer-quality reduction.
